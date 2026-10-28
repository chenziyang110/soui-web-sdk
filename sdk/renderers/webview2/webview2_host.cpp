#include <soui/host/webview2_host.h>
#include <soui/host/runtime_profile.h>
#include "webview2_preload.h"
#include "request_capacity.h"
#include "preparation_lifetime.h"
#include "loader_policy.h"

#include <windows.h>
#include <bcrypt.h>
#include <WebView2.h>
#include <wrl.h>
#include <WebView2EnvironmentOptions.h>
#include <exception>
#include <iterator>
#include <json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace soui::sdk {
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using json = nlohmann::json;
using clock_type = std::chrono::steady_clock;
using sdk::host_result;
namespace capacity = detail::webview2_capacity;

namespace {
constexpr wchar_t entry_url[] = L"https://soui-webview2.invalid/index.html";
constexpr wchar_t asset_origin[] = L"https://soui-webview2.invalid/";
constexpr std::size_t message_limit = capacity::message_limit;

host_result ok() { return {true, {}, {}}; }
host_result error(const char* code, std::string message) { return {false, code, std::move(message)}; }
std::string hresult_text(HRESULT value) {
  std::ostringstream out;
  out << "HRESULT 0x" << std::hex << static_cast<unsigned long>(value);
  return out.str();
}
void check(HRESULT result, const char* operation) {
  if (FAILED(result)) throw std::runtime_error(std::string(operation) + ": " + hresult_text(result));
}
std::string utf8(const std::wstring& text) {
  if (text.empty()) return {};
  if (text.size() > 1024U * 1024U) throw std::runtime_error("UTF-16 input exceeds limit");
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
      static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (!length) throw std::runtime_error("Invalid UTF-16 input");
  std::string result(static_cast<std::size_t>(length), '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      result.data(), length, nullptr, nullptr)) throw std::runtime_error("UTF-16 conversion failed");
  return result;
}
std::wstring wide(const std::string& text) {
  if (text.empty()) return {};
  if (text.size() > 1024U * 1024U) throw std::runtime_error("UTF-8 input exceeds limit");
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
      static_cast<int>(text.size()), nullptr, 0);
  if (!length) throw std::runtime_error("Invalid UTF-8 input");
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      result.data(), length)) throw std::runtime_error("UTF-8 conversion failed");
  return result;
}
struct task_string {
  LPWSTR value{nullptr};
  ~task_string() { CoTaskMemFree(value); }
};
std::wstring without_fragment(std::wstring value) {
  value.resize(value.find(L'#') == std::wstring::npos ? value.size() : value.find(L'#'));
  return value;
}
bool trusted_entry(const std::wstring& value) { return without_fragment(value) == entry_url; }
bool trusted_resource(const std::wstring& value) {
  return value.rfind(asset_origin, 0) == 0 && value.find(L'\\') == std::wstring::npos;
}
std::string document_identity() {
  GUID value{};
  check(CoCreateGuid(&value), "CoCreateGuid");
  wchar_t buffer[40]{};
  if (!StringFromGUID2(value, buffer, 40)) throw std::runtime_error("Document identity unavailable");
  return utf8(buffer);
}
std::string digest(const std::string& value) {
  struct hash_handles {
    BCRYPT_ALG_HANDLE algorithm{nullptr};
    BCRYPT_HASH_HANDLE hash{nullptr};
    std::vector<unsigned char> object;
    ~hash_handles() {
      if (hash) BCryptDestroyHash(hash);
      if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
  } handles;
  if (BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    throw std::runtime_error("SHA256 provider unavailable");
  ULONG object_size = 0, written = 0;
  if (BCryptGetProperty(handles.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
      sizeof(object_size), &written, 0) < 0 || !object_size)
    throw std::runtime_error("SHA256 object size unavailable");
  handles.object.resize(object_size);
  if (BCryptCreateHash(handles.algorithm, &handles.hash, handles.object.data(), object_size, nullptr, 0, 0) < 0)
    throw std::runtime_error("SHA256 hash unavailable");
  unsigned char bytes[32]{};
  if (BCryptHashData(handles.hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),
      static_cast<ULONG>(value.size()), 0) < 0 ||
      BCryptFinishHash(handles.hash, bytes, sizeof(bytes), 0) < 0)
    throw std::runtime_error("Session digest failed");
  std::ostringstream out;
  for (const auto byte : bytes) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
  return out.str();
}
std::string response_error(const std::string& id, const char* code, const char* message) {
  return json{{"requestId", id}, {"ok", false}, {"error", {{"code", code}, {"message", message}}}}.dump();
}
json parse_bounded(const std::string& raw) {
  return json::parse(raw, [](int depth, json::parse_event_t, json&) {
    if (depth > 32) throw std::runtime_error("JSON nesting exceeds limit");
    return true;
  });
}
}  // namespace

struct webview2_runtime_exit_watch::state {
  DWORD ui_thread{GetCurrentThreadId()};
  ComPtr<ICoreWebView2Environment5> environment;
  ComPtr<IUnknown> environment_identity;
  EventRegistrationToken token{};
  HANDLE process{};
  std::filesystem::path image;
  std::uint32_t pid{};
  std::uint64_t creation{};
  std::mutex mutex;
  webview2_runtime_exit_error failure{webview2_runtime_exit_error::none};
  HRESULT failure_hresult{S_OK};
  DWORD failure_win32{};
  UINT32 event_pid{}, callbacks{};
  webview2_browser_exit_kind exit_kind{webview2_browser_exit_kind::absent};
  bool event_observed{}, signaled{}, registered{}, armed{}, acquired{}, finishing{}, finished{};
  ~state() {
    // All resource-bearing states are owned until explicit successful finish.
    if (environment || environment_identity || process || registered || callbacks) std::terminate();
  }
};

namespace {
using exit_state = webview2_runtime_exit_watch::state;
void exit_failure(const std::shared_ptr<exit_state>& value, webview2_runtime_exit_error failure,
                  HRESULT result = S_OK, DWORD win32_error = 0) noexcept {
  std::lock_guard<std::mutex> lock(value->mutex);
  if (value->failure == webview2_runtime_exit_error::none) {
    value->failure = failure; value->failure_hresult = result; value->failure_win32 = win32_error;
  }
}
struct exit_callback_borrow {
  const std::shared_ptr<exit_state> value;
  explicit exit_callback_borrow(std::shared_ptr<exit_state> input) : value(std::move(input)) {
    std::lock_guard<std::mutex> lock(value->mutex);
    if (value->callbacks == UINT32_MAX) std::terminate();
    ++value->callbacks;
  }
  ~exit_callback_borrow() {
    std::lock_guard<std::mutex> lock(value->mutex);
    if (!value->callbacks) std::terminate();
    --value->callbacks;
  }
};
HRESULT exit_event(const std::shared_ptr<exit_state>& value,
                   ICoreWebView2Environment* sender,
                   ICoreWebView2BrowserProcessExitedEventArgs* args) noexcept {
  try {
    exit_callback_borrow borrow(value);
    if (GetCurrentThreadId() != value->ui_thread) {
      exit_failure(value, webview2_runtime_exit_error::wrong_thread); return S_OK;
    }
    ComPtr<IUnknown> identity;
    const auto identity_result = sender ? sender->QueryInterface(IID_PPV_ARGS(&identity)) : E_POINTER;
    if (FAILED(identity_result) || identity.Get() != value->environment_identity.Get()) {
      exit_failure(value, webview2_runtime_exit_error::sender_identity, identity_result); return S_OK;
    }
    UINT32 pid{};
    COREWEBVIEW2_BROWSER_PROCESS_EXIT_KIND kind{};
    const auto pid_result = args ? args->get_BrowserProcessId(&pid) : E_POINTER;
    const auto kind_result = args ? args->get_BrowserProcessExitKind(&kind) : E_POINTER;
    if (FAILED(pid_result) || FAILED(kind_result)) {
      exit_failure(value, webview2_runtime_exit_error::event_read,
                   FAILED(pid_result) ? pid_result : kind_result); return S_OK;
    }
    std::lock_guard<std::mutex> lock(value->mutex);
    if (!value->event_observed) {
      value->event_observed = true; value->event_pid = pid;
      value->exit_kind = kind == COREWEBVIEW2_BROWSER_PROCESS_EXIT_KIND_NORMAL
          ? webview2_browser_exit_kind::normal : webview2_browser_exit_kind::failed;
    }
    if (value->failure == webview2_runtime_exit_error::none) {
      if (pid != value->pid) value->failure = webview2_runtime_exit_error::browser_changed;
      else if (kind != COREWEBVIEW2_BROWSER_PROCESS_EXIT_KIND_NORMAL)
        value->failure = webview2_runtime_exit_error::abnormal_exit;
    }
    return S_OK;
  } catch (...) {
    exit_failure(value, webview2_runtime_exit_error::callback_exception, E_FAIL); return S_OK;
  }
}
HRESULT arm_exit_monitor(const std::shared_ptr<exit_state>& value, ICoreWebView2* core) noexcept {
  try {
    if (!value || !core) return E_POINTER;
    if (GetCurrentThreadId() != value->ui_thread) {
      exit_failure(value, webview2_runtime_exit_error::wrong_thread); return RPC_E_WRONG_THREAD;
    }
    if (value->armed) return S_OK;
    UINT32 pid{};
    auto result = core->get_BrowserProcessId(&pid);
    if (FAILED(result) || !pid) {
      exit_failure(value, webview2_runtime_exit_error::process_query, FAILED(result) ? result : E_FAIL);
      return FAILED(result) ? result : E_FAIL;
    }
    value->pid = pid;
    value->process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!value->process) {
      const auto native = GetLastError();
      exit_failure(value, webview2_runtime_exit_error::process_query, HRESULT_FROM_WIN32(native), native);
      return FAILED(HRESULT_FROM_WIN32(native)) ? HRESULT_FROM_WIN32(native) : E_FAIL;
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    wchar_t image[32768]{}; DWORD length = static_cast<DWORD>(std::size(image));
    if (!GetProcessTimes(value->process, &created, &exited, &kernel, &user) ||
        !QueryFullProcessImageNameW(value->process, 0, image, &length) || !length) {
      const auto native = GetLastError();
      exit_failure(value, webview2_runtime_exit_error::process_query, HRESULT_FROM_WIN32(native), native);
      return FAILED(HRESULT_FROM_WIN32(native)) ? HRESULT_FROM_WIN32(native) : E_FAIL;
    }
    value->creation = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) | created.dwLowDateTime;
    value->image = std::filesystem::path(std::wstring(image, length));
    const auto initial_wait = WaitForSingleObject(value->process, 0);
    if (initial_wait == WAIT_FAILED) {
      const auto native = GetLastError();
      exit_failure(value, webview2_runtime_exit_error::process_wait, S_OK, native); return E_FAIL;
    }
    if (!value->creation || initial_wait != WAIT_TIMEOUT) {
      exit_failure(value, webview2_runtime_exit_error::browser_changed); return E_FAIL;
    }
    const std::weak_ptr<exit_state> weak(value);
    auto callback = Callback<ICoreWebView2BrowserProcessExitedEventHandler>(
        [weak](ICoreWebView2Environment* sender, ICoreWebView2BrowserProcessExitedEventArgs* args) -> HRESULT {
          const auto held = weak.lock(); return held ? exit_event(held, sender, args) : S_OK;
        });
    if (!callback) {
      exit_failure(value, webview2_runtime_exit_error::registration, E_OUTOFMEMORY); return E_OUTOFMEMORY;
    }
    result = value->environment->add_BrowserProcessExited(callback.Get(), &value->token);
    if (FAILED(result)) {
      exit_failure(value, webview2_runtime_exit_error::registration, result); return result;
    }
    { std::lock_guard<std::mutex> lock(value->mutex); value->registered = true; }
    UINT32 current_pid{};
    result = core->get_BrowserProcessId(&current_pid);
    FILETIME current_created{};
    if (FAILED(result) || current_pid != pid ||
        !GetProcessTimes(value->process, &current_created, &exited, &kernel, &user) ||
        ((static_cast<std::uint64_t>(current_created.dwHighDateTime) << 32U) | current_created.dwLowDateTime) != value->creation) {
      exit_failure(value, webview2_runtime_exit_error::browser_changed, result); return E_FAIL;
    }
    const auto confirmed_wait = WaitForSingleObject(value->process, 0);
    if (confirmed_wait == WAIT_FAILED) {
      const auto native = GetLastError();
      exit_failure(value, webview2_runtime_exit_error::process_wait, S_OK, native); return E_FAIL;
    }
    if (confirmed_wait != WAIT_TIMEOUT) {
      exit_failure(value, webview2_runtime_exit_error::browser_changed); return E_FAIL;
    }
    std::lock_guard<std::mutex> lock(value->mutex);
    if (value->failure != webview2_runtime_exit_error::none) return E_FAIL;
    value->armed = true; return S_OK;
  } catch (...) {
    if (value) exit_failure(value, webview2_runtime_exit_error::callback_exception, E_FAIL);
    return E_FAIL;
  }
}
bool exit_finished(const std::shared_ptr<exit_state>& value) noexcept {
  if (!value) return true;
  std::lock_guard<std::mutex> lock(value->mutex);
  return value->finished && !value->callbacks;
}
void check_exit_owner(const std::shared_ptr<exit_state>& value) noexcept {
  if (value && GetCurrentThreadId() != value->ui_thread) std::terminate();
}
}  // namespace

webview2_runtime_exit_watch::webview2_runtime_exit_watch(std::shared_ptr<state> value)
    : state_(std::move(value)) {}
webview2_runtime_exit_watch::~webview2_runtime_exit_watch() {
  check_exit_owner(state_);
  if (!exit_finished(state_)) std::terminate();
}
webview2_runtime_exit_watch::webview2_runtime_exit_watch(webview2_runtime_exit_watch&& other) noexcept {
  check_exit_owner(other.state_); state_ = std::move(other.state_);
}
webview2_runtime_exit_watch& webview2_runtime_exit_watch::operator=(webview2_runtime_exit_watch&& other) noexcept {
  if (this == &other) return *this;
  check_exit_owner(state_); check_exit_owner(other.state_);
  if (!exit_finished(state_)) std::terminate();
  state_ = std::move(other.state_); return *this;
}
webview2_runtime_exit_observation webview2_runtime_exit_watch::observe() const {
  webview2_runtime_exit_observation result;
  const auto value = state_;
  if (!value) { result.phase = webview2_runtime_exit_phase::failed; result.error = webview2_runtime_exit_error::unavailable; return result; }
  if (GetCurrentThreadId() != value->ui_thread) {
    result.phase = webview2_runtime_exit_phase::failed; result.error = webview2_runtime_exit_error::wrong_thread; return result;
  }
  // No COM/native call is made under the scalar lock. The process handle is
  // released only by finish on this same STA; a finished snapshot is historical.
  if (value->process) {
    const auto waited = WaitForSingleObject(value->process, 0);
    if (waited == WAIT_OBJECT_0) { std::lock_guard<std::mutex> lock(value->mutex); value->signaled = true; }
    else if (waited == WAIT_FAILED) exit_failure(value, webview2_runtime_exit_error::process_wait, S_OK, GetLastError());
  }
  result.browser_process_id = value->pid; result.creation_file_time = value->creation; result.image_path = value->image;
  std::lock_guard<std::mutex> lock(value->mutex);
  result.error = value->failure; result.hresult = static_cast<std::int32_t>(value->failure_hresult);
  result.win32_error = value->failure_win32; result.event_observed = value->event_observed;
  result.event_browser_process_id = value->event_pid; result.exit_kind = value->exit_kind;
  result.process_signaled = value->signaled; result.registered = value->registered; result.active_callbacks = value->callbacks;
  if (value->failure != webview2_runtime_exit_error::none) result.phase = webview2_runtime_exit_phase::failed;
  else if (value->finished) result.phase = webview2_runtime_exit_phase::finished;
  else if (!value->finishing && value->armed && value->event_observed && value->event_pid == value->pid &&
           value->exit_kind == webview2_browser_exit_kind::normal && value->signaled && !value->callbacks)
    result.phase = webview2_runtime_exit_phase::complete;
  return result;
}
host_result webview2_runtime_exit_watch::finish() {
  const auto value = state_;
  if (!value) return error("E_RUNTIME_WATCH_UNAVAILABLE", "The runtime exit watch is moved from.");
  if (GetCurrentThreadId() != value->ui_thread) return error("E_THREAD", "Runtime exit finish requires its UI STA.");
  const auto observation = observe();
  if (observation.phase == webview2_runtime_exit_phase::finished) return ok();
  if (observation.phase == webview2_runtime_exit_phase::failed)
    return error("E_RUNTIME_WATCH_FAILED", "The runtime exit watch recorded a failure.");
  if (observation.phase != webview2_runtime_exit_phase::complete)
    return error("E_BUSY", "The actual runtime exit event/process/callback is pending.");
  bool registered{};
  { std::lock_guard<std::mutex> lock(value->mutex);
    if (value->finishing || value->callbacks) return error("E_BUSY", "Runtime exit finish is already borrowed.");
    value->finishing = true; registered = value->registered;
  }
  if (registered) {
    const auto removed = value->environment->remove_BrowserProcessExited(value->token);
    if (FAILED(removed)) {
      exit_failure(value, webview2_runtime_exit_error::removal, removed);
      { std::lock_guard<std::mutex> lock(value->mutex); value->finishing = false; }
      return error("E_RUNTIME_WATCH_FAILED", "Runtime exit event removal failed.");
    }
    { std::lock_guard<std::mutex> lock(value->mutex); value->registered = false; }
  }
  { std::lock_guard<std::mutex> lock(value->mutex);
    if (value->callbacks || value->failure != webview2_runtime_exit_error::none) {
      value->finishing = false; return error("E_RUNTIME_WATCH_FAILED", "Runtime exit callback cleanup is unresolved.");
    }
  }
  if (value->process && !CloseHandle(value->process)) {
    exit_failure(value, webview2_runtime_exit_error::process_handle_close, S_OK, GetLastError());
    { std::lock_guard<std::mutex> lock(value->mutex); value->finishing = false; }
    return error("E_RUNTIME_WATCH_FAILED", "Runtime exit process handle release failed.");
  }
  value->process = nullptr;
  value->environment.Reset(); value->environment_identity.Reset();
  { std::lock_guard<std::mutex> lock(value->mutex);
    if (value->callbacks || value->failure != webview2_runtime_exit_error::none) {
      value->finishing = false; return error("E_RUNTIME_WATCH_FAILED", "Runtime exit release was reentered.");
    }
    value->finished = true; value->finishing = false;
  }
  return ok();
}

struct webview2_adapter::state {
  enum class phase { created, preparing, prepared, mounted, closing, closed, failed };
  struct request {
    std::uint64_t generation{};
    std::string document_id, id, raw;
    clock_type::time_point deadline;
    bool revoke{false};
    std::shared_ptr<capacity::request_budget::lease> slot;
  };
  struct output {
    std::uint64_t generation{};
    std::string document_id, id, raw;
    bool event{false};
    bool ready{false};
    std::shared_ptr<capacity::request_budget::lease> slot;
  };
  HWND owner{};
  DWORD ui_thread{GetCurrentThreadId()};
  std::filesystem::path profile, fixed_runtime;
  webview2_profile_access profile_access{webview2_profile_access::shared};
  std::shared_ptr<webview2_runtime_exit_watch::state> exit_monitor;
  bool preparation_failed_without_environment{false};
  bool controller_creation_failed{false};
  HRESULT cancelled_controller_failure{S_OK};
  bool failed_preparation_cleanup_in_progress{false};
  bool failed_preparation_cleanup_done{false};
  std::function<void(host_result)> prepared;
  detail::preparation_lifetime preparation;
  std::string preload, runtime_version, script_id;
  ComPtr<ICoreWebView2Environment> environment;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> core;
  ComPtr<ICoreWebView2_3> core3;
  ComPtr<ICoreWebView2_4> core4;
  ComPtr<ICoreWebView2_22> core22;
  phase current{phase::created};
  host_result failure;
  bool allow_navigation{false}, document_ready{false}, application_ready{false};
  bool reload_preparing{false};
  bool diagnostic_query_pending{false};
  clock_type::time_point diagnostic_query_at{};
  std::string page_diagnostics;
  UINT64 navigation_id{};
  sdk::bridge_adapter* bridge{};
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<request> requests;
  std::deque<output> outputs;
  capacity::request_budget request_slots;
  capacity::output_budget output_slots;
  capacity::priority_policy request_priority, output_priority;
  std::uint64_t generation{1};
  std::string document_id, active_session, session_digest;
  bool stop_requested{false};
  std::atomic<bool> worker_done{true};
  std::atomic<bool> wake_pending{false};
  std::thread worker;
};

namespace {
using state = webview2_adapter::state;
bool live(const std::shared_ptr<state>& value) {
  return value && value->current != state::phase::closing && value->current != state::phase::closed &&
      value->current != state::phase::failed;
}
host_result release_failed_controller_monitor(const std::shared_ptr<state>& value) {
  const auto monitor = value->exit_monitor;
  if (!monitor) return error("E_RUNTIME_WATCH_FAILED", "Failed Controller monitor is absent.");
  if (GetCurrentThreadId() != value->ui_thread || GetCurrentThreadId() != monitor->ui_thread)
    return error("E_THREAD", "Failed Controller cleanup requires its UI STA.");
  struct release_borrow {
    bool& active;
    explicit release_borrow(bool& input) noexcept : active(input) { active = true; }
    ~release_borrow() { active = false; }
  } borrowed(value->failed_preparation_cleanup_in_progress);
  bool registered{};
  {
    std::lock_guard<std::mutex> lock(monitor->mutex);
    if (monitor->callbacks || monitor->finishing)
      return error("E_BUSY", "Failed Controller monitor is borrowed.");
    if (monitor->armed || monitor->acquired || monitor->finished || monitor->process || monitor->pid ||
        monitor->creation || !monitor->image.empty() || monitor->event_observed || monitor->event_pid ||
        monitor->exit_kind != webview2_browser_exit_kind::absent || monitor->signaled ||
        monitor->failure != webview2_runtime_exit_error::none ||
        !monitor->environment || !monitor->environment_identity)
      return error("E_RUNTIME_WATCH_FAILED", "Failed Controller cleanup cannot discard browser ownership.");
    monitor->finishing = true; registered = monitor->registered;
  }
  if (registered) {
    const auto removed = monitor->environment->remove_BrowserProcessExited(monitor->token);
    if (FAILED(removed)) {
      exit_failure(monitor, webview2_runtime_exit_error::removal, removed);
      { std::lock_guard<std::mutex> lock(monitor->mutex); monitor->finishing = false; }
      return error("E_RUNTIME_WATCH_FAILED", "Failed Controller event removal failed.");
    }
    { std::lock_guard<std::mutex> lock(monitor->mutex); monitor->registered = false; }
  }
  {
    std::lock_guard<std::mutex> lock(monitor->mutex);
    if (monitor->callbacks || monitor->failure != webview2_runtime_exit_error::none) {
      monitor->finishing = false;
      return error("E_RUNTIME_WATCH_FAILED", "Failed Controller removal was reentered.");
    }
  }
  // No Core or held browser was established. These are local failed-initializer
  // references, not permission to await or stop another environment's browser.
  monitor->environment.Reset(); monitor->environment_identity.Reset(); value->environment.Reset();
  {
    std::lock_guard<std::mutex> lock(monitor->mutex);
    if (monitor->callbacks || monitor->failure != webview2_runtime_exit_error::none) {
      monitor->finishing = false;
      return error("E_RUNTIME_WATCH_FAILED", "Failed Controller release was reentered.");
    }
    monitor->finishing = false; // Intentionally never mark a runtime watch finished.
  }
  value->exit_monitor.reset(); value->failed_preparation_cleanup_done = true;
  return ok();
}

void finish_preparation(const std::shared_ptr<state>& value, const host_result& result) {
  auto completed = std::move(value->prepared);
  if (!completed) return;
  try { completed(result); }
  catch (...) {
    value->failure = error("E_CALLBACK", "The preparation completion callback threw an exception.");
    if (!value->preparation.cancelled()) value->current = state::phase::failed;
    value->document_ready = value->application_ready = false;
  }
}
void wake_owner(const std::shared_ptr<state>& value);
void fail(const std::shared_ptr<state>& value, std::string detail) {
  value->failure = error("E_WEBVIEW2", std::move(detail));
  value->document_ready = value->application_ready = false;
  value->current = state::phase::failed;
  finish_preparation(value, value->failure);
  wake_owner(value);
}
template<class Function>
HRESULT boundary(const std::weak_ptr<state>& weak, Function function) noexcept {
  const auto value = weak.lock();
  if (!live(value)) return S_OK;
  try { function(value); return S_OK; }
  catch (const std::exception& exception) { fail(value, exception.what()); return E_FAIL; }
  catch (...) { fail(value, "Unexpected WebView2 callback failure"); return E_FAIL; }
}
bool current_source(const std::shared_ptr<state>& value) {
  if (!value->core) return false;
  task_string source;
  return SUCCEEDED(value->core->get_Source(&source.value)) && source.value && trusted_entry(source.value);
}
void wake_owner(const std::shared_ptr<state>& value) {
  if (!value->wake_pending.exchange(true, std::memory_order_acq_rel) &&
      !PostMessageW(value->owner, sdk::webview2_event_message, 0, 0))
    value->wake_pending.store(false, std::memory_order_release);
}
void push_output(const std::shared_ptr<state>& value, state::output output) {
  std::lock_guard<std::mutex> lock(value->mutex);
  if (value->stop_requested || output.generation != value->generation) return;
  if (output.raw.size() > message_limit) {
    output.raw = response_error(output.id, "E_LIMIT_MESSAGE_SIZE", "Native response exceeds limit.");
    output.event = false;
    output.ready = false;
  }
  const auto category = output.slot ? output.slot->category : capacity::lane::ordinary;
  const auto bytes = output.raw.size();
  if (!value->output_slots.reserve(category, bytes)) {
    return; // the page's bounded request timer observes missing delivery
  }
  try { value->outputs.push_back(std::move(output)); }
  catch (...) { value->output_slots.release(category, bytes); throw; }
  wake_owner(value);
}
void bridge_worker(const std::shared_ptr<state>& value) noexcept {
  std::string session;
  const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  try {
    check(apartment, "Bridge worker COM apartment");
    for (;;) {
      state::request request;
      {
        std::unique_lock<std::mutex> lock(value->mutex);
        value->wake.wait(lock, [&] { return value->stop_requested || !value->requests.empty(); });
        if (value->stop_requested) break;
        // Navigation's revoke barrier always precedes the new document's work.
        auto selected = capacity::next_with_barrier(value->requests, value->request_priority,
            [](const state::request& item) { return item.slot->category; },
            [](const state::request& item) { return item.revoke; });
        request = std::move(*selected);
        value->requests.erase(selected);
      }
      if (request.revoke) {
        if (!session.empty()) value->bridge->shutdown_session(session);
        session.clear();
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(value->mutex);
        if (value->stop_requested || request.generation != value->generation) continue;
      }
      std::string response;
      bool ready = false;
      try {
        if (clock_type::now() >= request.deadline) {
          response = response_error(request.id, "E_TIMEOUT", "Request expired before native dispatch.");
        } else {
          const auto packet = parse_bounded(request.raw);
          const auto method = packet.value("method", std::string{});
          if (method != "bridge.bootstrap" && (session.empty() || packet.value("sessionId", std::string{}) != session)) {
            response = response_error(request.id, "E_UNAUTHORIZED", "Request belongs to another document session.");
          } else {
            // Handlers must be bounded; long operations use the SDK task service.
            // No browser/JSON claim can grant a native user gesture.
            response = value->bridge->handle_json(request.raw, false);
            if (response.size() <= message_limit) {
              const auto result = parse_bounded(response);
              if (result.is_object() && result.value("ok", false)) {
                if (method == "bridge.bootstrap") {
                  session = result.at("result").at("sessionId").get<std::string>();
                  const auto fingerprint = digest(session);
                  std::lock_guard<std::mutex> lock(value->mutex);
                  if (request.generation == value->generation) {
                    value->active_session = session;
                    value->session_digest = fingerprint;
                  }
                }
                ready = method == "bridge.probe.report";
              }
            }
          }
        }
      } catch (...) { response = response_error(request.id, "E_NATIVE_EXCEPTION", "Native request failed."); }
      push_output(value, {request.generation, request.document_id, request.id, std::move(response),
          false, ready, std::move(request.slot)});
    }
  } catch (...) {
    // No UI COM work from this thread. The owner observes worker_done/status.
  }
  if (!session.empty()) {
    try { value->bridge->shutdown_session(session); } catch (...) {}
  }
  if (SUCCEEDED(apartment)) CoUninitialize();
  value->worker_done.store(true, std::memory_order_release);
  wake_owner(value);
}
void invalidate_document(const std::shared_ptr<state>& value) {
  value->document_ready = value->application_ready = false;
  value->diagnostic_query_pending = false;
  value->page_diagnostics.clear();
  const auto identity = document_identity();
  {
    std::lock_guard<std::mutex> lock(value->mutex);
    ++value->generation;
    value->document_id = identity;
    value->active_session.clear();
    value->session_digest.clear();
    value->requests.clear();
    value->outputs.clear();
    value->request_slots = capacity::request_budget{};
    value->output_slots = capacity::output_budget{};
    value->request_priority = capacity::priority_policy{};
    value->output_priority = capacity::priority_policy{};
    value->requests.push_back({0, {}, {}, {}, {}, true});
  }
  value->wake.notify_one();
}
void register_preload(const std::shared_ptr<state>& value, std::function<void()> completed) {
  std::string source = value->preload;
  const std::string marker = "__SOUI_DOCUMENT_ID__";
  const auto offset = source.find(marker);
  if (offset == std::string::npos) throw std::runtime_error("Preload document marker missing");
  source.replace(offset, marker.size(), json(value->document_id).dump());
  const auto script = wide(source);
  const std::weak_ptr<state> weak(value);
  auto callback = Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
          [value, weak, completed](HRESULT result, LPCWSTR identifier) -> HRESULT {
            auto script_completion = value->preparation.receive(detail::preparation_lifetime::operation::script);
            if (!script_completion) return S_OK;
            return boundary(weak, [completed, result, identifier](const auto& current) {
              check(result, "Register preload");
              if (!identifier) throw std::runtime_error("Preload identity missing");
              current->script_id = utf8(identifier);
              completed();
            });
          });
  if (!value->preparation.begin(detail::preparation_lifetime::operation::script))
    throw std::runtime_error("Preload registration is cancelled or already pending");
  const auto submitted = value->core->AddScriptToExecuteOnDocumentCreated(script.c_str(), callback.Get());
  if (FAILED(submitted)) (void)value->preparation.settle(detail::preparation_lifetime::operation::script);
  check(submitted, "AddScriptToExecuteOnDocumentCreated");
}
void receive_message(const std::shared_ptr<state>& value, ICoreWebView2WebMessageReceivedEventArgs* args) {
  if (value->current != state::phase::mounted || value->reload_preparing || !current_source(value)) return;
  task_string source, raw;
  check(args->get_Source(&source.value), "Message source");
  if (!source.value || !trusted_entry(source.value)) return;
  check(args->TryGetWebMessageAsString(&raw.value), "String message expected");
  if (!raw.value || wcsnlen_s(raw.value, 4 * message_limit + 1) > 4 * message_limit) return;
  const auto wire = parse_bounded(utf8(raw.value));
  if (!wire.is_object() || wire.size() != 2 || !wire.contains("documentId") ||
      !wire["documentId"].is_string() || !wire.contains("request") || !wire["request"].is_string()) return;
  const auto identity = wire["documentId"].get<std::string>();
  const auto request = wire["request"].get<std::string>();
  if (request.empty() || request.size() > message_limit) return;
  const auto packet = parse_bounded(request);
  if (!packet.is_object() || !packet.contains("requestId") || !packet["requestId"].is_string()) return;
  const auto id = packet["requestId"].get<std::string>();
  if (id.empty() || id.size() > 512) return;
  unsigned timeout = 10000;
  if (packet.contains("timeoutMs")) {
    if (!packet["timeoutMs"].is_number_integer() || packet["timeoutMs"].get<double>() < 1 ||
        packet["timeoutMs"].get<double>() > 60000) return;
    timeout = packet["timeoutMs"].get<unsigned>();
  }
  const auto category = capacity::classify(packet);
  std::uint64_t generation = 0;
  const char* rejection = nullptr;
  {
    std::lock_guard<std::mutex> lock(value->mutex);
    if (value->stop_requested || identity != value->document_id) return;
    generation = value->generation;
    capacity::request_budget::rejection rejected;
    auto slot = value->request_slots.acquire(id, generation, category, rejected);
    if (!slot) rejection = rejected == capacity::request_budget::rejection::duplicate ? "E_DUPLICATE_REQUEST" : "E_BUSY";
    else {
      value->requests.push_back({generation, identity, id, request,
          clock_type::now() + std::chrono::milliseconds(timeout), false, std::move(slot)});
    }
  }
  if (rejection) push_output(value, {generation, identity, id,
      response_error(id, rejection, "Request was not admitted."), false, false, {}});
  else value->wake.notify_one();
}
void install_policy(const std::shared_ptr<state>& value) {
  ComPtr<ICoreWebView2Settings> settings;
  check(value->core->get_Settings(&settings), "Get settings");
  check(settings->put_AreHostObjectsAllowed(FALSE), "Disable host objects");
  check(settings->put_AreDefaultScriptDialogsEnabled(FALSE), "Disable script dialogs");
  check(settings->put_AreDevToolsEnabled(FALSE), "Disable DevTools");
  check(settings->put_AreDefaultContextMenusEnabled(FALSE), "Disable context menus");
  check(settings->put_IsWebMessageEnabled(TRUE), "Enable web messages");
  check(value->core.As(&value->core3), "Virtual host interface");
  check(value->core.As(&value->core4), "Download policy interface");
  check(value->core.As(&value->core22), "All-source resource policy interface");
  const std::weak_ptr<state> weak(value);
  EventRegistrationToken token{};
  check(value->core->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
      [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
        return boundary(weak, [&](const auto& current) {
          task_string uri;
          check(args->get_Uri(&uri.value), "Navigation URI");
          const bool allowed = current->allow_navigation && uri.value && trusted_entry(uri.value);
          check(args->put_Cancel(allowed ? FALSE : TRUE), "Navigation policy");
          if (allowed) {
            current->allow_navigation = false;
            current->document_ready = current->application_ready = false;
            check(args->get_NavigationId(&current->navigation_id), "Navigation identity");
          }
        });
      }).Get(), &token), "Navigation policy registration");
  check(value->core->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
      [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT { return args->put_Cancel(TRUE); }
      ).Get(), &token), "Frame policy registration");
  check(value->core->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
      [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
        return boundary(weak, [&](const auto& current) {
          UINT64 identity{};
          check(args->get_NavigationId(&identity), "Completed navigation identity");
          if (identity != current->navigation_id) return;
          BOOL successful{};
          check(args->get_IsSuccess(&successful), "Navigation result");
          if (!successful || !current_source(current)) throw std::runtime_error("Trusted document navigation failed");
          current->document_ready = true;
        });
      }).Get(), &token), "Navigation completion registration");
  check(value->core->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
      [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT { return args->put_Handled(TRUE); }
      ).Get(), &token), "Popup policy registration");
  check(value->core->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
      [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
        return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
      }).Get(), &token), "Permission policy registration");
  check(value->core4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
      [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT { return args->put_Cancel(TRUE); }
      ).Get(), &token), "Download policy registration");
  check(value->core->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
      [weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
        return boundary(weak, [](const auto& current) { fail(current, "WebView2 renderer process failed"); });
      }).Get(), &token), "Process failure registration");
  check(value->core22->AddWebResourceRequestedFilterWithRequestSourceKinds(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,
      COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL), "Resource request filter");
  check(value->core->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
      [weak](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
        return boundary(weak, [&](const auto& current) {
          ComPtr<ICoreWebView2WebResourceRequest> request;
          task_string uri;
          check(args->get_Request(&request), "Resource request");
          check(request->get_Uri(&uri.value), "Resource URI");
          if (uri.value && trusted_resource(uri.value)) return;
          ComPtr<ICoreWebView2WebResourceResponse> response;
          check(current->environment->CreateWebResourceResponse(nullptr, 403, L"Forbidden", L"", &response), "Deny resource");
          check(args->put_Response(response.Get()), "Apply resource denial");
        });
      }).Get(), &token), "Resource policy registration");
  check(value->core->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
      [weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
        // Malformed/untrusted messages are rejected without breaking the renderer.
        const auto current = weak.lock();
        if (!live(current)) return S_OK;
        try { receive_message(current, args); } catch (...) {}
        return S_OK;
      }).Get(), &token), "Web message registration");
}
host_result require_mounted(const std::shared_ptr<state>& value) {
  if (GetCurrentThreadId() != value->ui_thread) return error("E_THREAD", "WebView2 calls require the owning UI STA.");
  if (value->current == state::phase::failed) return value->failure;
  if (value->current != state::phase::mounted) return error("E_NOT_STARTED", "WebView2 is not mounted.");
  if (value->reload_preparing) return error("E_BUSY", "Reload preparation is in progress.");
  return ok();
}
HRESULT complete_controller_preparation(const std::shared_ptr<state>& current,
                                       HRESULT controller_result, ICoreWebView2Controller* controller) {
  auto completion = current->preparation.receive(detail::preparation_lifetime::operation::controller);
  if (!completion) return S_OK;
  const std::weak_ptr<state> weak(current);
  if (!live(current) || current->preparation.cancelled()) {
    if (!controller) {
      // Only this accepted native failure proves creation returned no object.
      // Cancellation alone (or S_OK/null) cannot release Environment ownership.
      if (current->preparation.cancelled() && GetCurrentThreadId() == current->ui_thread && FAILED(controller_result))
        current->cancelled_controller_failure = controller_result;
      return S_OK;
    }
    // A returned completion is not proof that its actual Controller closed.
    current->controller = controller;
    if (current->exit_monitor) {
      ComPtr<ICoreWebView2> cancelled_core;
      const auto core_result = controller->get_CoreWebView2(&cancelled_core);
      const auto armed = SUCCEEDED(core_result) ? arm_exit_monitor(current->exit_monitor, cancelled_core.Get()) : core_result;
      if (FAILED(armed)) {
        exit_failure(current->exit_monitor, webview2_runtime_exit_error::process_query, armed);
        current->failure = error("E_RUNTIME_WATCH_FAILED", "Cancelled controller exit identity could not be armed.");
        return armed;
      }
    }
    const auto closed = controller->Close();
    if (FAILED(closed)) {
      current->failure = error("E_WEBVIEW2_CLOSE", hresult_text(closed));
      return closed;
    }
    current->controller.Reset();
    return S_OK;
  }
  if (GetCurrentThreadId() == current->ui_thread && FAILED(controller_result) && !controller)
    current->controller_creation_failed = true;
  return boundary(weak, [weak, controller_result, controller](const auto& prepared) {
    check(controller_result, "Create WebView2 controller");
    if (!controller) throw std::runtime_error("WebView2 controller missing");
    prepared->controller = controller;
    check(controller->get_CoreWebView2(&prepared->core), "Get WebView2 core");
    if (prepared->exit_monitor)
      check(arm_exit_monitor(prepared->exit_monitor, prepared->core.Get()), "Arm exclusive runtime exit watch");
    install_policy(prepared);
    register_preload(prepared, [weak] {
      const auto ready = weak.lock();
      if (!live(ready)) return;
      ready->current = state::phase::prepared;
      finish_preparation(ready, ok());
    });
  });
}

host_result retire_failed_controller_preparation(const std::shared_ptr<state>& value) {
  if (value->failed_preparation_cleanup_in_progress || !value->preparation.settled() ||
      !value->worker_done.load(std::memory_order_acquire) || value->worker.joinable() || value->bridge)
    return error("E_BUSY", "Failed Controller preparation is still borrowed.");
  const bool failed_completion = (value->controller_creation_failed && value->failure.code == "E_WEBVIEW2") ||
      FAILED(value->cancelled_controller_failure);
  if (value->profile_access == webview2_profile_access::exclusive && failed_completion &&
      !value->controller && !value->core && !value->core3 &&
      !value->core4 && !value->core22)
    return release_failed_controller_monitor(value);
  return ok();
}
}  // namespace

webview2_adapter::webview2_adapter(void* owner, std::filesystem::path profile, std::string preload,
                                 std::filesystem::path fixed_runtime)
    : state_(std::make_shared<state>()) {
  state_->owner = static_cast<HWND>(owner);
  state_->profile = std::move(profile);
  state_->preload = preload.empty() ? detail::webview2_preload : std::move(preload);
  state_->fixed_runtime = std::move(fixed_runtime);
  state_->document_id = document_identity();
}

webview2_adapter::~webview2_adapter() {
  // Applications must finish close before destruction. Joining preserves the
  // borrowed Bridge's lifetime even when a consumer violates that contract.
  auto value = state_;
  if (value->profile_access == webview2_profile_access::exclusive &&
      (GetCurrentThreadId() != value->ui_thread || !value->preparation.settled() || !exit_finished(value->exit_monitor)))
    std::terminate(); // Pending application-exclusive ownership cannot be silently discarded.
  value->preparation.cancel();
  value->current = state::phase::closing;
  auto completed = std::move(value->prepared);
  if (completed) { try { completed(error("E_CANCELLED", "WebView2 preparation was cancelled.")); } catch (...) {} }
  {
    std::lock_guard<std::mutex> lock(value->mutex);
    value->stop_requested = true;
    value->requests.clear();
    value->outputs.clear();
    value->output_slots = capacity::output_budget{};
  }
  value->wake.notify_all();
  if (value->worker.joinable()) value->worker.join();
  if (value->bridge) {
    value->bridge->set_event_sink({});
    value->bridge->set_owner_window(nullptr);
  }
  if (value->controller) (void)value->controller->Close();
  value->current = state::phase::closed;
}

host_result webview2_adapter::configure_profile_access(webview2_profile_access access) {
  const auto value = state_;
  if (GetCurrentThreadId() != value->ui_thread) return error("E_THREAD", "Profile access requires the UI STA.");
  if (value->current != state::phase::created) return error("E_INVALID_STATE", "Profile access is frozen before preparation.");
  if (access != webview2_profile_access::shared && access != webview2_profile_access::exclusive)
    return error("E_INVALID_PARAMS", "Unknown profile access policy.");
  value->profile_access = access; return ok();
}
host_result webview2_adapter::acquire_runtime_exit_watch(std::unique_ptr<webview2_runtime_exit_watch>& output) {
  const auto value = state_;
  if (GetCurrentThreadId() != value->ui_thread) return error("E_THREAD", "Runtime exit acquisition requires the UI STA.");
  if (value->profile_access != webview2_profile_access::exclusive)
    return error("E_RUNTIME_WATCH_SHARED", "A shared profile has no application-exclusive runtime barrier.");
  if (output || (value->exit_monitor && value->exit_monitor->acquired))
    return error("E_RUNTIME_WATCH_ALREADY_ACQUIRED", "The owning runtime exit watch was already acquired.");
  const auto monitor = value->exit_monitor;
  bool armed{}, failed{};
  if (monitor) {
    std::lock_guard<std::mutex> lock(monitor->mutex);
    armed = monitor->armed; failed = monitor->failure != webview2_runtime_exit_error::none;
  }
  if (!monitor || (!armed && !failed)) {
    if (!value->preparation.settled()) return error("E_PENDING", "Runtime exit identity is not yet available.");
    return error("E_RUNTIME_WATCH_UNAVAILABLE", "No real browser exit identity was established.");
  }
  auto acquired = std::unique_ptr<webview2_runtime_exit_watch>(new webview2_runtime_exit_watch(monitor));
  monitor->acquired = true; output = std::move(acquired); return ok();
}

bool webview2_adapter::failed_preparation_cleanup_complete() const noexcept {
  const auto& value = state_;
  return GetCurrentThreadId() == value->ui_thread && value->failed_preparation_cleanup_done &&
      !value->failed_preparation_cleanup_in_progress && value->current == state::phase::closed &&
      value->preparation.settled() && !value->exit_monitor && !value->environment && !value->controller &&
      !value->core && !value->core3 && !value->core4 && !value->core22 && !value->bridge &&
      value->worker_done.load(std::memory_order_acquire) && !value->worker.joinable();
}

void webview2_adapter::prepare_async(std::function<void(host_result)> completed) {
  auto value = state_;
  if (!completed) throw std::invalid_argument("WebView2 preparation requires a completion callback.");
  if (GetCurrentThreadId() != value->ui_thread || value->current != state::phase::created || !IsWindow(value->owner)) {
    completed(error("E_INVALID_STATE", "Prepare requires a live owner window on its UI STA."));
    return;
  }
  const auto reject_before_environment = [&](host_result result) {
    value->preparation_failed_without_environment = true;
    value->failure = std::move(result);
    value->current = state::phase::failed;
    completed(value->failure);
  };
  host_result loader_policy;
  try { loader_policy = detail::check_webview2_loader_policy(); }
  catch (...) {
    reject_before_environment(error("E_WEBVIEW2_LOADER_POLICY", "External loader policy could not be verified."));
    return;
  }
  if (!loader_policy.ok) { reject_before_environment(std::move(loader_policy)); return; }
  const auto selected = select_runtime_profile(inspect_runtime_environment(), {false, true}, runtime_profile::modern);
  if (!selected.ok) { reject_before_environment(error(selected.code.c_str(), selected.reason)); return; }
  if (value->profile.empty() || !value->profile.is_absolute() ||
      (!value->fixed_runtime.empty() && !value->fixed_runtime.is_absolute())) {
    reject_before_environment(error("E_INVALID_PATH", "Profile and fixed-runtime directories must be explicit absolute paths."));
    return;
  }
  ComPtr<ICoreWebView2EnvironmentOptions> options;
  if (value->profile_access == webview2_profile_access::exclusive) {
    auto exclusive = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    if (!exclusive) { reject_before_environment(error("E_WEBVIEW2_OPTIONS", "Exclusive environment options allocation failed.")); return; }
    BOOL actual{};
    auto configured = exclusive->put_ExclusiveUserDataFolderAccess(TRUE);
    if (SUCCEEDED(configured)) configured = exclusive->get_ExclusiveUserDataFolderAccess(&actual);
    if (FAILED(configured)) { reject_before_environment(error("E_WEBVIEW2_OPTIONS", hresult_text(configured))); return; }
    if (actual != TRUE) { reject_before_environment(error("E_WEBVIEW2_OPTIONS", "Exclusive options returned a false readback.")); return; }
    configured = exclusive.As(&options);
    if (FAILED(configured)) { reject_before_environment(error("E_WEBVIEW2_OPTIONS", hresult_text(configured))); return; }
  }
  value->current = state::phase::preparing;
  value->prepared = std::move(completed);
  const std::weak_ptr<state> weak(value);
  auto callback = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [value, weak](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            auto environment_completion = value->preparation.receive(detail::preparation_lifetime::operation::environment);
            if (!environment_completion || value->preparation.cancelled()) return S_OK;
            return boundary(weak, [weak, result, environment](const auto& current) {
              if (FAILED(result) && !environment) current->preparation_failed_without_environment = true;
              check(result, "Create WebView2 environment");
              if (!environment || !IsWindow(current->owner)) throw std::runtime_error("Owner closed during preparation");
              current->environment = environment;
              if (current->profile_access == webview2_profile_access::exclusive) {
                ComPtr<ICoreWebView2Environment5> exit_environment;
                check(current->environment.As(&exit_environment), "Exclusive runtime exit interface");
                current->exit_monitor = std::make_shared<exit_state>();
                current->exit_monitor->environment = std::move(exit_environment);
                check(current->exit_monitor->environment.As(&current->exit_monitor->environment_identity), "Runtime environment IUnknown identity");
              }
              task_string runtime;
              check(environment->get_BrowserVersionString(&runtime.value), "Runtime identity");
              current->runtime_version = utf8(runtime.value ? runtime.value : L"");
              auto controller_callback = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                      [current](HRESULT controller_result, ICoreWebView2Controller* controller) -> HRESULT {
                        return complete_controller_preparation(current, controller_result, controller);
                      });
              if (!current->preparation.begin(detail::preparation_lifetime::operation::controller))
                throw std::runtime_error("Controller creation is cancelled or already pending");
              const auto submitted = environment->CreateCoreWebView2Controller(current->owner, controller_callback.Get());
              if (FAILED(submitted)) (void)current->preparation.settle(detail::preparation_lifetime::operation::controller);
              check(submitted, "CreateCoreWebView2Controller");
            });
          });
  if (!value->preparation.begin(detail::preparation_lifetime::operation::environment)) {
    fail(value, "Environment creation is cancelled or already pending");
    return;
  }
  const auto result = CreateCoreWebView2EnvironmentWithOptions(
      value->fixed_runtime.empty() ? nullptr : value->fixed_runtime.c_str(), value->profile.c_str(), options.Get(), callback.Get());
  if (FAILED(result)) {
    (void)value->preparation.settle(detail::preparation_lifetime::operation::environment);
    value->preparation_failed_without_environment = true;
  }
  if (FAILED(result)) fail(value, hresult_text(result));
}

host_result webview2_adapter::status() const {
  if (state_->failed_preparation_cleanup_done) return state_->failure;
  if (state_->failure.code == "E_CALLBACK" || state_->failure.code == "E_WEBVIEW2_CLOSE") return state_->failure;
  if (state_->current == state::phase::failed) return state_->failure;
  if (state_->current == state::phase::created || state_->current == state::phase::preparing)
    return error("E_PENDING", "Preparing WebView2 asynchronously.");
  return ok();
}
std::string webview2_adapter::runtime_version() const { return state_->runtime_version; }
std::string webview2_adapter::session_digest() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->session_digest;
}

host_result webview2_adapter::mount(const sdk::host_config& config) {
  auto value = state_;
  if (GetCurrentThreadId() != value->ui_thread || value->current != state::phase::prepared ||
      config.owner_window != value->owner || !config.bridge_adapter)
    return error("E_INVALID_STATE", "Mount requires the prepared owner and a live Bridge.");
  try {
    const auto resources = std::filesystem::canonical(config.resource_root);
    const auto profile = std::filesystem::weakly_canonical(value->profile);
    auto resource_prefix = resources.native();
    if (resource_prefix.back() != L'\\') resource_prefix += L'\\';
    const auto profile_text = profile.native();
    if (CompareStringOrdinal(profile_text.c_str(), -1, resources.c_str(), -1, TRUE) == CSTR_EQUAL ||
        (profile_text.size() >= resource_prefix.size() &&
         CompareStringOrdinal(profile_text.data(), static_cast<int>(resource_prefix.size()),
                              resource_prefix.data(), static_cast<int>(resource_prefix.size()), TRUE) == CSTR_EQUAL))
      throw std::runtime_error("Browser profile must be outside the served resource directory");
    check(value->core3->SetVirtualHostNameToFolderMapping(L"soui-webview2.invalid", resources.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY), "Map controlled resources");
    RECT bounds{};
    if (!GetClientRect(value->owner, &bounds)) throw std::runtime_error("Owner bounds unavailable");
    check(value->controller->put_Bounds(bounds), "Set initial bounds");
    value->bridge = config.bridge_adapter;
    value->bridge->set_owner_window(value->owner);
    const std::weak_ptr<state> weak(value);
    value->bridge->set_event_sink([weak](std::string_view raw) {
      const auto current = weak.lock();
      if (!current || raw.size() > message_limit) return;
      try {
        const auto packet = parse_bounded(std::string(raw));
        state::output output;
        {
          std::lock_guard<std::mutex> lock(current->mutex);
          if (current->stop_requested || current->active_session.empty() ||
              packet.value("sessionId", std::string{}) != current->active_session) return;
          output = {current->generation, current->document_id, {}, std::string(raw), true, false};
        }
        push_output(current, std::move(output));
      } catch (...) {}
    });
    value->worker_done.store(false, std::memory_order_release);
    try { value->worker = std::thread([value] { bridge_worker(value); }); }
    catch (...) { value->worker_done.store(true, std::memory_order_release); throw; }
    value->current = state::phase::mounted;
    value->allow_navigation = true;
    check(value->core->Navigate(entry_url), "Navigate controlled entry");
    return ok();
  } catch (const std::exception& exception) {
    fail(value, exception.what());
    // No work has been admitted before the initial Navigate callback.
    {
      std::lock_guard<std::mutex> lock(value->mutex);
      value->stop_requested = true;
    }
    value->wake.notify_all();
    if (value->worker.joinable()) value->worker.join();
    if (value->bridge) { value->bridge->set_event_sink({}); value->bridge->set_owner_window(nullptr); value->bridge = nullptr; }
    if (value->controller) (void)value->controller->Close();
    return value->failure;
  }
}

host_result webview2_adapter::unmount() {
  auto value = state_;
  if (GetCurrentThreadId() != value->ui_thread) return error("E_THREAD", "Unmount requires the UI STA.");
  if (value->current == state::phase::closed) return ok();
  if (value->failed_preparation_cleanup_in_progress)
    return error("E_BUSY", "Failed Controller ownership release is already active.");
  value->preparation.cancel();
  value->current = state::phase::closing;
  finish_preparation(value, error("E_CANCELLED", "WebView2 preparation was cancelled."));
  value->document_ready = value->application_ready = false;
  {
    std::lock_guard<std::mutex> lock(value->mutex);
    value->stop_requested = true;
    ++value->generation;
    value->requests.clear();
    value->outputs.clear();
    value->output_slots = capacity::output_budget{};
  }
  value->wake.notify_all();
  if (!value->preparation.settled())
    return error("E_BUSY", "WebView2 initialization callbacks are finishing bounded teardown.");
  if (!value->worker_done.load(std::memory_order_acquire)) return error("E_BUSY", "Bridge worker is finishing bounded teardown.");
  if (value->worker.joinable()) value->worker.join();
  if (value->bridge) { value->bridge->set_event_sink({}); value->bridge->set_owner_window(nullptr); value->bridge = nullptr; }
  if (value->preparation_failed_without_environment && !value->failure.ok &&
      !value->exit_monitor && !value->environment && !value->controller && !value->core &&
      !value->core3 && !value->core4 && !value->core22)
    value->failed_preparation_cleanup_done = true; // Local failure cleanup, never a browser exit receipt.
  const auto failed_cleanup = retire_failed_controller_preparation(value);
  if (!failed_cleanup.ok) return failed_cleanup;
  if (value->exit_monitor && value->core) {
    UINT32 current_pid{};
    const auto identified = value->core->get_BrowserProcessId(&current_pid);
    if (FAILED(identified) || current_pid != value->exit_monitor->pid)
      exit_failure(value->exit_monitor, webview2_runtime_exit_error::browser_changed, identified);
  }
  const auto result = value->controller ? value->controller->Close() : S_OK;
  if (FAILED(result)) return error("E_WEBVIEW2_CLOSE", hresult_text(result));
  value->core22.Reset(); value->core4.Reset(); value->core3.Reset(); value->core.Reset();
  value->controller.Reset(); value->environment.Reset();
  value->current = state::phase::closed;
  return ok();
}

host_result webview2_adapter::show(bool visible) {
  const auto active = require_mounted(state_);
  if (!active.ok) return active;
  const auto result = state_->controller->put_IsVisible(visible ? TRUE : FALSE);
  return SUCCEEDED(result) ? ok() : error("E_WEBVIEW2", hresult_text(result));
}
host_result webview2_adapter::resize(int width, int height) {
  const auto active = require_mounted(state_);
  if (!active.ok) return active;
  if (width < 0 || height < 0 || width > 32768 || height > 32768) return error("E_INVALID_PARAMS", "Invalid WebView2 bounds.");
  const auto result = state_->controller->put_Bounds(RECT{0, 0, width, height});
  return SUCCEEDED(result) ? ok() : error("E_WEBVIEW2", hresult_text(result));
}
host_result webview2_adapter::focus() {
  const auto active = require_mounted(state_);
  if (!active.ok) return active;
  const auto result = state_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
  return SUCCEEDED(result) ? ok() : error("E_WEBVIEW2", hresult_text(result));
}
host_result webview2_adapter::reload() {
  auto value = state_;
  const auto active = require_mounted(value);
  if (!active.ok) return active;
  try {
    invalidate_document(value);
    value->reload_preparing = true;
    check(value->core->RemoveScriptToExecuteOnDocumentCreated(wide(value->script_id).c_str()), "Remove prior preload");
    const std::weak_ptr<state> weak(value);
    register_preload(value, [weak] {
      const auto current = weak.lock();
      if (!live(current)) return;
      current->reload_preparing = false;
      current->allow_navigation = true;
      check(current->core->Reload(), "Reload controlled entry");
    });
    return ok(); // accepted asynchronous navigation, readiness remains false
  } catch (const std::exception& exception) { fail(value, exception.what()); return value->failure; }
}

void webview2_adapter::pump() {
  auto value = state_;
  value->wake_pending.store(false, std::memory_order_release);
  if (!require_mounted(value).ok) return;
  try {
    if (value->worker_done.load(std::memory_order_acquire)) throw std::runtime_error("Bridge worker exited unexpectedly");
    if (!value->application_ready && !value->diagnostic_query_pending && current_source(value) &&
        clock_type::now() - value->diagnostic_query_at >= std::chrono::seconds(1)) {
      value->diagnostic_query_pending = true;
      value->diagnostic_query_at = clock_type::now();
      const std::weak_ptr<state> weak(value);
      const auto queried_generation = value->generation;
      const auto queried = value->core->ExecuteScript(
          LR"((function(){return {document:document.readyState,visibility:document.visibilityState,binding:typeof window.__souiNativePostMessage};})())",
          Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
              [weak, queried_generation](HRESULT result, LPCWSTR raw) -> HRESULT {
                const auto current = weak.lock();
                if (!live(current) || current->generation != queried_generation || !current_source(current)) return S_OK;
                current->diagnostic_query_pending = false;
                try {
                  if (SUCCEEDED(result) && raw) {
                    current->page_diagnostics = utf8(raw);
                  }
                }
                catch (...) {}
                return S_OK;
              }).Get());
      if (FAILED(queried)) value->diagnostic_query_pending = false;
    }
    // Owner notification drains a bounded batch; workers never invoke WebView2 COM.
    for (unsigned batch = 0; batch < 16; ++batch) {
      state::output output;
      {
        std::lock_guard<std::mutex> lock(value->mutex);
        if (value->outputs.empty()) break;
        auto selected = capacity::next(value->outputs, value->output_priority,
            [](const state::output& item) { return item.slot ? item.slot->category : capacity::lane::ordinary; });
        output = std::move(*selected);
        value->outputs.erase(selected);
        value->output_slots.release(output.slot ? output.slot->category : capacity::lane::ordinary, output.raw.size());
        // Release before notifying the page; its completion may send new work.
        if (output.slot) output.slot->release();
        if (output.generation != value->generation || output.document_id != value->document_id) continue;
      }
      if (!current_source(value)) continue;
      const auto packet = output.event
          ? json{{"kind", "event"}, {"documentId", output.document_id}, {"data", parse_bounded(output.raw)}}
          : json{{"kind", "response"}, {"documentId", output.document_id}, {"requestId", output.id}, {"raw", output.raw}};
      check(value->core->PostWebMessageAsJson(wide(packet.dump()).c_str()), "Deliver native response");
      if (output.ready) value->application_ready = true;
    }
    {
      std::lock_guard<std::mutex> lock(value->mutex);
      if (!value->outputs.empty()) wake_owner(value);
    }
  } catch (const std::exception& exception) { fail(value, exception.what()); }
}
bool webview2_adapter::document_ready() const noexcept { return state_->current == state::phase::mounted && state_->document_ready; }
bool webview2_adapter::application_ready() { return state_->current == state::phase::mounted && state_->application_ready; }
std::string webview2_adapter::application_diagnostics() {
  if (state_->current == state::phase::failed) return state_->failure.message;
  return std::string("webview2;document=") + (document_ready() ? "ready" : "pending") +
      ";application=" + (application_ready() ? "ready" : "pending") + ";page=" + state_->page_diagnostics;
}

}  // namespace soui::sdk
