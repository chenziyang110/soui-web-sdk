#pragma once

#include <string>

namespace soui_app {

// Template-only, best-effort presentation. Call once on the startup/UI thread,
// after the application has been destroyed, and never from --diagnose.
// Input must already be sanitized printable ASCII (plus newlines), at most
// 4096 bytes, with no embedded NUL. Invalid input never reaches native UI.
// The native alert is modal until dismissed. False means presentation failed;
// the caller must retain the stderr report regardless of this result.
bool show_startup_error(const std::string& sanitized_text) noexcept;

}  // namespace soui_app
