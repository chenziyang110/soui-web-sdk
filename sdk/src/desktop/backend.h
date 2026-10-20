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
  std::function<void(result)> error;
  // Renderer-initiated full reloads of the selected document are cancelled
  // first, then queued here. Common code creates a fresh token before a native
  // load. Never allow a new document to inherit an earlier preload/session.
  std::function<void()> reload;
  // Only the current native-selected document (plus a fragment) is admissible.
  std::function<bool(std::string_view uri)> allow_navigation;
};

class backend {
 public:
  virtual ~backend() = default;
  virtual result create(const options&, backend_events) = 0;
  // Called after create, and before run. Deferred async environments must retain
  // the most recent request and never execute an earlier navigation's preload.
  virtual result load(std::string uri, std::string preload) = 0;
  virtual result run() = 0;
  virtual result close() = 0;
  virtual result evaluate(std::string script) = 0;
  // Thread-safe submission; never execute work after close.
  virtual result dispatch(std::function<void()> work) = 0;
  virtual result set_title(std::string_view) = 0;
  virtual result resize(int width, int height) = 0;
  virtual result show(bool) = 0;
  virtual result focus() = 0;
  virtual value_result clipboard_read() = 0;
  virtual result clipboard_write(std::string_view) = 0;
  virtual value_result choose_directory() = 0;
  virtual capabilities get_capabilities() const = 0;
  // GTK's script-message signal has no native sender-frame metadata. Its
  // explicit false policy additionally relies on the top-only closure token.
  virtual bool has_message_frame_metadata() const { return true; }
};

// Exactly one implementation is selected by CMake. The common layer owns it.
std::unique_ptr<backend> make_backend();

}  // namespace soui::desktop::detail
