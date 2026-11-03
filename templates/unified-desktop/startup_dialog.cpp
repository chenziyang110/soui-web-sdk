#include "startup_dialog.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <gtk/gtk.h>
#endif

#if !defined(__APPLE__)
namespace soui_app {
namespace {

constexpr std::size_t maximum_text_bytes = 4096;

bool valid_text(const std::string& text) noexcept {
  if (text.empty() || text.size() > maximum_text_bytes) return false;
  for (const unsigned char byte : text) {
    if (byte != '\n' && (byte < 0x20 || byte > 0x7e)) return false;
  }
  return true;
}

}  // namespace

bool show_startup_error(const std::string& sanitized_text) noexcept {
  if (!valid_text(sanitized_text)) return false;
  try {
#if defined(_WIN32)
    // A service/noninteractive window station cannot display a usable alert.
    // Only inspect the process's existing station; do not switch desktops or
    // acquire broader access. The returned station handle is not ours to close.
    const auto station = GetProcessWindowStation();
    USEROBJECTFLAGS station_flags{};
    DWORD returned_bytes = 0;
    if (!station || !GetUserObjectInformationW(station, UOI_FLAGS, &station_flags,
            sizeof(station_flags), &returned_bytes) ||
        !(station_flags.dwFlags & WSF_VISIBLE)) return false;
    // Sanitized ASCII is a UTF-8 subset. Widen directly into a bounded,
    // NUL-terminated stack buffer without locale-dependent conversion.
    wchar_t message[maximum_text_bytes + 1]{};
    for (std::size_t index = 0; index < sanitized_text.size(); ++index)
      message[index] = static_cast<unsigned char>(sanitized_text[index]);
    return MessageBoxW(nullptr, message, L"Application startup failed",
                       MB_OK | MB_ICONERROR) == IDOK;
#elif defined(__linux__)
    // gtk_init_check does not terminate the process when no display is usable.
    // Do not retry, modify the environment, or run the application's main loop.
    if (!gtk_init_check(nullptr, nullptr)) return false;
    GtkWidget* dialog = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", "Application startup failed");
    if (!dialog) return false;
    gtk_window_set_title(GTK_WINDOW(dialog), "Application startup failed");
    // A literal format string also keeps '%' in the report as plain text.
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
                                            sanitized_text.c_str());
    const auto response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response != GTK_RESPONSE_NONE;
#else
    return false;
#endif
  } catch (...) {
    return false;
  }
}

}  // namespace soui_app
#endif
