#pragma once

#include <soui/desktop/app.h>

namespace soui::desktop::detail {

enum class message_frame { unknown, main, subframe };

struct backend_events {
  // Backends inject preload ONLY in the main frame at document start.
  // Unknown frame/origin information is never guessed as a trusted source.
  std::function<void(std::string_view payload, std::string_view source_uri, message_frame)> message;
  std::function<void(std::string_view uri)> loaded;
  std::function<void()> closed;
  // A user/WM request only: common approval decides whether to call close().
  std::function<void()> close_requested;
  std::function<void(window_state)> state_changed;
  std::function<void(result)> error;
  // Renderer-initiated full reloads of the selected document are cancelled
  // first, then queued here. Common code creates a fresh token before a native
  // load. Never allow a new document to inherit an earlier preload/session.
  std::function<void()> reload;
  // Native menu/tray callbacks. Never route commands between backend instances.
  std::function<void(std::string_view command)> command;
  // OS-level application quit, distinct from closing one native window.
  std::function<void()> quit_requested;
  // Only the current native-selected document (plus a fragment) is admissible.
  std::function<bool(std::string_view uri)> allow_navigation;
  // A never-reused registration token, delivered only to its owning window.
  // Defer off the OS event stack through a bounded window-lifetime queue,
  // independent of renderer health. Common code serializes business callbacks.
  std::function<void(std::uint64_t token)> global_shortcut;
};

class backend {
 public:
  virtual ~backend() = default;
  virtual result create(const options&, backend_events) = 0;
  // Called after create, and before run. Deferred async environments must retain
  // the most recent request and never execute an earlier navigation's preload.
  virtual result load(std::string uri, std::string preload) = 0;
  virtual result run() = 0;
  // Closing one window MUST NOT stop the shared UI loop. Common code stops it
  // only after the last window closes or explicit application quit completes.
  // The loop owner may already have a closed window. Both are UI-thread calls.
  virtual result quit_loop() = 0;
  virtual result close() = 0;
  virtual result evaluate(std::string script) = 0;
  // Thread-safe, non-inline submission. Never run work on the submitting worker
  // or while holding the queue mutex; never execute queued work after close.
  virtual result dispatch(std::function<void()> work) = 0;
  virtual result set_title(std::string_view) = 0;
  virtual result resize(int width, int height) = 0;
  virtual result show(bool) = 0;
  virtual result focus() = 0;
  virtual result minimize() { return result::failure("not_supported", "Window minimization is unavailable."); }
  virtual result maximize() { return result::failure("not_supported", "Window maximization is unavailable."); }
  virtual result restore() { return result::failure("not_supported", "Window restoration is unavailable."); }
  virtual window_state_result get_window_state() const {
    return {result::failure("not_supported", "Window state observation is unavailable."), {}};
  }
  virtual value_result clipboard_read() = 0;
  virtual result clipboard_write(std::string_view) = 0;
  virtual value_result choose_directory() = 0;
  virtual file_selection_result choose_files(const open_file_options&) {
    return {result::failure("not_supported", "File open dialogs are unavailable."), {}};
  }
  virtual value_result choose_save_file(const save_file_options&) {
    return {result::failure("not_supported", "File save dialogs are unavailable."), {}};
  }
  virtual result popup_menu(const std::vector<menu_item>&) = 0;
  virtual result set_tray(const tray_options&) = 0;
  virtual result clear_tray() = 0;
  // A failed registration must leave no native resource, except the explicit
  // shortcut_cleanup_pending status: retain that token inactive for unregister
  // retry. Common code keeps its logical ID and capacity slot reserved.
  virtual result register_global_shortcut(std::uint64_t, const global_shortcut&) {
    return result::failure("not_supported", "Global shortcuts are unavailable.");
  }
  virtual result unregister_global_shortcut(std::uint64_t) {
    return result::failure("not_supported", "Global shortcuts are unavailable.");
  }
  virtual capabilities get_capabilities() const = 0;
  // GTK's script-message signal has no native sender-frame metadata. Its
  // explicit false policy additionally relies on the top-only closure token.
  virtual bool has_message_frame_metadata() const { return true; }
};

// Exactly one implementation is selected by CMake. The common layer owns it.
std::unique_ptr<backend> make_backend();

}  // namespace soui::desktop::detail
