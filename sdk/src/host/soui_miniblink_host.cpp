#include <soui/host/soui_miniblink_host.h>
#include <soui/host/window_chrome_script.h>
#include <soui/runtime/host/trusted_document.h>
#include <soui/runtime/services/desktop/window_commands.h>
#include "bridge_pending_script.h"
#include "bridge_request_budget.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <unordered_map>

#include <soui/bridge_adapter.h>
#include "../../../third_party/include/json/json.hpp"
#ifdef SOUI_TEST_KIT
#include "../../test-kit/native/dom-agent.generated.h"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif
#ifdef SOUI_BRIDGE_BENCHMARK
#include <mutex>
#include <vector>
#endif

#ifdef _WIN32
#ifndef ENABLE_WKE
#define ENABLE_WKE 0
#endif
#include "../../../third_party/include/miniblink/wke.h"
#include <imm.h>
#include <windows.h>
#include <windowsx.h>
#ifdef SOUI_SOURCE_KERNEL_HOST
#include "source_kernel_api.h"
#endif
#endif

namespace soui::sdk {
namespace {
#ifdef _WIN32
constexpr wchar_t kInteractionWindowClass[] = L"SouiMiniblinkInteractionWindowV1";

LRESULT CALLBACK interaction_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* adapter = reinterpret_cast<dynamic_miniblink_adapter*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    adapter = static_cast<dynamic_miniblink_adapter*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(adapter));
  }
  if (adapter != nullptr) {
    std::intptr_t result = 0;
    if (adapter->handle_native_window_message(message,
                                              static_cast<std::uintptr_t>(wparam),
                                              static_cast<std::intptr_t>(lparam), result)) {
      return static_cast<LRESULT>(result);
    }
  }
  if (message == WM_NCDESTROY)
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  return DefWindowProcW(window, message, wparam, lparam);
}
#endif

#ifdef SOUI_BRIDGE_BENCHMARK
std::mutex paint_samples_mutex;
std::vector<double> paint_samples;
std::vector<double> paint_pixel_changes;
unsigned paint_pixel_failures{0};
class paint_band_sampler {
 public:
  ~paint_band_sampler() { reset(); }
  bool sample(HDC source, bool& changed) {
    BITMAP bitmap{};
    if (!GetObjectW(GetCurrentObject(source, OBJ_BITMAP), sizeof(bitmap), &bitmap) || bitmap.bmWidth <= 0 || (bitmap.bmHeight >= -220 && bitmap.bmHeight <= 220)) return false;
    if (width_ != bitmap.bmWidth) {
      reset();
      BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth=bitmap.bmWidth; info.bmiHeader.biHeight=-1;
      info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
      dc_=CreateCompatibleDC(source);
      bitmap_=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels_,nullptr,0);
      if (!dc_ || !bitmap_ || !pixels_) { reset(); return false; }
      old_=SelectObject(dc_,bitmap_);
      if (!old_ || old_==HGDI_ERROR) { reset(); return false; }
      width_=bitmap.bmWidth;
      std::memset(pixels_,0,static_cast<std::size_t>(width_)*4);
    }
    if (!BitBlt(dc_,0,0,width_,1,source,0,220,SRCCOPY)) return false;
    if (!GdiFlush()) return false;
    std::uint32_t hash=2166136261U;
    const auto* bytes=static_cast<const unsigned char*>(pixels_);
    for (int x=0;x<width_;++x) for(int channel=0;channel<3;++channel) hash=(hash^bytes[x*4+channel])*16777619U;
    changed=!has_hash_ || hash!=hash_; hash_=hash; has_hash_=true;
    return true;
  }
 private:
  void reset() {
    if(dc_ && old_ && old_!=HGDI_ERROR) SelectObject(dc_,old_);
    if(bitmap_) DeleteObject(bitmap_);
    if(dc_) DeleteDC(dc_);
    dc_=nullptr; bitmap_=nullptr; old_=nullptr; pixels_=nullptr; width_=0; has_hash_=false;
  }
  HDC dc_{nullptr}; HBITMAP bitmap_{nullptr}; HGDIOBJ old_{nullptr}; void* pixels_{nullptr};
  int width_{0}; std::uint32_t hash_{0}; bool has_hash_{false};
};
paint_band_sampler paint_band;
std::vector<std::string> paint_phases;
std::string paint_phase{"startup"};
std::filesystem::path gallery_checkpoint_path;
unsigned gallery_checkpoint_count{0};
jsValue WKE_CALL_TYPE gallery_checkpoint(jsExecState es, void*) {
  const char* raw=jsToString(es,jsArg(es,0));
  if (!raw || gallery_checkpoint_path.empty() || gallery_checkpoint_count>=5000) return jsBoolean(false);
  const std::string name(raw);
  if(name.empty() || name.size()>48 || !std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)!=0;}))return jsBoolean(false);
  const bool mounted=jsToBoolean(es,jsArg(es,1))!=FALSE;
  std::ofstream stream(gallery_checkpoint_path,std::ios::app);
  stream<<nlohmann::json{{"name",name},{"stage",mounted?"mounted":"before"}}.dump()<<'\n';
  ++gallery_checkpoint_count;
  return jsBoolean(stream.good());
}
double first_vue_ready_qpc_ms{0};
double first_content_paint_qpc_ms{0};
std::filesystem::path cycle_evidence_path;
jsValue WKE_CALL_TYPE write_benchmark_cycle(jsExecState es, void*) {
  const char* raw = jsToString(es, jsArg(es, 0));
  if (!raw || cycle_evidence_path.empty()) return jsBoolean(false);
  const std::string value(raw);
  if (value.size() > 1024U * 1024U) return jsBoolean(false);
  auto record = nlohmann::json::parse(value, nullptr, false);
  if (!record.is_object() || record.value("files", 0) != 10000) return jsBoolean(false);
  std::ofstream stream(cycle_evidence_path, std::ios::app | std::ios::binary);
  stream << value << '\n';
  stream.flush();
  return jsBoolean(stream.good());
}
jsValue WKE_CALL_TYPE mark_benchmark_phase(jsExecState es, void*) {
  const char* raw = jsToString(es, jsArg(es, 0));
  const std::string phase = raw ? raw : "";
  if (phase == "scroll" || phase == "idle" || phase == "paging" || phase == "zoom" || phase == "resize" || phase == "drag" || phase == "animation" || phase == "input") {
    std::lock_guard<std::mutex> lock(paint_samples_mutex);
    paint_phase = phase;
  }
  LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
  return jsDouble(1000.0 * now.QuadPart / frequency.QuadPart);
}
jsValue WKE_CALL_TYPE benchmark_click_callback(jsExecState es, void* param) {
  if (!param) return jsDouble(0);
  return jsDouble(static_cast<soui::sdk::dynamic_miniblink_adapter*>(param)->benchmark_click(jsToInt(es, jsArg(es, 0)), jsToInt(es, jsArg(es, 1))));
}
jsValue WKE_CALL_TYPE benchmark_text_callback(jsExecState, void* param) {
  if (!param) return jsBoolean(false);
  static_cast<soui::sdk::dynamic_miniblink_adapter*>(param)->benchmark_text();
  return jsBoolean(true);
}
void WKE_CALL_TYPE record_benchmark_paint(wkeWebView, void*, HDC dc, int, int, int, int) {
  LARGE_INTEGER tick{}, frequency{};
  QueryPerformanceCounter(&tick);
  QueryPerformanceFrequency(&frequency);
  std::lock_guard<std::mutex> lock(paint_samples_mutex);
  bool pixel_changed=false;
  if (!dc || !paint_band.sample(dc,pixel_changed)) ++paint_pixel_failures;
  else if (pixel_changed && paint_pixel_changes.size()<100000) paint_pixel_changes.push_back(1000.0*tick.QuadPart/frequency.QuadPart);
  if (first_vue_ready_qpc_ms > 0 && first_content_paint_qpc_ms == 0) {
    first_content_paint_qpc_ms = 1000.0 * tick.QuadPart / frequency.QuadPart;
  }
  if (paint_samples.size() < 100000) {
    paint_samples.push_back(1000.0 * tick.QuadPart / frequency.QuadPart);
    paint_phases.push_back(paint_phase);
  }
}
jsValue WKE_CALL_TYPE benchmark_motion_callback(jsExecState es, void* param) {
  const int index = jsToInt(es, jsArg(es, 0));
  if (!param || index < 0 || index > 120) return jsBoolean(false);
  { std::ofstream trace(cycle_evidence_path.parent_path() / "motion-steps.log", std::ios::app); trace << index << '\n'; }
  static_cast<soui::sdk::dynamic_miniblink_adapter*>(param)->benchmark_motion(static_cast<unsigned>(index));
  return jsBoolean(true);
}
#endif

#ifdef _WIN32
void WKE_CALL_TYPE record_live_paint(wkeWebView view, void* context, HDC dc, int x, int y, int width, int height) {
  if (context) static_cast<bridge_adapter*>(context)->record_paint();
#ifdef SOUI_BRIDGE_BENCHMARK
  record_benchmark_paint(view, nullptr, dc, x, y, width, height);
#else
  (void)view; (void)dc; (void)x; (void)y; (void)width; (void)height;
#endif
}
#endif

host_result failure(std::string code, std::string message) {
  return {false, std::move(code), std::move(message)};
}

void bridge_diagnostic_trace(const void* adapter, std::string_view phase,
                             std::string_view request_id = {},
                             std::string_view detail = {}) {
#ifdef _WIN32
  char enabled[2]{};
  if (GetEnvironmentVariableA("SOUI_BRIDGE_TRACE", enabled, sizeof(enabled)) != 1 ||
      enabled[0] != '1') return;
  char temporary[MAX_PATH + 1]{};
  const DWORD length = GetTempPathA(MAX_PATH, temporary);
  if (!length || length > MAX_PATH) return;
  static std::mutex trace_mutex;
  std::lock_guard<std::mutex> lock(trace_mutex);
  std::ofstream trace(std::filesystem::path(temporary) /
      ("soui-bridge-trace-" + std::to_string(GetCurrentProcessId()) + ".log"),
      std::ios::app | std::ios::binary);
  trace << GetCurrentThreadId() << ' ' << adapter << ' ' << phase;
  if (!request_id.empty()) trace << " id=" << request_id;
  if (!detail.empty()) trace << ' ' << detail;
  trace << '\n';
#else
  (void)adapter; (void)phase; (void)request_id; (void)detail;
#endif
}

host_result success(std::string message = {}) { return {true, {}, std::move(message)}; }

bool contains_remote_runtime_reference(const std::filesystem::path& path,
                                       const std::string& content) {
  // Every existing rule requires this literal token. Avoid the regex engine
  // altogether when it is absent; matching semantics remain unchanged.
  if (content.find("//") == std::string::npos) return false;
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

  const std::regex* pattern = nullptr;
  if (extension == ".html" || extension == ".htm") {
    static const std::regex html{R"remote(\b(src|href|action|poster|srcset)\s*=\s*["'][^"']*(https?:)?//)remote",std::regex::icase};
    pattern = &html;
  } else if (extension == ".css") {
    static const std::regex css{R"remote((url\(|@import\s+(url\()?)[[:space:]]*["']?[[:space:]]*(https?:)?//)remote",std::regex::icase};
    pattern = &css;
  } else if (extension == ".js" || extension == ".mjs") {
    static const std::regex js{R"remote((fetch|importScripts|WebSocket|EventSource|import)\s*\(\s*["']\s*(https?:)?//|\.open\s*\(\s*["'][A-Z]+["']\s*,\s*["']\s*(https?:)?//|\.(src|href)\s*=\s*["']\s*(https?:)?//|setAttribute\s*\(\s*["'](src|href)["']\s*,\s*["']\s*(https?:)?//)remote",std::regex::icase};
    pattern = &js;
  } else if (extension == ".json") {
    static const std::regex json{R"remote(["'](file|src|href|url)["']\s*:\s*["']\s*(https?:)?//)remote",std::regex::icase};
    pattern = &json;
  }
  return pattern != nullptr && std::regex_search(content, *pattern);
}

bool contains_untrusted_frame_surface(const std::filesystem::path& path,
                                      const std::string& content) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  if (extension == ".html" || extension == ".htm") {
    static const std::regex html{R"(<\s*(iframe|frame|object|embed)\b)",std::regex::icase};
    return std::regex_search(content, html);
  }
  if (extension == ".js" || extension == ".mjs") {
    static const std::regex js{R"(createElement\s*\(\s*["'](iframe|frame|object|embed)["'])",std::regex::icase};
    return std::regex_search(content, js);
  }
  return false;
}

void append_host_smoke_log(const std::filesystem::path& resource_root, const char* line) {
#ifdef SOUI_BRIDGE_BENCHMARK
  LARGE_INTEGER now{}, frequency{};
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&frequency);
  std::ofstream stages(resource_root.parent_path() / "startup-stages.ndjson", std::ios::app);
  stages.precision(16);
  stages << nlohmann::json{{"stage", line}, {"qpc_ms", 1000.0 * now.QuadPart / frequency.QuadPart}}.dump() << '\n';
#endif
  const auto path = resource_root.parent_path() / "webview-smoke.log";
  if (!std::filesystem::is_regular_file(path)) return;
  std::ofstream output(path, std::ios::app);
  if (output) output << line << '\n';
}

template <typename Bridge>
auto bridge_handle_json(Bridge& bridge, std::string_view request, int)
    -> decltype(std::string{bridge.handle_json(request)}) {
  return std::string{bridge.handle_json(request)};
}

template <typename Bridge>
std::string bridge_handle_json(Bridge&, std::string_view, ...) {
  return R"({"ok":false,"error":{"code":"E_BRIDGE_UNAVAILABLE","message":"The native bridge is not connected."}})";
}

template <typename Bridge>
auto bridge_set_owner_window(Bridge& bridge, void* owner, int)
    -> decltype(bridge.set_owner_window(owner), void()) {
  bridge.set_owner_window(owner);
}

template <typename Bridge>
void bridge_set_owner_window(Bridge&, void*, ...) {}

template <typename Bridge>
auto bridge_set_event_sink_string(Bridge& bridge, std::function<void(std::string)> sink, int)
    -> decltype(bridge.set_event_sink(std::move(sink)), void()) {
  bridge.set_event_sink(std::move(sink));
}

template <typename Bridge>
void bridge_set_event_sink_string(Bridge&, std::function<void(std::string)>, ...) {}

template <typename Bridge>
auto bridge_set_event_sink_view(Bridge& bridge, std::function<void(std::string_view)> sink, int)
    -> decltype(bridge.set_event_sink(std::move(sink)), void()) {
  bridge.set_event_sink(std::move(sink));
}

template <typename Bridge>
void bridge_set_event_sink_view(Bridge&, std::function<void(std::string_view)>, ...) {}

template <typename Bridge>
auto bridge_shutdown_session(Bridge& bridge, const std::string& session_id, int)
    -> decltype(bridge.shutdown_session(session_id), void()) {
  bridge.shutdown_session(session_id);
}

template <typename Bridge>
auto bridge_shutdown_session(Bridge& bridge, const std::string&, long)
    -> decltype(bridge.shutdown_session(), void()) {
  bridge.shutdown_session();
}

template <typename Bridge>
void bridge_shutdown_session(Bridge&, const std::string&, ...) {}

#ifdef _WIN32

struct pending_event {
  dynamic_miniblink_adapter* adapter{nullptr};
  std::string json;
};

std::string json_string_literal(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 2U);
  result.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\r': result += "\\r"; break;
      case '\n': result += "\\n"; break;
      case '\t': result += "\\t"; break;
      default:
        if (character < 0x20U) {
          static constexpr char hex[] = "0123456789abcdef";
          result += "\\u00";
          result.push_back(hex[(character >> 4U) & 0x0FU]);
          result.push_back(hex[character & 0x0FU]);
        } else {
          result.push_back(static_cast<char>(character));
        }
        break;
    }
  }
  result.push_back('"');
  return result;
}

std::filesystem::path canonical_path(const std::filesystem::path& value) {
  std::error_code error;
  const auto result = std::filesystem::weakly_canonical(value, error);
  return error ? value : result;
}

constexpr std::size_t max_bridge_message_bytes = 64U * 1024U;

nlohmann::json bounded_bridge_packet(std::string_view request) {
  if (request.empty() || request.size() > max_bridge_message_bytes) return {};
  try {
    return nlohmann::json::parse(request.begin(), request.end(),
        [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
          if (depth > 32) throw std::invalid_argument("Bridge JSON depth exceeded");
          return true;
        });
  } catch (const std::exception&) { return {}; }
}

std::string packet_string(const nlohmann::json& packet, const char* field) {
  if (!packet.is_object()) return {};
  const auto value = packet.find(field);
  return value != packet.end() && value->is_string() && value->get_ref<const std::string&>().size() <= 512
      ? value->get<std::string>() : std::string{};
}

std::string bridge_error(const std::string& id, const char* code, const char* message) {
  return nlohmann::json{{"requestId", id}, {"ok", false},
      {"error", {{"code", code}, {"message", message}}}}.dump();
}

std::string bridge_resolution_script(const std::string& id, const std::string& response) {
  return "(function(){var p=window.__souiBridgePending;var id=" + json_string_literal(id)
      + ";if(!p||!p[id])return;return p[id].finish(" + json_string_literal(response) + ");})()";
}

std::uint32_t bridge_wait_timeout(const nlohmann::json& packet) {
  const auto timeout = packet.find("timeoutMs");
  if (timeout == packet.end()) return 10000;
  if (!timeout->is_number_unsigned() || timeout->get<std::uint64_t>() < 1 ||
      timeout->get<std::uint64_t>() > 60000) return 0;
  return timeout->get<std::uint32_t>();
}

std::string bridge_request_method(std::string_view request) {
  return packet_string(bounded_bridge_packet(request), "method");
}

// The pinned engine owns process-wide Blink/V8 state and cannot be restarted
// after finalize. All views and bindings therefore share one stable owner.
struct process_runtime_state {
  std::mutex mutex;
  std::unique_ptr<engine_thread> thread;
  HMODULE module{nullptr};
  std::filesystem::path binary;
  std::size_t hosts{0};
  bool initialized{false};
  bool retired{false};
  // Accessed only on the engine owner, including JS callbacks and destruction.
  std::unordered_map<wkeWebView, dynamic_miniblink_adapter*> views;
  ~process_runtime_state() { if (thread) thread->stop(); }
};

process_runtime_state& process_runtime() {
  static process_runtime_state state;
  return state;
}

// Teardown may not return while callbacks still reference the adapter. A native
// kernel hang cannot be safely preempted; external supervision records failure.
bool invoke_lifecycle(engine_thread* thread, engine_thread::task work) {
  if (!thread || !work) return false;
  if (thread->is_owner_thread()) { work(); return true; }
  auto completion = std::make_shared<std::promise<void>>();
  auto future = completion->get_future();
  if (!thread->post([work = std::move(work), completion] {
        try { work(); completion->set_value(); }
        catch (...) { completion->set_exception(std::current_exception()); }
      })) return false;
  try { future.get(); return true; } catch (...) { return false; }
}

host_result acquire_runtime(const host_config& config, engine_thread*& thread, void*& module) {
  auto& runtime = process_runtime();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  if (runtime.retired) return failure("E_RUNTIME_SHUTDOWN", "The process runtime has already shut down.");
  const auto binary = canonical_path(config.miniblink_binary);
  if (!std::filesystem::is_regular_file(binary)) return failure("E_DEPENDENCY_MISSING", "Miniblink binary is missing.");
  if (!binary_matches_architecture(binary, config.architecture)) return failure("E_WRONG_ARCHITECTURE", "Miniblink binary architecture does not match the host.");
  if (runtime.thread && runtime.binary != binary)
    return failure("E_RUNTIME_BINARY", "All windows must use the same process runtime binary.");
  if (!runtime.thread) {
    auto candidate = std::make_unique<engine_thread>();
    host_result result = failure("E_DEPENDENCY_INIT", "Miniblink process initialization failed.");
    const bool started = candidate->start([&runtime, binary, config, &result] {
      runtime.module = LoadLibraryW(binary.c_str());
      if (!runtime.module) { result = failure("E_DEPENDENCY_LOAD", "Miniblink binary could not be loaded."); return false; }
      if (!GetProcAddress(runtime.module, "wkeInitializeEx")) {
        result = failure("E_DEPENDENCY_API", "Miniblink initialization API is missing."); return false;
      }
      wkeSetWkeDllHandle(runtime.module);
#ifdef SOUI_BRIDGE_BENCHMARK
      if (config.noopt_experiment) {
        using set_flags = void(__cdecl*)(const char*, int);
#ifdef _WIN64
        const char* symbol = "?SetFlagsFromString@V8@v8@@SAXPEBDH@Z";
#else
        const char* symbol = "?SetFlagsFromString@V8@v8@@SAXPBDH@Z";
#endif
        const auto set = reinterpret_cast<set_flags>(GetProcAddress(runtime.module, symbol));
        if (!set) { result = failure("E_DEPENDENCY_API", "V8 diagnostic flag export unavailable."); return false; }
        set("--noopt", 7);
      }
#endif
#ifdef SOUI_SOURCE_KERNEL_HOST
      runtime.initialized = initialize_source_kernel(runtime.module);
#else
      runtime.initialized = wkeInitialize() != 0;
#endif
      return runtime.initialized;
    }, [&runtime] {
      if (runtime.initialized) {
        if (wkeShutdown) wkeShutdown();
        else if (wkeFinalize) wkeFinalize();
        runtime.initialized = false;
        runtime.retired = true;
        // Preserve the pinned engine's loader lifetime; do not unload its CRT
        // and process statics a second time after kernel shutdown.
      } else if (runtime.module) FreeLibrary(runtime.module);
      runtime.module = nullptr;
    });
    if (!started) return result;
    runtime.binary = binary;
    runtime.thread = std::move(candidate);
  }
  ++runtime.hosts;
  thread = runtime.thread.get();
  module = runtime.module;
  return success();
}

void release_runtime() {
  auto& runtime = process_runtime();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  if (runtime.hosts) --runtime.hosts;
}

dynamic_miniblink_adapter* adapter_for_context(jsExecState state) {
  if (!state || !jsGetWebView) return nullptr;
  const auto& views = process_runtime().views;
  const auto found = views.find(jsGetWebView(state));
  return found == views.end() ? nullptr : found->second;
}

#endif

}  // namespace

host_result shutdown_miniblink_runtime() {
#ifdef _WIN32
  auto& runtime = process_runtime();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  if (runtime.hosts) return failure("E_RUNTIME_IN_USE", "Destroy all hosts before shutting down the process runtime.");
  if (runtime.thread) {
    if (runtime.thread->is_owner_thread()) return failure("E_RUNTIME_THREAD", "Runtime shutdown must run outside the engine owner.");
    runtime.thread->stop();
    runtime.thread.reset();
  }
  runtime.retired = true;
  return success("Miniblink process runtime shutdown completed.");
#else
  return failure("E_UNSUPPORTED", "The Miniblink runtime requires Windows.");
#endif
}

#ifdef _WIN32
void WKE_CALL_TYPE document_ready_callback(wkeWebView, void* param);
bool WKE_CALL_TYPE document_navigation_callback(wkeWebView, void* param, wkeNavigationType, wkeString url);
void WKE_CALL_TYPE script_context_created_callback(wkeWebView view, void* param, wkeWebFrameHandle frame, void*, int, int world);
jsValue WKE_CALL_TYPE native_post_message(jsExecState es, void* param);
jsValue WKE_CALL_TYPE native_window_chrome(jsExecState es, void* param);
#endif

bool dynamic_miniblink_adapter::create_input_window() {
#ifdef _WIN32
  if (owner_window_ == nullptr) return false;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_DBLCLKS;
  window_class.lpfnWndProc = &interaction_window_proc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  window_class.lpszClassName = kInteractionWindowClass;
  if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;

  RECT bounds{};
  GetClientRect(static_cast<HWND>(owner_window_), &bounds);
  const int width = std::max(1L, bounds.right - bounds.left);
  const int height = std::max(1L, bounds.bottom - bounds.top);
  const HWND child = CreateWindowExW(
      0, kInteractionWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
      0, 0, width, height, static_cast<HWND>(owner_window_), nullptr, instance, this);
  if (!child) return false;
  webview_window_ = child;
  window_chrome_.set_viewport_width(width);
  return true;
#else
  return false;
#endif
}

void dynamic_miniblink_adapter::destroy_input_window() {
  window_chrome_.clear();
  pressed_chrome_ = chrome_action::none;
#ifdef _WIN32
  const HWND child = static_cast<HWND>(webview_window_);
  webview_window_ = nullptr;
  if (child != nullptr && IsWindow(child)) DestroyWindow(child);
#endif
}

void dynamic_miniblink_adapter::refresh_wheel_settings() noexcept {
#ifdef _WIN32
  UINT lines = 3;
  UINT characters = 1;
  if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
    lines = 3;
  if (!SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &characters, 0))
    characters = 1;
  wheel_scroll_lines_.store(lines, std::memory_order_relaxed);
  wheel_scroll_characters_.store(characters, std::memory_order_relaxed);
#endif
}

struct dynamic_miniblink_adapter::event_dispatch_lifetime {
  std::mutex mutex;
  dynamic_miniblink_adapter* adapter{nullptr};
};

dynamic_miniblink_adapter::~dynamic_miniblink_adapter() { (void)unmount(); }

host_result dynamic_miniblink_adapter::mount(const host_config& config) {
#ifdef _WIN32
  if (config.owner_window == nullptr)
    return failure("E_HOST_WINDOW_MISSING", "A SOUI host window is required for the child WebView.");
  if (engine_thread_ != nullptr)
    return failure("E_ALREADY_STARTED", "The Miniblink engine thread is already running.");
#ifdef SOUI_BRIDGE_BENCHMARK
  benchmark_result_slot_.reset();
#endif

  owner_window_ = config.owner_window;
  bridge_adapter_ = config.bridge_adapter;
  event_lifetime_ = std::make_shared<event_dispatch_lifetime>();
  event_lifetime_->adapter = this;
  // SystemParametersInfo can take tens of milliseconds on its first call.
  // Resolve wheel metrics before the user-visible input path is armed.
  refresh_wheel_settings();
  if (!create_input_window()) {
    owner_window_ = nullptr;
    bridge_adapter_ = nullptr;
    return failure("E_WEBVIEW_WINDOW", "SOUI could not create the independent interaction child window.");
  }

  RECT client{};
  GetClientRect(static_cast<HWND>(webview_window_), &client);
  const int width = std::max(1L, client.right - client.left);
  const int height = std::max(1L, client.bottom - client.top);
  interaction_generation_.view += 1;
  if (!interaction_generation_.view) interaction_generation_.view = 1;
  interaction_generation_.document = 1;
  interaction_generation_.geometry = 1;
  next_input_sequence_ = 0;
  wheel_smoothing_.reset();

  const auto acquired = acquire_runtime(config, engine_thread_, module_);
  if (!acquired.ok) {
    destroy_input_window(); owner_window_ = nullptr; bridge_adapter_ = nullptr;
    return acquired;
  }
  // Arm the worker before loading any page on the shared engine owner.
  bridge_generation_.store(1, std::memory_order_release);
  bridge_budget_ = std::make_shared<detail::bridge_request_budget>();
  bridge_thread_ = std::make_unique<engine_thread>();
  if (!bridge_thread_->start(
          [this] {
            bridge_com_initialized_ =
                SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
            return bridge_com_initialized_;
          },
          [this] {
            if (bridge_com_initialized_) CoUninitialize();
            bridge_com_initialized_ = false;
          })) {
    bridge_thread_.reset();
    (void)unmount();
    return failure("E_BRIDGE_THREAD", "The asynchronous native bridge worker could not start.");
  }
  bridge_accepting_.store(true, std::memory_order_release);

  host_result startup = failure("E_DEPENDENCY_INIT", "Miniblink engine startup did not run.");
  const host_config engine_config = config;
  const bool invoked = invoke_lifecycle(engine_thread_,
      [this, engine_config, width, height, &startup] {
        startup = mount_on_engine(engine_config, width, height);
      });
  if (!invoked || !startup.ok) {
    (void)unmount();
    return startup;
  }

  interaction_router_.activate(interaction_generation_);
  ShowWindow(static_cast<HWND>(webview_window_), SW_SHOW);
#ifdef SOUI_TEST_KIT
  if (soui::test_kit::enabled()) {
    test_agent_ = std::make_unique<soui::test_kit::pipe_agent>(
        [this](const soui::test_kit::json& request) { return test_kit_request(request); },
        static_cast<HWND>(owner_window_), config.architecture);
  } else
#endif
  SetFocus(static_cast<HWND>(webview_window_));
  return success("SOUI interaction window mounted on the shared Miniblink engine thread.");
#else
  (void)config;
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

host_result dynamic_miniblink_adapter::mount_on_engine(const host_config& config, int width, int height) {
#ifdef _WIN32
  if (config.owner_window == nullptr) {
    return failure("E_HOST_WINDOW_MISSING", "A SOUI host window is required for the child WebView.");
  }
  if (config.miniblink_binary.empty()) {
    return failure("E_DEPENDENCY_MISSING", "Miniblink binary path is required.");
  }

#ifdef SOUI_BRIDGE_BENCHMARK
  if(config.noopt_experiment) {
    const auto opt = reinterpret_cast<const bool*>(GetProcAddress(static_cast<HMODULE>(module_),"?FLAG_opt@internal@v8@@3_NA"));
    std::ofstream check(config.benchmark_output_directory/"v8-noopt.json");
    check<<nlohmann::json{{"requested",true},{"readable",opt!=nullptr},{"optEnabled",opt?*opt:true}}.dump();
  }
#endif
  if (wkeCreateWebView == nullptr || wkeDestroyWebView == nullptr || wkeSetHandle == nullptr ||
      wkeGetInteractionApi == nullptr || wkeLoadFileW == nullptr ||
      wkeOnNavigation == nullptr || wkeOnDidCreateScriptContext == nullptr || wkeIsMainFrame == nullptr || wkeGetString == nullptr ||
      wkeSetNavigationToNewWindowEnable == nullptr ||
      wkeSetContextMenuEnabled == nullptr || wkeOnDocumentReady == nullptr || wkeJsBindFunction == nullptr ||
      wkeResize == nullptr || wkeFireMouseEvent == nullptr || wkeFireMouseWheelEvent == nullptr ||
      wkeFireKeyDownEvent == nullptr || wkeFireKeyUpEvent == nullptr || wkeFireKeyPressEvent == nullptr ||
      wkeRunJS == nullptr || wkeIsProcessingUserGesture == nullptr || jsGetWebView == nullptr ||
      wkeGetURL == nullptr || wkeGlobalExec == nullptr || jsIsString == nullptr || jsBoolean == nullptr ||
      (config.managed_frame_timing && wkeSetDebugConfig == nullptr)) {
    return failure("E_DEPENDENCY_API", "The installed Miniblink package lacks the required WebView API.");
  }

  managed_frame_timing_ = config.managed_frame_timing;
  host_managed_pump_ = managed_frame_timing_;
  char other_thread_paint_option[2] = {};
  experimental_other_thread_paint_ =
      GetEnvironmentVariableA("SOUI_PAINT_CALLBACK_OTHER_THREAD", other_thread_paint_option,
                              sizeof(other_thread_paint_option)) == 1 &&
      other_thread_paint_option[0] == '1';
  char compositor_rate_option[2] = {};
  experimental_compositor_rate_ =
      GetEnvironmentVariableA("SOUI_COMPOSITOR_FRAME_RATE", compositor_rate_option,
                              sizeof(compositor_rate_option)) == 1 &&
      compositor_rate_option[0] == '1';
  configured_wake_interval_ = -1;
  configured_draw_min_interval_ = -1;
  configured_main_frame_rate_ = -1;
#ifdef SOUI_SOURCE_KERNEL_HOST
  char main_frame_option[2] = {};
  experimental_main_frame_scheduler_ = GetEnvironmentVariableA("SOUI_SOURCE_MAIN_FRAME_SCHEDULER", main_frame_option, sizeof(main_frame_option)) == 1 && main_frame_option[0] == '1';
#endif
#ifdef SOUI_BRIDGE_BENCHMARK
  benchmark_dataset_ = config.benchmark_dataset;
#endif
  resource_root_ = canonical_path(config.resource_root);
  gpu_trace_active_ = false;
  gpu_trace_path_.clear();
  char gpu_trace_option[MAX_PATH] = {};
  const DWORD gpu_trace_length = GetEnvironmentVariableA("SOUI_GPU_PRESENT_TRACE", gpu_trace_option,
                                                         sizeof(gpu_trace_option));
  if (gpu_trace_length > 0 && gpu_trace_length < sizeof(gpu_trace_option))
    gpu_trace_path_ = std::filesystem::path{gpu_trace_option};
#ifdef SOUI_BRIDGE_BENCHMARK
  cycle_evidence_path = resource_root_.parent_path() / "task-cycles.ndjson";
  gallery_checkpoint_path = config.benchmark_output_directory.empty() ? std::filesystem::path{} : config.benchmark_output_directory / "gallery-checkpoints.ndjson";
  gallery_checkpoint_count = 0;
#endif
  const auto view = wkeCreateWebView();
  if (view == nullptr) {
    return failure("E_WEBVIEW_CREATE", "Miniblink could not create a child WebView.");
  }
  webview_ = view;
#ifdef SOUI_BRIDGE_BENCHMARK
  append_host_smoke_log(resource_root_, "webview-created");
#endif
  wkeSetHandle(view, static_cast<HWND>(webview_window_));

  const auto* interaction_api = wkeGetInteractionApi(WKE_INTERACTION_ABI_VERSION);
  if (interaction_api == nullptr || interaction_api->structSize < sizeof(wkeInteractionApi)
      || interaction_api->abiVersion != WKE_INTERACTION_ABI_VERSION
      || (interaction_api->capabilities & WKE_INTERACTION_CAP_DIRECT_WHEEL) == 0
      || (interaction_api->capabilities & WKE_INTERACTION_CAP_CLICK_BARRIER) == 0
      || (interaction_api->capabilities & WKE_INTERACTION_CAP_DIRECT_PRESENTATION) == 0
      || interaction_api->createEndpoint == nullptr || interaction_api->submitWheel == nullptr
      || interaction_api->configurePresentation == nullptr
      || interaction_api->captureScrollBarrier == nullptr
      || interaction_api->synchronizeScrollBarrier == nullptr
      || interaction_api->invalidateEndpoint == nullptr || interaction_api->releaseEndpoint == nullptr) {
    wkeDestroyWebView(view);
    webview_ = nullptr;
    return failure("E_INDEPENDENT_INTERACTION_UNAVAILABLE",
                   "The Miniblink binary does not expose the required independent interaction ABI.");
  }
  interaction_api_ = interaction_api;
  interaction_endpoint_ = interaction_api->createEndpoint(view, interaction_generation_.view);
  if (interaction_endpoint_ == nullptr) {
    wkeDestroyWebView(view);
    webview_ = nullptr;
    return failure("E_INTERACTION_ENDPOINT", "Miniblink could not create the compositor interaction endpoint.");
  }
  if (!interaction_api->configurePresentation(interaction_endpoint_,
          WKE_INTERACTION_PRESENT_DIRECT_HWND)) {
    interaction_api->releaseEndpoint(interaction_endpoint_);
    interaction_endpoint_ = nullptr;
    wkeDestroyWebView(view);
    webview_ = nullptr;
    return failure("E_INDEPENDENT_PRESENTATION",
                   "Miniblink could not enable compositor-owned window presentation.");
  }

  wkeSetNavigationToNewWindowEnable(view, false);
  wkeSetContextMenuEnabled(view, false);
  wkeOnDocumentReady(view, &document_ready_callback, this);
  wkeOnNavigation(view, &document_navigation_callback, this);
  wkeOnDidCreateScriptContext(view, &script_context_created_callback, this);
  if (managed_frame_timing_) {
    // This framework owns an explicit standard profile, not an inferred vendor default.
    wkeSetDebugConfig(view, "wakeMinInterval", "5");
    configured_wake_interval_ = 5;
  }
  if (experimental_other_thread_paint_ && wkeSetDebugConfig)
    wkeSetDebugConfig(view, "paintcallbackInOtherThread", "1");
  char direct_gpu_present_option[2] = {};
  if (GetEnvironmentVariableA("SOUI_GPU_DIRECT_PRESENT", direct_gpu_present_option,
                              sizeof(direct_gpu_present_option)) == 1 &&
      direct_gpu_present_option[0] == '1' && wkeSetDebugConfig)
    wkeSetDebugConfig(view, "gpuPresent", "1");
#ifdef SOUI_BRIDGE_BENCHMARK
  if (config.engine_timing_experiment && wkeSetDebugConfig) {
    // Isolated-process experiment only. The original values cannot be queried
    // reliably from these DLLs, so this is intentionally not a runtime toggle.
    if(config.engine_timing_experiment & 1) wkeSetDebugConfig(view, "wakeMinInterval", "0");
    if(config.engine_timing_experiment & 2) wkeSetDebugConfig(view, "drawMinInterval", "0");
  }
#endif
  if (wkeOnPaintUpdated) wkeOnPaintUpdated(view, &record_live_paint, bridge_adapter_);
  process_runtime().views.emplace(view, this);
  wkeJsBindFunction("__souiNativePostMessage", &native_post_message, nullptr, 1);
  wkeJsBindFunction("__souiNativeWindowChrome", &native_window_chrome, nullptr, 1);
#ifdef SOUI_BRIDGE_BENCHMARK
  wkeJsBindFunction("__souiBenchmarkMarkPhase", &mark_benchmark_phase, nullptr, 1);
  wkeJsBindFunction("__souiBenchmarkWriteCycle", &write_benchmark_cycle, nullptr, 1);
  wkeJsBindFunction("__souiBenchmarkMotion", &benchmark_motion_callback, this, 1);
  wkeJsBindFunction("__souiBenchmarkClick", &benchmark_click_callback, this, 2);
  wkeJsBindFunction("__souiBenchmarkText", &benchmark_text_callback, this, 0);
  wkeJsBindFunction("__souiBenchmarkGalleryCheckpoint", &gallery_checkpoint, nullptr, 2);
#endif
  if (bridge_adapter_ != nullptr) {
    auto& bridge = *static_cast<bridge_adapter*>(bridge_adapter_);
    bridge_set_owner_window(bridge, owner_window_, 0);
    const std::weak_ptr<event_dispatch_lifetime> weak = event_lifetime_;
    bridge.set_event_sink([weak](std::string_view event) {
      if (const auto lifetime = weak.lock()) {
        std::lock_guard<std::mutex> lock(lifetime->mutex);
        if (lifetime->adapter) lifetime->adapter->dispatch_event(std::string{event});
      }
    });
  }

  // React is the product surface.  The original Vue reference remains a
  // deliberate compatibility entry (`--entry=index.html`) but must not be
  // the default, otherwise the commercial React shell, 4K player and full
  // component catalog are never exercised by a normal F5/demo launch.
  std::filesystem::path entry = resource_root_ / "react.html";
  if (!std::filesystem::is_regular_file(entry))
    entry = resource_root_ / "index.html";
  char entry_option[MAX_PATH] = {};
  const DWORD entry_length = GetEnvironmentVariableA("SOUI_ENTRY_FILE", entry_option, sizeof(entry_option));
  if (entry_length > 0 && entry_length < sizeof(entry_option)) {
    const auto candidate = canonical_path(resource_root_ / std::filesystem::path{entry_option});
    const auto root = canonical_path(resource_root_);
    const auto root_text = root.wstring();
    const auto candidate_text = candidate.wstring();
    if (candidate_text.rfind(root_text, 0) == 0 && std::filesystem::is_regular_file(candidate))
      entry = candidate;
  }
  const auto index = entry.wstring();
  wkeLoadFileW(view, index.c_str());
#ifdef SOUI_BRIDGE_BENCHMARK
  append_host_smoke_log(resource_root_, "local-page-requested");
#endif
  wkeResize(view, width, height);
  return success("Miniblink WebView bound to the SOUI-owned interaction window.");
#else
  (void)config;
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

host_result dynamic_miniblink_adapter::unmount() {
#ifdef _WIN32
  bridge_accepting_.store(false, std::memory_order_release);
  // Finish native callbacks that could already be inside admission before
  // another thread stops and destroys the worker they access.
  if (engine_thread_ != nullptr && !invoke_lifecycle(engine_thread_, [] {}))
    return failure("E_ENGINE_THREAD", "The engine could not stop admitting bridge requests.");
#ifdef SOUI_TEST_KIT
  // Join the controller worker before invalidating its engine/adapter lifetime.
  test_agent_.reset();
#endif
  interaction_router_.deactivate();
  wheel_smoothing_.reset();
  bridge_generation_.fetch_add(1, std::memory_order_acq_rel);
  if (event_lifetime_) {
    std::lock_guard<std::mutex> lock(event_lifetime_->mutex);
    event_lifetime_->adapter = nullptr;
  }
  if (bridge_adapter_) bridge_adapter_->set_event_sink({});
  auto drain_events = [this] {
    if (owner_window_ == nullptr) return;
    MSG message{};
    while (PeekMessageW(&message, static_cast<HWND>(owner_window_), bridge_event_message,
                        bridge_event_message, PM_REMOVE) != FALSE) {
      delete static_cast<pending_event*>(reinterpret_cast<void*>(message.lParam));
    }
  };
  drain_events();
  if (bridge_thread_ != nullptr) {
    bridge_thread_->stop(false);
    bridge_thread_.reset();
  }
  if (engine_thread_ != nullptr) {
    if (!invoke_lifecycle(engine_thread_, [this] { unmount_on_engine(); }))
      return failure("E_ENGINE_THREAD", "The engine could not complete WebView teardown.");
    release_runtime();
    engine_thread_ = nullptr;
  }
#ifdef SOUI_BRIDGE_BENCHMARK
  benchmark_result_slot_.reset();
#endif
  drain_events();
  destroy_input_window();
  owner_window_ = nullptr;
  bridge_adapter_ = nullptr;
#endif
  document_ready_ = false;
  application_ready_ = false;
  return success("Miniblink WebView and endpoint unmounted; process runtime remains available.");
}

void dynamic_miniblink_adapter::unmount_on_engine() {
#ifdef _WIN32
  append_host_smoke_log(resource_root_, "adapter-unmount-start");
  std::string closing_session;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    closing_session.swap(session_id_);
  }
  if (bridge_adapter_ != nullptr && !closing_session.empty())
    bridge_shutdown_session(*static_cast<bridge_adapter*>(bridge_adapter_), closing_session, 0);
  append_host_smoke_log(resource_root_, "adapter-bridge-shutdown");
  const auto* interaction_api = static_cast<const wkeInteractionApi*>(interaction_api_);
  if (interaction_api != nullptr && interaction_endpoint_ != nullptr) {
    interaction_api->invalidateEndpoint(interaction_endpoint_);
    interaction_api->releaseEndpoint(interaction_endpoint_);
    interaction_endpoint_ = nullptr;
    interaction_api_ = nullptr;
  }
  append_host_smoke_log(resource_root_, "adapter-events-drained");
  if (webview_ != nullptr && wkeDestroyWebView != nullptr) {
    process_runtime().views.erase(static_cast<wkeWebView>(webview_));
    wkeDestroyWebView(static_cast<wkeWebView>(webview_));
    webview_ = nullptr;
  }
  append_host_smoke_log(resource_root_, "adapter-webview-destroyed");
  module_ = nullptr; // borrowed module; only application exit finalizes it
  resource_root_.clear();
  gpu_trace_path_.clear();
  gpu_trace_active_ = false;
#endif
  document_ready_ = false;
  application_ready_ = false;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    probe_report_.clear();
  }
}

host_result dynamic_miniblink_adapter::show(bool visible) {
#ifdef _WIN32
  if (webview_window_ == nullptr || !IsWindow(static_cast<HWND>(webview_window_))) {
    return failure("E_NOT_STARTED", "The child WebView has not been created.");
  }
  ShowWindow(static_cast<HWND>(webview_window_), visible ? SW_SHOW : SW_HIDE);
  return success(visible ? "WebView shown." : "WebView hidden without cancelling native tasks.");
#else
  (void)visible;
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

host_result dynamic_miniblink_adapter::reload() {
  window_chrome_.clear();
#ifdef _WIN32
  if (engine_thread_ == nullptr) {
    return failure("E_NOT_STARTED", "The child WebView has not been created.");
  }
  interaction_generation_.document += 1;
  if (!interaction_generation_.document) interaction_generation_.document = 1;
  next_input_sequence_ = 0;
  wheel_smoothing_.reset();
  interaction_router_.activate(interaction_generation_);
  bridge_generation_.fetch_add(1, std::memory_order_acq_rel);
#ifdef SOUI_BRIDGE_BENCHMARK
  benchmark_result_slot_.reset();
#endif
  host_result result = failure("E_ENGINE_THREAD", "The reload did not reach the Miniblink owner thread.");
  const bool invoked = invoke_lifecycle(engine_thread_, [this, &result] {
    if (webview_ == nullptr || wkeReload == nullptr) {
      result = failure("E_NOT_STARTED", "The child WebView has not been created.");
      return;
    }
    if (wkeRunJS != nullptr) {
      static constexpr const char* prepare_reload =
          "(function(){try{if(typeof window.__souiBeforeReload==='function')"
          "window.__souiBeforeReload();}catch(e){}})();";
      (void)wkeRunJS(static_cast<wkeWebView>(webview_), prepare_reload);
    }
    std::string closing_session;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      closing_session.swap(session_id_);
    }
    if (bridge_adapter_ != nullptr && !closing_session.empty())
      bridge_shutdown_session(*static_cast<bridge_adapter*>(bridge_adapter_), closing_session, 0);
    document_ready_ = false;
    application_ready_ = false;
    wkeReload(static_cast<wkeWebView>(webview_));
    result = success("WebView reload requested on the engine thread.");
  });
  return invoked ? result : failure("E_ENGINE_THREAD", "The Miniblink owner thread did not accept reload.");
#else
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
host_result dynamic_miniblink_adapter::request_memory_recovery() {
#ifdef _WIN32
  if (engine_thread_ == nullptr)
    return failure("E_NOT_STARTED", "The Miniblink engine thread is not running.");
  host_result result = failure("E_ENGINE_THREAD", "The memory recovery request did not reach the Miniblink owner thread.");
  const bool invoked = engine_thread_->invoke([&result] {
    using gc_timer = void(__cdecl*)(void*, long);
    const auto module = GetModuleHandleW(L"node.dll");
    const auto gc = reinterpret_cast<gc_timer>(GetProcAddress(module, "wkeGC"));
    const auto resource_gc = reinterpret_cast<gc_timer>(GetProcAddress(module, "wkeSetResourceGc"));
    if (!gc || !resource_gc) {
      result = failure("E_GC_API_MISSING", "The Miniblink recovery timer APIs are unavailable.");
      return;
    }
    gc(nullptr, 1);
    resource_gc(nullptr, 1);
    result = success("Miniblink recovery timers requested on the engine owner thread.");
  });
  return invoked ? result
                 : failure("E_ENGINE_THREAD", "The memory recovery request timed out on the Miniblink owner thread.");
#else
  return failure("E_UNSUPPORTED", "Memory recovery diagnostics require Windows.");
#endif
}
#endif

host_result dynamic_miniblink_adapter::resize(int width, int height) {
#ifdef _WIN32
  if (engine_thread_ == nullptr || webview_window_ == nullptr || width <= 0 || height <= 0) {
    return failure("E_INVALID_ARGUMENT", "A live WebView and positive dimensions are required.");
  }
  window_chrome_.set_viewport_width(width);
  MoveWindow(static_cast<HWND>(webview_window_), 0, 0, width, height, TRUE);
  interaction_generation_.geometry += 1;
  if (!interaction_generation_.geometry) interaction_generation_.geometry = 1;
  next_input_sequence_ = 0;
  wheel_smoothing_.reset();
  interaction_router_.activate(interaction_generation_);
  if (!engine_thread_->post([this, width, height] {
        if (webview_ != nullptr && wkeResize != nullptr)
          wkeResize(static_cast<wkeWebView>(webview_), width, height);
      })) {
    return failure("E_ENGINE_THREAD", "The Miniblink owner thread rejected resize.");
  }
  return success("SOUI window resized; engine resize queued.");
#else
  (void)width;
  (void)height;
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

host_result dynamic_miniblink_adapter::focus() {
#ifdef _WIN32
  if (engine_thread_ == nullptr || webview_window_ == nullptr) {
    return failure("E_NOT_STARTED", "The child WebView has not been created.");
  }
  SetFocus(static_cast<HWND>(webview_window_));
  if (!engine_thread_->post([this] {
        if (webview_ != nullptr && wkeSetFocus != nullptr)
          wkeSetFocus(static_cast<wkeWebView>(webview_));
      })) {
    return failure("E_ENGINE_THREAD", "The Miniblink owner thread rejected focus.");
  }
  return success("SOUI input window focused; engine focus queued.");
#else
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

void dynamic_miniblink_adapter::pump() {
#ifdef _WIN32
  if (engine_thread_ == nullptr || pump_pending_.exchange(true)) return;
  if (!engine_thread_->post([this] {
        pump_on_engine();
        pump_pending_ = false;
      })) {
    pump_pending_ = false;
  }
#endif
}

void dynamic_miniblink_adapter::pump_on_engine() {
#ifdef _WIN32
  if (webview_ != nullptr && wkeUpdate != nullptr) {
    bool gpu_active = false;
    if (bridge_adapter_ != nullptr && webview_window_ != nullptr) {
      char active_option[2] = {};
      const bool process_active = GetEnvironmentVariableA("SOUI_GPU_ACTIVE", active_option,
                                                          sizeof(active_option)) == 1 && active_option[0] == '1';
      if (!gpu_trace_active_ && !gpu_trace_path_.empty()) {
        std::ifstream trace(gpu_trace_path_, std::ios::in | std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(trace)), std::istreambuf_iterator<char>());
        if (content.find("swapped=1") != std::string::npos)
          gpu_trace_active_ = true;
      }
      gpu_active = gpu_trace_active_ ||
          GetPropA(static_cast<HWND>(webview_window_), "SOUI_GPU_ACTIVE") != nullptr || process_active;
      static_cast<bridge_adapter*>(bridge_adapter_)->set_gpu_active(gpu_active);
      const auto present_property = GetPropA(static_cast<HWND>(webview_window_), "SOUI_GPU_PRESENT_COUNT");
      std::uint64_t present_count = present_property
          ? static_cast<std::uint64_t>(reinterpret_cast<ULONG_PTR>(present_property))
          : 0;
      if (!present_property) {
        char present_option[32] = {};
        const DWORD present_length = GetEnvironmentVariableA("SOUI_GPU_PRESENT_COUNT",
                                                               present_option, sizeof(present_option));
        if (present_length && present_length < sizeof(present_option))
          present_count = _strtoui64(present_option, nullptr, 10);
      }
      static_cast<bridge_adapter*>(bridge_adapter_)->set_present_count(present_count);
    }
    const bool high_rate = bridge_adapter_ &&
        static_cast<bridge_adapter*>(bridge_adapter_)->active_frame_rate() > 60;
#ifdef SOUI_SOURCE_KERNEL_HOST
    if (managed_frame_timing_ && wkeSetDebugConfig) {
      const int rate = experimental_main_frame_scheduler_ ?
          (bridge_adapter_ ? static_cast<bridge_adapter*>(bridge_adapter_)->active_frame_rate() : 60) : 0;
      if (configured_main_frame_rate_ != rate) {
        const auto value = std::to_string(rate);
        wkeSetDebugConfig(static_cast<wkeWebView>(webview_), "mainFrameRate", value.c_str());
        configured_main_frame_rate_ = rate;
      }
    }
#endif
    if (managed_frame_timing_ && wkeSetDebugConfig) {
      const int desired = high_rate ? 0 : 5;
      if (configured_wake_interval_ != desired) {
        wkeSetDebugConfig(static_cast<wkeWebView>(webview_), "wakeMinInterval", high_rate ? "0" : "5");
        configured_wake_interval_ = desired;
      }
    }
    if (wkeSetDebugConfig) {
      const int rate = bridge_adapter_ ? static_cast<bridge_adapter*>(bridge_adapter_)->active_frame_rate() : 60;
      // The GPU path is paced by the display clock and can draw as soon as a
      // dirty frame is ready. CPU-safe fallback follows the same selected
      // 60/120/144 Hz tier so the reported draw budget matches the actual
      // cadence; the engine may still miss a pulse when the CPU cannot keep up.
      char cpu_high_option[2] = {};
      const bool cpu_high_refresh = GetEnvironmentVariableA("SOUI_CPU_HIGH_REFRESH", cpu_high_option,
                                                            sizeof(cpu_high_option)) == 1 && cpu_high_option[0] == '1';
      char cpu_interval_override[16] = {};
      const DWORD cpu_interval_length = GetEnvironmentVariableA("SOUI_CPU_DRAW_INTERVAL_MS",
                                                                 cpu_interval_override, sizeof(cpu_interval_override));
      const int interval = experimental_compositor_rate_
          ? (rate > 0 ? (1000 + rate - 1) / rate : 17)
          : (gpu_active ? 0 : (cpu_interval_length > 0 && cpu_interval_length < sizeof(cpu_interval_override)
                                ? std::clamp(std::atoi(cpu_interval_override), 0, 1000)
                                : ((cpu_high_refresh || rate > 60) && rate > 60
                                    ? (1000 + rate - 1) / rate
                                    : 17)));
      if (configured_draw_min_interval_ != interval) {
        const auto value = std::to_string(interval);
        wkeSetDebugConfig(static_cast<wkeWebView>(webview_), "drawMinInterval", value.c_str());
        configured_draw_min_interval_ = interval;
      }
      if (bridge_adapter_ != nullptr)
        static_cast<bridge_adapter*>(bridge_adapter_)->set_draw_interval(
            configured_draw_min_interval_ < 0 ? 0U : static_cast<unsigned>(configured_draw_min_interval_));
    }
    wkeUpdate();
    if (high_rate && wkeWake) {
      wkeWake(static_cast<wkeWebView>(webview_));
    }
    if (wkeRepaintIfNeeded != nullptr &&
        (wkeIsDirty == nullptr || wkeIsDirty(static_cast<wkeWebView>(webview_))))
      wkeRepaintIfNeeded(static_cast<wkeWebView>(webview_));
  }
#endif
}

input_packet dynamic_miniblink_adapter::make_input_packet(input_kind kind) noexcept {
  input_packet packet;
  packet.kind = kind;
  packet.generation = interaction_generation_;
  packet.sequence = next_input_sequence_.fetch_add(1) + 1;
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  packet.timestamp_seconds = std::chrono::duration<double>(now).count();
  return packet;
}

bool dynamic_miniblink_adapter::submit_wheel(const input_packet& packet,
                                             scroll_sample& sample) {
#ifdef _WIN32
  const auto* api = static_cast<const wkeInteractionApi*>(interaction_api_);
  if (api == nullptr || interaction_endpoint_ == nullptr || api->submitWheel == nullptr)
    return false;
  // Ctrl+wheel is page zoom, not scrolling. Keep it on Blink's ordinary
  // input path. Windows wheel deltas also have the opposite sign from scroll
  // offsets and are expressed in WHEEL_DELTA ticks rather than CSS pixels.
  if ((packet.modifiers & MK_CONTROL) != 0)
    return false;
  static constexpr double scrollbar_pixels_per_line = 100.0 / 3.0;
  const double wheel_ticks = packet.delta_y / static_cast<double>(WHEEL_DELTA);
  const std::uint32_t wheel_scroll_lines =
      wheel_scroll_lines_.load(std::memory_order_relaxed);
  const std::uint32_t wheel_scroll_characters =
      wheel_scroll_characters_.load(std::memory_order_relaxed);
  double compositor_delta_x = 0.0;
  double compositor_delta_y = 0.0;
  if ((packet.modifiers & MK_SHIFT) != 0) {
    compositor_delta_x = -wheel_ticks
        * static_cast<double>(wheel_scroll_characters)
        * scrollbar_pixels_per_line;
  } else {
    compositor_delta_y = -wheel_ticks
        * (wheel_scroll_lines == WHEEL_PAGESCROLL
               ? 1.0
               : static_cast<double>(wheel_scroll_lines)
                   * scrollbar_pixels_per_line);
  }
  wkeInteractionWheelInput input{};
  input.structSize = sizeof(input);
  input.flags = packet.modifiers;
  input.viewGeneration = packet.generation.view;
  input.documentGeneration = packet.generation.document;
  input.geometryVersion = packet.generation.geometry;
  input.inputSequence = packet.sequence;
  input.gestureId = packet.gesture;
  input.monotonicTime = packet.timestamp_seconds;
  input.x = packet.x;
  input.y = packet.y;
  input.deltaX = compositor_delta_x;
  input.deltaY = compositor_delta_y;
  input.smooth = wheel_smoothing_.should_smooth(packet.timestamp_seconds) ? 1 : 0;
  wkeInteractionScrollSample output{};
  output.structSize = sizeof(output);
  if (!api->submitWheel(interaction_endpoint_, &input, &output) || !output.accepted)
    return false;
  sample.scene_version = output.sceneVersion;
  sample.scroll_sequence = output.inputSequence;
  sample.offset_x = output.offsetX;
  sample.offset_y = output.offsetY;
  return true;
#else
  (void)packet;
  (void)sample;
  return false;
#endif
}

bool dynamic_miniblink_adapter::capture_scroll_barrier(
    const input_packet& packet, scroll_sample& sample) {
#ifdef _WIN32
  const auto* api = static_cast<const wkeInteractionApi*>(interaction_api_);
  if (api == nullptr || interaction_endpoint_ == nullptr
      || api->captureScrollBarrier == nullptr)
    return false;

  wkeInteractionBarrierRequest request{};
  request.structSize = sizeof(request);
  request.flags = packet.modifiers;
  request.viewGeneration = packet.generation.view;
  request.documentGeneration = packet.generation.document;
  request.geometryVersion = packet.generation.geometry;
  request.x = packet.x;
  request.y = packet.y;
  wkeInteractionScrollBarrier output{};
  output.structSize = sizeof(output);
  if (!api->captureScrollBarrier(interaction_endpoint_, &request, &output)
      || !output.valid)
    return false;

  sample.scene_version = output.sceneVersion;
  sample.view_id = output.viewId;
  sample.node_id = output.nodeId;
  sample.node_generation = output.nodeGeneration;
  sample.scroll_epoch = output.scrollEpoch;
  sample.sample_sequence = output.sampleSequence;
  sample.scroll_sequence = output.inputSequence;
  sample.consumed_total_x = output.consumedTotalX;
  sample.consumed_total_y = output.consumedTotalY;
  sample.offset_x = output.offsetX;
  sample.offset_y = output.offsetY;
  return true;
#else
  (void)packet;
  (void)sample;
  return false;
#endif
}

bool dynamic_miniblink_adapter::post_input(const input_packet& packet) {
  return engine_thread_ != nullptr
      && engine_thread_->post([this, packet] { dispatch_input_on_engine(packet); });
}

void dynamic_miniblink_adapter::dispatch_input_on_engine(input_packet packet) {
#ifdef _WIN32
  if (webview_ == nullptr || !(packet.generation == interaction_router_.generation())) return;
  const auto view = static_cast<wkeWebView>(webview_);
  switch (packet.kind) {
    case input_kind::wheel:
      if (wkeFireMouseWheelEvent != nullptr)
        (void)wkeFireMouseWheelEvent(view, packet.x, packet.y,
                                     static_cast<int>(packet.delta_y), packet.modifiers);
      break;
    case input_kind::pointer_move:
    case input_kind::pointer_down:
    case input_kind::pointer_up: {
      if (wkeFireMouseEvent == nullptr) break;
      if (packet.kind == input_kind::pointer_down
          && packet.barrier.scene_version != 0) {
        const auto* api = static_cast<const wkeInteractionApi*>(interaction_api_);
        if (api == nullptr || interaction_endpoint_ == nullptr
            || api->synchronizeScrollBarrier == nullptr)
          break;
        wkeInteractionScrollBarrier barrier{};
        barrier.structSize = sizeof(barrier);
        barrier.valid = 1;
        barrier.viewGeneration = packet.generation.view;
        barrier.sceneVersion = packet.barrier.scene_version;
        barrier.viewId = packet.barrier.view_id;
        barrier.nodeId = packet.barrier.node_id;
        barrier.nodeGeneration = packet.barrier.node_generation;
        barrier.scrollEpoch = packet.barrier.scroll_epoch;
        barrier.sampleSequence = packet.barrier.sample_sequence;
        barrier.inputSequence = packet.barrier.scroll_sequence;
        barrier.consumedTotalX = packet.barrier.consumed_total_x;
        barrier.consumedTotalY = packet.barrier.consumed_total_y;
        barrier.offsetX = packet.barrier.offset_x;
        barrier.offsetY = packet.barrier.offset_y;
        if (!api->synchronizeScrollBarrier(interaction_endpoint_, &barrier))
          break;
      }
      unsigned int message = WM_MOUSEMOVE;
      if (packet.kind == input_kind::pointer_down)
        message = packet.double_click ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN;
      if (packet.kind == input_kind::pointer_up) message = WM_LBUTTONUP;
      (void)wkeFireMouseEvent(view, message, packet.x, packet.y, packet.modifiers);
      break;
    }
    case input_kind::key_down:
      if (wkeFireKeyDownEvent != nullptr)
        (void)wkeFireKeyDownEvent(view, packet.key_code, packet.modifiers, packet.system_key);
      break;
    case input_kind::key_up:
      if (wkeFireKeyUpEvent != nullptr)
        (void)wkeFireKeyUpEvent(view, packet.key_code, packet.modifiers, packet.system_key);
      break;
    case input_kind::character:
      if (wkeFireKeyPressEvent != nullptr)
        (void)wkeFireKeyPressEvent(view, packet.key_code, packet.modifiers, packet.system_key);
      break;
    case input_kind::focus:
      if (wkeSetFocus != nullptr) wkeSetFocus(view);
      break;
    case input_kind::blur:
      if (wkeKillFocus != nullptr) wkeKillFocus(view);
      break;
    case input_kind::ime_commit:
      if (wkeConfirmComposition != nullptr) {
        const std::wstring text(packet.text.begin(), packet.text.end());
        (void)wkeConfirmComposition(view, text.c_str());
      } else if (wkeFireKeyPressEvent != nullptr) {
        // Older packaged kernels do not expose the composition extension.
        // Preserve committed text input while source-kernel packages use the
        // full marked-text path above.
        for (char16_t character : packet.text) {
          (void)wkeFireKeyPressEvent(view,
                                    static_cast<unsigned int>(character),
                                    packet.modifiers, false);
        }
      }
      ime_composing_ = false;
      ime_committed_ = true;
      break;
    case input_kind::ime_start:
      ime_composing_ = true;
      ime_committed_ = false;
      if (wkeGetCaretRect != nullptr && webview_window_ != nullptr) {
        const wkeRect caret = wkeGetCaretRect(view);
        ime_caret_x_.store(caret.x, std::memory_order_release);
        ime_caret_y_.store(caret.y + caret.h, std::memory_order_release);
        PostMessageW(static_cast<HWND>(webview_window_), ime_position_message,
                     static_cast<WPARAM>(caret.x),
                     static_cast<LPARAM>(caret.y + caret.h));
      }
      break;
    case input_kind::ime_update:
      if (wkeSetComposition != nullptr) {
        const std::wstring text(packet.text.begin(), packet.text.end());
        (void)wkeSetComposition(view, text.c_str(), packet.selection_start,
                                packet.selection_end);
        ime_composing_ = true;
      }
      break;
    case input_kind::ime_end:
      if (ime_composing_ && !ime_committed_ && wkeCancelComposition != nullptr)
        (void)wkeCancelComposition(view);
      ime_composing_ = false;
      ime_committed_ = false;
      break;
  }
#else
  (void)packet;
#endif
}

bool dynamic_miniblink_adapter::handle_native_window_message(
    unsigned int message, std::uintptr_t wparam_value, std::intptr_t lparam_value,
    std::intptr_t& result) {
#ifdef _WIN32
  const WPARAM wparam = static_cast<WPARAM>(wparam_value);
  const LPARAM lparam = static_cast<LPARAM>(lparam_value);
  const HWND child = static_cast<HWND>(webview_window_);
  auto route = [this](input_packet packet) {
    (void)interaction_router_.route(packet, *this, *this);
  };
  auto pointer_packet = [this, lparam](input_kind kind, WPARAM flags) {
    input_packet packet = make_input_packet(kind);
    packet.x = GET_X_LPARAM(lparam);
    packet.y = GET_Y_LPARAM(lparam);
    packet.modifiers = static_cast<std::uint32_t>(flags);
    return packet;
  };

  auto chrome_hit = [&] {
    RECT bounds{};
    GetClientRect(child, &bounds);
    return window_chrome_.hit(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), bounds.right);
  };
  const auto owner = static_cast<HWND>(owner_window_);

  switch (message) {
    case WM_SETTINGCHANGE:
      refresh_wheel_settings();
      break;
    case WM_NCHITTEST: {
      // The full-client child must not swallow SOUI's native resize border.
      const LRESULT hit = SendMessageW(owner, WM_NCHITTEST, wparam, lparam);
      if (hit >= HTLEFT && hit <= HTBOTTOMRIGHT) {
        result = HTTRANSPARENT;
        return true;
      }
      break;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
      pressed_chrome_ = chrome_action::none;
      break;
    case ime_position_message: {
      const HIMC context = ImmGetContext(child);
      if (context != nullptr) {
        COMPOSITIONFORM form{};
        form.dwStyle = CFS_POINT | CFS_FORCE_POSITION;
        form.ptCurrentPos.x = static_cast<LONG>(wparam);
        form.ptCurrentPos.y = static_cast<LONG>(lparam);
        (void)ImmSetCompositionWindow(context, &form);
        ImmReleaseContext(child, context);
      }
      result = 0;
      return true;
    }
    case WM_ERASEBKGND:
      result = 1;
      return true;
    case WM_SETFOCUS:
      route(make_input_packet(input_kind::focus));
      result = 0;
      return true;
    case WM_KILLFOCUS:
      route(make_input_packet(input_kind::blur));
      result = 0;
      return true;
    case WM_MOUSEMOVE:
      route(pointer_packet(input_kind::pointer_move, wparam));
      result = 0;
      return true;
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
      const auto hit = chrome_hit();
      if (hit == chrome_action::drag) {
        pressed_chrome_ = chrome_action::none;
        (void)soui::runtime::execute_window_command(owner,
            message == WM_LBUTTONDBLCLK ? soui::runtime::window_command::toggle_maximize
                                        : soui::runtime::window_command::begin_drag);
        result = 0;
        return true;
      }
      if (hit != chrome_action::none) {
        pressed_chrome_ = hit;
        SetCapture(child);
        result = 0;
        return true;
      }
      SetFocus(child);
      SetCapture(child);
      auto packet = pointer_packet(input_kind::pointer_down, wparam);
      packet.double_click = message == WM_LBUTTONDBLCLK;
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_LBUTTONUP: {
      if (pressed_chrome_ != chrome_action::none) {
        const auto pressed = pressed_chrome_;
        pressed_chrome_ = chrome_action::none;
        if (GetCapture() == child) ReleaseCapture();
        if (pressed == chrome_hit()) {
          if (pressed == chrome_action::close) PostMessageW(owner, WM_CLOSE, 0, 0);
          else (void)soui::runtime::execute_window_command(owner,
              pressed == chrome_action::minimize ? soui::runtime::window_command::minimize
                                                 : soui::runtime::window_command::toggle_maximize);
        }
        result = 0;
        return true;
      }
      route(pointer_packet(input_kind::pointer_up, wparam));
      if (GetCapture() == child) ReleaseCapture();
      result = 0;
      return true;
    }
    case WM_MOUSEWHEEL: {
      input_packet packet = make_input_packet(input_kind::wheel);
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(child, &point);
      packet.x = point.x;
      packet.y = point.y;
      packet.delta_y = static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam));
      packet.modifiers = static_cast<std::uint32_t>(GET_KEYSTATE_WPARAM(wparam));
      packet.gesture = 1;
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      input_packet packet = make_input_packet(input_kind::key_down);
      packet.key_code = static_cast<std::uint32_t>(wparam);
      packet.modifiers = static_cast<std::uint32_t>(HIWORD(lparam));
      packet.system_key = message == WM_SYSKEYDOWN;
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      input_packet packet = make_input_packet(input_kind::key_up);
      packet.key_code = static_cast<std::uint32_t>(wparam);
      packet.modifiers = static_cast<std::uint32_t>(HIWORD(lparam));
      packet.system_key = message == WM_SYSKEYUP;
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_CHAR: {
      input_packet packet = make_input_packet(input_kind::character);
      packet.key_code = static_cast<std::uint32_t>(wparam);
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_IME_STARTCOMPOSITION:
      route(make_input_packet(input_kind::ime_start));
      result = 0;
      return true;
    case WM_IME_COMPOSITION: {
      const HIMC context = ImmGetContext(child);
      if (context != nullptr) {
        auto copy_text = [context](DWORD selector) {
          std::u16string text;
          const LONG bytes = ImmGetCompositionStringW(context, selector, nullptr, 0);
          if (bytes > 0) {
            text.resize(static_cast<std::size_t>(bytes) / sizeof(char16_t));
            (void)ImmGetCompositionStringW(context, selector, text.data(), bytes);
          }
          return text;
        };
        if ((lparam & GCS_COMPSTR) != 0) {
          input_packet packet = make_input_packet(input_kind::ime_update);
          packet.text = copy_text(GCS_COMPSTR);
          const LONG cursor = ImmGetCompositionStringW(
              context, GCS_CURSORPOS, nullptr, 0);
          packet.selection_start = cursor >= 0 ? static_cast<int>(cursor)
                                               : static_cast<int>(packet.text.size());
          packet.selection_end = packet.selection_start;
          route(std::move(packet));
        }
        if ((lparam & GCS_RESULTSTR) != 0) {
          input_packet packet = make_input_packet(input_kind::ime_commit);
          packet.text = copy_text(GCS_RESULTSTR);
          route(std::move(packet));
        }
        ImmReleaseContext(child, context);
      }
      result = 0;
      return true;
    }
    case WM_IME_ENDCOMPOSITION:
      route(make_input_packet(input_kind::ime_end));
      result = 0;
      return true;
    case WM_IME_REQUEST:
      if (wparam == IMR_QUERYCHARPOSITION && lparam != 0) {
        auto* position = reinterpret_cast<IMECHARPOSITION*>(lparam);
        POINT caret{ime_caret_x_.load(std::memory_order_acquire),
                    ime_caret_y_.load(std::memory_order_acquire)};
        ClientToScreen(child, &caret);
        position->pt = caret;
        position->cLineHeight = 18;
        GetClientRect(child, &position->rcDocument);
        POINT document_origin{position->rcDocument.left,
                              position->rcDocument.top};
        ClientToScreen(child, &document_origin);
        OffsetRect(&position->rcDocument, document_origin.x,
                   document_origin.y);
        result = 1;
        return true;
      }
      return false;
    default:
      return false;
  }
  return false;
#else
  (void)message;
  (void)wparam_value;
  (void)lparam_value;
  (void)result;
  return false;
#endif
}

bool dynamic_miniblink_adapter::document_ready() const noexcept { return document_ready_; }

#ifdef SOUI_TEST_KIT
soui::test_kit::json dynamic_miniblink_adapter::test_kit_request(const soui::test_kit::json& request) {
  using soui::test_kit::json;
  using soui::test_kit::failed;
  const auto method = request.value("method", std::string{});
  if (method == "memory.sample") {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) return failed("MEMORY_SAMPLE");
    return {{"ok", true}, {"result", {{"privateBytes", memory.PrivateUsage}, {"workingSetBytes", memory.WorkingSetSize}, {"clock", "GetTickCount64"}, {"timestampMs", GetTickCount64()}}}};
  }
  if (method != "snapshot" && method != "dom.query" && method != "dom.activate" && method != "dom.fill") return failed("UNSUPPORTED");
  // Shared ownership prevents a timed-out callback writing into a dead stack.
  auto promise = std::make_shared<std::promise<json>>();
  auto result = promise->get_future();
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  if (!engine_thread_ || !engine_thread_->post([this, request, method, promise, cancelled] {
    if (*cancelled) return;
    try {
      if (!webview_ || !document_ready_ || !wkeGlobalExec) { promise->set_value(failed("NOT_READY")); return; }
      const auto view = static_cast<wkeWebView>(webview_);
      const auto url = wkeGetURL(view);
      if (!url || !soui::runtime::is_trusted_entry_url(url, resource_root_)) { promise->set_value(failed("UNTRUSTED_DOCUMENT")); return; }
      const auto generation = bridge_generation_.load(std::memory_order_acquire);
      if (method == "dom.activate" || method == "dom.fill") {
        const auto& params = request["params"];
        if (!params.contains("generation") || !params["generation"].is_number_unsigned() || params["generation"].get<std::uint64_t>() != generation) {
          promise->set_value(failed("STALE_DOCUMENT")); return;
        }
      }
      const auto es = wkeGlobalExec(view);
      if (!es) { promise->set_value(failed("NOT_READY")); return; }
      std::string script;
      if (method == "snapshot") {
        script = "return JSON.stringify({ok:true,result:{title:document.title,readyState:document.readyState,text:(document.body?document.body.textContent:'').slice(0,8192)}});";
      } else {
        // Only the fixed agent is evaluated; selectors and values are JSON data.
        json command = {{"method", method}, {"params", request["params"]}};
        script = "return JSON.stringify(" + std::string(soui::test_kit::dom_script) + "(" + command.dump(-1, ' ', true) + "));";
      }
      const auto value = jsEval(es, script.c_str());
      const auto raw = jsToTempString(es, value);
      auto response = raw ? json::parse(raw, nullptr, false) : json();
      if (!response.is_object() || !response.contains("ok")) { promise->set_value(failed("DOM_RESPONSE")); return; }
      if (response.value("ok", false)) {
        response["result"]["generation"] = generation;
        if (method == "snapshot") {
          response["result"]["documentReady"] = document_ready_.load();
          response["result"]["applicationReady"] = application_ready_.load();
          response["result"]["architecture"] = sizeof(void*) == 8 ? "x64" : "x86";
          response["result"]["kernelVersion"] = wkeGetVersionString ? wkeGetVersionString() : "unknown";
          response["result"]["domAgentSha256"] = soui::test_kit::dom_script_sha256;
        }
      }
      promise->set_value(std::move(response));
    } catch (...) { promise->set_value(failed("ENGINE_ERROR")); }
  })) return failed("ENGINE_STOPPED");
  if (result.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
    *cancelled = true;
    return failed("ENGINE_TIMEOUT");
  }
  return result.get();
}
#endif

bool dynamic_miniblink_adapter::application_ready() {
#if defined(SOUI_BRIDGE_BENCHMARK) || defined(SOUI_PERFORMANCE_DIAGNOSTICS)
  // Isolated benchmark and diagnostic pages may intentionally have no native
  // bridge dependency. Their renderer-side marker keeps the bounded smoke
  // lifecycle usable without changing ordinary product readiness semantics.
  if (document_ready_ && !application_ready_ && engine_thread_ != nullptr) {
    (void)engine_thread_->invoke([this] {
      if (webview_ == nullptr || wkeGlobalExec == nullptr) return;
      const auto es = wkeGlobalExec(static_cast<wkeWebView>(webview_));
      if (es == nullptr) return;
      const auto value = jsEval(es, "return window.__souiBenchmarkReady === true ? 'ready' : ''; ");
      const auto marker = jsToString(es, value);
      if (marker != nullptr && std::string_view{marker} == "ready") {
        application_ready_ = true;
        std::lock_guard<std::mutex> lock(state_mutex_);
        probe_report_ = "benchmark=ready(no-native-service)";
      }
    });
  }
#endif
  return document_ready_ && application_ready_;
}

std::string dynamic_miniblink_adapter::application_diagnostics() {
  std::string diagnostics = std::string{"document="} + (document_ready_ ? "ready" : "waiting") +
         ",bridge=" + (application_ready_ ? "ready" : "waiting") +
         ",draw_interval_ms=" + std::to_string(configured_draw_min_interval_.load());
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!probe_report_.empty()) diagnostics += "," + probe_report_;
  }
  char script_diagnostics[2] = {};
  if (engine_thread_ != nullptr &&
      GetEnvironmentVariableA("SOUI_DIAGNOSTIC_SCRIPT", script_diagnostics,
                              sizeof(script_diagnostics)) == 1 &&
      script_diagnostics[0] == '1') {
    const auto script_result = std::make_shared<std::string>();
    const bool collected = engine_thread_->invoke([this, script_result] {
      if (webview_ == nullptr || wkeGlobalExec == nullptr) return;
      const auto es = wkeGlobalExec(static_cast<wkeWebView>(webview_));
      if (es == nullptr) return;
      const auto value = jsEval(es,
          "return JSON.stringify({title:document.title,body:(document.body&&document.body.innerHTML||'').length,"
          "root:(document.getElementById('react-app')||document.getElementById('app'))&&"
          "(document.getElementById('react-app')||document.getElementById('app')).innerHTML.slice(0,180),"
          "bootstrap:window.__souiBootstrapError||'',react:window.__souiReactBootstrapError||'',"
          "render:window.__souiReactRenderError||'',"
          "ready:document.documentElement.getAttribute('data-soui-react-ready')||''});");
      const auto raw = jsToTempString(es, value);
      if (raw != nullptr) *script_result = raw;
    });
    if (collected && !script_result->empty()) diagnostics += ",script=" + *script_result;
  }
  return diagnostics;
}

void dynamic_miniblink_adapter::set_session_id(std::string session_id) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  // A fast page may have bootstrapped before the UI thread finishes start().
  if (session_id_.empty()) session_id_ = std::move(session_id);
}

#ifdef SOUI_BRIDGE_BENCHMARK
std::string dynamic_miniblink_adapter::benchmark_result() {
  if (engine_thread_ == nullptr) return {};
  return benchmark_result_slot_.poll(engine_thread_,
      [this] { return collect_benchmark_result_on_engine(); });
}

std::string dynamic_miniblink_adapter::collect_benchmark_result_on_engine() {
  if (engine_thread_ == nullptr || !engine_thread_->is_owner_thread()) return {};
  if (!webview_) return {};
  const auto es = wkeGlobalExec(static_cast<wkeWebView>(webview_));
  const auto value = jsEval(es, "return window.__souiPerformanceResult ? JSON.stringify(window.__souiPerformanceResult) : ''; ");
  const auto text = jsToString(es, value);
  if (!text || !*text) return {};
  auto report = nlohmann::json::parse(text);
  {
    std::lock_guard<std::mutex> lock(paint_samples_mutex);
    report["nativePaintQpcMs"] = paint_samples;
    report["managedFrameTiming"] = managed_frame_timing_;
    report["hostManagedPump"] = host_managed_pump_;
    report["dirtyGatedRepaint"] = host_managed_pump_;
    report["otherThreadPaintExperiment"] = experimental_other_thread_paint_;
    report["compositorRateExperiment"] = experimental_compositor_rate_;
    report["configuredWakeInterval"] = configured_wake_interval_.load();
    report["configuredDrawMinInterval"] = configured_draw_min_interval_.load();
    report["nativePixelChangeQpcMs"] = paint_pixel_changes;
    report["pixelSampleFailures"] = paint_pixel_failures;
    report["pixelSampleScope"] = "RGB hash changes on full-width memory bitmap scanline y=220; not screen presentation";
    if (first_vue_ready_qpc_ms > 0 && first_content_paint_qpc_ms == 0) return {};
    report["vueReadyQpcMs"] = first_vue_ready_qpc_ms;
    report["firstPaintAfterVueQpcMs"] = first_content_paint_qpc_ms;
    report["nativePaintPhases"] = paint_phases;
    report["nativePaintScope"] = "Miniblink paint-updated callback timestamps; not GPU presentation timestamps";
  }
  return report.dump();
}
void dynamic_miniblink_adapter::benchmark_motion(unsigned index) {
  if (!owner_window_ || !webview_) return;
  const auto window = static_cast<HWND>(owner_window_);
  if (!benchmark_width_) {
    RECT bounds{}; GetWindowRect(window, &bounds);
    benchmark_x_ = bounds.left; benchmark_y_ = bounds.top;
    benchmark_width_ = bounds.right - bounds.left; benchmark_height_ = bounds.bottom - bounds.top;
  }
  if (index < 40) wkeSetZoomFactor(static_cast<wkeWebView>(webview_), 1.0F + (index % 20) * 0.01F);
  else if (index < 80) SetWindowPos(window, nullptr, 0, 0, benchmark_width_ + (index % 10) * 2,
                                 benchmark_height_ + (index % 10), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  else if (index < 120) SetWindowPos(window, nullptr, benchmark_x_ + (index % 20) * 2,
                                    benchmark_y_ + (index % 20), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  else {
    wkeSetZoomFactor(static_cast<wkeWebView>(webview_), 1.0F);
    SetWindowPos(window, nullptr, benchmark_x_, benchmark_y_, benchmark_width_, benchmark_height_, SWP_NOZORDER | SWP_NOACTIVATE);
  }
}
double dynamic_miniblink_adapter::benchmark_click(int x, int y) {
  if (!webview_window_ || x < 0 || y < 0 || x > 8191 || y > 8191) return 0;
  LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
  const auto position = MAKELPARAM(x, y);
  PostMessageW(static_cast<HWND>(webview_window_), WM_LBUTTONDOWN, MK_LBUTTON, position);
  PostMessageW(static_cast<HWND>(webview_window_), WM_LBUTTONUP, 0, position);
  return 1000.0 * now.QuadPart / frequency.QuadPart;
}
void dynamic_miniblink_adapter::benchmark_text() {
  if (!webview_window_) return;
  for (const WPARAM character : {WPARAM{0x4e2d}, WPARAM{0x6587}, WPARAM{0x41}})
    PostMessageW(static_cast<HWND>(webview_window_), WM_CHAR, character, 1);
}
std::string soui_miniblink_host::benchmark_result() {
  auto* adapter = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get());
  return adapter ? adapter->benchmark_result() : std::string{};
}
int soui_miniblink_host::configured_wake_interval() const noexcept {
  const auto* adapter = dynamic_cast<const dynamic_miniblink_adapter*>(adapter_.get());
  return adapter ? adapter->configured_wake_interval() : -1;
}
void soui_miniblink_host::benchmark_gallery(bool all_components) {
  if (auto* adapter=dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get())) adapter->benchmark_gallery(all_components);
}
void dynamic_miniblink_adapter::benchmark_gallery(bool all_components) {
  if (engine_thread_ != nullptr && !engine_thread_->is_owner_thread()) {
    (void)engine_thread_->post(
        [this, all_components] { benchmark_gallery(all_components); });
    return;
  }
  if (!webview_ || !wkeRunJS) return;
  const std::string source=std::string("window.__galleryAll=")+(all_components?"true;":"false;")+R"JS(
  (function(){
    var rows=[], started=performance.now(), deadline=started+15000;
    var mark=window.__souiBenchmarkMarkPhase;
    function finish(){window.__souiPerformanceResult={status:rows.some(function(r){return r.state==='failed';})?'failed':'measured',gallery:rows,scope:'DOM-triggered selection to component DOM readiness; not physical input or screen presentation'};}
    function wait(name,index,start){
      var area=document.querySelector('.gallery-example');
      var error=document.querySelector('.gallery-error');
      var active=document.querySelector('.gallery-list button.active small');
      if(active && active.textContent===name && error){
        rows.push({name:name,state:error.textContent.indexOf('隔离')>=0?'isolated':'failed',error:error.textContent,ms:performance.now()-start});next(index+1);return;
      }
      if(active && active.textContent===name && area && area.getAttribute('aria-busy')!=='true' && area.children.length){
        var bounds=area.getBoundingClientRect();
        rows.push({name:name,state:'rendered',ms:performance.now()-start,elements:area.querySelectorAll('*').length,width:bounds.width,height:bounds.height});
        if(window.__galleryAll && window.__souiBenchmarkGalleryCheckpoint)window.__souiBenchmarkGalleryCheckpoint(name,true);
        next(index+1);return;
      }
      if(performance.now()>deadline){rows.push({name:name,state:'failed',error:'Readiness deadline'});finish();return;}
      setTimeout(function(){wait(name,index,start);},5);
    }
    function next(index){
      if(!window.__galleryAll){finish();return;}
      setTimeout(function(){selectNext(index);},350);
    }
    function selectNext(index){
      var buttons=document.querySelectorAll('.gallery-list button');
      if(index>=buttons.length){finish();return;}
      var name=buttons[index].querySelector('small').textContent;
      if(window.__souiBenchmarkGalleryCheckpoint)window.__souiBenchmarkGalleryCheckpoint(name,false);
      deadline=performance.now()+5000;
      var start=performance.now();buttons[index].click();
      setTimeout(function(){wait(name,index,start);},10);
    }
    var buttons=document.querySelectorAll('.nav-item');
    for(var i=0;i<buttons.length;i++)if(buttons[i].textContent.indexOf('全部组件')>=0){
      if(mark)mark('input');
      buttons[i].click();wait('Button',0,started);return;
    }
    rows.push({state:'failed',error:'Gallery navigation missing'});finish();
  })();
  )JS";
  wkeRunJS(static_cast<wkeWebView>(webview_),source.c_str());
}
#endif

void dynamic_miniblink_adapter::notify_document_ready() { on_document_ready(); }

bool dynamic_miniblink_adapter::allow_document_navigation(std::string_view url) const noexcept {
#ifdef SOUI_SOURCE_KERNEL_HOST
  try {
    std::ofstream trace(resource_root_.parent_path() / "source-navigation.log", std::ios::app);
    trace << "allowed=" << soui::runtime::is_trusted_entry_url(url, resource_root_) << " url=" << std::string(url) << '\n';
  } catch (...) { }
#endif
  return soui::runtime::is_trusted_entry_url(url, resource_root_);
}

void dynamic_miniblink_adapter::notify_main_context_created() {
  window_chrome_.clear();
  bridge_generation_.fetch_add(1, std::memory_order_acq_rel);
  std::string closing_session;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    closing_session.swap(session_id_);
  }
  if (bridge_adapter_ != nullptr && !closing_session.empty())
    bridge_shutdown_session(*static_cast<bridge_adapter*>(bridge_adapter_), closing_session, 0);
  document_ready_ = false;
  application_ready_ = false;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    probe_report_.clear();
  }
}

std::string dynamic_miniblink_adapter::handle_message(std::string_view request,
                                                      bool native_user_gesture) {
  if (bridge_adapter_ == nullptr) {
    return R"({"ok":false,"error":{"code":"E_BRIDGE_UNAVAILABLE","message":"The native bridge is not connected."}})";
  }
  const std::string method = bridge_request_method(request);
  auto response = static_cast<bridge_adapter*>(bridge_adapter_)->handle_json(request, native_user_gesture);
  if (method == "bridge.bootstrap" &&
      response.find("\"ok\":true") != std::string::npos) {
    const std::regex session_pattern(R"session("sessionId"\s*:\s*"([^"]+)")session");
    std::smatch match;
    if (std::regex_search(response, match, session_pattern) && match.size() > 1U) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        session_id_ = match[1].str();
      }
#ifdef SOUI_BRIDGE_BENCHMARK
      if (!benchmark_dataset_.empty()) {
        auto packet = nlohmann::json::parse(response);
        const auto& result = packet.at("result");
        const soui::runtime::scan_request_context context{result.at("sessionId").get<std::string>(),
          result.at("origin").get<std::string>(), result.at("token").get<std::string>()};
        const auto benchmark_root_token = static_cast<bridge_adapter*>(bridge_adapter_)->register_root(context, benchmark_dataset_);
        packet["result"]["benchmarkRootToken"] = benchmark_root_token;
        response = packet.dump();
      }
#endif
    }
  }
  if (method == "bridge.probe.report" &&
      response.find("\"ok\":true") != std::string::npos) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      probe_report_ = "probe=script/json/event/error/local/proxy/promise/symbol/reflect/vue/reactivity/dom/bridge";
    }
    application_ready_ = true;
#ifdef SOUI_BRIDGE_BENCHMARK
    std::lock_guard<std::mutex> lock(paint_samples_mutex);
    if (first_vue_ready_qpc_ms == 0) {
      LARGE_INTEGER now{}, frequency{};
      QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
      first_vue_ready_qpc_ms = 1000.0 * now.QuadPart / frequency.QuadPart;
    }
#endif
  }
  return response;
}

bool dynamic_miniblink_adapter::enqueue_bridge_message(
    std::string request, bool native_user_gesture) {
  return try_enqueue_bridge_message(std::move(request), native_user_gesture).ok;
}

host_result dynamic_miniblink_adapter::try_enqueue_bridge_message(
    std::string request, bool native_user_gesture) {
  if (!bridge_accepting_.load(std::memory_order_acquire))
    return failure("E_DISPOSED", "The Host is no longer accepting native requests.");
  if (bridge_thread_ == nullptr || engine_thread_ == nullptr || !bridge_budget_)
    return failure("E_BRIDGE_QUEUE", "The asynchronous native bridge queue is unavailable.");
  if (request.empty() || request.size() > max_bridge_message_bytes)
    return failure("E_LIMIT_MESSAGE_SIZE", "Bridge message exceeds 64 KiB.");
  const auto packet = bounded_bridge_packet(request);
  const auto request_id = packet_string(packet, "requestId");
  const auto method = packet_string(packet, "method");
  if (request_id.empty() || method.empty())
    return failure("E_INVALID_PARAMS", "Expected bounded requestId, method and JSON envelope.");
  const auto timeout = bridge_wait_timeout(packet);
  if (!timeout) return failure("E_INVALID_PARAMS", "timeoutMs must be between 1 and 60000.");
  const std::uint64_t generation =
      bridge_generation_.load(std::memory_order_acquire);
  detail::bridge_request_budget::rejection rejected;
  const auto lease = bridge_budget_->acquire(request_id, generation,
      method == "task.unsubscribe" || method == "task.cancel", rejected);
  if (!lease) return rejected == detail::bridge_request_budget::rejection::duplicate
      ? failure("E_DUPLICATE_REQUEST", "This document already has an unfinished native request with this identity.")
      : failure("E_BUSY", "The native bridge request capacity is exhausted.");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
  bridge_diagnostic_trace(this, "queue.admitted", request_id, method);
  const bool queued = bridge_thread_->post(
      [this, request = std::move(request), request_id,
       native_user_gesture, generation, deadline, lease] {
        bridge_diagnostic_trace(this, "worker.begin", request_id);
        if (generation != bridge_generation_.load(std::memory_order_acquire)) {
          bridge_diagnostic_trace(this, "worker.drop-generation", request_id);
          return;
        }
        std::string response;
        if (std::chrono::steady_clock::now() >= deadline) {
          response = bridge_error(request_id, "E_TIMEOUT", "The request expired in the native queue before execution.");
        } else try {
          response = handle_message(request, native_user_gesture);
          if (response.size() > max_bridge_message_bytes)
            response = bridge_error(request_id, "E_LIMIT_MESSAGE_SIZE", "Native response exceeds 64 KiB.");
        } catch (...) {
          response = bridge_error(request_id, "E_NATIVE_EXCEPTION", "The native handler failed.");
        }
        bridge_diagnostic_trace(this, "worker.response", request_id,
            "bytes=" + std::to_string(response.size()));
        if (generation != bridge_generation_.load(std::memory_order_acquire)
            || engine_thread_ == nullptr) {
          bridge_diagnostic_trace(this, "worker.drop-before-engine", request_id);
          return;
        }
        const bool posted = engine_thread_->post(
            [this, request_id, response = std::move(response), generation, lease] {
              bridge_diagnostic_trace(this, "engine.resolve-begin", request_id);
              // Completing a JS Promise may reenter admission before RunJS
              // returns. Return its capacity before notifying that caller.
              lease->release();
              resolve_bridge_response(request_id, response, generation);
              bridge_diagnostic_trace(this, "engine.resolve-end", request_id);
            });
        bridge_diagnostic_trace(this, posted ? "worker.engine-posted" : "worker.engine-post-failed",
            request_id);
      });
  bridge_diagnostic_trace(this, queued ? "queue.posted" : "queue.post-failed", request_id);
  return queued ? success("Native request admitted.")
      : failure("E_BRIDGE_QUEUE", "The asynchronous native bridge queue is stopping.");
}

void dynamic_miniblink_adapter::resolve_bridge_response(
    std::string request_id, std::string response, std::uint64_t generation) {
#ifdef _WIN32
  if (generation != bridge_generation_.load(std::memory_order_acquire)
      || webview_ == nullptr || wkeRunJS == nullptr) {
    bridge_diagnostic_trace(this, "resolver.drop", request_id);
    return;
  }
  const auto script = bridge_resolution_script(request_id, response);
  const auto result = wkeRunJS(static_cast<wkeWebView>(webview_), script.c_str());
  bridge_diagnostic_trace(this, "resolver.ran", request_id,
      "result=" + std::to_string(static_cast<long long>(result)));
#else
  (void)request_id;
  (void)response;
  (void)generation;
#endif
}

bool dynamic_miniblink_adapter::processing_user_gesture() const noexcept {
#ifdef _WIN32
  return webview_ != nullptr && wkeIsProcessingUserGesture != nullptr &&
         wkeIsProcessingUserGesture(static_cast<wkeWebView>(webview_)) != FALSE;
#else
  return false;
#endif
}

bool dynamic_miniblink_adapter::owns_execution_context(void* execution_state) const noexcept {
#ifdef _WIN32
  if (execution_state == nullptr || webview_ == nullptr || jsGetWebView == nullptr) {
    return false;
  }
  const auto state = static_cast<jsExecState>(execution_state);
  const auto view = static_cast<wkeWebView>(webview_);
  return jsGetWebView(state) == view;
#else
  (void)execution_state;
  return false;
#endif
}

void dynamic_miniblink_adapter::on_document_ready() {
#ifdef _WIN32
  document_ready_ = true;
  install_js_bindings();
  dispatch_event(R"({"kind":"event","event":"host.ready","data":{"localResources":true}})");
#endif
}

bool dynamic_miniblink_adapter::update_window_chrome(void* state, std::string_view raw) {
#ifdef _WIN32
  // Native decoration only; no alternative privileged business Bridge.
  // Miniblink creates a fresh temporary exec-state wrapper on each call to
  // wkeGlobalExec; pointer equality does not identify a JavaScript context.
  if (!owns_execution_context(state) ||
      !jsToBoolean(static_cast<jsExecState>(state), jsEval(static_cast<jsExecState>(state), "return window === window.top;")) ||
      raw.size() > 8192 || !soui::runtime::is_trusted_entry_url(wkeGetURL(static_cast<wkeWebView>(webview_)), resource_root_))
    return false;
  try {
    const auto packet = nlohmann::json::parse(raw, nullptr, false);
    if (!packet.is_object() || !packet.contains("width") || !packet["width"].is_number_integer() ||
        packet["width"].get<double>() <= 0 || packet["width"].get<double>() > 32768 ||
        !packet.contains("regions") || !packet["regions"].is_array() ||
        packet["regions"].size() > window_chrome::capacity) return false;
    std::array<chrome_region, window_chrome::capacity> regions{};
    std::size_t count = 0;
    for (const auto& item : packet["regions"]) {
      if (!item.is_array() || item.size() != 5 || !item[0].is_string()) return false;
      for (int i = 1; i < 5; ++i)
        if (!item[i].is_number_integer() || item[i].get<double>() < 0 ||
            item[i].get<double>() > 32768) return false;
      const auto name = item[0].get<std::string>();
      chrome_action action = chrome_action::none;
      if (name == "drag") action = chrome_action::drag;
      else if (name == "minimize") action = chrome_action::minimize;
      else if (name == "maximize") action = chrome_action::maximize;
      else if (name == "close") action = chrome_action::close;
      regions[count++] = {action, item[1].get<int>(), item[2].get<int>(), item[3].get<int>(), item[4].get<int>()};
    }
    return window_chrome_.publish(packet["width"].get<int>(), regions, count);
  } catch (...) { return false; }
#else
  (void)state;
  (void)raw;
  return false;
#endif
}

void dynamic_miniblink_adapter::install_js_bindings() {
#ifdef _WIN32
  if (webview_ == nullptr || wkeRunJS == nullptr) return;
  static constexpr const char* bootstrap =
      "(function(){"
      "var n=window.SouiNative=window.SouiNative||{};"
      "n.postMessage=window.__souiNativePostMessage;"
      "n.onMessage=n.onMessage||function(e){if(n._listeners){n._listeners.slice().forEach(function(f){f(e);});}};"
      "n.onEvent=n.onEvent||function(f){n._listeners=n._listeners||[];n._listeners.push(f);return function(){"
      "var i=n._listeners.indexOf(f);if(i>=0)n._listeners.splice(i,1);};};"
      "n.ready=n.ready||function(){};"
      "})();";
  (void)wkeRunJS(static_cast<wkeWebView>(webview_), bootstrap);
  (void)wkeRunJS(static_cast<wkeWebView>(webview_), window_chrome_script);
  char initial_page[32] = {};
  const DWORD initial_page_length = GetEnvironmentVariableA("SOUI_INITIAL_PAGE", initial_page,
                                                            sizeof(initial_page));
  if (initial_page_length > 0 && initial_page_length < sizeof(initial_page)) {
    const auto page_literal = json_string_literal(std::string{initial_page, initial_page_length});
    const auto page_script = "window.__souiInitialPage=" + page_literal + ";";
    (void)wkeRunJS(static_cast<wkeWebView>(webview_), page_script.c_str());
  }
  char react_mode[2] = {};
  if (GetEnvironmentVariableA("SOUI_REACT_MODE", react_mode, sizeof(react_mode)) == 1 && react_mode[0] == '1')
    (void)wkeRunJS(static_cast<wkeWebView>(webview_), "window.__souiReactMode=true;");
#endif
}

void dynamic_miniblink_adapter::dispatch_event(const std::string& event_json) {
#ifdef _WIN32
  if (!document_ready_ || engine_thread_ == nullptr) return;
  const auto lifetime = event_lifetime_;
  const auto generation = bridge_generation_.load(std::memory_order_acquire);
  (void)engine_thread_->post([lifetime, generation, event_json] {
    dynamic_miniblink_adapter* adapter = nullptr;
    {
      std::lock_guard<std::mutex> lock(lifetime->mutex);
      adapter = lifetime->adapter;
    }
    // Teardown itself is serialized on this same owner thread. Drop events
    // queued by a detached sink or an obsolete document without touching it.
    if (adapter && generation == adapter->bridge_generation_.load(std::memory_order_acquire))
      adapter->dispatch_event_now(event_json);
  });
#else
  (void)event_json;
#endif
}

void dynamic_miniblink_adapter::dispatch_event_now(const std::string& event_json) {
#ifdef _WIN32
  if (!document_ready_ || webview_ == nullptr || wkeRunJS == nullptr) return;
  const auto literal = json_string_literal(event_json);
  const auto script = "(function(){var e=JSON.parse(" + literal + ");"
                      "if(window.__souiNativeDispatchEvent){window.__souiNativeDispatchEvent(e);}"
                      "else if(window.SouiNative&&window.SouiNative.onMessage){window.SouiNative.onMessage(e);}})();";
  (void)wkeRunJS(static_cast<wkeWebView>(webview_), script.c_str());
#else
  (void)event_json;
#endif
}

void dynamic_miniblink_adapter::deliver_posted_event(void* payload) {
#ifdef _WIN32
  std::unique_ptr<pending_event> event(static_cast<pending_event*>(payload));
  if (event == nullptr || event->adapter != this) return;
  dispatch_event(event->json);
#else
  (void)payload;
#endif
}

#ifdef _WIN32
bool WKE_CALL_TYPE document_navigation_callback(wkeWebView, void* param, wkeNavigationType, wkeString url) {
  if (!param || !url || !wkeGetString) return false;
  const auto value = wkeGetString(url);
  return value && static_cast<dynamic_miniblink_adapter*>(param)->allow_document_navigation(value);
}

void WKE_CALL_TYPE script_context_created_callback(wkeWebView view, void* param, wkeWebFrameHandle frame, void*, int, int world) {
  if (param && world == 0 && wkeIsMainFrame(view, frame)) {
    char react_mode[2] = {};
    if (GetEnvironmentVariableA("SOUI_REACT_MODE", react_mode, sizeof(react_mode)) == 1 &&
        react_mode[0] == '1' && wkeRunJS != nullptr)
      (void)wkeRunJS(view, "window.__souiReactMode=true;");
    // Publish the requested route before the bundled module evaluates.  The
    // document-ready callback remains a fallback for older Miniblink builds,
    // but setting it here removes a race where React mounted the home page
    // before the initial route was visible.
    char initial_page[32] = {};
    const DWORD initial_page_length = GetEnvironmentVariableA("SOUI_INITIAL_PAGE", initial_page,
                                                               sizeof(initial_page));
    if (initial_page_length > 0 && initial_page_length < sizeof(initial_page) && wkeRunJS != nullptr) {
      const auto page_literal = json_string_literal(std::string{initial_page, initial_page_length});
      const auto page_script = "window.__souiInitialPage=" + page_literal + ";";
      (void)wkeRunJS(view, page_script.c_str());
    }
    static_cast<dynamic_miniblink_adapter*>(param)->notify_main_context_created();
  }
}

void WKE_CALL_TYPE document_ready_callback(wkeWebView, void* param) {
  if (param != nullptr) static_cast<dynamic_miniblink_adapter*>(param)->notify_document_ready();
}

jsValue WKE_CALL_TYPE native_window_chrome(jsExecState es, void*) {
  auto* adapter = adapter_for_context(es);
  if (!adapter || !es || jsArgCount(es) != 1 || !jsIsString(jsArg(es, 0))) return jsBoolean(false);
  const char* raw = jsToTempString(es, jsArg(es, 0));
  return jsBoolean(raw && adapter->update_window_chrome(es, raw));
}

jsValue WKE_CALL_TYPE native_post_message(jsExecState es, void*) {
  auto* adapter = adapter_for_context(es);
  if (adapter == nullptr || es == nullptr || jsArgCount(es) != 1 || !jsIsString(jsArg(es, 0))) return jsUndefined();
  const auto raw = jsToTempString(es, jsArg(es, 0));
  if (raw == nullptr) return jsUndefined();
  if (!adapter->owns_execution_context(es)) {
    return jsString(es, R"({"requestId":"","ok":false,"error":{"code":"E_UNAUTHORIZED","message":"Bridge calls are restricted to the trusted WebView context."}})");
  }
  std::string request_id;
  try {
    const std::string_view raw_request(raw);
    if (raw_request.empty() || raw_request.size() > max_bridge_message_bytes) {
      const auto response = bridge_error({}, "E_LIMIT_MESSAGE_SIZE", "Bridge message exceeds 64 KiB.");
      return jsString(es, response.c_str());
    }
    const std::string request(raw_request); // jsEval can invalidate Miniblink's temporary string.
    const auto packet = bounded_bridge_packet(request);
    request_id = packet_string(packet, "requestId");
    const auto method = packet_string(packet, "method");
    if (request_id.empty() || method.empty()) {
      const auto response = bridge_error(request_id, "E_INVALID_PARAMS", "Expected bounded requestId, method and JSON envelope.");
      return jsString(es, response.c_str());
    }
    if (method == "bridge.bootstrap") {
      bridge_diagnostic_trace(adapter, "binding.bootstrap", request_id);
      const std::string response = adapter->handle_message(request, adapter->processing_user_gesture());
      return jsString(es, response.c_str());
    }
    const auto timeout = bridge_wait_timeout(packet);
    if (!timeout) {
      const auto response = bridge_error(request_id, "E_INVALID_PARAMS", "timeoutMs must be between 1 and 60000.");
      return jsString(es, response.c_str());
    }
    // Reserve the document resolver before admitting any side effects. Both
    // creation and response delivery run on this same engine owner.
    const auto create = std::string{"return "} + detail::create_bridge_pending_script
        + "(" + json_string_literal(request_id) + "," + std::to_string(timeout) + ");";
    const auto waiting = jsEval(es, create.c_str());
    const bool waiting_is_string = jsIsString(waiting);
    bridge_diagnostic_trace(adapter, "binding.resolver-created", request_id,
        "value=" + std::to_string(static_cast<long long>(waiting)) +
        " string=" + (waiting_is_string ? std::string{"1"} : std::string{"0"}));
    if (waiting_is_string) return waiting;
    host_result admitted;
    try { admitted = adapter->try_enqueue_bridge_message(std::string(request), adapter->processing_user_gesture()); }
    catch (...) { admitted = failure("E_NATIVE_EXCEPTION", "The native bridge could not admit this request."); }
    bridge_diagnostic_trace(adapter, admitted.ok ? "binding.admitted" : "binding.rejected",
        request_id, admitted.code);
    if (!admitted.ok) {
      const auto response = bridge_error(request_id, admitted.code.c_str(), admitted.message.c_str());
      const auto resolve = "return " + bridge_resolution_script(request_id, response) + ";";
      return jsEval(es, resolve.c_str());
    }
    return waiting;
  } catch (...) {
    const auto response = bridge_error(request_id, "E_NATIVE_EXCEPTION", "The native bridge could not read this request.");
    return jsString(es, response.c_str());
  }
}

#endif

soui_miniblink_host::soui_miniblink_host(std::unique_ptr<webview_host_adapter> adapter)
    : adapter_(std::move(adapter)) {}

soui_miniblink_host::~soui_miniblink_host() {
  if (session_ != nullptr && !session_->terminal()) (void)destroy();
}

host_result soui_miniblink_host::start(const host_config& config) {
  if (session_ != nullptr && !session_->terminal()) return failure("E_ALREADY_STARTED", "Host is already running.");
  if (!adapter_) return failure("E_HOST_ADAPTER_MISSING", "A SOUI/Miniblink host adapter is required.");
  const auto architecture = validate_architecture(config);
  if (!architecture.ok) return architecture;
  const auto resources = validate_local_resources(config.resource_root);
  if (!resources.ok) return resources;
  const auto mounted = adapter_->mount(config);
  if (!mounted.ok) return mounted;
  session_ = std::make_unique<soui::runtime::webview_session>();
  if (auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get()); dynamic != nullptr) {
    dynamic->set_session_id(session_->session_id());
  }
  (void)session_->mark_ready();
  return success("SOUI SHostWnd and Miniblink child WebView are ready with local resources.");
}

host_result soui_miniblink_host::hide() {
  if (session_ == nullptr) return failure("E_NOT_STARTED", "Host has not started.");
  const auto event = session_->hide();
  if (!event.has_value()) return failure("E_INVALID_LIFECYCLE", "WebView cannot be hidden in its current state.");
  if (auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get()); dynamic != nullptr) {
    return dynamic->show(false);
  }
  return success("WebView hidden without cancelling tasks.");
}

host_result soui_miniblink_host::reload() {
  if (session_ == nullptr) return failure("E_NOT_STARTED", "Host has not started.");
  if (auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get()); dynamic != nullptr) {
    const auto result = dynamic->reload();
    if (!result.ok) return result;
  }
  return session_->reload().has_value() ? success("WebView reload requested.")
                                        : failure("E_INVALID_LIFECYCLE", "WebView cannot reload in its current state.");
}

host_result soui_miniblink_host::close() {
  if (session_ == nullptr) return failure("E_NOT_STARTED", "Host has not started.");
  const auto event = session_->close();
  if (!event.has_value()) return failure("E_INVALID_LIFECYCLE", "WebView cannot close in its current state.");
  return adapter_->unmount();
}

host_result soui_miniblink_host::destroy() {
  if (session_ == nullptr) return success("WebView was not started.");
  const auto event = session_->destroy();
  if (!event.has_value()) return success("WebView was already destroyed.");
  return adapter_->unmount();
}

host_result soui_miniblink_host::resize(int width, int height) {
  if (auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get()); dynamic != nullptr) {
    return dynamic->resize(width, height);
  }
  return success("Host resize recorded by the test adapter.");
}

host_result soui_miniblink_host::focus() {
  if (auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get()); dynamic != nullptr) {
    return dynamic->focus();
  }
  return success("Host focus recorded by the test adapter.");
}

void soui_miniblink_host::pump() {
  if (auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get()); dynamic != nullptr) dynamic->pump();
}

void soui_miniblink_host::deliver_posted_event(void* payload) {
  if (auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get()); dynamic != nullptr) {
    dynamic->deliver_posted_event(payload);
  }
}

bool soui_miniblink_host::document_ready() const noexcept {
  const auto* dynamic = dynamic_cast<const dynamic_miniblink_adapter*>(adapter_.get());
  return dynamic != nullptr && dynamic->document_ready();
}

bool soui_miniblink_host::application_ready() {
  auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get());
  return dynamic != nullptr && dynamic->application_ready();
}

std::string soui_miniblink_host::application_diagnostics() {
  auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get());
  return dynamic == nullptr ? "test-adapter" : dynamic->application_diagnostics();
}

#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
host_result soui_miniblink_host::request_memory_recovery() {
  auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get());
  return dynamic == nullptr
      ? failure("E_HOST_ADAPTER_MISSING", "The dynamic Miniblink adapter is unavailable.")
      : dynamic->request_memory_recovery();
}
#endif

const soui::runtime::webview_session* soui_miniblink_host::session() const noexcept { return session_.get(); }

host_result soui_miniblink_host::validate_local_resources(const std::filesystem::path& root) {
  std::error_code error;
  if (root.empty() || !std::filesystem::is_directory(root, error) || error) {
    return failure("E_RESOURCE_MISSING", "Local Web resource root does not exist.");
  }
  const auto index = root / "index.html";
  if (!std::filesystem::is_regular_file(index, error) || error) {
    return failure("E_RESOURCE_MISSING", "Local Web resource root must contain index.html.");
  }
  for (const auto& file : std::filesystem::recursive_directory_iterator(root, error)) {
    if (error) break;
    if (!file.is_regular_file(error)) continue;
    std::ifstream input(file.path(), std::ios::binary);
    if (!input) return failure("E_RESOURCE_READ", "A local Web resource could not be read.");
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (contains_untrusted_frame_surface(file.path(), content)) {
      return failure("E_UNTRUSTED_FRAME", "Embedded frame surfaces are forbidden in local Web resources.");
    }
    if (contains_remote_runtime_reference(file.path(), content)) {
      return failure("E_REMOTE_RESOURCE", "Remote Web runtime URLs are forbidden.");
    }
  }
  return success();
}

bool binary_matches_architecture(const std::filesystem::path& binary,
                                 std::string_view architecture) {
  if (architecture != "x86" && architecture != "x64") return false;
  std::ifstream input(binary, std::ios::binary);
  if (!input) return false;

  std::uint16_t dos_signature = 0;
  input.read(reinterpret_cast<char*>(&dos_signature), sizeof(dos_signature));
  if (!input || dos_signature != 0x5A4DU) return false;
  input.seekg(0x3C, std::ios::beg);
  std::uint32_t pe_offset = 0;
  input.read(reinterpret_cast<char*>(&pe_offset), sizeof(pe_offset));
  if (!input || pe_offset > 64U * 1024U * 1024U) return false;
  input.seekg(static_cast<std::streamoff>(pe_offset), std::ios::beg);
  std::uint32_t pe_signature = 0;
  std::uint16_t machine = 0;
  input.read(reinterpret_cast<char*>(&pe_signature), sizeof(pe_signature));
  input.read(reinterpret_cast<char*>(&machine), sizeof(machine));
  if (!input || pe_signature != 0x00004550U) return false;
  return architecture == "x64" ? machine == 0x8664U : machine == 0x014CU;
}

host_result soui_miniblink_host::validate_architecture(const host_config& config) {
  if (config.architecture != "x86" && config.architecture != "x64") {
    return failure("E_INVALID_ARCHITECTURE", "Architecture must be x86 or x64.");
  }
  return success();
}

}  // namespace soui::sdk
