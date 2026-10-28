// One translation unit exercises the actual private callback and COM-retirement
// functions. No browser, HWND, loader entry point, or business worker is started.
#include "../../sdk/renderers/webview2/webview2_host.cpp"

#include <iostream>

namespace {
using namespace soui::sdk;
using operation = detail::preparation_lifetime::operation;
void require(bool condition, const char* reason) {
  if (!condition) throw std::runtime_error(reason);
}
struct observations {
  unsigned destroyed{}, removed{}, added{};
  std::function<void()> on_release;
};
class fake_environment final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ICoreWebView2Environment5> {
 public:
  explicit fake_environment(observations& output) : output_(output) {}
  ~fake_environment() {
    ++output_.destroyed;
    if (output_.on_release) output_.on_release();
  }
  HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(IStream*, int, LPCWSTR, LPCWSTR, ICoreWebView2WebResourceResponse**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_NewBrowserVersionAvailable(ICoreWebView2NewBrowserVersionAvailableEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_NewBrowserVersionAvailable(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateWebResourceRequest(LPCWSTR, LPCWSTR, IStream*, LPCWSTR, ICoreWebView2WebResourceRequest**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateCoreWebView2CompositionController(HWND, ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateCoreWebView2PointerInfo(ICoreWebView2PointerInfo**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetAutomationProviderForWindow(HWND, IUnknown**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_BrowserProcessExited(ICoreWebView2BrowserProcessExitedEventHandler*, EventRegistrationToken*) override { ++output_.added; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_BrowserProcessExited(EventRegistrationToken) override { ++output_.removed; return S_OK; }
 private:
  observations& output_;
};
struct fixture {
  observations observed;
  std::shared_ptr<webview2_adapter::state> value = std::make_shared<webview2_adapter::state>();
  unsigned completions{};
  host_result delivered;
  fixture() {
    value->profile_access = webview2_profile_access::exclusive;
    value->current = webview2_adapter::state::phase::preparing;
    value->exit_monitor = std::make_shared<webview2_runtime_exit_watch::state>();
    auto environment = Microsoft::WRL::Make<fake_environment>(observed);
    require(bool(environment), "Fake Environment allocation");
    value->environment = environment;
    value->exit_monitor->environment = environment;
    require(SUCCEEDED(environment.As(&value->exit_monitor->environment_identity)), "Fake COM identity");
    require(value->preparation.begin(operation::controller), "Controller submission is owned");
    value->prepared = [this](host_result result) { ++completions; delivered = std::move(result); };
  }
  void cancel() {
    value->preparation.cancel(); value->current = webview2_adapter::state::phase::closing;
    finish_preparation(value, error("E_CANCELLED", "WebView2 preparation was cancelled."));
  }
  ~fixture() {
    // Synthetic refs only: ensure an assertion failure remains a diagnostic
    // instead of the production monitor's fail-stop hiding the failed assertion.
    observed.on_release = {};
    if (value->exit_monitor) {
      value->exit_monitor->environment.Reset();
      value->exit_monitor->environment_identity.Reset();
      value->exit_monitor->registered = false;
      value->exit_monitor->callbacks = 0;
      value->exit_monitor.reset();
    }
    value->environment.Reset();
  }
};

void cancelled_failure() {
  fixture f; f.cancel();
  const std::weak_ptr<webview2_runtime_exit_watch::state> monitor(f.value->exit_monitor);
  require(retire_failed_controller_preparation(f.value).code == "E_BUSY", "Pending creation retains Environment");
  require(SUCCEEDED(complete_controller_preparation(f.value, E_ABORT, nullptr)), "Receive cancelled failure");
  require(f.value->cancelled_controller_failure == E_ABORT, "Accepted cancelled failure must retain the original HRESULT");
  require(f.value->preparation.settled() && f.completions == 1 && f.delivered.code == "E_CANCELLED", "Native completion does not replace cancellation");
  require(SUCCEEDED(complete_controller_preparation(f.value, E_ACCESSDENIED, nullptr)) &&
      f.value->cancelled_controller_failure == E_ABORT, "Duplicate failure cannot overwrite the original fact");
  bool reentry_busy = false, release_unlocked = false;
  const std::weak_ptr<webview2_adapter::state> weak(f.value);
  f.observed.on_release = [&] {
    const auto current = weak.lock();
    reentry_busy = current && retire_failed_controller_preparation(current).code == "E_BUSY";
    if (const auto held = monitor.lock()) {
      release_unlocked = held->mutex.try_lock();
      if (release_unlocked) held->mutex.unlock();
    }
  };
  require(retire_failed_controller_preparation(f.value).ok, "Settled failed initialization retires its local refs");
  require(f.value->failed_preparation_cleanup_done && monitor.expired() && !f.value->environment &&
      f.observed.destroyed == 1 && reentry_busy && release_unlocked, "Actual COM refs retire outside scalar lock; reentry stays busy");
  require(f.observed.added == 0 && f.observed.removed == 0 && f.completions == 1,
      "No browser event, fabricated unregister or second completion");
}
void unsupported_and_duplicate_results() {
  fixture f; f.cancel();
  require(SUCCEEDED(complete_controller_preparation(f.value, S_OK, nullptr)), "Receive unknown success without object");
  require(SUCCEEDED(complete_controller_preparation(f.value, E_FAIL, nullptr)), "Reject duplicate completion");
  require(SUCCEEDED(f.value->cancelled_controller_failure), "Unknown/duplicate results do not manufacture failure eligibility");
  require(retire_failed_controller_preparation(f.value).ok && !f.value->failed_preparation_cleanup_done &&
      f.value->exit_monitor && f.observed.destroyed == 0, "Unknown ownership remains retained");
}
void settled_submission_is_not_completion() {
  fixture f; require(f.value->preparation.settle(operation::controller), "Synchronous submission failed"); f.cancel();
  require(SUCCEEDED(complete_controller_preparation(f.value, E_FAIL, nullptr)) &&
      SUCCEEDED(f.value->cancelled_controller_failure), "Unaccepted callback cannot invent a native outcome");
}
void missing_cancel_or_owner_is_not_failure_evidence() {
  for (unsigned scenario = 0; scenario != 2; ++scenario) {
    fixture f;
    if (scenario == 0) f.value->current = webview2_adapter::state::phase::closing;
    else { f.cancel(); ++f.value->ui_thread; }
    (void)complete_controller_preparation(f.value, E_FAIL, nullptr);
    require(SUCCEEDED(f.value->cancelled_controller_failure) && !f.value->controller_creation_failed,
        "Closed state or wrong owner cannot stand in for an accepted cancelled failure");
    require(retire_failed_controller_preparation(f.value).ok && !f.value->failed_preparation_cleanup_done &&
        f.observed.destroyed == 0, "Unqualified completion retains synthetic Environment references");
  }
}
void callback_borrow_and_monitor_guards() {
  for (unsigned guard = 0; guard != 7; ++guard) {
    fixture f; require(f.value->preparation.begin(operation::script), "Additional callback ownership"); f.cancel();
    (void)complete_controller_preparation(f.value, E_FAIL, nullptr);
    {
      auto callback = f.value->preparation.receive(operation::script);
      require(bool(callback) && retire_failed_controller_preparation(f.value).code == "E_BUSY", "Active callback prevents release");
    }
    auto& monitor = *f.value->exit_monitor;
    if (guard == 0) monitor.armed = true;
    if (guard == 1) monitor.acquired = true;
    if (guard == 2) monitor.pid = 123;
    if (guard == 3) monitor.event_observed = true;
    if (guard == 4) monitor.failure = webview2_runtime_exit_error::event_read;
    if (guard == 5) monitor.callbacks = 1;
    if (guard == 6) monitor.finishing = true;
    require(!retire_failed_controller_preparation(f.value).ok && !f.value->failed_preparation_cleanup_done &&
        f.observed.destroyed == 0, "Existing browser/monitor evidence cannot be discarded as failed creation");
  }
}
void ordinary_failure() {
  fixture f;
  (void)complete_controller_preparation(f.value, E_FAIL, nullptr);
  require(f.value->controller_creation_failed && f.value->failure.code == "E_WEBVIEW2" && f.completions == 1,
      "Noncancelled failure retains its existing path");
  require(retire_failed_controller_preparation(f.value).ok && f.value->failed_preparation_cleanup_done &&
      f.observed.destroyed == 1, "Existing failed preparation cleanup remains valid");
}
}  // namespace
int main() {
  try {
    cancelled_failure(); unsupported_and_duplicate_results(); settled_submission_is_not_completion();
    missing_cancel_or_owner_is_not_failure_evidence(); callback_borrow_and_monitor_guards(); ordinary_failure();
    std::cout << "Actual Controller completion/retirement: cancellation, refs, duplicate, borrow and monitor guards passed; fake COM only\n";
    return 0;
  } catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
