#include <soui/host/webview_host.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using soui::sdk::host_config;
using soui::sdk::host_result;
using soui::sdk::webview_host;
using soui::sdk::webview_host_adapter;
using soui::runtime::lifecycle_state;

static_assert(std::is_same_v<webview_host, soui::sdk::soui_miniblink_host>);

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void require_error(const host_result& result, const char* code, const char* message) {
  require(!result.ok && result.code == code, message);
}

void require_same_result(const host_result& result, const host_result& expected,
                         const char* message) {
  require(result.ok == expected.ok && result.code == expected.code &&
              result.message == expected.message, message);
}

class resource_fixture {
 public:
  resource_fixture() {
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto parent = std::filesystem::temp_directory_path();
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
      const auto candidate = parent / ("soui-host-adapter-tests-" + std::to_string(seed) +
                                       "-" + std::to_string(attempt));
      if (std::filesystem::create_directory(candidate)) {
        root_ = candidate;
        break;
      }
    }
    require(!root_.empty(), "could not create exclusive resource fixture");
    std::ofstream index(root_ / "index.html", std::ios::binary);
    index << "<!doctype html><html><body>controlled adapter fixture</body></html>";
    index.close();
    if (!index) {
      std::error_code ignored;
      std::filesystem::remove_all(root_, ignored);
      throw std::runtime_error("could not write resource fixture");
    }
  }

  resource_fixture(const resource_fixture&) = delete;
  resource_fixture& operator=(const resource_fixture&) = delete;
  ~resource_fixture() {
    std::error_code ignored;
    // root_ is only assigned after this fixture exclusively creates it.
    std::filesystem::remove_all(root_, ignored);
  }

  host_config config() const {
    host_config result;
    result.architecture = sizeof(void*) == 8 ? "x64" : "x86";
    result.resource_root = root_;
    return result;
  }

 private:
  std::filesystem::path root_;
};

struct adapter_state {
  host_result mount_result{true, "MOUNTED", "fake renderer mounted"};
  host_result unmount_result{true, "UNMOUNTED", "fake renderer unmounted"};
  host_result show_result{true, "VISIBILITY", "fake visibility changed"};
  host_result reload_result{true, "RELOADED", "fake renderer reloaded"};
  host_result resize_result{true, "RESIZED", "fake dimensions applied"};
  host_result focus_result{true, "FOCUSED", "fake renderer focused"};
  host_config mounted_config;
  std::string session_id;
  std::function<void()> on_unmount;
  bool mounted{false};
  bool visible{true};
  bool document_ready{true};
  bool application_ready{true};
  int mount_calls{0};
  int unmount_calls{0};
  int show_calls{0};
  int reload_calls{0};
  int resize_calls{0};
  int focus_calls{0};
  int pump_calls{0};
  int document_queries{0};
  int application_queries{0};
  int diagnostic_queries{0};
  int session_updates{0};
  int width{0};
  int height{0};
  int delivered_events{0};
  int discarded_events{0};
  int released_events{0};
  int event_value{0};
};

class fake_adapter final : public webview_host_adapter {
 public:
  explicit fake_adapter(std::shared_ptr<adapter_state> state) : state_(std::move(state)) {}

  host_result mount(const host_config& config) override {
    ++state_->mount_calls;
    state_->mounted_config = config;
    if (state_->mount_result.ok) state_->mounted = true;
    return state_->mount_result;
  }
  host_result unmount() override {
    ++state_->unmount_calls;
    if (state_->on_unmount) state_->on_unmount();
    if (state_->unmount_result.ok) state_->mounted = false;
    return state_->unmount_result;
  }
  host_result show(bool visible) override {
    ++state_->show_calls;
    if (state_->show_result.ok) state_->visible = visible;
    return state_->show_result;
  }
  host_result reload() override {
    ++state_->reload_calls;
    return state_->reload_result;
  }
  host_result resize(int width, int height) override {
    ++state_->resize_calls;
    state_->width = width;
    state_->height = height;
    return state_->resize_result;
  }
  host_result focus() override {
    ++state_->focus_calls;
    return state_->focus_result;
  }
  void pump() override { ++state_->pump_calls; }
  bool document_ready() const noexcept override {
    ++state_->document_queries;
    return state_->document_ready;
  }
  bool application_ready() override {
    ++state_->application_queries;
    return state_->application_ready;
  }
  std::string application_diagnostics() override {
    ++state_->diagnostic_queries;
    return "fake renderer diagnostics";
  }
  void set_session_id(std::string session_id) override {
    ++state_->session_updates;
    state_->session_id = std::move(session_id);
  }
  void* make_posted_event(int value) {
    return std::make_unique<pending_event>(state_, value).release();
  }
  void deliver_posted_event(void* payload) override {
    const std::unique_ptr<pending_event> event(static_cast<pending_event*>(payload));
    if (!event) return;
    if (state_->mounted) {
      ++state_->delivered_events;
      state_->event_value = event->value;
    } else {
      ++state_->discarded_events;
    }
  }

 private:
  struct pending_event {
    pending_event(std::shared_ptr<adapter_state> owner, int data)
        : state(std::move(owner)), value(data) {}
    ~pending_event() { ++state->released_events; }
    std::shared_ptr<adapter_state> state;
    int value;
  };
  std::shared_ptr<adapter_state> state_;
};

class minimal_adapter final : public webview_host_adapter {
 public:
  host_result mount(const host_config&) override { return {true, {}, {}}; }
  host_result unmount() override { ++unmount_calls; return {true, {}, {}}; }
  int unmount_calls{0};
};

void require_inactive(webview_host& host, const adapter_state& state, const char* code) {
  const auto show_calls = state.show_calls;
  const auto reload_calls = state.reload_calls;
  const auto resize_calls = state.resize_calls;
  const auto focus_calls = state.focus_calls;
  const auto unmount_calls = state.unmount_calls;
  const auto pump_calls = state.pump_calls;
  const auto document_queries = state.document_queries;
  const auto application_queries = state.application_queries;
  const auto diagnostic_queries = state.diagnostic_queries;
  require_error(host.hide(), code, "inactive hide must fail");
  require_error(host.reload(), code, "inactive reload must fail");
  require_error(host.resize(30, 40), code, "inactive resize must fail");
  require_error(host.focus(), code, "inactive focus must fail");
  require_error(host.close(), code, "inactive close must fail");
  require(host.destroy().ok, "inactive destroy remains idempotent");
  host.pump();
  require(!host.document_ready() && !host.application_ready(), "inactive host must not report readiness");
  require(host.application_diagnostics().empty(), "inactive diagnostics must be empty");
  require(state.show_calls == show_calls && state.reload_calls == reload_calls &&
              state.resize_calls == resize_calls && state.focus_calls == focus_calls &&
              state.unmount_calls == unmount_calls && state.pump_calls == pump_calls &&
              state.document_queries == document_queries &&
              state.application_queries == application_queries &&
              state.diagnostic_queries == diagnostic_queries,
          "inactive operations must not reach the adapter");
}

void test_optional_operations(const host_config& config) {
  auto adapter = std::make_unique<minimal_adapter>();
  auto* observed = adapter.get();
  webview_host host(std::move(adapter));
  require(host.start(config).ok, "mount-only adapter starts");
  const auto session_id = host.session()->session_id();
  require_error(host.hide(), "E_NOT_SUPPORTED", "unimplemented visibility cannot report success");
  require_error(host.reload(), "E_NOT_SUPPORTED", "unimplemented reload cannot report success");
  require_error(host.resize(640, 480), "E_NOT_SUPPORTED", "unimplemented resize cannot report success");
  require_error(host.focus(), "E_NOT_SUPPORTED", "unimplemented focus cannot report success");
  require(host.session()->state() == lifecycle_state::ready &&
              host.session()->session_id() == session_id,
          "unsupported operations must preserve the session");
  require(!host.document_ready() && !host.application_ready() &&
              host.application_diagnostics().empty(), "default readiness must be false");
  host.pump();
  require(host.destroy().ok && host.destroy().ok && observed->unmount_calls == 1,
          "destroy must unmount exactly once");
}

void test_forwarding_and_lifecycle(const host_config& config) {
  auto state = std::make_shared<adapter_state>();
  auto adapter = std::make_unique<fake_adapter>(state);
  auto* producer = adapter.get();
  {
    webview_host host(std::move(adapter));
    require_inactive(host, *state, "E_NOT_STARTED");
    require_same_result(host.start(config), state->mount_result, "mount result must be preserved");
    require(state->mount_calls == 1 && state->mounted_config.architecture == config.architecture &&
                state->mounted_config.resource_root == config.resource_root &&
                state->mounted_config.owner_window == config.owner_window,
            "mount must receive the supplied config");
    require(state->session_updates == 1 && state->session_id == host.session()->session_id(),
            "adapter must receive the initial session");
    require_error(host.start(config), "E_ALREADY_STARTED", "live host cannot mount again");
    require_same_result(host.resize(800, 600), state->resize_result, "resize result must be preserved");
    require(state->resize_calls == 1 && state->width == 800 && state->height == 600,
            "resize arguments must reach custom adapter");
    require_same_result(host.focus(), state->focus_result, "focus result must be preserved");
    require_same_result(host.hide(), state->show_result, "visibility result must be preserved");
    require(state->show_calls == 1 && !state->visible && host.session()->state() == lifecycle_state::hidden,
            "hide must invoke show(false) and update session");
    host.pump();
    require(state->pump_calls == 1 && host.document_ready() && host.application_ready(),
            "pump and readiness must reach custom adapter");
    state->document_ready = false;
    state->application_ready = false;
    require(!host.document_ready() && !host.application_ready(), "adapter readiness false must propagate");
    require(host.application_diagnostics() == "fake renderer diagnostics", "diagnostics must be forwarded");
    host.deliver_posted_event(producer->make_posted_event(42));
    require(state->event_value == 42 && state->delivered_events == 1 && state->released_events == 1,
            "active event must be delivered and consumed exactly once");
    const auto old_session = host.session()->session_id();
    require_same_result(host.reload(), state->reload_result, "reload result must be preserved");
    require(host.session()->state() == lifecycle_state::reloaded &&
                host.session()->session_id() != old_session && state->session_updates == 2 &&
                state->session_id == host.session()->session_id(),
            "reload must notify the adapter about the replacement session");
    void* late_event = producer->make_posted_event(99);
    auto subscription = host.session()->events().subscribe([](const auto&) {});
    require_same_result(host.close(), state->unmount_result, "close must preserve unmount result");
    require(host.session()->state() == lifecycle_state::closed && state->unmount_calls == 1,
            "close transitions only after unmount");
    require_inactive(host, *state, "E_INVALID_LIFECYCLE");
    require(!subscription.active(), "destroy after close must release session subscriptions");
    host.deliver_posted_event(late_event);
    require(state->discarded_events == 1 && state->released_events == 2 && state->delivered_events == 1,
            "late event must be consumed without dispatch after close");
    require(host.start(config).ok && state->mount_calls == 2, "closed host can restart");
    require(host.destroy().ok, "restarted host can be destroyed");
    require_inactive(host, *state, "E_INVALID_LIFECYCLE");
  }
  require(state->unmount_calls == 2, "destructor must not unmount terminal sessions twice");
}

void test_errors_and_retry(const host_config& config) {
  auto state = std::make_shared<adapter_state>();
  webview_host host(std::make_unique<fake_adapter>(state));
  state->mount_result = {false, "E_FAKE_MOUNT", "mount rejected"};
  require_same_result(host.start(config), state->mount_result, "mount error must propagate");
  require(host.session() == nullptr && state->session_updates == 0, "failed mount cannot publish a session");
  state->mount_result = {true, {}, {}};
  require(host.start(config).ok, "mount can retry after failure");
  const auto old_session = host.session()->session_id();

  state->show_result = {false, "E_FAKE_SHOW", "visibility rejected"};
  require_same_result(host.hide(), state->show_result, "show error must propagate");
  require(host.session()->state() == lifecycle_state::ready, "failed hide must preserve ready state");
  state->show_result = {true, {}, {}};
  require(host.hide().ok && host.session()->state() == lifecycle_state::hidden, "hide can retry");

  state->reload_result = {false, "E_FAKE_RELOAD", "reload rejected"};
  require_same_result(host.reload(), state->reload_result, "reload error must propagate");
  require(host.session()->state() == lifecycle_state::hidden && host.session()->session_id() == old_session &&
              state->session_updates == 1, "failed reload must preserve state and session identity");
  state->reload_result = {true, {}, {}};
  require(host.reload().ok && host.session()->session_id() != old_session, "reload can retry");

  state->resize_result = {false, "E_FAKE_RESIZE", "resize rejected"};
  state->focus_result = {false, "E_FAKE_FOCUS", "focus rejected"};
  require_same_result(host.resize(900, 700), state->resize_result, "resize error must propagate");
  require_same_result(host.focus(), state->focus_result, "focus error must propagate");

  state->unmount_result = {false, "E_FAKE_UNMOUNT", "unmount rejected"};
  require_same_result(host.close(), state->unmount_result, "close error must propagate");
  require(!host.session()->terminal() && host.session()->state() == lifecycle_state::reloaded,
          "failed close cannot make the session terminal");
  require_same_result(host.destroy(), state->unmount_result, "destroy error must propagate");
  require(!host.session()->terminal() && state->unmount_calls == 2,
          "failed destroy must allow another unmount attempt");
  state->unmount_result = {true, {}, {}};
  require(host.destroy().ok && host.session()->state() == lifecycle_state::destroyed &&
              state->unmount_calls == 3, "destroy retry must finish teardown");
  require(host.destroy().ok && state->unmount_calls == 3, "successful cleanup cannot repeat unmount");
}

void test_close_retry_and_reentrancy(const host_config& config) {
  auto state = std::make_shared<adapter_state>();
  webview_host host(std::make_unique<fake_adapter>(state));
  require(host.start(config).ok, "start for close retry");
  state->unmount_result = {false, "E_FAKE_UNMOUNT", "retry close"};
  require_error(host.close(), "E_FAKE_UNMOUNT", "first close fails");
  host_result nested_close;
  host_result nested_destroy;
  host_result nested_start;
  state->on_unmount = [&] {
    nested_close = host.close();
    nested_destroy = host.destroy();
    nested_start = host.start(config);
  };
  state->unmount_result = {true, {}, {}};
  require(host.close().ok && host.session()->terminal(), "close retry succeeds");
  require_error(nested_close, "E_BUSY", "reentrant close cannot unmount twice");
  require_error(nested_destroy, "E_BUSY", "reentrant destroy cannot unmount twice");
  require_error(nested_start, "E_BUSY", "reentrant start cannot replace the adapter session");
  require(state->unmount_calls == 2, "only the failed close and successful retry reach unmount");
  state->on_unmount = {};
}

void test_destructor_retries_failed_close(const host_config& config) {
  auto state = std::make_shared<adapter_state>();
  {
    webview_host host(std::make_unique<fake_adapter>(state));
    require(host.start(config).ok, "start for destructor retry");
    state->unmount_result = {false, "E_FAKE_UNMOUNT", "close rejected"};
    require_error(host.close(), "E_FAKE_UNMOUNT", "close must fail before destructor retry");
    state->unmount_result = {true, {}, {}};
  }
  require(state->unmount_calls == 2 && !state->mounted, "destructor retries a failed close");
}

void test_validation(const host_config& config) {
  webview_host missing(nullptr);
  require_error(missing.start(config), "E_HOST_ADAPTER_MISSING", "missing adapter must be rejected");
  require(missing.destroy().ok, "missing adapter cleanup is harmless");
  auto state = std::make_shared<adapter_state>();
  webview_host host(std::make_unique<fake_adapter>(state));
  auto invalid = config;
  invalid.architecture = "unknown";
  require_error(host.start(invalid), "E_INVALID_ARCHITECTURE", "architecture gate is retained");
  invalid = config;
  invalid.resource_root /= "missing";
  require_error(host.start(invalid), "E_RESOURCE_MISSING", "controlled local resource gate is retained");
  require(state->mount_calls == 0, "invalid config must not mount custom adapter");
}

}  // namespace

int main() {
  try {
    const resource_fixture fixture;
    auto config = fixture.config();
    int owner_marker = 0;
    config.owner_window = &owner_marker; // opaque identity, never used as a real HWND
    test_optional_operations(config);
    test_forwarding_and_lifecycle(config);
    test_errors_and_retry(config);
    test_close_retry_and_reentrancy(config);
    test_destructor_retries_failed_close(config);
    test_validation(config);
    std::cout << "host adapter contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
