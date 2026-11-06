#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace soui::benchmark::coordinate_diagnostics {

// Cold, read-only observations only. Missing optional exports are explicit nulls.
// No method of this class is called inside the injection loop.
class environment final {
 public:
  static constexpr std::size_t maximum_line_bytes = 4096;
  void bind(DWORD pid, std::uint64_t birth, const std::string& run) {
    target_pid_ = pid; expected_birth_ = birth; run_ = run;
  }
  void target_birth(std::uint64_t value) noexcept { actual_birth_ = value; }
  void window(HWND value) noexcept { window_ = value; }
  void setter(BOOL returned, DWORD immediate_error) noexcept {
    setter_attempted_ = true; setter_returned_ = returned != FALSE; setter_error_ = immediate_error;
  }
  void origin(POINT value) noexcept { origin_ = value; origin_known_ = true; }
  void virtual_screen(int x, int y, int width, int height) noexcept {
    vx_ = x; vy_ = y; vw_ = width; vh_ = height; virtual_known_ = true;
  }
  void capture_optional() noexcept {
    if (captured_) return;
    captured_ = true; begin_ = clock(); thread_id_ = GetCurrentThreadId(); injector_pid_ = GetCurrentProcessId();
    FILETIME c{}, e{}, k{}, u{};
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
      injector_birth_ = (static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    HMODULE user = GetModuleHandleW(L"user32.dll"); user32_available_ = user != nullptr;
    const auto thread_context = load<DPI_AWARENESS_CONTEXT(WINAPI*)()>(user, "GetThreadDpiAwarenessContext");
    const auto window_context = load<DPI_AWARENESS_CONTEXT(WINAPI*)(HWND)>(user, "GetWindowDpiAwarenessContext");
    const auto awareness = load<DPI_AWARENESS(WINAPI*)(DPI_AWARENESS_CONTEXT)>(user, "GetAwarenessFromDpiAwarenessContext");
    const auto window_dpi = load<UINT(WINAPI*)(HWND)>(user, "GetDpiForWindow");
    const auto system_dpi = load<UINT(WINAPI*)()>(user, "GetDpiForSystem");
    const auto process_aware = load<BOOL(WINAPI*)()>(user, "IsProcessDPIAware");
    thread_context_.available = thread_context != nullptr;
    window_context_.available = window_context != nullptr;
    thread_context_.awareness_available = window_context_.awareness_available = awareness != nullptr;
    if (thread_context) thread_context_.raw = thread_context();
    if (window_context && window_) window_context_.raw = window_context(window_);
    if (awareness && thread_context_.raw) thread_context_.awareness = awareness(thread_context_.raw);
    if (awareness && window_context_.raw) window_context_.awareness = awareness(window_context_.raw);
    window_dpi_available_ = window_dpi != nullptr; system_dpi_available_ = system_dpi != nullptr;
    process_aware_available_ = process_aware != nullptr;
    if (window_dpi && window_) window_dpi_ = window_dpi(window_);
    if (system_dpi) system_dpi_ = system_dpi();
    if (process_aware) process_aware_ = process_aware() != FALSE;
    primary_width_ = GetSystemMetrics(SM_CXSCREEN); primary_height_ = GetSystemMetrics(SM_CYSCREEN);
    if (window_) client_known_ = GetClientRect(window_, &client_) != FALSE;
    end_ = clock();
  }
  void emit_once() noexcept {
    if (emitted_) return;
    emitted_ = true;
    try {
      const auto b = [](bool value) { return value ? "true" : "false"; };
      const auto integer = [](bool known, std::int64_t value) { return known ? std::to_string(value) : std::string("null"); };
      const auto decimal = [](std::uint64_t value) { return value ? ('"' + std::to_string(value) + '"') : std::string("null"); };
      std::string text = "{\"schema\":\"soui.input-coordinate-environment/v1\",\"maximumLineBytes\":4096,\"run_id\":";
      text += run_.size() == 32 ? ('"' + run_ + '"') : "null";
      text += ",\"injector\":{\"pid\":" + integer(injector_pid_ != 0, injector_pid_) + ",\"creationFileTime\":" + decimal(injector_birth_) +
        ",\"thread_id\":" + integer(thread_id_ != 0, thread_id_) + "},\"target\":{\"pid\":" + integer(target_pid_ != 0, target_pid_) +
        ",\"expectedCreationFileTime\":" + decimal(expected_birth_) + ",\"actualCreationFileTime\":" + decimal(actual_birth_) +
        ",\"hwnd\":" + decimal(reinterpret_cast<std::uintptr_t>(window_)) + "}";
      text += ",\"existingSetProcessDPIAware\":{\"attempted\":" + std::string(b(setter_attempted_)) + ",\"returned\":" +
        (setter_attempted_ ? std::string(b(setter_returned_)) : "null") + ",\"immediateLastError\":" + integer(setter_attempted_, setter_error_) +
        ",\"lastErrorIsNotFinalAwarenessProof\":true}";
      text += ",\"awareness\":{\"captureStage\":\"" + std::string(setter_attempted_ ? "after-existing-setter" : "preflight-failure-before-setter") +
        "\",\"user32Available\":" + b(user32_available_) + ",\"thread\":" + context_json(thread_context_) + ",\"window\":" + context_json(window_context_) +
        ",\"GetDpiForWindow\":{\"available\":" + b(window_dpi_available_) + ",\"value\":" + integer(window_dpi_ != 0, window_dpi_) +
        "},\"GetDpiForSystem\":{\"available\":" + b(system_dpi_available_) + ",\"value\":" + integer(system_dpi_ != 0, system_dpi_) +
        "},\"IsProcessDPIAware\":{\"available\":" + b(process_aware_available_) + ",\"value\":" + (process_aware_available_ ? b(process_aware_) : "null") + "}}";
      text += ",\"metrics\":{\"virtualUsedForNormalization\":{\"known\":" + std::string(b(virtual_known_)) + ",\"x\":" + integer(virtual_known_, vx_) +
        ",\"y\":" + integer(virtual_known_, vy_) + ",\"width\":" + integer(virtual_known_, vw_) + ",\"height\":" + integer(virtual_known_, vh_) +
        "},\"primary\":{\"width\":" + integer(captured_, primary_width_) + ",\"height\":" + integer(captured_, primary_height_) + "}}";
      text += ",\"geometry\":{\"existingClientToScreenOrigin\":{\"known\":" + std::string(b(origin_known_)) + ",\"x\":" + integer(origin_known_, origin_.x) +
        ",\"y\":" + integer(origin_known_, origin_.y) + "},\"clientRect\":{\"known\":" + b(client_known_) + ",\"left\":" + integer(client_known_, client_.left) +
        ",\"top\":" + integer(client_known_, client_.top) + ",\"right\":" + integer(client_known_, client_.right) + ",\"bottom\":" + integer(client_known_, client_.bottom) + "}}";
      text += ",\"captureBeginQpc\":" + decimal(begin_) + ",\"captureEndQpc\":" + decimal(end_) +
        ",\"readOnly\":true,\"coordinateMappingValidated\":false,\"performanceGate\":false}";
      if (text.size() + 1 <= maximum_line_bytes) std::cout << text << '\n';
      else std::cout << "{\"schema\":\"soui.input-coordinate-environment/v1\",\"captureError\":\"bounded-json-overflow\",\"coordinateMappingValidated\":false}\n";
      std::cout.flush();
    } catch (...) {
      // Preserve the original input/preflight result even if metadata allocation fails.
      try { std::cout << "{\"schema\":\"soui.input-coordinate-environment/v1\",\"captureError\":\"metadata-unavailable\",\"coordinateMappingValidated\":false}\n"; } catch (...) {}
    }
  }

 private:
  struct context final {
    bool available{false}, awareness_available{false};
    DPI_AWARENESS_CONTEXT raw{nullptr}; DPI_AWARENESS awareness{DPI_AWARENESS_INVALID};
  };
  template<class Function> static Function load(HMODULE module, const char* name) noexcept {
    return module ? reinterpret_cast<Function>(GetProcAddress(module, name)) : nullptr;
  }
  static std::uint64_t clock() noexcept {
    LARGE_INTEGER value{}; return QueryPerformanceCounter(&value) && value.QuadPart > 0 ? static_cast<std::uint64_t>(value.QuadPart) : 0;
  }
  static std::string context_json(const context& value) {
    const auto bits = reinterpret_cast<std::uintptr_t>(value.raw);
    return std::string("{\"available\":") + (value.available ? "true" : "false") + ",\"rawContext\":" +
      (value.raw ? ('"' + std::to_string(bits) + '"') : std::string("null")) + ",\"awarenessQueryAvailable\":" +
      (value.awareness_available ? "true" : "false") + ",\"awareness\":" +
      (value.raw && value.awareness != DPI_AWARENESS_INVALID ? std::to_string(static_cast<int>(value.awareness)) : std::string("null")) + "}";
  }
  std::string run_;
  DWORD target_pid_{}, injector_pid_{}, thread_id_{}, setter_error_{};
  std::uint64_t expected_birth_{}, actual_birth_{}, injector_birth_{}, begin_{}, end_{};
  HWND window_{}; POINT origin_{}; RECT client_{}; context thread_context_, window_context_;
  int vx_{}, vy_{}, vw_{}, vh_{}, primary_width_{}, primary_height_{};
  UINT window_dpi_{}, system_dpi_{};
  bool setter_attempted_{false}, setter_returned_{false}, captured_{false}, emitted_{false}, user32_available_{false};
  bool origin_known_{false}, virtual_known_{false}, client_known_{false};
  bool window_dpi_available_{false}, system_dpi_available_{false}, process_aware_available_{false}, process_aware_{false};
};
} // namespace soui::benchmark::coordinate_diagnostics
