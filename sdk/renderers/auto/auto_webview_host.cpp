#include <soui/host/auto_webview_host.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/host/webview2_host.h>
#include "auto_host_policy.h"
#include "../webview2/loader_policy.h"
#include <WebView2.h>
#include "../webview2/webview2_loader_bridge.h"
#include <windows.h>
#include <objbase.h>
#include <exception>
#include <fstream>
#include <utility>

namespace soui::sdk {
namespace {
host_result success() { return {true, {}, {}}; }
host_result failure(const char* code, const char* message) { return {false, code, message}; }
host_result busy() { return failure("E_BUSY", "Auto host shutdown retains resources; keep dispatching owner messages and retry."); }
host_result owner_sta(DWORD owner) {
  if (GetCurrentThreadId() != owner) return failure("E_THREAD", "Auto host calls require the constructing UI STA.");
  APTTYPE type{};
  APTTYPEQUALIFIER qualifier{};
  if (FAILED(CoGetApartmentType(&type, &qualifier)) || (type != APTTYPE_STA && type != APTTYPE_MAINSTA))
    return failure("E_COM_APARTMENT", "Initialize COM as STA before starting an auto host.");
  return success();
}
host_result resource_path(const std::filesystem::path& path) {
  if (path.empty() || !path.is_absolute()) return failure("E_INVALID_PATH", "Selected resource root must be absolute.");
  std::error_code error;
  if (!std::filesystem::is_directory(path, error) || error ||
      !std::filesystem::is_regular_file(path / "index.html", error) || error)
    return failure("E_RESOURCE_MISSING", "Selected resource root must contain index.html.");
  std::ifstream input(path / "index.html", std::ios::binary);
  return input ? success() : failure("E_RESOURCE_READ", "Selected index.html could not be read.");
}
host_result profile_path(const std::filesystem::path& path) {
  if (path.empty() || !path.is_absolute())
    return failure("E_INVALID_PATH", "WebView2 user data directory must be absolute.");
  // A new profile is valid, but its nearest existing ancestor must be a directory.
  // Do not create it or infer listing/write permission before the Runtime.
  auto current = path;
  for (;;) {
    const auto attributes = GetFileAttributesW(current.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
      if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return failure("E_INVALID_PATH", "WebView2 user data path contains an existing file instead of a directory.");
      return success();
    }
    const auto error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
      const auto code = error == ERROR_INVALID_NAME || error == ERROR_BAD_PATHNAME ||
          error == ERROR_FILENAME_EXCED_RANGE || error == ERROR_DIRECTORY ? "E_INVALID_PATH" : "E_PROFILE_ACCESS";
      return {false, code, "WebView2 user data path query failed: Win32 error " + std::to_string(error) + "."};
    }
    const auto parent = current.parent_path();
    if (parent.empty() || parent == current)
      return failure("E_INVALID_PATH", "WebView2 user data path has no existing directory ancestor.");
    current = parent;
  }
}
struct active_call final {
  unsigned& depth;
  explicit active_call(unsigned& value) : depth(value) { ++depth; }
  ~active_call() { --depth; }
};
}

webview2_runtime_discovery inspect_webview2_runtime(const std::filesystem::path& fixed_runtime) {
  webview2_runtime_discovery output;
  output.result = detail::auto_fixed_runtime_path(fixed_runtime);
  if (!output.result.ok) return output;
  try { output.result = detail::check_webview2_loader_policy(); }
  catch (...) { output.result = failure("E_WEBVIEW2_LOADER_POLICY", "WebView2 loader policy could not be established."); }
  if (!output.result.ok) return output;
  struct task_string final {
    LPWSTR value{};
    ~task_string() { CoTaskMemFree(value); }
  } version;
  auto native_runtime = fixed_runtime;
  native_runtime.make_preferred();
  const auto result = detail::webview2_loader_bridge::get_available_version(
      native_runtime.empty() ? nullptr : native_runtime.c_str(), &version.value);
  std::string text;
  if (result == S_OK && version.value) {
    const auto length = wcsnlen_s(version.value, 512);
    if (length && length < 512) {
      const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, version.value,
          static_cast<int>(length), nullptr, 0, nullptr, nullptr);
      if (count > 0) {
        text.resize(static_cast<std::size_t>(count));
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, version.value,
            static_cast<int>(length), text.data(), count, nullptr, nullptr) != count) text.clear();
      }
    }
  }
  return detail::classify_auto_discovery(result, std::move(text));
}

struct auto_webview_host::state {
  const DWORD owner{GetCurrentThreadId()};
  auto_host_config config;
  runtime_selection selected;
  webview2_runtime_discovery discovered;
  host_result current{false, "E_NOT_STARTED", "Auto host has not started."};
  std::function<void(host_result)> completed;
  std::unique_ptr<webview_host> host;
  webview2_adapter* modern{}; // owned by host
  std::unique_ptr<webview2_runtime_exit_watch> exit_watch;
  unsigned active{};
  bool admitted{}, completion_sent{}, preparing{}, preparation_started{}, closing{}, closed{}, mounted{};

  void complete(host_result result) {
    if (completion_sent) return;
    completion_sent = true;
    current = std::move(result);
    auto callback = std::move(completed);
    if (callback) {
      const active_call call(active);
      try { callback(current); }
      catch (...) { current = failure("E_CALLBACK", "Auto host startup completion threw an exception."); }
    }
  }

  host_result select() {
    const auto environment = inspect_runtime_environment();
    if (config.host.architecture != environment.process_architecture)
      return failure("E_INVALID_ARCHITECTURE", "Host architecture must match the current x86/x64 process.");
    const auto window = static_cast<HWND>(config.host.owner_window);
    if (!IsWindow(window) || GetWindowThreadProcessId(window, nullptr) != owner || !config.host.bridge_adapter)
      return failure("E_INVALID_PARAMS", "Auto host requires its live UI owner window and a borrowed Bridge.");
    auto valid = detail::auto_fixed_runtime_path(config.webview2_fixed_runtime);
    if (!valid.ok) return valid;
    // Validate policy/OS before loader discovery. Unknown OS/PE/architecture and
    // conflicting modern requirements fail; they do not trigger compatibility.
    const auto candidate = select_runtime_profile(environment, {true, true}, config.requested, config.requires_modern_web);
    if (!candidate.ok) { selected = candidate; return {false, candidate.code, candidate.reason}; }
    if (candidate.backend == renderer_backend::webview2) {
      if (config.webview2_user_data.empty() || !config.webview2_user_data.is_absolute())
        return failure("E_INVALID_PATH", "WebView2 user data directory must be absolute.");
      discovered = inspect_webview2_runtime(config.webview2_fixed_runtime);
      if (!discovered.result.ok) return discovered.result;
    }
    std::error_code error;
    const bool local_runtime = !config.host.miniblink_binary.empty() && config.host.miniblink_binary.is_absolute() &&
        std::filesystem::is_regular_file(config.host.miniblink_binary, error) && !error;
    selected = select_runtime_profile(environment, {local_runtime, discovered.available}, config.requested, config.requires_modern_web);
    if (!selected.ok) return {false, selected.code, selected.reason};
    config.host.resource_root = selected.backend == renderer_backend::webview2 ?
        config.modern_resource_root : config.compatibility_resource_root;
    valid = resource_path(config.host.resource_root);
    if (!valid.ok) return valid;
    if (selected.backend == renderer_backend::miniblink &&
        !binary_matches_architecture(config.host.miniblink_binary, config.host.architecture))
      return failure("E_MINIBLINK_BINARY", "The application-local MiniBlink binary is unreadable or has the wrong architecture.");
    return success();
  }

  void prepared(host_result result) {
    const active_call call(active);
    preparing = false;
    try {
      // Cancellation suppresses mounting, not acquisition of the successful
      // initializer's real exit identity. Acquire before any controller close.
      if (result.ok) result = modern->acquire_runtime_exit_watch(exit_watch);
      if (result.ok && exit_watch->observe().phase == webview2_runtime_exit_phase::failed)
        result = failure("E_RUNTIME_WATCH_FAILED", "WebView2 exit identity failed before mount.");
      if (result.ok && closing) result = failure("E_CANCELLED", "Auto host startup was cancelled.");
      if (result.ok) {
        result = host->start(config.host);
        mounted = result.ok;
      }
    } catch (...) {
      result = failure("E_AUTO_START", "Selected backend preparation completion failed; retain it for cleanup.");
    }
    // Runtime/permission/resource/controller errors retain the selected backend.
    // They never become a second startup attempt or replay business actions.
    complete(std::move(result));
  }
  host_result running() const {
    if (GetCurrentThreadId() != owner) return failure("E_THREAD", "Auto host calls require the constructing UI STA.");
    if (completion_sent && !current.ok) return current;
    if (!mounted || closing || closed) return failure("E_NOT_READY", "Auto host is not running.");
    return success();
  }
};

auto_webview_host::auto_webview_host() : state_(std::make_unique<state>()) {}
auto_webview_host::~auto_webview_host() {
  if (GetCurrentThreadId() != state_->owner || state_->active) std::terminate();
  if (state_->admitted && !state_->closed && !shutdown().ok) std::terminate();
}

host_result auto_webview_host::start_async(const auto_host_config& config,
    std::function<void(host_result)> completed) {
  auto& value = *state_;
  const auto thread = owner_sta(value.owner);
  if (!thread.ok) return thread;
  if (value.admitted || value.closing) return failure("E_ALREADY_STARTED", "Auto host startup is single-shot.");
  if (!completed) return failure("E_INVALID_PARAMS", "Auto host startup requires a completion callback.");
  value.admitted = true;
  value.current = failure("E_PENDING", "Auto host is preparing its selected backend.");
  value.completed = std::move(completed);
  const active_call call(value.active);
  try {
    value.config = config;
    auto result = value.select();
    if (!result.ok) { value.complete(std::move(result)); return success(); }
    if (value.selected.backend == renderer_backend::webview2) {
      result = profile_path(config.webview2_user_data);
      if (!result.ok) { value.complete(std::move(result)); return success(); }
      auto adapter = std::make_unique<webview2_adapter>(config.host.owner_window,
          config.webview2_user_data, std::string{}, config.webview2_fixed_runtime);
      value.modern = adapter.get();
      value.host = std::make_unique<webview_host>(std::move(adapter));
      result = value.modern->configure_profile_access(webview2_profile_access::exclusive);
      if (!result.ok) { value.complete(std::move(result)); return success(); }
      value.preparing = true;
      value.preparation_started = true;
      value.modern->prepare_async([&value](host_result prepared) { value.prepared(std::move(prepared)); });
    } else {
      value.host = std::make_unique<webview_host>(std::make_unique<dynamic_miniblink_adapter>());
      result = value.host->start(value.config.host);
      value.mounted = result.ok;
      if (value.closing) result = failure("E_CANCELLED", "Auto host startup was cancelled.");
      value.complete(std::move(result));
    }
  } catch (...) { value.complete(failure("E_AUTO_START", "Auto host startup failed; the selected backend is retained for cleanup.")); }
  return success();
}

host_result auto_webview_host::shutdown() {
  auto& value = *state_;
  if (GetCurrentThreadId() != value.owner) return failure("E_THREAD", "Auto host shutdown requires the constructing UI STA.");
  value.closing = true;
  if (value.active) return busy();
  if (value.closed) return success();
  // The current backend's environment callback may return a real browser
  // identity. Logical cancellation forbids mount, but keeps that initializer
  // alive until its original callback supplies the cleanup owner. Premature
  // unmount would suppress the identity needed for an exclusive exit watch.
  if (value.preparing) return busy();
  const active_call call(value.active);
  if (value.host) {
    auto result = value.host->destroy();
    if (!result.ok) return result.code == "E_CURSOR_CLOSE_PENDING" ? busy() : result;
    // A prepared adapter can own a controller even when Host::start was never
    // called. The Host does not know about that pre-mount ownership.
    if (value.modern) {
      result = value.modern->unmount();
      if (!result.ok) return result;
      if (value.preparation_started && !value.exit_watch) {
        result = value.modern->acquire_runtime_exit_watch(value.exit_watch);
        if (!result.ok && !(result.code == "E_RUNTIME_WATCH_UNAVAILABLE" &&
            value.modern->failed_preparation_cleanup_complete())) return result;
      }
      if (value.exit_watch) {
        const auto observed = value.exit_watch->observe();
        if (observed.phase == webview2_runtime_exit_phase::failed)
          return failure("E_RUNTIME_WATCH_FAILED", "WebView2 did not provide a successful exclusive runtime exit barrier.");
        result = value.exit_watch->finish();
        if (!result.ok) return result;
      }
    }
  }
  if (!value.completion_sent && value.admitted)
    value.complete(failure("E_CANCELLED", "Auto host startup was cancelled."));
  value.mounted = false;
  value.closed = true;
  return success();
}

bool auto_webview_host::shutdown_complete() const noexcept { return state_->closed; }
host_result auto_webview_host::status() const {
  if (GetCurrentThreadId() != state_->owner) return failure("E_THREAD", "Auto host status requires the constructing UI STA.");
  if (!state_->current.ok) return state_->current;
  if (state_->modern && !state_->closed) return state_->modern->status();
  return state_->current;
}
const runtime_selection& auto_webview_host::selection() const noexcept { return state_->selected; }
const webview2_runtime_discovery& auto_webview_host::discovery() const noexcept { return state_->discovered; }
std::string auto_webview_host::runtime_version() const {
  if (GetCurrentThreadId() != state_->owner) return {};
  return state_->modern ? state_->modern->runtime_version() : std::string{};
}
void auto_webview_host::pump() {
  if (GetCurrentThreadId() != state_->owner) return;
  const active_call call(state_->active);
  if (state_->host) state_->host->pump();
}
host_result auto_webview_host::resize(int width, int height) {
  const auto result = state_->running();
  if (!result.ok) return result;
  const active_call call(state_->active);
  return state_->host->resize(width, height);
}
host_result auto_webview_host::focus() {
  const auto result = state_->running();
  if (!result.ok) return result;
  const active_call call(state_->active);
  return state_->host->focus();
}
host_result auto_webview_host::show() {
  const auto result = state_->running();
  if (!result.ok) return result;
  const active_call call(state_->active);
  return state_->host->show();
}
host_result auto_webview_host::hide() {
  const auto result = state_->running();
  if (!result.ok) return result;
  const active_call call(state_->active);
  return state_->host->hide();
}
host_result auto_webview_host::reload() {
  const auto result = state_->running();
  if (!result.ok) return result;
  const active_call call(state_->active);
  return state_->host->reload();
}
void auto_webview_host::deliver_posted_event(void* payload) {
  if (GetCurrentThreadId() != state_->owner) std::terminate(); // Cannot abandon an owned payload.
  const active_call call(state_->active);
  if (state_->host) state_->host->deliver_posted_event(payload);
}
bool auto_webview_host::document_ready() const noexcept {
  return GetCurrentThreadId() == state_->owner && state_->current.ok && state_->mounted &&
      !state_->closing && state_->host->document_ready();
}
bool auto_webview_host::application_ready() {
  if (!state_->running().ok) return false;
  const active_call call(state_->active);
  return state_->host->application_ready();
}
std::string auto_webview_host::application_diagnostics() {
  if (GetCurrentThreadId() != state_->owner || !state_->host) return {};
  const active_call call(state_->active);
  return state_->host->application_diagnostics();
}

} // namespace soui::sdk
