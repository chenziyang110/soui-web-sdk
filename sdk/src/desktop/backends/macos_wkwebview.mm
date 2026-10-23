#include "../backend.h"
#include "../resource_uri.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#include <utility>

#if !__has_feature(objc_arc)
#error "The WKWebView desktop backend requires Objective-C ARC (-fobjc-arc)."
#endif
#if __MAC_OS_X_VERSION_MIN_REQUIRED < 120000
#error "The WKWebView desktop backend requires macOS 12.0 or later."
#endif

namespace {

using soui::desktop::detail::backend_events;
using soui::desktop::detail::message_frame;
using soui::desktop::result;
using soui::desktop::value_result;

struct macos_state;
struct macos_hotkey {
  std::weak_ptr<macos_state> owner;
  std::uint64_t token{0};
  UInt32 native_id{0};
  UInt32 key_code{0};
  UInt32 modifiers{0};
  EventHotKeyRef reference{nullptr};
  bool active{false};
};

// Carbon delivers to an application target, not an NSWindow. The process-wide
// router outlives every window and retains failed unregisters for final cleanup.
// Its IDs never restart, even when all windows have closed and a new group opens.
struct macos_hotkey_router {
  EventHandlerRef handler{nullptr};
  std::uint64_t next_id{1};
  std::map<UInt32, std::shared_ptr<macos_hotkey>> registrations;

  ~macos_hotkey_router() {
    for (auto& entry : registrations) entry.second->active = false;
    for (auto& entry : registrations) {
      auto& registration = entry.second;
      if (!registration->reference) continue;
      const auto status = UnregisterEventHotKey(registration->reference);
      if (status != noErr)
        std::fprintf(stderr, "SOUI: final global shortcut cleanup failed (OSStatus %ld)\n",
                     static_cast<long>(status));
    }
    if (handler) {
      const auto status = RemoveEventHandler(handler);
      if (status != noErr)
        std::fprintf(stderr, "SOUI: global shortcut router cleanup failed (OSStatus %ld)\n",
                     static_cast<long>(status));
    }
  }
};

macos_hotkey_router& hotkey_router() {
  static macos_hotkey_router router;
  return router;
}

// AppKit has one application per process. A managed window group owns these
// controls once; closing its root window must not remove them from its children.
struct macos_application {
  NSApplication* application{nil};
  id delegate{nil};
  NSMenu* previous_menu{nil};
  NSMenu* installed_menu{nil};
  NSApplicationActivationPolicy previous_activation{NSApplicationActivationPolicyProhibited};
  std::function<void()> request_quit;
  bool controls_installed{false};
  bool running{false};
  bool quit_requested{false};
  bool retired{false};
  std::size_t modal_depth{0};
};
std::weak_ptr<macos_application> current_application;

enum class job_kind { native_work, message, notification, terminal };
struct ui_job {
  job_kind kind;
  std::uint64_t generation;
  std::function<void(const std::shared_ptr<macos_state>&)> callback;
};

// Only the atomics and mutex-protected job queue are accessed off main.
// The backend's shared_ptr never changes, and delegates/blocks retain weak_ptrs.
// This avoids both WKUserContentController retain cycles and raw-host callbacks.
struct macos_state {
  std::atomic<bool> accepting_work{false};
  std::atomic<bool> terminal_failure{false};
  bool created{false};
  bool shutdown_started{false};
  bool loop_owner{false};
  bool enable_resource_server{false};
  bool native_navigation_ticket{false};
  bool native_provisional_authorized{false};
  bool document_committed{false};
  bool document_failed{false};
  bool reload_pending{false};
  bool state_observation_scheduled{false};
  soui::desktop::window_state last_window_state;
  std::uint64_t generation{0};
  std::mutex jobs_mutex;
  std::deque<ui_job> jobs;
  std::size_t native_jobs{0};
  std::size_t message_jobs{0};
  bool drain_scheduled{false};
  backend_events events;
  std::shared_ptr<macos_application> application_group;
  NSWindow* window{nil};
  WKWebView* webview{nil};
  WKNavigation* navigation{nil};
  NSURL* resource_root{nil};
  NSOpenPanel* directory_panel{nil};
  id delegate{nil};
  NSMenu* popup{nil};
  NSStatusItem* tray{nil};
  NSMenu* tray_menu{nil};
  std::uint64_t popup_generation{0};
  std::uint64_t tray_generation{0};
  std::map<std::uint64_t, std::shared_ptr<macos_hotkey>> shortcuts;
  // Native shortcuts survive navigation and renderer failure. Do not put them
  // in the document-generation queue or let renderer traffic consume their cap.
  std::deque<std::uint64_t> shortcut_jobs;
  bool shortcut_drain_scheduled{false};
};

NSString* cocoa_string(std::string_view text) {
  return [[NSString alloc] initWithBytes:text.data()
                                 length:text.size()
                               encoding:NSUTF8StringEncoding];
}

// Do not use strlen(UTF8String): a script message or clipboard can contain NUL.
std::string utf8_string(NSString* text) {
  if (!text) return {};
  NSData* data = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
  if (!data || !data.length) return {};
  return {static_cast<const char*>(data.bytes), data.length};
}

bool is_live(const std::shared_ptr<macos_state>& state) {
  return state && state->accepting_work.load(std::memory_order_acquire);
}

void schedule_main_block(std::function<void()> work) {
  @autoreleasepool {
    // Main GCD queue blocks are serial, including across nested modal loops.
    // Run-loop blocks let close/dispatch continue when a prior work callback
    // opens the synchronous directory picker.
    NSArray<NSRunLoopMode>* modes =
        @[NSDefaultRunLoopMode, NSModalPanelRunLoopMode, NSEventTrackingRunLoopMode];
    CFRunLoopPerformBlock(CFRunLoopGetMain(), (__bridge CFArrayRef)modes, ^{ work(); });
    CFRunLoopWakeUp(CFRunLoopGetMain());
  }
}

void drain_jobs(const std::weak_ptr<macos_state>& weak_state) {
  auto state = weak_state.lock();
  if (!state) return;
  ui_job job{job_kind::notification, 0, {}};
  bool again = false;
  {
    std::lock_guard<std::mutex> lock(state->jobs_mutex);
    state->drain_scheduled = false;
    if (!is_live(state) || state->jobs.empty()) return;
    job = std::move(state->jobs.front());
    state->jobs.pop_front();
    if (job.kind == job_kind::native_work) --state->native_jobs;
    if (job.kind == job_kind::message) --state->message_jobs;
    again = !state->jobs.empty();
    state->drain_scheduled = again;
  }
  // CFRunLoop can detach a whole batch of blocks before invoking the first.
  // Queue the next drain before client code can enter a nested modal loop.
  if (again) schedule_main_block([weak_state] { drain_jobs(weak_state); });
  if (!is_live(state) ||
      ((job.kind == job_kind::message || job.kind == job_kind::notification) &&
       job.generation != state->generation)) return;
  job.callback(state);
}

result enqueue_job(const std::shared_ptr<macos_state>& state, ui_job job) {
  bool wake = false;
  {
    std::lock_guard<std::mutex> lock(state->jobs_mutex);
    if (!is_live(state)) return result::failure("E_CLOSED", "The desktop window is closed");
    if (job.kind != job_kind::terminal && state->terminal_failure.load(std::memory_order_acquire))
      return result::failure("E_WEB_PROCESS", "The WebKit content process terminated");
    if (job.kind != job_kind::terminal &&
        ((job.kind == job_kind::native_work && state->native_jobs >= 1024) ||
         (job.kind == job_kind::message && state->message_jobs >= 1024) ||
         state->jobs.size() >= 4096))
      return result::failure("E_BUSY", "The desktop callback queue is full");
    const auto kind = job.kind;
    if (kind == job_kind::terminal) state->jobs.push_front(std::move(job));
    else state->jobs.push_back(std::move(job));
    if (kind == job_kind::native_work) ++state->native_jobs;
    if (kind == job_kind::message) ++state->message_jobs;
    wake = !state->drain_scheduled;
    state->drain_scheduled = true;
  }
  if (wake) {
    const std::weak_ptr<macos_state> weak_state = state;
    schedule_main_block([weak_state] { drain_jobs(weak_state); });
  }
  return result::success();
}

void discard_jobs(const std::shared_ptr<macos_state>& state, bool include_native) {
  std::deque<ui_job> discarded;
  {
    std::lock_guard<std::mutex> lock(state->jobs_mutex);
    discarded.swap(state->jobs);
    state->native_jobs = 0;
    state->message_jobs = 0;
    if (!include_native) {
      for (auto& job : discarded) {
        if (job.kind == job_kind::native_work || job.kind == job_kind::terminal) {
          const bool native = job.kind == job_kind::native_work;
          state->jobs.push_back(std::move(job));
          if (native) ++state->native_jobs;
        }
      }
    }
    // Keep a previously scheduled drain valid, even if the queue is now empty.
  }
  // Destroy captured client objects outside the mutex.
}

void deliver_error(const std::shared_ptr<macos_state>& state, result error) {
  if (!is_live(state)) return;
  // A client callback can synchronously close, clearing the original function.
  const auto callback = state->events.error;
  if (callback) {
    try { callback(std::move(error)); } catch (...) {}
  }
}

void report_error(const std::shared_ptr<macos_state>& state, result error) {
  if (!is_live(state)) return;
  (void)enqueue_job(state, {job_kind::notification, state->generation,
      [error = std::move(error)](const std::shared_ptr<macos_state>& live) {
    deliver_error(live, error);
  }});
}

bool active_shortcut(const std::shared_ptr<macos_state>& state, std::uint64_t token) {
  if (!is_live(state)) return false;
  const auto found = state->shortcuts.find(token);
  return found != state->shortcuts.end() && found->second->active;
}

void drain_shortcuts(const std::weak_ptr<macos_state>& weak_state) {
  const auto state = weak_state.lock();
  if (!state) return;
  state->shortcut_drain_scheduled = false;
  if (!is_live(state) || state->shortcut_jobs.empty()) return;
  const auto token = state->shortcut_jobs.front();
  state->shortcut_jobs.pop_front();
  if (!state->shortcut_jobs.empty()) {
    state->shortcut_drain_scheduled = true;
    schedule_main_block([weak_state] { drain_shortcuts(weak_state); });
  }
  // Recheck after queueing, including unregister/re-register and nested close.
  if (!active_shortcut(state, token)) return;
  const auto callback = state->events.global_shortcut;
  if (callback) {
    try { callback(token); }
    catch (...) {
      report_error(state, result::failure("E_CALLBACK", "Global shortcut callback failed"));
    }
  }
}

constexpr UInt32 shortcut_signature = 0x534F5549;  // SOUI, not a window identity.

OSStatus handle_global_shortcut(EventHandlerCallRef, EventRef event, void* context) {
  if (GetEventClass(event) != kEventClassKeyboard ||
      GetEventKind(event) != kEventHotKeyPressed) return eventNotHandledErr;
  EventHotKeyID identity{};
  if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                        sizeof(identity), nullptr, &identity) != noErr ||
      identity.signature != shortcut_signature) return eventNotHandledErr;
  auto& router = *static_cast<macos_hotkey_router*>(context);
  const auto found = router.registrations.find(identity.id);
  if (found == router.registrations.end()) return eventNotHandledErr;
  try {
    const auto registration = found->second;
    const auto state = registration->owner.lock();
    if (!registration->active || !active_shortcut(state, registration->token)) return noErr;
    // Drop excess repeat events instead of growing memory without bound. There
    // is no cross-platform guarantee of one callback per physical key press.
    if (state->shortcut_jobs.size() >= 256) return noErr;
    state->shortcut_jobs.push_back(registration->token);
    if (!state->shortcut_drain_scheduled) {
      const std::weak_ptr<macos_state> weak_state = state;
      state->shortcut_drain_scheduled = true;
      schedule_main_block([weak_state] { drain_shortcuts(weak_state); });
    }
  } catch (...) {
    // Never unwind a C++ exception through Carbon's event dispatcher.
  }
  return noErr;
}

result hotkey_error(const char* code, const char* operation, OSStatus status) {
  return result::failure(code, std::string(operation) + " (OSStatus " +
                                  std::to_string(status) + ")");
}

result native_shortcut(const soui::desktop::global_shortcut& shortcut,
                       UInt32& key, UInt32& modifiers) {
  using soui::desktop::shortcut_key;
  namespace bits = soui::desktop::shortcut_modifiers;
  const auto allowed = bits::control | bits::alt | bits::shift | bits::primary;
  if ((shortcut.modifiers & ~allowed) ||
      !(shortcut.modifiers & (bits::control | bits::alt | bits::primary)))
    return result::failure("E_ARGUMENT", "A global shortcut requires Control, Alt or Primary");
  switch (shortcut.key) {
    case shortcut_key::f1: key = kVK_F1; break;
    case shortcut_key::f2: key = kVK_F2; break;
    case shortcut_key::f3: key = kVK_F3; break;
    case shortcut_key::f4: key = kVK_F4; break;
    case shortcut_key::f5: key = kVK_F5; break;
    case shortcut_key::f6: key = kVK_F6; break;
    case shortcut_key::f7: key = kVK_F7; break;
    case shortcut_key::f8: key = kVK_F8; break;
    case shortcut_key::f9: key = kVK_F9; break;
    case shortcut_key::f10: key = kVK_F10; break;
    case shortcut_key::f11: key = kVK_F11; break;
    case shortcut_key::left: key = kVK_LeftArrow; break;
    case shortcut_key::right: key = kVK_RightArrow; break;
    case shortcut_key::up: key = kVK_UpArrow; break;
    case shortcut_key::down: key = kVK_DownArrow; break;
    case shortcut_key::home: key = kVK_Home; break;
    case shortcut_key::end: key = kVK_End; break;
    case shortcut_key::page_up: key = kVK_PageUp; break;
    case shortcut_key::page_down: key = kVK_PageDown; break;
    case shortcut_key::delete_key: key = kVK_ForwardDelete; break;
    // kVK_Help is not Insert, and kVK_Delete is Backspace. Do not silently
    // substitute either one for the public logical key.
    case shortcut_key::insert_key:
      return result::failure("not_supported", "macOS has no exact Insert shortcut mapping");
    default:
      return result::failure("E_ARGUMENT", "Unknown global shortcut key");
  }
  modifiers = 0;
  if (shortcut.modifiers & bits::control) modifiers |= controlKey;
  if (shortcut.modifiers & bits::alt) modifiers |= optionKey;
  if (shortcut.modifiers & bits::shift) modifiers |= shiftKey;
  if (shortcut.modifiers & bits::primary) modifiers |= cmdKey;
  return result::success();
}

result release_shortcut(const std::shared_ptr<macos_state>& state, std::uint64_t token) {
  const auto found = state->shortcuts.find(token);
  if (found == state->shortcuts.end()) return result::success();
  const auto registration = found->second;
  registration->active = false;
  if (registration->reference) {
    const auto status = UnregisterEventHotKey(registration->reference);
    if (status != noErr)
      return hotkey_error("E_SHORTCUT_UNREGISTER", "Could not unregister the global shortcut", status);
    registration->reference = nullptr;
  }
  hotkey_router().registrations.erase(registration->native_id);
  state->shortcuts.erase(found);
  return result::success();
}

result release_shortcuts(const std::shared_ptr<macos_state>& state) {
  // Revoke all delivery before the first OS operation, including close reentry.
  for (auto& entry : state->shortcuts) entry.second->active = false;
  state->shortcut_jobs.clear();
  result first_error = result::success();
  for (auto it = state->shortcuts.begin(); it != state->shortcuts.end();) {
    const auto token = (it++)->first;
    const auto status = release_shortcut(state, token);
    if (!status.ok && first_error.ok) first_error = status;
  }
  return first_error;
}

bool allows_main_document(const std::shared_ptr<macos_state>& state, NSURL* url) {
  if (!is_live(state) || state->terminal_failure.load(std::memory_order_acquire) ||
      !url || !url.absoluteString ||
      !state->events.allow_navigation) return false;
  try {
    const auto uri = utf8_string(url.absoluteString);
    // Loopback syntax is only the transport gate. The common policy still binds
    // every navigation, response and message to the current native selection.
    if (!url.isFileURL && !(state->enable_resource_server &&
        soui::desktop::detail::is_resource_uri(uri))) return false;
    const auto callback = state->events.allow_navigation;
    const bool allowed = callback(uri);
    return allowed && is_live(state);
  } catch (...) {
    report_error(state, result::failure("E_NAVIGATION_POLICY", "Navigation policy failed"));
    return false;
  }
}

void request_fresh_navigation(const std::shared_ptr<macos_state>& state) {
  if (!is_live(state) || state->reload_pending) return;
  state->reload_pending = true;
  const auto generation = state->generation;
  const auto queued = enqueue_job(state, {job_kind::notification, generation,
      [](const std::shared_ptr<macos_state>& live) {
    live->reload_pending = false;
    const auto reload = live->events.reload;
    if (reload) {
      try { reload(); }
      catch (...) { report_error(live, result::failure("E_CALLBACK", "Reload callback failed")); }
    }
  }});
  if (!queued.ok) state->reload_pending = false;
}

bool is_fragment_navigation(const std::shared_ptr<macos_state>& state,
                            WKNavigationAction* action) {
  if (!state->document_committed || !action.sourceFrame.isMainFrame ||
      (action.navigationType != WKNavigationTypeLinkActivated &&
       action.navigationType != WKNavigationTypeOther)) return false;
  NSURL* current = state->webview.URL;
  NSURL* target = action.request.URL;
  if (!current || !target) return false;
  NSURLComponents* from = [NSURLComponents componentsWithURL:current resolvingAgainstBaseURL:NO];
  NSURLComponents* to = [NSURLComponents componentsWithURL:target resolvingAgainstBaseURL:NO];
  const bool same_fragment = (!from.fragment && !to.fragment) ||
      [from.fragment isEqualToString:to.fragment];
  from.fragment = nil;
  to.fragment = nil;
  return from && to && !same_fragment && [from.URL isEqual:to.URL];
}

void restore_application(const std::shared_ptr<macos_application>& group) {
  if (!group || !group->quit_requested || group->running || group->modal_depth ||
      group->retired) return;
  if (group->controls_installed) {
    if (group->application.delegate == group->delegate) group->application.delegate = nil;
    if (group->application.mainMenu == group->installed_menu)
      group->application.mainMenu = group->previous_menu;
    [group->application setActivationPolicy:group->previous_activation];
    group->controls_installed = false;
  }
  group->delegate = nil;
  group->installed_menu = nil;
  group->previous_menu = nil;
  group->request_quit = {};
  group->retired = true;
}

void stop_event_loop(const std::shared_ptr<macos_application>& group) {
  if (!group || !group->quit_requested || !group->running || !group->application) return;
  [group->application stop:nil];
  // stop: alone does not wake NSApplication when invoked from a dispatch block
  // or a WebKit completion rather than an NSEvent.
  NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                   location:NSZeroPoint
                              modifierFlags:0
                                  timestamp:0
                               windowNumber:0
                                    context:nil
                                    subtype:0
                                      data1:0
                                      data2:0];
  [group->application postEvent:wake atStart:YES];
}

void remove_tray(const std::shared_ptr<macos_state>& state) {
  ++state->tray_generation;
  [state->tray_menu cancelTrackingWithoutAnimation];
  state->tray.menu = nil;
  if (state->tray) [[NSStatusBar systemStatusBar] removeStatusItem:state->tray];
  state->tray = nil;
  state->tray_menu = nil;
}

enum class window_close_origin { backend, appkit };

result shut_down(const std::shared_ptr<macos_state>& state, bool notify,
               window_close_origin origin = window_close_origin::backend) {
  // Native cancellation and close can synchronously notify observers. Do not
  // enter cleanup twice, even before the public closed callback is delivered.
  if (state->shutdown_started) return release_shortcuts(state);
  state->shutdown_started = true;
  const bool was_live = state->accepting_work.exchange(false, std::memory_order_acq_rel);
  ++state->generation;
  state->native_navigation_ticket = false;
  state->native_provisional_authorized = false;
  state->document_committed = false;
  state->reload_pending = false;
  discard_jobs(state, true);
  auto callbacks = std::move(state->events);
  state->events = {};
  // The OS registrations must be released before their native owner disappears.
  // Failed records remain inactive, so a later close/destructor can retry them.
  const auto shortcut_status = release_shortcuts(state);

  // ARC must keep the native objects alive through cleanup and any synchronous
  // observers, not merely through their last ordinary use. Revoke delegates and
  // clear the published handles before sending any reentrant native messages.
  NSWindow* __attribute__((objc_precise_lifetime)) window = state->window;
  WKWebView* __attribute__((objc_precise_lifetime)) webview = state->webview;
  id __attribute__((objc_precise_lifetime)) delegate = state->delegate;
  (void)delegate;
  window.delegate = nil;
  webview.navigationDelegate = nil;
  webview.UIDelegate = nil;
  state->window = nil;
  state->webview = nil;
  state->delegate = nil;

  // Native callbacks are already revoked above. Cancel native tracking before
  // notifying the group: its last-window handler may restore process menus.
  [state->directory_panel cancel:nil];
  state->directory_panel = nil;
  [state->popup cancelTrackingWithoutAnimation];
  ++state->popup_generation;
  state->popup = nil;
  remove_tray(state);
  [webview stopLoading];
  [webview.configuration.userContentController removeScriptMessageHandlerForName:@"soui"];
  [webview.configuration.userContentController removeAllUserScripts];
  [webview removeFromSuperview];
  window.contentView = nil;
  // windowWillClose: is already inside -[NSWindow close]. Calling close again
  // from that notification sends a second will-close to all other observers.
  // AppKit owns the pending native close; only backend-initiated teardown sends it.
  if (origin == window_close_origin::backend) {
    [window orderOut:nil];
    [window close];
  }
  state->navigation = nil;
  state->resource_root = nil;
  if (was_live && notify && !shortcut_status.ok && callbacks.error) {
    try { callbacks.error(shortcut_status); } catch (...) {}
  }
  if (was_live && notify && callbacks.closed) {
    try { callbacks.closed(); } catch (...) {}
  }
  return shortcut_status;
}

result require_main_thread() {
  return [NSThread isMainThread] ? result::success()
      : result::failure("E_THREAD", "WKWebView operations require the macOS main thread");
}

result require_open(const std::shared_ptr<macos_state>& state) {
  auto thread = require_main_thread();
  if (!thread.ok) return thread;
  if (!is_live(state))
    return result::failure("E_CLOSED", "The macOS desktop window is not open");
  if (state->terminal_failure.load(std::memory_order_acquire))
    return result::failure("E_WEB_PROCESS", "The WebKit content process terminated");
  return result::success();
}

soui::desktop::window_state_result observe_window_state(
    const std::shared_ptr<macos_state>& state) {
  auto open = require_open(state);
  if (!open.ok) return {open, {}};
  if (!state->window)
    return {result::failure("E_WINDOW_STATE", "The native window is unavailable"), {}};
  // Zoom is the AppKit windowed maximization operation, not full screen.
  // A miniaturized window can retain its zoomed state: keep the two bits independent.
  return {result::success(), {state->window.miniaturized != NO, state->window.zoomed != NO}};
}

void schedule_window_state_observation(const std::shared_ptr<macos_state>& state) {
  if (!is_live(state) || state->state_observation_scheduled) return;
  state->state_observation_scheduled = true;
  const std::weak_ptr<macos_state> weak_state = state;
  // AppKit resize notifications may precede the final zoom flag update. Observe
  // after the native stack unwinds, coalescing animation/resize notifications.
  // This belongs to the window, not a navigation generation, and carries no old
  // page snapshot into a newly loaded page.
  schedule_main_block([weak_state] {
    auto live = weak_state.lock();
    if (!is_live(live)) return;
    live->state_observation_scheduled = false;
    const auto observed = observe_window_state(live);
    if (!observed.status.ok) return;
    if (observed.value.minimized == live->last_window_state.minimized &&
        observed.value.maximized == live->last_window_state.maximized) return;
    live->last_window_state = observed.value;
    const auto callback = live->events.state_changed;
    if (callback) {
      try { callback(observed.value); }
      catch (...) { report_error(live, result::failure("E_CALLBACK", "Window state callback failed")); }
    }
  });
}

NSMenu* enabled_command_root(NSMenuItem* sender) {
  if (!sender || !sender.enabled || sender.separatorItem || sender.submenu) return nil;
  NSMenu* menu = sender.menu;
  // A submenu command's immediate menu is not the published popup/tray root.
  // Bound the walk even if native menu links have been changed unexpectedly.
  for (std::size_t depth = 1; menu && depth <= 4; ++depth) {
    NSMenu* parent = menu.supermenu;
    if (!parent) return menu;
    NSMenuItem* owner = nil;
    for (NSMenuItem* candidate in parent.itemArray) {
      if (candidate.submenu == menu) {
        owner = candidate;
        break;
      }
    }
    if (!owner || !owner.enabled || owner.separatorItem) return nil;
    menu = parent;
  }
  return nil;
}

NSMenu* application_menu() {
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
  NSMenuItem* app_item = [[NSMenuItem alloc] initWithTitle:@"" action:NULL keyEquivalent:@""];
  NSMenu* app_menu = [[NSMenu alloc] initWithTitle:@"Application"];
  NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                              action:@selector(terminate:)
                                       keyEquivalent:@"q"];
  quit.target = NSApp;
  [app_menu addItem:quit];
  app_item.submenu = app_menu;
  [menu addItem:app_item];
  NSMenuItem* edit_item = [[NSMenuItem alloc] initWithTitle:@"Edit" action:NULL keyEquivalent:@""];
  NSMenu* edit_menu = [[NSMenu alloc] initWithTitle:@"Edit"];
  [edit_menu addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
  NSMenuItem* redo = [edit_menu addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"Z"];
  redo.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
  [edit_menu addItem:[NSMenuItem separatorItem]];
  [edit_menu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
  [edit_menu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
  [edit_menu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
  [edit_menu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
  edit_item.submenu = edit_menu;
  [menu addItem:edit_item];
  NSMenuItem* window_item = [[NSMenuItem alloc] initWithTitle:@"Window" action:NULL keyEquivalent:@""];
  NSMenu* window_menu = [[NSMenu alloc] initWithTitle:@"Window"];
  NSMenuItem* close = [window_menu addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
  // The responder chain selects the current key window, not the closed root.
  close.target = nil;
  window_item.submenu = window_menu;
  [menu addItem:window_item];
  return menu;
}

}  // namespace

@interface SOUIDesktopWKDelegate : NSObject
    <NSWindowDelegate, WKNavigationDelegate,
     WKUIDelegate, WKScriptMessageHandler> {
 @private
  std::weak_ptr<macos_state> _state;
}
- (instancetype)initWithState:(const std::shared_ptr<macos_state>&)state;
- (void)onNativeMenuCommand:(NSMenuItem*)sender;
@end

@interface SOUIDesktopApplicationDelegate : NSObject <NSApplicationDelegate> {
 @private
  std::weak_ptr<macos_application> _group;
}
- (instancetype)initWithGroup:(const std::shared_ptr<macos_application>&)group;
@end

@implementation SOUIDesktopApplicationDelegate
- (instancetype)initWithGroup:(const std::shared_ptr<macos_application>&)group {
  self = [super init];
  if (self) _group = group;
  return self;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  auto group = _group.lock();
  if (group && sender == group->application && !group->quit_requested) {
    // Leave the AppKit delegate stack before common code closes every window.
    const std::weak_ptr<macos_application> weak_group = group;
    schedule_main_block([weak_group] {
      auto live = weak_group.lock();
      if (!live || live->quit_requested || live->retired) return;
      const auto callback = live->request_quit;
      if (callback) { try { callback(); } catch (...) {} }
    });
  }
  // Return from application::run() so C++ destructors run; never call exit().
  return NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  return NO;
}
@end

@implementation SOUIDesktopWKDelegate

- (instancetype)initWithState:(const std::shared_ptr<macos_state>&)state {
  self = [super init];
  if (self) _state = state;
  return self;
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
  id __attribute__((objc_precise_lifetime)) delegate = self;
  (void)delegate;
  // performClose: still has work to do after this delegate returns. Keep its
  // receiver alive in the caller's pool when shutdown clears state->window.
  NSWindow* __autoreleasing closing_window = sender;
  auto state = _state.lock();
  if (is_live(state) && closing_window == state->window) {
    // Approval may synchronously close the window and clear events. Keep both
    // the shared state and a copy of the callback alive through that decision.
    const auto callback = state->events.close_requested;
    if (callback) { try { callback(); } catch (...) {} }
  }
  // Common approval invokes backend::close(), whose shutdown revokes this
  // delegate before closing. Never let AppKit bypass a veto or ask twice.
  return NO;
}

- (void)windowWillClose:(NSNotification*)notification {
  id __attribute__((objc_precise_lifetime)) delegate = self;
  (void)delegate;
  auto state = _state.lock();
  // Clearing the last owning state reference must not destroy NSWindow while
  // its outer close invocation is still unwinding. Use the caller's autorelease
  // pool, not a new pool scoped to this delegate callback.
  NSWindow* __autoreleasing closing_window = notification.object;
  if (state && closing_window == state->window)
    shut_down(state, true, window_close_origin::appkit);
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
  auto state = _state.lock();
  if (is_live(state) && notification.object == state->window)
    schedule_window_state_observation(state);
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
  auto state = _state.lock();
  if (is_live(state) && notification.object == state->window)
    schedule_window_state_observation(state);
}

- (void)windowDidResize:(NSNotification*)notification {
  auto state = _state.lock();
  if (is_live(state) && notification.object == state->window)
    schedule_window_state_observation(state);
}

- (void)onNativeMenuCommand:(NSMenuItem*)sender {
  try {
    auto state = _state.lock();
    if (!is_live(state)) return;
    NSMenu* root = enabled_command_root(sender);
    if (!root || (root != state->popup && root != state->tray_menu) ||
        ![sender.representedObject isKindOfClass:[NSString class]]) return;
    const auto command = utf8_string((NSString*)sender.representedObject);
    const bool tray = root == state->tray_menu;
    const auto generation = tray ? state->tray_generation : state->popup_generation;
    // Commands belong to the native window lifetime, not a renderer generation.
    const auto status = enqueue_job(state, {job_kind::native_work, 0,
        [command, tray, generation](const std::shared_ptr<macos_state>& live) {
      if (generation != (tray ? live->tray_generation : live->popup_generation)) return;
      const auto callback = live->events.command;
      if (callback) {
        try { callback(command); }
        catch (...) { report_error(live, result::failure("E_CALLBACK", "Menu callback failed")); }
      }
    }});
    if (!status.ok) report_error(state, status);
  } catch (...) {
    if (auto state = _state.lock())
      report_error(state, result::failure("E_CALLBACK", "Could not queue native menu command"));
  }
}

- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {
  auto state = _state.lock();
  if (!is_live(state) || controller != state->webview.configuration.userContentController ||
      message.webView != state->webview || ![message.name isEqualToString:@"soui"] ||
      !state->document_committed || !message.frameInfo.isMainFrame ||
      ![message.body isKindOfClass:[NSString class]])
    return;
  NSURL* source = message.frameInfo.request.URL;
  if (!allows_main_document(state, source) || !allows_main_document(state, state->webview.URL))
    return;
  NSString* body = (NSString*)message.body;
  // The transport bounds allocations before the common protocol parser runs.
  constexpr NSUInteger maximum_request_bytes = 65536;
  if (body.length > maximum_request_bytes ||
      [body lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > maximum_request_bytes) return;
  (void)enqueue_job(state, {job_kind::message, state->generation,
      [payload = utf8_string(body), source_uri = utf8_string(source.absoluteString)]
      (const std::shared_ptr<macos_state>& live) {
    if (!live->document_committed) return;
    const auto callback = live->events.message;
    if (callback) {
      try { callback(payload, source_uri, message_frame::main); }
      catch (...) {
        report_error(live, result::failure("E_CALLBACK", "Native message callback failed"));
      }
    }
  }});
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
  auto state = _state.lock();
  const bool admissible = is_live(state) && webView == state->webview &&
      action.targetFrame && action.targetFrame.isMainFrame && !action.shouldPerformDownload &&
      allows_main_document(state, action.request.URL);
  // Missing targetFrame is a popup; subframe documents and downloads are denied.
  if (!admissible) {
    decisionHandler(WKNavigationActionPolicyCancel);
    return;
  }
  if (state->native_navigation_ticket && action.navigationType == WKNavigationTypeOther) {
    state->native_navigation_ticket = false;
    decisionHandler(WKNavigationActionPolicyAllow);
    return;
  }
  if (is_fragment_navigation(state, action)) {
    decisionHandler(WKNavigationActionPolicyAllow);
    return;
  }
  // A new renderer-initiated document must never inherit the prior preload.
  decisionHandler(WKNavigationActionPolicyCancel);
  request_fresh_navigation(state);
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationResponse:(WKNavigationResponse*)response
    decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler {
  auto state = _state.lock();
  const bool allowed = is_live(state) && webView == state->webview && response.isForMainFrame &&
      state->native_provisional_authorized && response.canShowMIMEType &&
      allows_main_document(state, response.response.URL);
  decisionHandler(allowed ? WKNavigationResponsePolicyAllow : WKNavigationResponsePolicyCancel);
}

- (void)webView:(WKWebView*)webView didStartProvisionalNavigation:(WKNavigation*)navigation {
  auto state = _state.lock();
  if (!is_live(state) || webView != state->webview) return;
  // WKNavigationAction has no WKNavigation identity. The one-shot action ticket
  // is followed by this exact identity check before document commit.
  if (navigation != state->navigation) {
    state->native_provisional_authorized = false;
    state->document_committed = false;
    state->document_failed = true;
    [webView stopLoading];
    request_fresh_navigation(state);
  } else {
    state->native_provisional_authorized = true;
  }
}

- (void)webView:(WKWebView*)webView didCommitNavigation:(WKNavigation*)navigation {
  auto state = _state.lock();
  if (!is_live(state) || webView != state->webview) return;
  if (navigation != state->navigation || !state->native_provisional_authorized ||
      !allows_main_document(state, webView.URL)) {
    state->native_provisional_authorized = false;
    state->document_committed = false;
    state->document_failed = true;
    [webView stopLoading];
    request_fresh_navigation(state);
    return;
  }
  state->document_committed = true;
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
  auto state = _state.lock();
  if (!is_live(state) || webView != state->webview || navigation != state->navigation ||
      !state->document_committed || !allows_main_document(state, webView.URL)) return;
  state->native_provisional_authorized = false;
  const auto uri = utf8_string(webView.URL.absoluteString);
  const auto generation = state->generation;
  (void)enqueue_job(state, {job_kind::notification, generation,
      [uri](const std::shared_ptr<macos_state>& live) {
    if (!live->document_committed) return;
    const auto callback = live->events.loaded;
    if (callback) {
      try { callback(uri); }
      catch (...) { report_error(live, result::failure("E_CALLBACK", "Load callback failed")); }
    }
  }});
}

- (void)webView:(WKWebView*)webView
    didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error {
  auto state = _state.lock();
  if (!is_live(state) || webView != state->webview || navigation != state->navigation) return;
  state->native_provisional_authorized = false;
  if ([error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled) return;
  state->document_committed = false;
  state->document_failed = true;
  report_error(state, result::failure("E_NAVIGATION", utf8_string(error.localizedDescription)));
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
  [self webView:webView didFailNavigation:navigation withError:error];
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
  auto state = _state.lock();
  if (!is_live(state) || webView != state->webview) return;
  if (state->terminal_failure.exchange(true, std::memory_order_acq_rel)) return;
  state->native_provisional_authorized = false;
  state->document_committed = false;
  // Terminal teardown has priority and cannot be invalidated by navigation.
  const auto queued = enqueue_job(state, {job_kind::terminal, 0,
      [](const std::shared_ptr<macos_state>& live) {
    deliver_error(live, result::failure("E_WEB_PROCESS", "The WebKit content process terminated"));
    // Do not silently reload with a stale bridge token or continue a dead window.
    shut_down(live, true);
  }});
  if (!queued.ok) shut_down(state, true);
}

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
    forNavigationAction:(WKNavigationAction*)action windowFeatures:(WKWindowFeatures*)features {
  return nil;
}

- (void)webViewDidClose:(WKWebView*)webView {
  auto state = _state.lock();
  if (is_live(state) && webView == state->webview) shut_down(state, true);
}

- (void)webView:(WKWebView*)webView
    navigationAction:(WKNavigationAction*)action didBecomeDownload:(WKDownload*)download {
  [download cancel:nil];
}

- (void)webView:(WKWebView*)webView
    navigationResponse:(WKNavigationResponse*)response didBecomeDownload:(WKDownload*)download {
  [download cancel:nil];
}

- (void)webView:(WKWebView*)webView
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge*)challenge
    completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential*))completionHandler {
  completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
}

- (void)webView:(WKWebView*)webView
    requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin
    initiatedByFrame:(WKFrameInfo*)frame type:(WKMediaCaptureType)type
    decisionHandler:(void (^)(WKPermissionDecision))decisionHandler {
  decisionHandler(WKPermissionDecisionDeny);
}

- (void)webView:(WKWebView*)webView
    runOpenPanelWithParameters:(WKOpenPanelParameters*)parameters
    initiatedByFrame:(WKFrameInfo*)frame
    completionHandler:(void (^)(NSArray<NSURL*>*))completionHandler {
  // A directory selection by native business code is not a web upload grant.
  completionHandler(nil);
}

- (void)webView:(WKWebView*)webView runJavaScriptAlertPanelWithMessage:(NSString*)message
    initiatedByFrame:(WKFrameInfo*)frame completionHandler:(void (^)(void))completionHandler {
  completionHandler();
}

- (void)webView:(WKWebView*)webView runJavaScriptConfirmPanelWithMessage:(NSString*)message
    initiatedByFrame:(WKFrameInfo*)frame completionHandler:(void (^)(BOOL))completionHandler {
  completionHandler(NO);
}

- (void)webView:(WKWebView*)webView runJavaScriptTextInputPanelWithPrompt:(NSString*)prompt
    defaultText:(NSString*)defaultText initiatedByFrame:(WKFrameInfo*)frame
    completionHandler:(void (^)(NSString*))completionHandler {
  completionHandler(nil);
}

@end

namespace soui::desktop::detail {
namespace {

// Nested AppKit loops can consume stop:. Repeat it after the final modal loop
// unwinds, and do not restore process-global menus/delegates while they run.
struct modal_guard {
  std::shared_ptr<macos_application> group;
  explicit modal_guard(std::shared_ptr<macos_application> value) : group(std::move(value)) {
    ++group->modal_depth;
  }
  ~modal_guard() {
    --group->modal_depth;
    stop_event_loop(group);
    restore_application(group);
  }
};

NSMenu* make_native_menu_level(const std::vector<menu_item>& items, id target,
                              std::set<std::string>& ids, std::size_t& count,
                              std::size_t depth, bool ancestors_enabled) {
  if (depth > 4 || count > 64 || items.size() > 64 - count) return nil;
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
  if (!menu) return nil;
  menu.autoenablesItems = NO;
  for (const auto& item : items) {
    if (++count > 64) return nil;
    if (item.separator) {
      if (!item.id.empty() || !item.label.empty() || !item.children.empty()) return nil;
      [menu addItem:[NSMenuItem separatorItem]];
      continue;
    }
    NSString* label = cocoa_string(item.label);
    if (item.label.empty() || item.label.size() > 4096 ||
        item.label.find('\0') != std::string::npos || !label) return nil;
    const bool enabled = ancestors_enabled && item.enabled;
    if (!item.children.empty()) {
      if (!item.id.empty() || item.checked) return nil;
      NSMenu* submenu = make_native_menu_level(item.children, target, ids, count,
                                              depth + 1, enabled);
      if (!submenu) return nil;
      NSMenuItem* native = [[NSMenuItem alloc] initWithTitle:label
          action:NULL keyEquivalent:@""];
      if (!native) return nil;
      native.enabled = enabled;
      // NSMenu owns its items, and each item owns its submenu. ARC releases
      // the entire partial tree if any later node fails validation.
      native.submenu = submenu;
      [menu addItem:native];
      continue;
    }
    NSString* command = cocoa_string(item.id);
    if (item.id.empty() || item.id.size() > 64 || !command || !ids.insert(item.id).second)
      return nil;
    for (unsigned char character : item.id)
      if (!((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-')) return nil;
    NSMenuItem* native = [[NSMenuItem alloc] initWithTitle:label
        action:@selector(onNativeMenuCommand:) keyEquivalent:@""];
    if (!native) return nil;
    native.target = target;
    native.representedObject = command;
    native.enabled = enabled;
    native.state = item.checked ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:native];
  }
  return menu;
}

NSMenu* make_native_menu(const std::vector<menu_item>& items, id target) {
  std::set<std::string> ids;
  std::size_t count = 0;
  return make_native_menu_level(items, target, ids, count, 1, true);
}

class wkwebview_backend final : public backend {
 public:
  wkwebview_backend() : state_(std::make_shared<macos_state>()) {}

  ~wkwebview_backend() override {
    // The public contract requires main-thread destruction. Still prevent late
    // callbacks even if a client violates it, and dispose AppKit objects on main.
    state_->accepting_work.store(false, std::memory_order_release);
    const auto state = state_;
    if ([NSThread isMainThread]) {
      @autoreleasepool {
        const auto cleanup = shut_down(state, false);
        if (!cleanup.ok)
          std::fprintf(stderr, "SOUI: final window shortcut cleanup failed: %s\n",
                       cleanup.message.c_str());
        if (state->loop_owner && state->application_group) {
          state->application_group->quit_requested = true;
          stop_event_loop(state->application_group);
          restore_application(state->application_group);
        }
      }
    } else {
      schedule_main_block([state] {
        const auto cleanup = shut_down(state, false);
        if (!cleanup.ok)
          std::fprintf(stderr, "SOUI: final window shortcut cleanup failed: %s\n",
                       cleanup.message.c_str());
        if (state->loop_owner && state->application_group) {
          state->application_group->quit_requested = true;
          stop_event_loop(state->application_group);
          restore_application(state->application_group);
        }
      });
    }
  }

  result create(const options& settings, backend_events events) override {
    auto thread = require_main_thread();
    if (!thread.ok) return thread;
    if (state_->created || state_->shutdown_started)
      return result::failure("E_STATE", "A backend cannot be created twice or after closing");
    if (!settings.user_data_directory.empty())
      return result::failure("E_UNSUPPORTED",
          "WKWebView uses an ephemeral data store; custom user_data_directory is unsupported");
    if (settings.width <= 0 || settings.height <= 0)
      return result::failure("E_ARGUMENT", "Window dimensions must be positive");
    @autoreleasepool {
      if (@available(macOS 12.0, *)) {
        NSString* title = cocoa_string(settings.title);
        std::error_code error;
        auto root = std::filesystem::canonical(settings.resource_root, error);
        if (error || !std::filesystem::is_directory(root, error) || error)
          return result::failure("E_RESOURCE_ROOT", "Resource root must be an existing directory");
        NSString* root_path = cocoa_string(root.native());
        if (!title || !root_path)
          return result::failure("E_UTF8", "Title and resource root must be valid UTF-8");
        NSApplication* app = [NSApplication sharedApplication];
        auto group = current_application.lock();
        if (group && group->retired) group.reset();
        if (!group) {
          if (app.isRunning || app.delegate)
            return result::failure("E_APPLICATION_IN_USE",
                "Soui::Desktop requires ownership of the macOS application event loop");
          group = std::make_shared<macos_application>();
          group->application = app;
          group->previous_activation = app.activationPolicy;
          group->previous_menu = app.mainMenu;
          if (![app setActivationPolicy:NSApplicationActivationPolicyRegular])
            return result::failure("E_APPLICATION", "Could not activate a regular macOS application");
          group->delegate = [[SOUIDesktopApplicationDelegate alloc] initWithGroup:group];
          group->installed_menu = application_menu();
          group->request_quit = events.quit_requested;
          group->controls_installed = true;
          app.mainMenu = group->installed_menu;
          app.delegate = group->delegate;
          current_application = group;
          state_->loop_owner = true;
        } else if (group->quit_requested || app.delegate != group->delegate) {
          return result::failure("E_APPLICATION_IN_USE", "The macOS application group is unavailable");
        }
        state_->application_group = group;
        state_->events = std::move(events);
        state_->enable_resource_server = settings.enable_resource_server;
        state_->resource_root = [NSURL fileURLWithPath:root_path isDirectory:YES];
        SOUIDesktopWKDelegate* delegate = [[SOUIDesktopWKDelegate alloc] initWithState:state_];
        state_->delegate = delegate;
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        config.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        config.preferences.javaScriptCanOpenWindowsAutomatically = NO;
        [config.userContentController addScriptMessageHandler:delegate name:@"soui"];
        const NSRect rect = NSMakeRect(0, 0, settings.width, settings.height);
        state_->window = [[NSWindow alloc] initWithContentRect:rect
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        state_->window.releasedWhenClosed = NO;
        state_->webview = [[WKWebView alloc] initWithFrame:rect configuration:config];
        if (!state_->window || !state_->webview || !state_->delegate) {
          shut_down(state_, false);
          if (state_->loop_owner) {
            group->quit_requested = true;
            restore_application(group);
          }
          return result::failure("E_WEBVIEW_CREATE", "Could not create the native WKWebView window");
        }
        state_->webview.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        state_->webview.navigationDelegate = delegate;
        state_->webview.UIDelegate = delegate;
        state_->window.contentView = state_->webview;
        state_->window.title = title;
        state_->window.delegate = delegate;
        [state_->window center];
        state_->created = true;
        state_->accepting_work.store(true, std::memory_order_release);
        state_->last_window_state = observe_window_state(state_).value;
        if (settings.visible) {
          [state_->window makeKeyAndOrderFront:nil];
          [app activateIgnoringOtherApps:YES];
        }
        return result::success();
      } else {
        return result::failure("E_UNSUPPORTED", "The WKWebView backend requires macOS 12.0");
      }
    }
  }

  result load(std::string uri, std::string preload) override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    @autoreleasepool {
      NSString* uri_string = cocoa_string(uri);
      NSString* script_string = cocoa_string(preload);
      if (!uri_string || !script_string)
        return result::failure("E_UTF8", "Navigation URI and preload must be valid UTF-8");
      NSURL* url = [NSURL URLWithString:uri_string];
      if (!allows_main_document(state_, url))
        return result::failure("E_NAVIGATION_BLOCKED", "Navigation is outside the selected document");
      ++state_->generation;
      state_->native_navigation_ticket = true;
      state_->native_provisional_authorized = false;
      state_->document_committed = false;
      // Only a new native load reopens JavaScript delivery after a failed page.
      state_->document_failed = false;
      state_->reload_pending = false;
      discard_jobs(state_, false);
      // Cancellation belongs to the previous request, not this new generation.
      state_->navigation = nil;
      [state_->webview stopLoading];
      WKUserContentController* controller = state_->webview.configuration.userContentController;
      [controller removeAllUserScripts];
      WKUserScript* script = [[WKUserScript alloc] initWithSource:script_string
          injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES];
      [controller addUserScript:script];
      if (url.isFileURL) {
        // The sole file-system read grant is the canonical configured resource root.
        state_->navigation =
            [state_->webview loadFileURL:url allowingReadAccessToURL:state_->resource_root];
      } else {
        // Only the opt-in loopback URL admitted above reaches the HTTP loader.
        // Do not change ATS or grant file access to the HTTP document.
        state_->navigation = [state_->webview loadRequest:[NSURLRequest requestWithURL:url]];
      }
      if (!state_->navigation) {
        state_->native_navigation_ticket = false;
        state_->document_failed = true;
        return result::failure("E_NAVIGATION", "WKWebView did not start the selected navigation");
      }
      return result::success();
    }
  }

  result run() override {
    auto thread = require_main_thread();
    if (!thread.ok) return thread;
    const auto group = state_->application_group;
    if (!state_->created || !state_->loop_owner || !group)
      return result::failure("E_STATE", "Only the initialized application owner can run the loop");
    if (group->quit_requested) return result::success();
    if (group->running || group->application.isRunning)
      return result::failure("E_STATE", "The macOS event loop is already running");
    @autoreleasepool {
      while (!group->quit_requested) {
        group->running = true;
        [group->application run];
        group->running = false;
        if (!group->quit_requested) {
          const auto callback = group->request_quit;
          if (callback) { try { callback(); } catch (...) {} }
          // A native stop is only a quit proposal. Common code sets the flag
          // through quit_loop() after approval; a veto resumes event handling.
        }
      }
      restore_application(group);
    }
    return result::success();
  }

  result quit_loop() override {
    auto thread = require_main_thread();
    if (!thread.ok) return thread;
    const auto group = state_->application_group;
    if (!group) return result::failure("E_STATE", "The application has not been initialized");
    group->quit_requested = true;
    stop_event_loop(group);
    restore_application(group);
    return result::success();
  }

  result close() override {
    auto thread = require_main_thread();
    if (!thread.ok) return thread;
    @autoreleasepool { return shut_down(state_, true); }
  }

  result evaluate(std::string script) override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    // The common layer's ready flag is not revoked by every backend load error.
    // Reject failed/reloading documents before submitting JavaScript, while
    // retaining normal document-start evaluation before the commit callback.
    if (state_->document_failed || state_->reload_pending)
      return result::failure("E_NAVIGATION", "The current document is not available for JavaScript");
    @autoreleasepool {
      NSString* source = cocoa_string(script);
      if (!source) return result::failure("E_UTF8", "JavaScript must be valid UTF-8");
      const std::weak_ptr<macos_state> weak_state = state_;
      const auto generation = state_->generation;
      [state_->webview evaluateJavaScript:source completionHandler:^(id value, NSError* error) {
        auto state = weak_state.lock();
        if (!is_live(state) || generation != state->generation ||
            state->document_failed || state->reload_pending || !error) return;
        report_error(state, result::failure("E_JAVASCRIPT", utf8_string(error.localizedDescription)));
      }];
      return result::success();
    }
  }

  result dispatch(std::function<void()> work) override {
    if (!work) return result::failure("E_ARGUMENT", "Dispatch requires a callback");
    return enqueue_job(state_, {job_kind::native_work, 0,
        [work = std::move(work)](const std::shared_ptr<macos_state>& state) {
      try { work(); }
      catch (const std::exception& error) {
        report_error(state, result::failure("E_CALLBACK", error.what()));
      } catch (...) {
        report_error(state, result::failure("E_CALLBACK", "Dispatched callback failed"));
      }
    }});
  }

  result set_title(std::string_view title) override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    @autoreleasepool {
      NSString* text = cocoa_string(title);
      if (!text) return result::failure("E_UTF8", "Window title must be valid UTF-8");
      state_->window.title = text;
    }
    return result::success();
  }

  result resize(int width, int height) override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    if (width <= 0 || height <= 0)
      return result::failure("E_ARGUMENT", "Window dimensions must be positive");
    [state_->window setContentSize:NSMakeSize(width, height)];
    return result::success();
  }

  result show(bool visible) override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    if (visible) [state_->window makeKeyAndOrderFront:nil];
    else [state_->window orderOut:nil];
    return result::success();
  }

  result focus() override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    [state_->window deminiaturize:nil];
    [state_->window makeKeyAndOrderFront:nil];
    [state_->window makeFirstResponder:state_->webview];
    [state_->application_group->application activateIgnoringOtherApps:YES];
    return result::success();
  }

  result minimize() override {
    const auto state = state_;
    auto observed = observe_window_state(state);
    if (!observed.status.ok) return observed.status;
    if (observed.value.minimized) return result::success();
    NSWindow* __attribute__((objc_precise_lifetime)) window = state->window;
    if (!(window.styleMask & NSWindowStyleMaskMiniaturizable))
      return result::failure("not_supported", "This native window cannot be minimized");
    [window miniaturize:nil];
    // Completion and any subsequent changes are reported only by observation.
    if (!is_live(state)) return result::failure("E_CLOSED", "The owning window closed");
    schedule_window_state_observation(state);
    return result::success();
  }

  result maximize() override {
    const auto state = state_;
    auto observed = observe_window_state(state);
    if (!observed.status.ok) return observed.status;
    NSWindow* __attribute__((objc_precise_lifetime)) window = state->window;
    if (!(window.styleMask & NSWindowStyleMaskResizable) ||
        (window.styleMask & NSWindowStyleMaskFullScreen))
      return result::failure("not_supported", "Windowed maximization is unavailable");
    if (window.miniaturized) [window deminiaturize:nil];
    if (!is_live(state)) return result::failure("E_CLOSED", "The owning window closed");
    // zoom: toggles, so an unconditional call would undo a prior maximize.
    if (!window.zoomed) [window zoom:nil];
    if (!is_live(state)) return result::failure("E_CLOSED", "The owning window closed");
    schedule_window_state_observation(state);
    return result::success();
  }

  result restore() override {
    const auto state = state_;
    auto observed = observe_window_state(state);
    if (!observed.status.ok) return observed.status;
    NSWindow* __attribute__((objc_precise_lifetime)) window = state->window;
    if (window.styleMask & NSWindowStyleMaskFullScreen)
      return result::failure("not_supported", "Full-screen restoration is outside window controls");
    if (window.miniaturized) [window deminiaturize:nil];
    if (!is_live(state)) return result::failure("E_CLOSED", "The owning window closed");
    if (window.zoomed) [window zoom:nil];
    if (!is_live(state)) return result::failure("E_CLOSED", "The owning window closed");
    schedule_window_state_observation(state);
    return result::success();
  }

  window_state_result get_window_state() const override {
    return observe_window_state(state_);
  }

  value_result clipboard_read() override {
    auto open = require_open(state_);
    if (!open.ok) return {open, {}};
    @autoreleasepool {
      NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
      if (!text) return {result::failure("E_CLIPBOARD_FORMAT", "Clipboard contains no text"), {}};
      constexpr NSUInteger maximum_bytes = 1024 * 1024;
      if (text.length > maximum_bytes ||
          [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > maximum_bytes)
        return {result::failure("E_CLIPBOARD_SIZE", "Clipboard text exceeds 1 MiB"), {}};
      NSData* data = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
      if (!data)
        return {result::failure("E_UTF8", "Clipboard text is not valid Unicode"), {}};
      const auto value = data.length
          ? std::string(static_cast<const char*>(data.bytes), data.length) : std::string{};
      if (value.find('\0') != std::string::npos)
        return {result::failure("E_UTF8", "Clipboard text contains NUL"), {}};
      return {result::success(), value};
    }
  }

  result clipboard_write(std::string_view text) override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    @autoreleasepool {
      NSString* value = cocoa_string(text);
      if (!value) return result::failure("E_UTF8", "Clipboard text must be valid UTF-8");
      NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
      [pasteboard clearContents];
      if (![pasteboard setString:value forType:NSPasteboardTypeString])
        return result::failure("E_CLIPBOARD", "Could not write the system clipboard");
      return result::success();
    }
  }

  value_result choose_directory() override {
    auto open = require_open(state_);
    if (!open.ok) return {open, {}};
    if (state_->directory_panel)
      return {result::failure("E_BUSY", "A directory picker is already open"), {}};
    @autoreleasepool {
      // Keep state and panel alive if shutdown runs in the nested modal loop.
      const auto state = state_;
      NSOpenPanel* panel = [NSOpenPanel openPanel];
      panel.canChooseDirectories = YES;
      panel.canChooseFiles = NO;
      panel.canCreateDirectories = NO;
      panel.allowsMultipleSelection = NO;
      panel.resolvesAliases = YES;
      panel.title = @"Choose a directory";
      state->directory_panel = panel;
      modal_guard guard(state->application_group);
      const auto response = [panel runModal];
      if (state->directory_panel == panel) state->directory_panel = nil;
      if (!is_live(state)) {
        // stop: called inside runModal may stop that loop rather than run().
        return {result::failure("E_CLOSED", "The window closed while choosing a directory"), {}};
      }
      if (response != NSModalResponseOK)
        return {result::failure("E_CANCELLED", "Directory selection was cancelled"), {}};
      NSURL* selected = panel.URL;
      if (!selected || !selected.isFileURL)
        return {result::failure("E_DIRECTORY", "The selected directory is not a local file URL"), {}};
      const auto path = utf8_string(selected.path);
      std::error_code error;
      if (path.empty() || !std::filesystem::is_directory(std::filesystem::u8path(path), error) || error)
        return {result::failure("E_DIRECTORY", "The selected directory is unavailable"), {}};
      // Returning a path does not expand WKWebView's file-access grant.
      return {result::success(), path};
    }
  }

  result popup_menu(const std::vector<menu_item>& items) override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    if (state_->application_group->modal_depth)
      return result::failure("E_BUSY", "A native modal operation is already active");
    @autoreleasepool {
      if (items.empty()) {
        [state_->popup cancelTrackingWithoutAnimation];
        ++state_->popup_generation;
        state_->popup = nil;
        return result::success();
      }
      if (!state_->window.visible)
        return result::failure("E_WINDOW_HIDDEN", "Show the owning window before opening a popup");
      NSMenu* menu = make_native_menu(items, state_->delegate);
      if (!menu) return result::failure("E_ARGUMENT", "Invalid native menu items");
      [state_->popup cancelTrackingWithoutAnimation];
      ++state_->popup_generation;
      state_->popup = menu;
      modal_guard guard(state_->application_group);
      [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, state_->webview.bounds.size.height)
                             inView:state_->webview];
      if (!is_live(state_)) return result::failure("E_CLOSED", "The owning window closed");
      return result::success();
    }
  }

  result set_tray(const tray_options& settings) override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    @autoreleasepool {
      NSString* label = cocoa_string(settings.label);
      NSString* tooltip = cocoa_string(settings.tooltip);
      NSMenu* menu = make_native_menu(settings.menu, state_->delegate);
      if (!label || !label.length || settings.label.size() > 256 || !tooltip ||
          settings.tooltip.size() > 4096 || !menu)
        return result::failure("E_ARGUMENT", "Invalid native tray options");
      NSStatusItem* tray = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
      if (!tray || !tray.button) {
        if (tray) [[NSStatusBar systemStatusBar] removeStatusItem:tray];
        return result::failure("E_TRAY_UNAVAILABLE", "AppKit did not create a status item");
      }
      tray.button.title = label;
      tray.button.toolTip = tooltip;
      tray.menu = menu;
      remove_tray(state_);
      state_->tray = tray;
      state_->tray_menu = menu;
      return result::success();
    }
  }

  result clear_tray() override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    @autoreleasepool { remove_tray(state_); }
    return result::success();
  }

  result register_global_shortcut(std::uint64_t token, const global_shortcut& shortcut) override {
    auto thread = require_main_thread();
    if (!thread.ok) return thread;
    if (!is_live(state_))
      return result::failure("E_CLOSED", "The macOS desktop window is not open");
    if (!token) return result::failure("E_ARGUMENT", "A global shortcut token cannot be zero");
    if (state_->shortcuts.count(token))
      return result::failure("E_SHORTCUT_DUPLICATE", "The global shortcut token is already registered");
    if (state_->shortcuts.size() >= 32)
      return result::failure("E_SHORTCUT_LIMIT", "The window global shortcut limit is 32");
    UInt32 key = 0;
    UInt32 modifiers = 0;
    auto mapped = native_shortcut(shortcut, key, modifiers);
    if (!mapped.ok) return mapped;

    auto& router = hotkey_router();
    // Failed unregisters may outlive their windows. Keep those resource records,
    // but never let repeated failed close/recreate cycles grow without bound.
    if (router.registrations.size() >= 1024)
      return result::failure("E_SHORTCUT_LIMIT", "The process global shortcut limit is 1024");
    for (const auto& entry : router.registrations) {
      const auto& registration = entry.second;
      if (registration->key_code == key && registration->modifiers == modifiers)
        return result::failure("E_SHORTCUT_CONFLICT", "The global shortcut is already owned by this application");
    }
    if (router.next_id > std::numeric_limits<UInt32>::max())
      return result::failure("E_SHORTCUT_LIMIT", "The native global shortcut identity space is exhausted");
    if (!router.handler) {
      const EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
      const auto status = InstallEventHandler(GetApplicationEventTarget(), &handle_global_shortcut,
                                             1, &type, &router, &router.handler);
      if (status != noErr)
        return hotkey_error("E_SHORTCUT_REGISTER", "Could not install the global shortcut handler", status);
    }

    // Reserve both ownership records before the native registration. No allocation
    // after OS success is required to retain the resource or make it active.
    const auto native_id = static_cast<UInt32>(router.next_id++);
    try {
      const auto registration = std::make_shared<macos_hotkey>();
      registration->owner = state_;
      registration->token = token;
      registration->native_id = native_id;
      registration->key_code = key;
      registration->modifiers = modifiers;
      state_->shortcuts.emplace(token, registration);
      try {
        router.registrations.emplace(native_id, registration);
      } catch (...) {
        state_->shortcuts.erase(token);
        throw;
      }
      const EventHotKeyID identity{shortcut_signature, native_id};
      const auto status = RegisterEventHotKey(key, modifiers, identity, GetApplicationEventTarget(),
                                             0, &registration->reference);
      if (status != noErr) {
        // Normally Carbon returns no reference on failure. If it does return a
        // resource, keep an inactive record unless native rollback succeeds.
        const auto cleanup = release_shortcut(state_, token);
        if (!cleanup.ok)
          return result::failure("shortcut_cleanup_pending",
              "Global shortcut registration failed (OSStatus " + std::to_string(status) +
              "); " + cleanup.message);
        return hotkey_error(status == eventHotKeyExistsErr ? "E_SHORTCUT_CONFLICT" : "E_SHORTCUT_REGISTER",
                            "Could not register the global shortcut", status);
      }
      registration->active = true;
      return result::success();
    } catch (...) {
      return result::failure("E_SHORTCUT_REGISTER", "Could not allocate the global shortcut registration");
    }
  }

  result unregister_global_shortcut(std::uint64_t token) override {
    auto thread = require_main_thread();
    if (!thread.ok) return thread;
    // Cleanup retry remains possible after close and renderer termination.
    return release_shortcut(state_, token);
  }

  capabilities get_capabilities() const override {
    return {"wkwebview", true, true, true, true, true, true, true, true, true, true, true, true};
  }

 private:
  const std::shared_ptr<macos_state> state_;
};

}  // namespace

std::unique_ptr<backend> make_backend() {
  return std::make_unique<wkwebview_backend>();
}

}  // namespace soui::desktop::detail
