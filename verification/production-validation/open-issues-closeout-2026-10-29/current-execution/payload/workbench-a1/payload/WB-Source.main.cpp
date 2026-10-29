#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#if defined(SOUI_PERFORMANCE_DIAGNOSTICS) || defined(SOUI_BRIDGE_BENCHMARK)
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <soui/host/soui_miniblink_host.h>
#include <soui/host/frame_pacer.h>
#include <soui/runtime/services/desktop/window_commands.h>
#include <soui/runtime/services/desktop/tray_service.h>
#include <soui/runtime/services/desktop/menu_service.h>
#include <soui/runtime/services/desktop/global_shortcut.h>
#include <soui/version.h>
#if defined(SOUI_DEVTOOLS_PACKAGE_AVAILABLE) && SOUI_DEVTOOLS_PACKAGE_AVAILABLE
#include <soui_devtools_package.h>
#endif
#include "../integration/demo_bridge_adapter.h"
#include "work_area_placement.h"
#include "../../../third_party/include/json/json.hpp"

#if defined(SOUI_SOURCE_OVERRIDE)
#define SOUI_DEVKIT_HAS_REAL_SOUI 1
#include <tchar.h>
#include <souistd.h>
#include <unknown/obj-ref-i.h>
#include <com-cfg.h>
#include <SApp.h>
#include <core/SHostWnd.h>
#elif __has_include("../../third_party/soui/sdk/config/com-def.h") && \
    __has_include("../../third_party/soui/sdk/config/core-def.h")
#define SOUI_DEVKIT_HAS_REAL_SOUI 1
#include <tchar.h>
#include "../../../third_party/include/soui/SOUI/include/souistd.h"
#include "../../../third_party/include/soui/utilities/include/unknown/obj-ref-i.h"
#include "../../../third_party/include/soui/components/com-cfg.h"
#include "../../../third_party/include/soui/SOUI/include/SApp.h"
#include "../../../third_party/include/soui/SOUI/include/core/SHostWnd.h"
#else
#define SOUI_DEVKIT_HAS_REAL_SOUI 0
#endif

#if SOUI_DEVKIT_HAS_REAL_SOUI
#include "native_controls_demo.h"
namespace {

constexpr UINT resident_close_message = WM_APP + 0x421;
constexpr UINT resident_exit_message = WM_APP + 0x422;
constexpr UINT resident_action_message = WM_APP + 0x423;
// Application-native policy only. Replace this decision with the application's
// trusted local identity/enterprise policy; Debug and Release share it. No
// command line, environment or business Bridge action enables this example.
constexpr bool allow_native_devtools = false;
// Resource origin selection does not grant a diagnostic session.
constexpr bool use_local_resource_origin = false;
constexpr UINT devtools_open_command = 0x6D01;
constexpr UINT devtools_end_command = 0x6D02;
constexpr UINT devtools_status_command = 0x6D03;
bool diagnostic_target_retired_with_failure(soui::sdk::soui_miniblink_host& host) {
  if (!allow_native_devtools) return false;
  const auto observed = host.observe_devtools();
  return observed.state == soui::sdk::devtools::phase::failed && !observed.resources_retained;
}

std::filesystem::path executable_path() {
  std::wstring value(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    if (length == 0) return {};
    if (length + 1U < value.size()) {
      value.resize(length);
      return std::filesystem::path{value};
    }
    value.resize(value.size() * 2U);
  }
}

std::filesystem::path find_kit_root(const std::filesystem::path& executable) {
  auto current = executable.parent_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "third_party" / "miniblink") &&
        std::filesystem::exists(current / "third_party" / "soui")) {
      return current;
    }
    const auto parent = current.parent_path();
    if (parent == current) break;
    current = parent;
  }
  return executable.parent_path();
}

std::wstring option_value(const std::vector<std::wstring>& args, const wchar_t* name) {
  const std::wstring prefix = std::wstring{name} + L"=";
  for (const auto& arg : args) {
    if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
  }
  return {};
}

int option_dimension(const std::vector<std::wstring>& args, const wchar_t* name, int fallback) {
  const auto value = option_value(args, name);
  if (value.empty()) return fallback;
  try { return std::clamp(std::stoi(value), 640, 3840); }
  catch (...) { return fallback; }
}

std::optional<soui::demo::initial_window_rect> initial_window_on_primary_work_area(
    int requested_width, int requested_height) {
  const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{sizeof(info)};
  RECT work{};
  if (monitor != nullptr && GetMonitorInfoW(monitor, &info)) {
    work = info.rcWork;
  } else if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
    return std::nullopt;
  }
  return soui::demo::fit_initial_window(
      {work.left, work.top, work.right, work.bottom}, requested_width, requested_height);
}

void append_smoke_log(const std::filesystem::path& path, const char* line) {
#ifdef SOUI_BRIDGE_BENCHMARK
  LARGE_INTEGER now{}, frequency{};
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&frequency);
  if (std::string_view(line) == "process-start") {
    FILETIME created{}, exited{}, kernel{}, user{}, utc{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
      using precise_clock = void(WINAPI*)(LPFILETIME);
      const auto precise = reinterpret_cast<precise_clock>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetSystemTimePreciseAsFileTime"));
      LARGE_INTEGER before{}, after{};
      QueryPerformanceCounter(&before);
      if (precise) precise(&utc); else GetSystemTimeAsFileTime(&utc);
      QueryPerformanceCounter(&after);
      ULARGE_INTEGER creation_value{}, utc_value{};
      creation_value.LowPart = created.dwLowDateTime; creation_value.HighPart = created.dwHighDateTime;
      utc_value.LowPart = utc.dwLowDateTime; utc_value.HighPart = utc.dwHighDateTime;
      std::ofstream clock(path.parent_path() / "process-clock.json");
      clock.precision(16);
      clock << "{\"creation_filetime\":\"" << creation_value.QuadPart << "\",\"utc_filetime\":\"" << utc_value.QuadPart
            << "\",\"qpc_before_ms\":" << 1000.0 * before.QuadPart / frequency.QuadPart
            << ",\"qpc_after_ms\":" << 1000.0 * after.QuadPart / frequency.QuadPart
            << ",\"precise_utc\":" << (precise ? "true" : "false") << "}";
    }
  }
  std::ofstream stages(path.parent_path() / "startup-stages.ndjson", std::ios::app);
  stages.precision(16);
  stages << "{\"stage\":" << std::quoted(line) << ",\"qpc_ms\":" << 1000.0 * now.QuadPart / frequency.QuadPart << "}\n";
#endif
  if (path.empty()) return;
  std::ofstream output(path, std::ios::app);
  if (output) output << line << '\n';
}

// Miniblink paints into a native child window. If the host is DPI-unaware,
// Windows bitmap-scales that child on a high-DPI monitor and the text/SVG
// edges become soft before SOUI ever presents the frame. Set system DPI
// awareness through the Win7-compatible user32 entry point before creating
// SOUI or the WebView. (Per-monitor v2 can be added by a newer host without
// changing the bridge contract.)
void enable_system_dpi_awareness() {
  // Prefer per-monitor awareness on Windows 8.1+ so moving a window between
  // monitors does not trigger bitmap scaling. The shcore entry point is
  // resolved dynamically and therefore remains safe on Windows 7.
  using set_process_dpi_awareness = HRESULT(WINAPI*)(int);
  const auto shcore = LoadLibraryW(L"shcore.dll");
  if (shcore != nullptr) {
    const auto set_per_monitor = reinterpret_cast<set_process_dpi_awareness>(
        GetProcAddress(shcore, "SetProcessDpiAwareness"));
    if (set_per_monitor != nullptr && SUCCEEDED(set_per_monitor(2))) {
      FreeLibrary(shcore);
      return;
    }
    FreeLibrary(shcore);
  }
  using set_process_dpi_aware = BOOL(WINAPI*)();
  const auto user32 = GetModuleHandleW(L"user32.dll");
  const auto set_aware = user32 == nullptr
      ? nullptr
      : reinterpret_cast<set_process_dpi_aware>(GetProcAddress(user32, "SetProcessDPIAware"));
  if (set_aware != nullptr) (void)set_aware();
}

#ifdef SOUI_BRIDGE_BENCHMARK
class diagnostic_timer_policy final {
 public:
  ~diagnostic_timer_policy() {
    if (enabled_) (void)set_(GetCurrentProcess(), ProcessPowerThrottling, &saved_, sizeof(saved_));
  }
  bool enable() {
    using query_function = BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    set_ = reinterpret_cast<query_function>(GetProcAddress(kernel,"SetProcessInformation"));
    const auto get = reinterpret_cast<query_function>(GetProcAddress(kernel,"GetProcessInformation"));
    saved_.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (!set_ || !get || !get(GetCurrentProcess(),ProcessPowerThrottling,&saved_,sizeof(saved_))) return false;
    auto desired = saved_;
    // PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION in newer Windows SDKs.
    constexpr ULONG ignore_timer_resolution = 0x4;
    desired.ControlMask |= ignore_timer_resolution;
    desired.StateMask &= ~ignore_timer_resolution;
    enabled_ = set_(GetCurrentProcess(),ProcessPowerThrottling,&desired,sizeof(desired)) != FALSE;
    return enabled_;
  }
 private:
  BOOL(WINAPI* set_)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD){nullptr};
  PROCESS_POWER_THROTTLING_STATE saved_{};
  bool enabled_{false};
};
#endif

class demo_soui_window final : public SOUI::SHostWnd, public SOUI::IMessageFilter {
 public:
  demo_soui_window(soui::sdk::soui_miniblink_host& host, soui::demo::demo_bridge_adapter& bridge, bool smoke_test,
                   std::filesystem::path smoke_log, bool background_pacing_experiment = false,
                   bool prefer_high_resolution = true, bool auto_high_refresh = false, bool native_frame = false)
      : host_{host}, bridge_{bridge}, smoke_test_{smoke_test}, smoke_log_{std::move(smoke_log)},
        auto_high_refresh_{auto_high_refresh}, smoke_started_at_{GetTickCount64()}, native_frame_{native_frame} {
    append_smoke_log(smoke_log_, "window-constructed");
#ifdef SOUI_BRIDGE_BENCHMARK
    background_pacing_experiment_ = background_pacing_experiment;
    prefer_high_resolution_ = prefer_high_resolution;
#else
    (void)background_pacing_experiment;
    (void)prefer_high_resolution;
#endif
  }
  ~demo_soui_window() override { remove_devtools_filter(); }
  void configure_native_devtools(SOUI::SMessageLoop* loop, const std::filesystem::path& application_root) {
    if (!allow_native_devtools) return;
    if (!loop || !::IsWindow(m_hWnd) || devtools_loop_)
      throw std::runtime_error("The native diagnostic route requires its retained business window and message loop");
    soui::sdk::devtools::configuration config;
    config.access = soui::sdk::devtools::policy::application_controlled;
    config.ending = soui::sdk::devtools::termination::destroy_target;
#if defined(SOUI_DEVTOOLS_PACKAGE_AVAILABLE) && SOUI_DEVTOOLS_PACKAGE_AVAILABLE
    config.frontend_root = application_root / soui::sdk::devtools::package::frontend_subdir;
    config.frontend_manifest_sha256 = soui::sdk::devtools::package::frontend_manifest_sha256;
    config.kernel_sha256 = soui::sdk::devtools::package::kernel_sha256;
#else
    (void)application_root;
    throw std::runtime_error("Controlled DevTools requires the reviewed matching native package at build time");
#endif
    config.f12 = config.ctrl_shift_i = true;
    const auto configured = host_.configure_devtools(config); // Before this Host's start.
    if (!configured.ok) throw std::runtime_error(configured.code + ": " + configured.message);
    install_devtools_menu();
    if (!loop->AddMessageFilter(this)) throw std::runtime_error("Cannot register native diagnostic key routing");
    devtools_loop_ = loop;
  }
  BOOL PreTranslateMessage(MSG* message) override {
    if (!allow_native_devtools || !message || message->message != WM_KEYDOWN ||
        close_requested_ || resident_close_pending_ || devtools_entry_active_ ||
        (message->hwnd != m_hWnd && !::IsChild(m_hWnd, message->hwnd))) return FALSE;
    if (!host_.devtools_shortcut(static_cast<unsigned>(message->wParam),
        (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0,
        (GetKeyState(VK_MENU) & 0x8000) != 0, (message->lParam & (1LL << 30)) != 0)) return FALSE;
    request_native_devtools();
    return TRUE;
  }
  void cancel_native_devtools_prompt() noexcept {
    devtools_entry_cancelled_ = true;
    if (!diagnostic_prompt_ || !::IsWindow(m_hWnd)) return;
    // This is the application's own active consent dialog, not a backend
    // inspector-window search. Cancel its modal loop before shutdown's original
    // deadline; the controller still defers releasing this object until return.
    const auto popup = ::GetLastActivePopup(m_hWnd);
    if (popup && popup != m_hWnd && ::GetWindow(popup, GW_OWNER) == m_hWnd)
      (void)::PostMessageW(popup, WM_COMMAND, diagnostic_dismiss_command_, 0);
  }
  void configure_resident(HWND controller, unsigned slot, std::uint32_t generation) {
    // SHostWnd::OnDestroy otherwise quits for an ordinary top-level HWND.
    if (FAILED(GetHostAttr().SetAttribute(L"wndType", L"normal")))
      throw std::runtime_error("SOUI refused the resident normal-window policy");
    resident_controller_ = controller;
    resident_slot_ = slot;
    resident_generation_ = generation;
  }
  void stop_for_resident_close() {
    resident_close_pending_ = true;
    cancel_native_devtools_prompt();
    remove_devtools_filter();
    update_devtools_menu();
    if (IsWindow()) KillTimer(1);
    pacer_.stop();
  }
  bool resident_dispatch_active() const {
    return GetCurrentMessage() != nullptr || devtools_entry_active_ || diagnostic_prompt_;
  }
#ifdef SOUI_BRIDGE_BENCHMARK
  void set_gallery_benchmark(bool all) { gallery_benchmark_=true; gallery_all_=all; }
#endif

 protected:
  void OnDpiChanged(WORD dpi, const RECT* suggested) {
    (void)dpi;
    if (suggested != nullptr && suggested->right > suggested->left && suggested->bottom > suggested->top) {
      ::SetWindowPos(m_hWnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    display_refresh_hz_ = 0;
    RECT client{};
    ::GetClientRect(m_hWnd, &client);
    (void)host_.resize(std::max(1L, client.right - client.left),
                       std::max(1L, client.bottom - client.top));
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
  }

  void OnSize(UINT type, SOUI::CSize size) {
    // Windows reports the icon's 160px geometry when minimized. It is not
    // the WebView viewport and must not invalidate the caption/layout cache.
    if (type == SIZE_MINIMIZED) return;
    display_refresh_hz_ = 0;
    (void)host_.resize(size.cx, size.cy);
  }

  void OnSetFocus(HWND old_window) {
    (void)old_window;
    (void)host_.focus();
  }

  void PumpHost() {
#ifdef SOUI_BRIDGE_BENCHMARK
    LARGE_INTEGER pump_start{}, pump_frequency{};
    QueryPerformanceCounter(&pump_start);
    QueryPerformanceFrequency(&pump_frequency);
#endif
    host_.pump();
    if (allow_native_devtools) {
      const auto observed = host_.observe_devtools();
      update_devtools_menu();
      // The user accepted target retirement before open. After actual native
      // drain, close only this business HWND through its existing owner route.
      if (diagnostic_open_requested_ && observed.state == soui::sdk::devtools::phase::drained &&
          !observed.resources_retained) OnClose();
      else if (observed.state == soui::sdk::devtools::phase::failed && !observed.resources_retained) {
        if (!close_exit_code_) close_exit_code_ = 1;
        OnClose();
      }
    }
#ifdef SOUI_BRIDGE_BENCHMARK
    LARGE_INTEGER pump_end{};
    QueryPerformanceCounter(&pump_end);
    if (pump_starts_.size() < 100000) {
      pump_starts_.push_back(1000.0 * pump_start.QuadPart / pump_frequency.QuadPart);
      pump_costs_.push_back(1000.0 * (pump_end.QuadPart - pump_start.QuadPart) / pump_frequency.QuadPart);
      pacing_counters_.push_back(pacer_.diagnostic_counters());
    }
#endif
  }

  LRESULT OnDisplayPathChanged(UINT, WPARAM, LPARAM) {
    display_refresh_hz_ = 0;
    failed_target_ = 0;
    SetMsgHandled(FALSE); // Preserve SHostWnd and default position processing.
    return 0;
  }

  void UpdatePacer() {
    if (close_requested_ || resident_close_pending_) return;
    if (!display_refresh_hz_)
      display_refresh_hz_ = soui::sdk::frame_pacer::query_display_refresh(m_hWnd);
    bridge_.set_display_refresh(display_refresh_hz_);
    const auto desired = bridge_.requested_frame_rate();
    const auto foreground = ::GetForegroundWindow();
    bool eligible = foreground == m_hWnd || ::GetAncestor(foreground, GA_ROOTOWNER) == m_hWnd;
    // The GPU experiment owns its presentation clock. Foreground ownership
    // can be transiently wrong for embedded/automated hosts, so keep the
    // high-rate pulse alive while the visible window is not minimized.
    eligible = eligible || auto_high_refresh_;
#ifdef SOUI_BRIDGE_BENCHMARK
    eligible = eligible || background_pacing_experiment_;
#endif
    const bool active = ::IsWindowVisible(m_hWnd) && !::IsIconic(m_hWnd) && eligible;
    // Keep requested scheduling and physical presentation distinct. Respect
    // the current monitor's integer rate without rounding it to a coarse tier.
    const unsigned effective = soui::sdk::clamp_frame_rate_to_display(desired, display_refresh_hz_);
    const unsigned target = active ? effective : 0;
    if (pacer_.failed()) { pacer_.stop(); failed_target_ = target; }
    if (target == 0) { if (pacer_.target()) pacer_.stop(); failed_target_ = 0; }
    else if (pacer_.target() != target && failed_target_ != target) {
      bool prefer_high_resolution = true;
#ifdef SOUI_BRIDGE_BENCHMARK
      prefer_high_resolution = prefer_high_resolution_;
#endif
      if (!pacer_.start(m_hWnd, target, prefer_high_resolution)) failed_target_ = target;
    }
    bridge_.set_active_frame_rate(pacer_.target() ? pacer_.target() : 60);
  }

  LRESULT OnFramePulse(UINT, WPARAM, LPARAM) {
    UpdatePacer();
    if (pacer_.target()) PumpHost();
    pacer_.acknowledge();
    return 0;
  }
#ifdef SOUI_BRIDGE_BENCHMARK
  // Read-only diagnostic message; absent from normal Release/Debug binaries.
  LRESULT OnPacerStateProbe(UINT, WPARAM selector, LPARAM) {
    if (selector == 0) return pacer_.target();
    if (selector == 1) return bridge_.requested_frame_rate();
    if (selector == 2) return pacer_.backend();
    if (selector == 3) return host_.configured_wake_interval();
    return -1;
  }
#endif

  void OnTimer(UINT_PTR timer_id) {
    if (timer_id != 1U) return;
    if (allow_native_devtools && devtools_entry_cancelled_ && diagnostic_prompt_) cancel_native_devtools_prompt();
    if (close_requested_) { finish_close_request(); return; }
    UpdatePacer();
    if (!pacer_.target()) PumpHost();
    if (close_requested_ || resident_close_pending_) return;
    if (!smoke_test_ || smoke_finished_) return;
    if ((++smoke_ticks_ % 60U) == 1U) {
      append_smoke_log(smoke_log_, host_.document_ready() ? "timer-document-ready" : "timer-waiting-document");
      const auto diagnostics = host_.application_diagnostics();
      append_smoke_log(smoke_log_, diagnostics.c_str());
    }
    if (reload_pending_) {
      const auto reloaded = host_.reload();
      if (!reloaded.ok && reloaded.code != "E_RELOAD_PENDING") {
        finish_smoke_test(4);
        return;
      }
      reload_pending_ = !reloaded.ok;
    }
    const bool application_ready = !reload_pending_ && host_.application_ready();
    // Keep the original budget through both the adapter operation and page
    // recovery, including time spent observing readiness after reload returns.
    if (reload_waiting_ready_ && GetTickCount64() - not_ready_since_ > 15000U) {
      append_smoke_log(smoke_log_, "application-reload-timeout");
      finish_smoke_test(2);
      return;
    }
    if (application_ready) {
      reload_waiting_ready_ = false;
      // A benchmark intentionally reloads the React page after the fixed
      // native session TTL. Track readiness transitions instead of measuring
      // the timeout from process start; otherwise the legitimate reload is
      // mistaken for a failed initial startup.
      application_ready_seen_ = true;
      not_ready_since_ = 0;
#ifdef SOUI_BRIDGE_BENCHMARK
      if(gallery_benchmark_ && !gallery_started_) {gallery_started_=true;host_.benchmark_gallery(gallery_all_);}
      if (GetTickCount64() - last_benchmark_sample_ >= 5000U) {
        last_benchmark_sample_ = GetTickCount64();
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
          std::ofstream sample(smoke_log_.parent_path() / "task-loop-memory.ndjson", std::ios::app);
          sample << "{\"elapsed_ms\":" << (GetTickCount64() - smoke_started_at_)
                 << ",\"working_set\":" << memory.WorkingSetSize << ",\"private_commit\":" << memory.PrivateUsage << "}\n";
        }
      }
      const auto benchmark_now = GetTickCount64();
      if (benchmark_now - last_benchmark_result_poll_ < 100U) return;
      last_benchmark_result_poll_ = benchmark_now;
      const auto report = host_.benchmark_result();
      if (report.empty()) {
        // The endurance fixture is 35 minutes. Keep a bounded diagnostic
        // deadline so a hung page still fails, while allowing the measured
        // workload and its 30-minute session recovery to finish naturally.
        if (GetTickCount64() - smoke_started_at_ > 2400000U) finish_smoke_test(6);
        return;
      }
      std::ofstream(smoke_log_.parent_path() / "bridge-benchmark.json") << report;
      std::ofstream pumps(smoke_log_.parent_path() / "pump-timing.ndjson");
      pumps.precision(16);
      for (std::size_t index = 0; index < pump_starts_.size(); ++index) {
        const auto counters = pacing_counters_[index];
        pumps << "{\"qpc_ms\":" << pump_starts_[index] << ",\"cost_ms\":" << pump_costs_[index]
              << ",\"timer_wakes\":" << counters.wakes << ",\"timer_due\":" << counters.due
              << ",\"timer_coalesced\":" << counters.coalesced << ",\"timer_missed\":" << counters.missed
              << ",\"timer_backend\":" << counters.backend << "}\n";
      }
      if (report.find("\"status\":\"failed\"") != std::string::npos) {
        finish_smoke_test(7);
        return;
      }
#endif
#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
      if (ready_observed_at_ == 0) ready_observed_at_ = GetTickCount64();
      // Miniblink's default recovery timer runs every 40 seconds. Keep both
      // boundary samples beyond that interval so the result cannot depend on
      // whether the 40-second callback wins a race with a 30-second sample.
      const auto settle_wait_ms = (gc_requested_ || reload_cycle_ == 0U) ? 45000U : 1000U;
      if (GetTickCount64() - ready_observed_at_ < settle_wait_ms) return;
      PROCESS_MEMORY_COUNTERS_EX counters{};
      counters.cb = sizeof(counters);
      if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        finish_smoke_test(3);
        return;
      }
      std::ofstream samples(smoke_log_.parent_path() / "reload-memory.ndjson", std::ios::app);
      samples << "{\"cycle\":" << reload_cycle_ << ",\"settle_wait_ms\":" << settle_wait_ms << ",\"settled\":" << (gc_requested_ ? "true" : "false") << ",\"gc_timer_requested\":" << (gc_api_invoked_ ? "true" : "false") << ",\"working_set\":" << counters.WorkingSetSize
              << ",\"private_commit\":" << counters.PrivateUsage << ",\"elapsed_ms\":"
              << (GetTickCount64() - diagnostic_started_at_) << "}\n";
      samples.close();
      if (reload_cycle_ < 50U) {
        ++reload_cycle_;
        ready_observed_at_ = 0;
        const auto requested_at = GetTickCount64();
        smoke_started_at_ = requested_at;
        not_ready_since_ = requested_at;
        reload_waiting_ready_ = true;
        const auto reloaded = host_.reload();
        reload_pending_ = !reloaded.ok && reloaded.code == "E_RELOAD_PENDING";
        if (!reloaded.ok && !reload_pending_) finish_smoke_test(4);
        return;
      }
      if (!gc_requested_) {
#ifndef SOUI_NATURAL_SETTLE
        // These APIs schedule collection; waiting is not proof every heap was
        // collected. They must run on Miniblink's stable owner thread.
        if (!host_.request_memory_recovery().ok) { finish_smoke_test(5); return; }
        gc_api_invoked_ = true;
#endif
        gc_requested_ = true;
        ready_observed_at_ = GetTickCount64();
        return;
      }
#endif
      const auto diagnostics = host_.application_diagnostics();
      append_smoke_log(smoke_log_, diagnostics.c_str());
      append_smoke_log(smoke_log_, "application-ready");
      finish_smoke_test(0);
    } else {
      if (not_ready_since_ == 0) not_ready_since_ = GetTickCount64();
      // Before the first ready event this catches a broken startup. After a
      // ready page has been observed it also catches a reload that genuinely
      // fails to recover, without penalising the normal reload transition.
      if (GetTickCount64() - not_ready_since_ > 15000U) {
        append_smoke_log(smoke_log_, application_ready_seen_ ? "application-reload-timeout" : "application-timeout");
        finish_smoke_test(2);
      }
    }
  }

  LRESULT OnBridgeEvent(UINT, WPARAM, LPARAM payload) {
    host_.deliver_posted_event(reinterpret_cast<void*>(payload));
    return 0;
  }
  LRESULT OnNativeCommand(UINT, WPARAM command, LPARAM source) {
    if (!allow_native_devtools || HIWORD(command) || source) { SetMsgHandled(FALSE); return 0; }
    if (LOWORD(command) == devtools_open_command) request_native_devtools();
    else if (LOWORD(command) == devtools_end_command) {
      const auto observed = host_.observe_devtools();
      if (diagnostic_open_requested_ || observed.retirement_required) OnClose();
    } else SetMsgHandled(FALSE);
    return 0;
  }

  LRESULT OnWindowCommand(UINT, WPARAM command, LPARAM payload) {
    const auto action = static_cast<soui::runtime::window_command>(command);
    // A delayed Bridge request must not start a move after the button is up.
    if (action != soui::runtime::window_command::begin_drag ||
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000))
      return soui::runtime::execute_window_command(m_hWnd, action, payload) ? 1 : 0;
    return 0;
  }

  LRESULT OnWindowHitTest(UINT message, WPARAM wparam, LPARAM position) {
    if (has_native_menu_frame()) return DefWindowProc(message, wparam, position);
    // This WebView-only host does not load SOUI's XML non-client frame.
    // Its full-client input child delegates these edges back to this HWND.
    return soui::runtime::resize_border_hit(m_hWnd, position);
  }

  LRESULT OnNativeNonClient(UINT message, WPARAM wparam, LPARAM lparam) {
    if (!has_native_menu_frame()) { SetMsgHandled(FALSE); return 0; }
    return DefWindowProc(message, wparam, lparam);
  }

  void OnClose() {
    cancel_native_devtools_prompt();
    if (resident_controller_ != nullptr) {
      // The controller destroys this object only after this WndProc returns.
      if (!resident_close_pending_) {
        resident_close_pending_ = ::PostMessageW(resident_controller_, resident_close_message,
            resident_slot_, static_cast<LPARAM>(resident_generation_)) != FALSE;
        if (!resident_close_pending_) {
          // This only asks the controller to exit; it never deletes this slot.
          ::SendMessageW(resident_controller_, resident_exit_message, 1, 0);
        }
      }
      return;
    }
    request_close(0);
  }

 public:
  void begin_shutdown(int exit_code) { request_close(exit_code); }
  int finish_shutdown_before_return(SOUI::SApplication& application, int exit_code) noexcept {
    try { request_close(exit_code); } catch (...) { std::terminate(); }
    while (::IsWindow(m_hWnd)) {
      MSG message{};
      const auto status = ::GetMessageW(&message, nullptr, 0, 0);
      if (status < 0) std::terminate();
      if (status == 0) {
        if (!close_exit_code_ && message.wParam) close_exit_code_ = static_cast<int>(message.wParam);
        try { finish_close_request(); } catch (...) { std::terminate(); }
        continue;
      }
      try { application.GetMsgLoop()->OnMsg(&message); }
      catch (...) { if (!close_exit_code_) close_exit_code_ = 1; }
      try { finish_close_request(); } catch (...) { std::terminate(); }
    }
    try {
      if (!host_.destroy().ok) {
        if (!diagnostic_target_retired_with_failure(host_)) std::terminate();
        if (!close_exit_code_) close_exit_code_ = 1;
      }
    } catch (...) { std::terminate(); }
    return close_exit_code_;
  }

 protected:

  BEGIN_MSG_MAP_EX(demo_soui_window)
    MESSAGE_HANDLER_EX(WM_NCCALCSIZE, OnNativeNonClient)
    MESSAGE_HANDLER_EX(WM_NCACTIVATE, OnNativeNonClient)
    MESSAGE_HANDLER_EX(WM_NCPAINT, OnNativeNonClient)
    MESSAGE_HANDLER_EX(WM_DISPLAYCHANGE, OnDisplayPathChanged)
    MESSAGE_HANDLER_EX(WM_WINDOWPOSCHANGED, OnDisplayPathChanged)
    MSG_WM_DPICHANGED(OnDpiChanged)
    MSG_WM_SIZE(OnSize)
    MSG_WM_SETFOCUS(OnSetFocus)
    MSG_WM_TIMER(OnTimer)
    MSG_WM_CLOSE(OnClose)
    MESSAGE_HANDLER_EX(soui::sdk::bridge_event_message, OnBridgeEvent)
    MESSAGE_HANDLER_EX(WM_COMMAND, OnNativeCommand)
    MESSAGE_HANDLER_EX(soui::runtime::window_command_message, OnWindowCommand)
    MESSAGE_HANDLER_EX(WM_NCHITTEST, OnWindowHitTest)
    MESSAGE_HANDLER_EX(soui::sdk::frame_pulse_message, OnFramePulse)
#ifdef SOUI_BRIDGE_BENCHMARK
    MESSAGE_HANDLER_EX(WM_APP + 0x353, OnPacerStateProbe)
#endif
    CHAIN_MSG_MAP(SHostWnd)
  END_MSG_MAP()

 private:
  bool has_native_menu_frame() const noexcept {
    return native_frame_ || (allow_native_devtools && devtools_menu_bar_ && ::GetMenu(m_hWnd) == devtools_menu_bar_);
  }
  bool native_frame_ = false;
  void remove_devtools_filter() noexcept {
    if (devtools_loop_) {
      if (!devtools_loop_->RemoveMessageFilter(this)) std::terminate();
      devtools_loop_ = nullptr;
    }
  }
  void install_devtools_menu() {
    HMENU bar = ::GetMenu(m_hWnd);
    const bool new_bar = bar == nullptr;
    if (new_bar) bar = ::CreateMenu();
    HMENU menu = ::CreatePopupMenu();
    if (!bar || !menu) {
      if (menu) ::DestroyMenu(menu);
      if (new_bar && bar) ::DestroyMenu(bar);
      throw std::runtime_error("Cannot create native diagnostic menu");
    }
    bool attached = false;
    if (::AppendMenuW(menu, MF_STRING, devtools_open_command, L"打开当前业务窗口的开发者工具") &&
        ::AppendMenuW(menu, MF_STRING | MF_GRAYED, devtools_end_command, L"结束诊断并关闭当前业务窗口") &&
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) &&
        ::AppendMenuW(menu, MF_STRING | MF_GRAYED, devtools_status_command, L"状态：未请求") &&
        ::AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(menu), L"诊断")) attached = true;
    if (!attached || (new_bar && !::SetMenu(m_hWnd, bar))) {
      if (!attached) ::DestroyMenu(menu);
      if (new_bar) ::DestroyMenu(bar);
      throw std::runtime_error("Cannot attach native diagnostic menu");
    }
    devtools_menu_bar_ = bar; devtools_menu_ = menu;
    // Keep the existing size and activation while reserving a real menu area.
    if (!::SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
      throw std::runtime_error("Cannot calculate the native diagnostic menu frame");
    ::DrawMenuBar(m_hWnd);
  }
  static const wchar_t* diagnostic_truth(soui::sdk::devtools::truth value) noexcept {
    return value == soui::sdk::devtools::truth::yes ? L"是" :
      value == soui::sdk::devtools::truth::no ? L"否" : L"未知";
  }
  void update_devtools_menu() {
    if (!allow_native_devtools || !devtools_menu_ || !::IsWindow(m_hWnd)) return;
    const auto observed = host_.observe_devtools();
    using phase = soui::sdk::devtools::phase;
    const wchar_t* state = L"未请求";
    switch (observed.state) {
      case phase::opening: state = L"打开请求处理中"; break;
      case phase::attached: state = L"连接已建立"; break;
      case phase::revoking: state = L"正在撤销并关闭目标"; break;
      case phase::drained: state = L"目标已排空"; break;
      case phase::failed: state = L"失败；检查关闭状态"; break;
      default: break;
    }
    std::wstring label = std::wstring(L"状态：") + state + L"；暂停：" + diagnostic_truth(observed.paused) +
      L"；窗口可见：" + diagnostic_truth(observed.visible) + L"；许可有效：" + (observed.grant_valid ? L"是" : L"否");
    if (!observed.code.empty()) {
      const auto limit = std::min<std::size_t>(observed.code.size(), 96);
      label += L"；"; label.append(observed.code.begin(), observed.code.begin() + limit);
    }
    const bool closing = close_requested_ || resident_close_pending_ || devtools_entry_cancelled_;
    const bool may_open = !closing && !diagnostic_open_requested_ && host_.session() && host_.document_ready() &&
      !observed.retirement_required && observed.state != phase::failed;
    const bool may_end = !closing && (diagnostic_open_requested_ || observed.retirement_required);
    if (label != devtools_status_ || may_open != devtools_may_open_ || may_end != devtools_may_end_) {
      ::ModifyMenuW(devtools_menu_, devtools_status_command, MF_BYCOMMAND | MF_STRING | MF_GRAYED,
        devtools_status_command, label.c_str());
      ::EnableMenuItem(devtools_menu_, devtools_open_command, MF_BYCOMMAND | (may_open ? MF_ENABLED : MF_GRAYED));
      ::EnableMenuItem(devtools_menu_, devtools_end_command, MF_BYCOMMAND | (may_end ? MF_ENABLED : MF_GRAYED));
      ::DrawMenuBar(m_hWnd);
      devtools_status_ = std::move(label); devtools_may_open_ = may_open; devtools_may_end_ = may_end;
    }
  }
  void request_native_devtools() {
    if (!allow_native_devtools || close_requested_ || resident_close_pending_ || devtools_entry_cancelled_ ||
        devtools_entry_active_ || !::IsWindow(m_hWnd) || !host_.session() || !host_.document_ready()) return;
    if (diagnostic_open_requested_) { update_devtools_menu(); return; }
    devtools_entry_active_ = true;
    struct entry_guard { bool& active; ~entry_guard() { active = false; } } entry{devtools_entry_active_};
    diagnostic_prompt_ = true;
    diagnostic_dismiss_command_ = IDNO;
    const auto answer = ::MessageBoxW(m_hWnd,
      L"允许调试当前业务窗口？调试者可查看页面数据并执行代码。\n\n结束诊断将关闭这个业务窗口；请先保存工作。其他工作窗口由各自许可保护。",
      L"当前业务窗口的开发者工具", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    diagnostic_prompt_ = false;
    if (answer != IDYES || close_requested_ || resident_close_pending_ || devtools_entry_cancelled_ || !::IsWindow(m_hWnd)) return;
    soui::sdk::devtools::grant grant;
    const auto authorized = host_.authorize_devtools(grant);
    if (!authorized.ok) {
      diagnostic_prompt_ = true; diagnostic_dismiss_command_ = IDOK;
      ::MessageBoxA(m_hWnd, authorized.message.c_str(), authorized.code.c_str(), MB_OK | MB_ICONERROR);
      diagnostic_prompt_ = false;
      return;
    }
    const auto opened = host_.open_devtools(grant);
    diagnostic_open_requested_ = opened.accepted();
    if (!opened.accepted()) {
      diagnostic_prompt_ = true; diagnostic_dismiss_command_ = IDOK;
      ::MessageBoxA(m_hWnd, opened.message.c_str(), opened.code.c_str(), MB_OK | MB_ICONERROR);
      diagnostic_prompt_ = false;
    }
    update_devtools_menu();
  }
  void finish_smoke_test(int exit_code) {
    smoke_finished_ = true;
    request_close(exit_code);
  }
  void request_close(int exit_code) {
    cancel_native_devtools_prompt();
    if (!close_exit_code_ && exit_code) close_exit_code_ = exit_code;
    if (!close_requested_) {
      close_requested_ = true;
      update_devtools_menu();
      pacer_.stop();
      if (::IsWindow(m_hWnd) && !SetTimer(1, 50)) std::terminate();
      append_smoke_log(smoke_log_, "host-close-start");
    }
    finish_close_request();
  }
  void finish_close_request() {
    const auto closed = host_.destroy();
    if (!closed.ok) {
      if (close_error_ != closed.code) {
        close_error_ = closed.code;
        append_smoke_log(smoke_log_, closed.code.c_str());
      }
      if (!diagnostic_target_retired_with_failure(host_)) return;
      if (!close_exit_code_) close_exit_code_ = 1;
    }
    if (!::IsWindow(m_hWnd)) return;
    if (!DestroyWindow()) {
      append_smoke_log(smoke_log_, "E_WINDOW_CLOSE_FAILED");
      return;
    }
    append_smoke_log(smoke_log_, "host-close-complete");
    append_smoke_log(smoke_log_, "window-destroyed");
    PostQuitMessage(close_exit_code_);
  }

  soui::sdk::soui_miniblink_host& host_;
  soui::demo::demo_bridge_adapter& bridge_;
  soui::sdk::frame_pacer pacer_;
  unsigned display_refresh_hz_{0};
  unsigned failed_target_{0};
  bool smoke_test_{false};
  bool auto_high_refresh_{false};
  bool close_requested_{false};
  int close_exit_code_{0};
  std::string close_error_;
#ifdef SOUI_BRIDGE_BENCHMARK
  bool background_pacing_experiment_{false};
  bool gallery_benchmark_{false}, gallery_all_{false}, gallery_started_{false};
  bool prefer_high_resolution_{true};
  ULONGLONG last_benchmark_sample_{0};
  ULONGLONG last_benchmark_result_poll_{0};
  std::vector<double> pump_starts_;
  std::vector<double> pump_costs_;
  std::vector<soui::sdk::frame_pacer::counters> pacing_counters_;
#endif
#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
  unsigned reload_cycle_{0};
  bool gc_requested_{false};
  bool gc_api_invoked_{false};
  ULONGLONG ready_observed_at_{0};
  ULONGLONG diagnostic_started_at_{GetTickCount64()};
#endif
  bool smoke_finished_{false};
  bool application_ready_seen_{false};
  bool reload_pending_{false};
  bool reload_waiting_ready_{false};
  std::filesystem::path smoke_log_;
  unsigned smoke_ticks_{0};
  ULONGLONG smoke_started_at_{0};
  ULONGLONG not_ready_since_{0};
  HWND resident_controller_{nullptr};
  unsigned resident_slot_{0};
  std::uint32_t resident_generation_{0};
  bool resident_close_pending_{false};
  SOUI::SMessageLoop* devtools_loop_{nullptr}; // Removed before this window bundle is released.
  HMENU devtools_menu_bar_{nullptr}; // Borrowed from this HWND after attachment.
  HMENU devtools_menu_{nullptr}; // Owned by the attached native menu/this HWND.
  bool devtools_entry_active_{false}, diagnostic_prompt_{false}, devtools_entry_cancelled_{false};
  bool diagnostic_open_requested_{false}, devtools_may_open_{false}, devtools_may_end_{false};
  UINT diagnostic_dismiss_command_{IDNO};
  std::wstring devtools_status_;
};

#include "resident_controller.h"

bool run_self_test(const std::filesystem::path& kit_root, const std::filesystem::path& output_root,
                   const std::string& architecture,
                   const std::filesystem::path& miniblink, const std::filesystem::path& resources) {
  const bool binary_exists = std::filesystem::is_regular_file(miniblink);
  const bool architecture_matches = binary_exists &&
                                    soui::sdk::binary_matches_architecture(miniblink, architecture);
  const bool local_resources = std::filesystem::is_regular_file(resources / "index.html");
  const auto runtime = kit_root / "third_party" / "soui" /
                       (architecture == "x64" ? "x64" : "x86") / "bin";
  const auto runtime_root = std::filesystem::is_regular_file(output_root / "soui.dll")
                                ? output_root
                                : runtime;
  const bool soui_runtime = std::filesystem::is_regular_file(runtime_root / "soui.dll") &&
                            (std::filesystem::is_regular_file(runtime_root / "render-gdi.dll") ||
                             std::filesystem::is_regular_file(runtime_root / "render-gdid.dll"));
  const bool ok = binary_exists && architecture_matches && soui_runtime && local_resources;
  const std::wstring message = ok ? L"SOUI Miniblink AI DevKit self-test: ready\n"
                                  : L"SOUI Miniblink AI DevKit self-test: failed\n";
  OutputDebugStringW(message.c_str());
  return ok;
}

bool run_public_task_self_test() {
  using json = nlohmann::json;
  try {
    std::atomic<unsigned> events{0};
    soui::demo::demo_bridge_adapter bridge;
    bridge.set_event_sink([&events](std::string_view text) {
      const auto event = json::parse(text.begin(), text.end(), nullptr, false);
      if (event.is_object() && event.value("eventName", "") == "task.progress" &&
          event.value("taskId", "").rfind("sdk-task-", 0) == 0) ++events;
    });
    const auto bootstrap = json::parse(bridge.handle_json(json{{"requestId", "demo-task-bootstrap"},
        {"sessionId", ""}, {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
        {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
    if (!bootstrap.value("ok", false)) return false;
    const auto auth = bootstrap.at("result");
    unsigned request_number = 0;
    const auto call = [&](const char* method, json params) {
      return json::parse(bridge.handle_json(json{{"requestId", "demo-task-" + std::to_string(++request_number)},
          {"sessionId", auth.at("sessionId")}, {"origin", auth.at("origin")},
          {"token", auth.at("token")}, {"method", method}, {"params", std::move(params)}}.dump()));
    };
    const auto started = call("task.start", {{"kind", "demo.count"}, {"operationId", "count-success"},
        {"input", {{"steps", 20}}}});
    if (!started.value("ok", false)) return false;
    const auto id = started.at("result").at("taskId").get<std::string>();
    if (id.rfind("sdk-task-", 0) != 0) return false;
    const auto subscribed = call("task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}});
    if (!subscribed.value("ok", false)) return false;
    const auto subscription_id = subscribed.at("result").at("subscriptionId").get<std::string>();
    if (subscription_id.rfind("sdk-subscription-", 0) != 0) return false;
    bool complete = false;
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
      const auto queried = call("task.query", {{"taskId", id}});
      if (!queried.value("ok", false)) return false;
      if (queried.at("result").value("state", "") == "succeeded") {
        complete = queried.at("result").at("result").at("sum") == 210;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!complete || events.load() == 0 ||
        !call("task.unsubscribe", {{"subscriptionId", subscription_id}}).value("ok", false)) return false;
    const auto cancellable = call("task.start", {{"kind", "demo.count"}, {"operationId", "count-cancel"},
        {"input", {{"steps", 200}}}});
    if (!cancellable.value("ok", false)) return false;
    const auto cancel_id = cancellable.at("result").at("taskId").get<std::string>();
    if (!call("task.cancel", {{"taskId", cancel_id}}).value("ok", false)) return false;
    bool cancelled = false;
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
      const auto queried = call("task.query", {{"taskId", cancel_id}});
      if (!queried.value("ok", false)) return false;
      if (queried.at("result").value("state", "") == "cancelled") { cancelled = true; break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    bridge.shutdown_session(auth.at("sessionId").get<std::string>());
    return cancelled && !call("task.query", {{"taskId", id}}).value("ok", true);
  } catch (...) { return false; }
}

int run_gui(HINSTANCE instance, const std::vector<std::wstring>& args, resident_run_context& resident_log) {
  enable_system_dpi_awareness();
  const auto executable = executable_path();
  const auto kit_root = find_kit_root(executable);
  const auto output_root = executable.parent_path();
  const bool resident = std::find(args.begin(), args.end(), L"--resident") != args.end();
  const bool resident_smoke = std::find(args.begin(), args.end(), L"--resident-smoke-test") != args.end();
  std::wstring resident_log_option;
  bool resident_log_specified = false;
  for (std::size_t index = 0; index < args.size(); ++index) {
    const auto& argument = args[index];
    if (argument != L"--resident-log" && argument.rfind(L"--resident-log=", 0) != 0) continue;
    if (resident_log_specified) return 2;
    resident_log_specified = true;
    if (argument == L"--resident-log") {
      if (++index == args.size() || args[index].rfind(L"--", 0) == 0) return 2;
      resident_log_option = args[index];
    } else resident_log_option = argument.substr(std::wstring{L"--resident-log="}.size());
    if (resident_log_option.empty() || !std::filesystem::path{resident_log_option}.is_absolute()) return 2;
  }
  if (resident_log_specified && !resident && !resident_smoke) return 2;
  if (resident || resident_smoke) {
    resident_log.started = GetTickCount64();
    resident_log.run_id = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(resident_log.started);
    if (resident_log_specified) resident_log.log = resident_log_option;
    else {
      std::error_code error;
      const auto temporary = std::filesystem::temp_directory_path(error);
      if (error) return 1;
      resident_log.log = temporary / ("soui-workbench-resident-" + resident_log.run_id + ".ndjson");
    }
    // Keep one exclusively created handle for this run. Existing evidence is
    // never overwritten or appended, and the installed payload stays clean.
    resident_log.file = CreateFileW(resident_log.log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (resident_log.file == INVALID_HANDLE_VALUE) return 1;
    resident_log.active = true;
    const bool conflicting = std::any_of(args.begin(), args.end(), [](const std::wstring& argument) {
      const auto name = argument.substr(0, argument.find(L'='));
      return name == L"--webview-smoke-test" || name == L"--self-test" ||
          name == L"--task-service-self-test" || name == L"--gallery-benchmark" ||
          name == L"--benchmark-dataset" || name == L"--v8-noopt-experiment" ||
          name == L"--unmanaged-frame-timing" || name == L"--zero-engine-timing-experiment" ||
          name == L"--engine-timing-experiment" || name == L"--honor-timer-resolution-experiment" ||
          name == L"--background-pacing-experiment" || name == L"--legacy-frame-clock";
    });
    if (!resident || conflicting) {
      (void)resident_write(resident_log, "mode.rejected", {{"exitCode", 2},
          {"message", "--resident-smoke-test requires --resident; residency cannot combine with single-window tests or benchmark experiments"}});
      return 2;
    }
    if (!resident_write(resident_log, "mode.accepted", {{"smoke", resident_smoke}})) return 1;
  }
#if defined(_WIN64)
  const std::string default_architecture = "x64";
#else
  const std::string default_architecture = "x86";
#endif
  const auto architecture_option = option_value(args, L"--arch");
  const std::string architecture = architecture_option.empty()
                                        ? default_architecture
                                        : std::filesystem::path{architecture_option}.string();
  const auto initial_page = option_value(args, L"--page");
  if (!initial_page.empty()) {
    const auto initial_page_narrow = std::filesystem::path{initial_page}.string();
    SetEnvironmentVariableA("SOUI_INITIAL_PAGE", initial_page_narrow.c_str());
  }
  else SetEnvironmentVariableA("SOUI_INITIAL_PAGE", nullptr);
  const auto renderer_option = option_value(args, L"--renderer");
  const bool gpu_renderer = std::find(args.begin(), args.end(), L"--gpu-renderer") != args.end() ||
                            renderer_option == L"gpu";
  const bool gpu_readback = std::find(args.begin(), args.end(), L"--gpu-readback") != args.end() ||
                            renderer_option == L"readback";
  const bool auto_renderer = std::find(args.begin(), args.end(), L"--auto-renderer") != args.end() ||
                             renderer_option == L"auto";
  const bool software_renderer = renderer_option == L"cpu" || renderer_option == L"software" ||
                                 (!gpu_renderer && !gpu_readback && !auto_renderer);
  const bool cpu_high_refresh = std::find(args.begin(), args.end(), L"--cpu-high-refresh") != args.end();
  if (!renderer_option.empty() && renderer_option != L"auto" && renderer_option != L"gpu" &&
      renderer_option != L"readback" && !software_renderer) {
    (void)resident_write(resident_log, "startup.error", {{"code", "E_RENDERER_OPTION"}, {"exitCode", 2}});
    if (!resident_smoke)
      MessageBoxW(nullptr, L"--renderer accepts auto, gpu, readback or cpu.", L"SOUI Miniblink AI DevKit", MB_ICONERROR);
    return 2;
  }
  // Request the crisp desktop text profile from the source Miniblink build.
  // It keeps outline/AA glyphs but disables fractional glyph positioning,
  // which otherwise leaves small Chinese labels with a soft fringe at 1x.
  // Older bundled binaries ignore this opt-in and retain their historical
  // text path.
  char text_rendering[16] = {};
  if (GetEnvironmentVariableA("SOUI_TEXT_RENDERING", text_rendering, sizeof(text_rendering)) == 0)
    SetEnvironmentVariableA("SOUI_TEXT_RENDERING", "crisp");
  // The GPU surface is an opaque horizontal desktop target. Tell Skia which
  // LCD order it should use so ClearType is not silently downgraded to
  // grayscale when the page is composited through the WGL presenter. Hosts
  // attached to a BGR or non-LCD panel can override this with `bgr` or
  // `unknown` before launching the executable.
  if (GetEnvironmentVariableA("SOUI_GPU_LCD_TEXT", nullptr, 0) == 0)
    SetEnvironmentVariableA("SOUI_GPU_LCD_TEXT", "rgb");
  if ((gpu_renderer || gpu_readback || auto_renderer) && !software_renderer) {
    SetEnvironmentVariableA("SOUI_GPU_COMPOSITOR", "1");
    // Auto mode is GPU-first: use the direct WGL/Ganesh presenter when a real
    // adapter is available and let the source compositor reject software GL.
    // The explicit readback switch remains for diagnostics and hosts that
    // need a CPU-visible dirty bitmap.
    const bool direct_gpu_present = gpu_renderer || (auto_renderer && !gpu_readback);
    SetEnvironmentVariableA("SOUI_GPU_DIRECT_PRESENT", direct_gpu_present ? "1" : "0");
    char trace_option[MAX_PATH] = {};
    if (GetEnvironmentVariableA("SOUI_GPU_PRESENT_TRACE", trace_option, sizeof(trace_option)) == 0) {
      const auto trace_path = output_root / "gpu-present-trace.txt";
      SetEnvironmentVariableA("SOUI_GPU_PRESENT_TRACE", trace_path.string().c_str());
    }
  } else if (software_renderer) {
    // A caller can launch from a shell that already exports the GPU
    // variables (the commercial launcher does this for auto mode). Explicit
    // CPU mode must clear them so the fallback is deterministic.
    SetEnvironmentVariableA("SOUI_GPU_COMPOSITOR", nullptr);
    SetEnvironmentVariableA("SOUI_GPU_DIRECT_PRESENT", nullptr);
    SetEnvironmentVariableA("SOUI_GPU_DIAGNOSTIC_READBACK", nullptr);
    SetEnvironmentVariableA("SOUI_GPU_ACTIVE", nullptr);
  }
  SetEnvironmentVariableA("SOUI_CPU_HIGH_REFRESH", cpu_high_refresh ? "1" : nullptr);
  const bool webview_smoke_test =
      std::find(args.begin(), args.end(), L"--webview-smoke-test") != args.end();
  const bool allow_repository_fallback =
      std::find(args.begin(), args.end(), L"--allow-repository-fallback") != args.end();
  const auto smoke_log = output_root / "webview-smoke.log";
  if (webview_smoke_test) {
    std::error_code ignored;
    std::filesystem::remove(smoke_log, ignored);
    append_smoke_log(smoke_log, "process-start");
  }
#ifdef SOUI_BRIDGE_BENCHMARK
  diagnostic_timer_policy timer_policy;
  if (std::find(args.begin(), args.end(), L"--honor-timer-resolution-experiment") != args.end()) {
    if (!timer_policy.enable()) {
      append_smoke_log(smoke_log,"timer-resolution-policy-unsupported");
      return 3;
    }
    append_smoke_log(smoke_log,"timer-resolution-policy-applied");
  }
#endif
  const auto packaged_miniblink = output_root / "node.dll";
  const auto repository_miniblink = kit_root / "third_party" / "miniblink" /
                                    (architecture == "x64" ? "x64" : "x86") / "node.dll";
  const auto default_miniblink = allow_repository_fallback &&
                                         !std::filesystem::is_regular_file(packaged_miniblink)
                                     ? repository_miniblink
                                     : packaged_miniblink;
  const auto packaged_resources = output_root / "resources";
  const auto repository_resources = kit_root / "web" / "dist";
  const auto default_resources = allow_repository_fallback &&
                                         !std::filesystem::is_regular_file(packaged_resources / "index.html")
                                     ? repository_resources
                                     : packaged_resources;
  const auto miniblink_option = option_value(args, L"--miniblink");
  const auto resources_option = option_value(args, L"--resources");
  const auto miniblink = miniblink_option.empty() ? default_miniblink : std::filesystem::path{miniblink_option};
  auto resources = resources_option.empty() ? default_resources : std::filesystem::path{resources_option};
  const auto entry_option = option_value(args, L"--entry");
  if (!entry_option.empty()) {
    const auto entry_narrow = std::filesystem::path{entry_option}.string();
    SetEnvironmentVariableA("SOUI_ENTRY_FILE", entry_narrow.c_str());
  } else {
    SetEnvironmentVariableA("SOUI_ENTRY_FILE", nullptr);
  }
  SetEnvironmentVariableA("SOUI_REACT_MODE",
                          std::find(args.begin(), args.end(), L"--react") != args.end() ? "1" : nullptr);

  if (std::find(args.begin(), args.end(), L"--self-test") != args.end()) {
    return run_self_test(kit_root, output_root, architecture, miniblink, resources) ? 0 : 1;
  }
  if (std::find(args.begin(), args.end(), L"--task-service-self-test") != args.end()) {
    return run_public_task_self_test() ? 0 : 1;
  }

  const auto repository_renderer = kit_root / "third_party" / "soui" /
                                   (architecture == "x64" ? "x64" : "x86") / "bin";
  const auto renderer_path = allow_repository_fallback &&
                                     !std::filesystem::is_regular_file(output_root / "soui.dll")
                                 ? repository_renderer
                                 : output_root;
  SComMgr components;
  const bool native_controls = std::find(args.begin(), args.end(), L"--native-controls") != args.end();
  native_controls_resources native_resources;
  components.SetDllPath(SOUI::SStringT(renderer_path.wstring().c_str()));
  SOUI::CAutoRefPtr<SOUI::IRenderFactory> render_factory;
  if (!components.CreateRender_GDI((IObjRef**)&render_factory)) {
    (void)resident_write(resident_log, "startup.error", {{"code", "E_SOUI_RENDERER"}, {"exitCode", 1}});
    if (!resident_smoke)
      MessageBoxW(nullptr, L"SOUI GDI renderer could not be loaded.", L"SOUI Miniblink AI DevKit", MB_ICONERROR);
    return 1;
  }
  SOUI::SApplication application(render_factory, instance, _T("SOUIWEBDEMO"));
  if (native_controls) {
    SOUI::CAutoRefPtr<SOUI::IImgDecoderFactory> decoder;
    if (!components.CreateImgDecoder((IObjRef**)&decoder)) return 1;
    render_factory->SetImgDecoderFactory(decoder);
    native_resources.module = LoadLibraryW((renderer_path / L"soui-sys-resource.dll").c_str());
    SOUI::CAutoRefPtr<SOUI::IResProvider> system_resources;
    SOUI::CreateResProvider(SOUI::RES_PE, reinterpret_cast<IObjRef**>(&system_resources));
    if (!native_resources.module || !system_resources ||
        !system_resources->Init(reinterpret_cast<WPARAM>(native_resources.module), 0)) return 1;
    application.LoadSystemNamedResource(system_resources);
  }
  application.SetAppDir(SOUI::SStringT(kit_root.wstring().c_str()));
  SOUI::CAutoRefPtr<SOUI::IResProvider> ui_resources;
  SOUI::CreateResProvider(SOUI::RES_PE, reinterpret_cast<IObjRef**>(&ui_resources));
  if (!ui_resources || !ui_resources->Init(reinterpret_cast<WPARAM>(instance), 0)) {
    (void)resident_write(resident_log, "startup.error", {{"code", "E_SOUI_RESOURCES"}, {"exitCode", 1}});
    if (!resident_smoke)
      MessageBoxW(nullptr, L"SOUI UI definitions could not be initialized.", L"SOUI Miniblink AI DevKit", MB_ICONERROR);
    return 1;
  }
  application.AddResProvider(ui_resources);

  if (resident) {
    resident_settings settings;
    settings.host.architecture = architecture;
    settings.host.miniblink_binary = miniblink;
    settings.host.resource_root = resources;
    settings.application_root = output_root;
    settings.width = option_dimension(args, L"--width", 1100);
    settings.height = option_dimension(args, L"--height", 720);
    settings.auto_high_refresh = cpu_high_refresh || (!software_renderer && (gpu_renderer || gpu_readback || auto_renderer));
    settings.smoke = resident_smoke;
    resident_controller controller(application, std::move(settings), resident_log);
    return controller.run();
  }

  soui::demo::demo_bridge_adapter bridge;
  auto adapter = use_local_resource_origin
      ? std::make_unique<soui::sdk::dynamic_miniblink_adapter>(
          soui::sdk::diagnostics::mode::disabled, soui::sdk::miniblink_local_resource_options{})
      : std::make_unique<soui::sdk::dynamic_miniblink_adapter>();
  soui::sdk::soui_miniblink_host host(std::move(adapter));
  bool background_pacing_experiment = false;
  bool prefer_high_resolution = true;
#ifdef SOUI_BRIDGE_BENCHMARK
  background_pacing_experiment = std::find(args.begin(), args.end(), L"--background-pacing-experiment") != args.end();
  prefer_high_resolution = std::find(args.begin(), args.end(), L"--legacy-frame-clock") == args.end();
#endif
  const bool auto_high_refresh = cpu_high_refresh || (!software_renderer && (gpu_renderer || gpu_readback || auto_renderer));
  native_controls_demo native_panel;
  demo_soui_window window(host, bridge, webview_smoke_test, smoke_log, background_pacing_experiment,
                          prefer_high_resolution, auto_high_refresh, native_controls);
  try {
#ifdef SOUI_BRIDGE_BENCHMARK
  const auto gallery_benchmark=option_value(args,L"--gallery-benchmark");
  if(gallery_benchmark==L"first" || gallery_benchmark==L"all") window.set_gallery_benchmark(gallery_benchmark==L"all");
#endif
  // Keep the initial resize border inside the primary monitor's work area,
  // including when a taskbar makes the full display bounds unavailable.
  // Explicit dimensions remain the requested maximum when the work area fits.
  const int window_width = option_dimension(args, L"--width", 1100);
  const int window_height = option_dimension(args, L"--height", 720);
  const auto initial = initial_window_on_primary_work_area(window_width, window_height);
  const HWND handle = window.Create(nullptr, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0,
                                    initial ? initial->left : CW_USEDEFAULT,
                                    initial ? initial->top : CW_USEDEFAULT,
                                    initial ? initial->width : window_width,
                                    initial ? initial->height : window_height);
  if (handle == nullptr) {
    MessageBoxW(nullptr, L"SOUI SHostWnd could not be created.", L"SOUI Miniblink AI DevKit", MB_ICONERROR);
    return 1;
  }

  soui::sdk::host_config config;
  config.architecture = architecture;
  config.miniblink_binary = miniblink;
  config.resource_root = resources;
  config.owner_window = handle;
  config.bridge_adapter = &bridge;
  window.configure_native_devtools(application.GetMsgLoop(), output_root);
#ifdef SOUI_BRIDGE_BENCHMARK
  config.benchmark_dataset = option_value(args, L"--benchmark-dataset");
  config.benchmark_output_directory = output_root;
  config.noopt_experiment = std::find(args.begin(),args.end(),L"--v8-noopt-experiment")!=args.end();
  config.managed_frame_timing = std::find(args.begin(), args.end(), L"--unmanaged-frame-timing") == args.end();
  if (std::find(args.begin(), args.end(), L"--zero-engine-timing-experiment") != args.end()) config.engine_timing_experiment = 3;
  const auto timing_experiment = option_value(args,L"--engine-timing-experiment");
  if(timing_experiment==L"wake") config.engine_timing_experiment=1;
  else if(timing_experiment==L"draw") config.engine_timing_experiment=2;
  else if(timing_experiment==L"both") config.engine_timing_experiment=3;
#endif
  const auto started = host.start(config);
  if (!started.ok) {
    append_smoke_log(smoke_log, started.code.c_str());
    if (!webview_smoke_test)
      MessageBoxA(handle, started.message.c_str(), started.code.c_str(), MB_ICONERROR);
    window.begin_shutdown(1);
    const auto loop_result = ::IsWindow(handle) ? application.GetMsgLoop()->Run() : 1;
    return window.finish_shutdown_before_return(application, loop_result);
  }
  append_smoke_log(smoke_log, "host-started");
  if (native_controls && !native_panel.open(handle))
    throw std::runtime_error("Cannot create the native SOUI control panel");
  ::SetWindowTextW(handle, L"SOUI + Miniblink AI DevKit");
#ifdef SOUI_TEST_KIT
  const bool test_kit_hidden = soui::test_kit::enabled();
  window.ShowWindow(test_kit_hidden ? SW_HIDE : SW_SHOW);
#else
  window.ShowWindow(SW_SHOW);
#endif
  window.UpdateWindow();
  // A custom WebView titlebar must receive the first real mouse gesture. Make
  // the freshly launched desktop window active once, then leave focus and
  // subsequent activation to normal Windows policy.
#ifdef SOUI_TEST_KIT
  if (!test_kit_hidden)
#endif
  {
    ::SetForegroundWindow(handle);
    ::SetFocus(handle);
  }
#ifdef SOUI_BRIDGE_BENCHMARK
  append_smoke_log(smoke_log, "window-show-update-returned");
#endif
#ifndef SOUI_PUMP_INTERVAL_MS
#define SOUI_PUMP_INTERVAL_MS 16
#endif
  if (!window.SetTimer(1, SOUI_PUMP_INTERVAL_MS))
    throw std::runtime_error("Cannot start the Host timer");
  OutputDebugStringW(L"SOUI Miniblink AI DevKit: ready, local WebView mounted.\n");
  const auto loop_result = application.GetMsgLoop()->Run();
  return window.finish_shutdown_before_return(application, loop_result);
  } catch (...) {
    try { append_smoke_log(smoke_log, "E_APPLICATION_EXCEPTION"); } catch (...) { }
    return window.finish_shutdown_before_return(application, 1);
  }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  int argc = 0;
  LPWSTR* raw_args = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::wstring> args;
  if (raw_args != nullptr) {
    args.assign(raw_args + 1, raw_args + argc);
    LocalFree(raw_args);
  }
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  resident_run_context resident_log;
  const int result = run_gui(instance, args, resident_log);
  const auto shutdown = soui::sdk::wait_for_miniblink_runtime_shutdown(std::chrono::seconds{20});
  (void)resident_write(resident_log, "runtime.shutdown-fallback",
      {{"ok", shutdown.ok}, {"code", shutdown.code}, {"message", shutdown.message}});
  if (SUCCEEDED(com_result)) CoUninitialize();
  const int exit_code = result == 0 && !shutdown.ok ? 7 : result;
  (void)resident_write(resident_log, "process.finished", {{"exitCode", exit_code},
      {"evidenceWriteFailed", resident_log.log_failed}});
  return exit_code == 0 && resident_log.log_failed ? 1 : exit_code;
}

#else

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
  MessageBoxW(nullptr,
              L"SOUI SDK headers are incomplete. Install the bundled SOUI SDK before running the Demo.",
              L"SOUI Miniblink AI DevKit", MB_ICONERROR);
  return 1;
}

#endif

#else

#include <iostream>

int main() {
  std::cerr << "SOUI + Miniblink Demo requires Windows.\n";
  return 1;
}

#endif
