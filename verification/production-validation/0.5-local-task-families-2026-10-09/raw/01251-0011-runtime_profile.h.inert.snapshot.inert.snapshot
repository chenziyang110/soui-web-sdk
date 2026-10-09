#pragma once

#include <string>

namespace soui::sdk {

enum class runtime_profile { automatic, compatibility, modern, pe };
enum class renderer_backend { none, miniblink, webview2 };

// Observations, not a certification of the current OS/image/driver combination.
struct runtime_environment {
  bool os_version_known{false};
  unsigned os_major{0}, os_minor{0}, os_build{0};
  bool pe_known{false}, windows_pe{false};
  bool remote_session{false};
  std::string process_architecture;
  std::string native_architecture;
};

struct renderer_availability {
  bool miniblink{false};
  // Set only after the optional backend probes its loader/runtime. This is
  // not a promise that controller creation or required interfaces will work.
  bool webview2{false};
};

struct runtime_selection {
  bool ok{false};
  runtime_profile profile{runtime_profile::automatic};
  renderer_backend backend{renderer_backend::none};
  bool software_presentation{true};
  std::string code;
  std::string reason;
};

[[nodiscard]] runtime_environment inspect_runtime_environment();
[[nodiscard]] runtime_selection select_runtime_profile(
    const runtime_environment& environment, renderer_availability available,
    runtime_profile requested, bool requires_modern_web = false);
[[nodiscard]] const char* profile_name(runtime_profile value) noexcept;
[[nodiscard]] const char* backend_name(renderer_backend value) noexcept;

}  // namespace soui::sdk
