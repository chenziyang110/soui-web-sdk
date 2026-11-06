#pragma once
#include <stdexcept>
#include <string>

namespace soui::benchmark {
// Diagnostic interventions are explicit and never qualify as formal samples.
struct harness_options final {
  bool suppress_focus{false}, borderless{false}, foreground{false}, logging{false};
  bool diagnostic() const noexcept { return suppress_focus || borderless || foreground || logging; }
  bool consume(const std::wstring& argument) {
    bool* selected = nullptr;
    if (argument == L"--diagnostic-suppress-focus") selected = &suppress_focus;
    else if (argument == L"--diagnostic-borderless") selected = &borderless;
    else if (argument == L"--diagnostic-foreground") selected = &foreground;
    else if (argument == L"--diagnostic-log") selected = &logging;
    if (!selected) return false;
    if (*selected) throw std::invalid_argument("Duplicate harness diagnostic option");
    *selected = true;
    return true;
  }
  std::string json() const {
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    return std::string("{\"suppress_focus\":") + boolean(suppress_focus) +
        ",\"borderless\":" + boolean(borderless) + ",\"foreground\":" + boolean(foreground) +
        ",\"logging\":" + boolean(logging) + "}";
  }
};
} // namespace soui::benchmark
