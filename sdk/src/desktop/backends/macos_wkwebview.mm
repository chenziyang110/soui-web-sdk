#include "../backend.h"
#include "../resource_uri.h"

#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <exception>
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
  bool reload_pending{false};
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

void shut_down(const std::shared_ptr<macos_state>& state, bool notify,
               window_close_origin origin = window_close_origin::backend) {
  // Native cancellation and close can synchronously notify observers. Do not
  // enter cleanup twice, even before the public closed callback is delivered.
  if (state->shutdown_started) return;
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
  if (was_live && notify && callbacks.closed) {
    try { callbacks.closed(); } catch (...) {}
  }
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
  if (state && closing_window == state->window) shut_down(state, true);
  return NO;  // shut_down owns the close sequence.
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

- (void)onNativeMenuCommand:(NSMenuItem*)sender {
  try {
    auto state = _state.lock();
    if (!is_live(state) || !sender.enabled ||
        (sender.menu != state->popup && sender.menu != state->tray_menu) ||
        ![sender.representedObject isKindOfClass:[NSString class]]) return;
    const auto command = utf8_string((NSString*)sender.representedObject);
    const bool tray = sender.menu == state->tray_menu;
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

NSMenu* make_native_menu(const std::vector<menu_item>& items, id target, bool allow_empty) {
  if (items.size() > 64 || (!allow_empty && items.empty())) return nil;
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
  menu.autoenablesItems = NO;
  std::set<std::string> ids;
  for (const auto& item : items) {
    if (item.separator) {
      [menu addItem:[NSMenuItem separatorItem]];
      continue;
    }
    NSString* label = cocoa_string(item.label);
    NSString* command = cocoa_string(item.id);
    if (item.id.empty() || item.id.size() > 64 || item.label.empty() ||
        item.label.size() > 4096 || !label || !command || !ids.insert(item.id).second)
      return nil;
    for (unsigned char character : item.id)
      if (!((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-')) return nil;
    NSMenuItem* native = [[NSMenuItem alloc] initWithTitle:label
        action:@selector(onNativeMenuCommand:) keyEquivalent:@""];
    native.target = target;
    native.representedObject = command;
    native.enabled = item.enabled;
    native.state = item.checked ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:native];
  }
  return menu;
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
        shut_down(state, false);
        if (state->loop_owner && state->application_group) {
          state->application_group->quit_requested = true;
          stop_event_loop(state->application_group);
          restore_application(state->application_group);
        }
      }
    } else {
      schedule_main_block([state] {
        shut_down(state, false);
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
      state_->reload_pending = false;
      discard_jobs(state_, false);
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
      group->running = true;
      [group->application run];
      group->running = false;
      if (!group->quit_requested) {
        const auto callback = group->request_quit;
        if (callback) { try { callback(); } catch (...) {} }
        group->quit_requested = true;
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
    @autoreleasepool { shut_down(state_, true); }
    return result::success();
  }

  result evaluate(std::string script) override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    @autoreleasepool {
      NSString* source = cocoa_string(script);
      if (!source) return result::failure("E_UTF8", "JavaScript must be valid UTF-8");
      const std::weak_ptr<macos_state> weak_state = state_;
      const auto generation = state_->generation;
      [state_->webview evaluateJavaScript:source completionHandler:^(id value, NSError* error) {
        auto state = weak_state.lock();
        if (!is_live(state) || generation != state->generation || !error) return;
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
    if (!state_->window.visible)
      return result::failure("E_WINDOW_HIDDEN", "Show the owning window before opening a popup");
    @autoreleasepool {
      NSMenu* menu = make_native_menu(items, state_->delegate, false);
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
      NSMenu* menu = make_native_menu(settings.menu, state_->delegate, true);
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

  capabilities get_capabilities() const override {
    return {"wkwebview", true, true, true, true, true, true, true};
  }

 private:
  const std::shared_ptr<macos_state> state_;
};

}  // namespace

std::unique_ptr<backend> make_backend() {
  return std::make_unique<wkwebview_backend>();
}

}  // namespace soui::desktop::detail
