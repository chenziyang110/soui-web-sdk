#include "../backend.h"

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
  bool running{false};
  bool owns_application{false};
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
  NSApplication* application{nil};
  NSWindow* window{nil};
  WKWebView* webview{nil};
  WKNavigation* navigation{nil};
  NSURL* resource_root{nil};
  NSOpenPanel* directory_panel{nil};
  id delegate{nil};
  NSMenu* previous_menu{nil};
  NSMenu* installed_menu{nil};
  NSApplicationActivationPolicy previous_activation{NSApplicationActivationPolicyProhibited};
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
      !url || !url.isFileURL || !url.absoluteString ||
      !state->events.allow_navigation) return false;
  const auto callback = state->events.allow_navigation;
  try {
    const bool allowed = callback(utf8_string(url.absoluteString));
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

void stop_event_loop(const std::shared_ptr<macos_state>& state) {
  if (!state->running || !state->application) return;
  [state->application stop:nil];
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
  [state->application postEvent:wake atStart:YES];
}

void shut_down(const std::shared_ptr<macos_state>& state, bool notify) {
  const bool was_live = state->accepting_work.exchange(false, std::memory_order_acq_rel);
  ++state->generation;
  state->native_navigation_ticket = false;
  state->native_provisional_authorized = false;
  state->document_committed = false;
  state->reload_pending = false;
  discard_jobs(state, true);
  auto callbacks = std::move(state->events);
  state->events = {};

  // Invalidate the common host/session before releasing either native object.
  if (was_live && notify && callbacks.closed) {
    try { callbacks.closed(); } catch (...) {}
  }
  [state->directory_panel cancel:nil];
  state->directory_panel = nil;
  [state->webview stopLoading];
  state->webview.navigationDelegate = nil;
  state->webview.UIDelegate = nil;
  [state->webview.configuration.userContentController removeScriptMessageHandlerForName:@"soui"];
  [state->webview.configuration.userContentController removeAllUserScripts];
  [state->webview removeFromSuperview];
  state->window.delegate = nil;
  state->window.contentView = nil;
  [state->window orderOut:nil];
  [state->window close];
  state->navigation = nil;
  state->webview = nil;
  state->window = nil;
  state->resource_root = nil;
  if (state->owns_application) {
    if (state->application.delegate == state->delegate) state->application.delegate = nil;
    if (state->application.mainMenu == state->installed_menu)
      state->application.mainMenu = state->previous_menu;
    [state->application setActivationPolicy:state->previous_activation];
    state->owns_application = false;
  }
  state->delegate = nil;
  state->installed_menu = nil;
  state->previous_menu = nil;
  stop_event_loop(state);
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

NSMenu* application_menu(NSWindow* window) {
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
  close.target = window;
  window_item.submenu = window_menu;
  [menu addItem:window_item];
  return menu;
}

}  // namespace

@interface SOUIDesktopWKDelegate : NSObject
    <NSApplicationDelegate, NSWindowDelegate, WKNavigationDelegate,
     WKUIDelegate, WKScriptMessageHandler> {
 @private
  std::weak_ptr<macos_state> _state;
}
- (instancetype)initWithState:(const std::shared_ptr<macos_state>&)state;
@end

@implementation SOUIDesktopWKDelegate

- (instancetype)initWithState:(const std::shared_ptr<macos_state>&)state {
  self = [super init];
  if (self) _state = state;
  return self;
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
  auto state = _state.lock();
  if (state && sender == state->window) shut_down(state, true);
  return NO;  // shut_down owns the close sequence.
}

- (void)windowWillClose:(NSNotification*)notification {
  auto state = _state.lock();
  if (state && notification.object == state->window) shut_down(state, true);
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  auto state = _state.lock();
  if (state && sender == state->application) shut_down(state, true);
  // Return from application::run() so C++ destructors run; never call exit().
  return NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  return NO;
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

class wkwebview_backend final : public backend {
 public:
  wkwebview_backend() : state_(std::make_shared<macos_state>()) {}

  ~wkwebview_backend() override {
    // The public contract requires main-thread destruction. Still prevent late
    // callbacks even if a client violates it, and dispose AppKit objects on main.
    state_->accepting_work.store(false, std::memory_order_release);
    const auto state = state_;
    if ([NSThread isMainThread]) {
      @autoreleasepool { shut_down(state, false); }
    } else {
      schedule_main_block([state] { shut_down(state, false); });
    }
  }

  result create(const options& settings, backend_events events) override {
    auto thread = require_main_thread();
    if (!thread.ok) return thread;
    if (state_->created) return result::failure("E_STATE", "A backend cannot be created twice");
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
        if (app.isRunning || app.delegate)
          return result::failure("E_APPLICATION_IN_USE",
              "Soui::Desktop requires ownership of the macOS application event loop");
        state_->application = app;
        state_->previous_activation = app.activationPolicy;
        state_->previous_menu = app.mainMenu;
        if (![app setActivationPolicy:NSApplicationActivationPolicyRegular])
          return result::failure("E_APPLICATION", "Could not activate a regular macOS application");
        state_->owns_application = true;
        state_->events = std::move(events);
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
          return result::failure("E_WEBVIEW_CREATE", "Could not create the native WKWebView window");
        }
        state_->webview.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        state_->webview.navigationDelegate = delegate;
        state_->webview.UIDelegate = delegate;
        state_->window.contentView = state_->webview;
        state_->window.title = title;
        state_->window.delegate = delegate;
        [state_->window center];
        state_->installed_menu = application_menu(state_->window);
        app.mainMenu = state_->installed_menu;
        app.delegate = delegate;
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
      // The sole file-system read grant is the canonical configured resource root.
      state_->navigation = [state_->webview loadFileURL:url allowingReadAccessToURL:state_->resource_root];
      if (!state_->navigation) {
        state_->native_navigation_ticket = false;
        return result::failure("E_NAVIGATION", "WKWebView did not start the file navigation");
      }
      return result::success();
    }
  }

  result run() override {
    auto open = require_open(state_);
    if (!open.ok) return open;
    if (state_->running || state_->application.isRunning)
      return result::failure("E_STATE", "The macOS event loop is already running");
    @autoreleasepool {
      state_->running = true;
      [state_->application run];
      state_->running = false;
      // An external stop must not leave the common host appearing live.
      if (is_live(state_)) shut_down(state_, true);
    }
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
    [state_->application activateIgnoringOtherApps:YES];
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
      const auto response = [panel runModal];
      if (state->directory_panel == panel) state->directory_panel = nil;
      if (!is_live(state)) {
        // stop: called inside runModal may stop that loop rather than run().
        stop_event_loop(state);
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

  capabilities get_capabilities() const override {
    return {"wkwebview", true, true, true, true, true};
  }

 private:
  const std::shared_ptr<macos_state> state_;
};

}  // namespace

std::unique_ptr<backend> make_backend() {
  return std::make_unique<wkwebview_backend>();
}

}  // namespace soui::desktop::detail
