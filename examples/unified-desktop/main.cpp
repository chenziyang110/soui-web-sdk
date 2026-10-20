#include <soui/desktop/app.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// This application contains no platform headers, #ifdefs or engine APIs.
namespace {
std::string json_string(std::string_view text) {
  constexpr char digits[] = "0123456789abcdef";
  std::string output{"\""};
  for (const unsigned char value : text) {
    if (value == '\\' || value == '"') { output += '\\'; output += static_cast<char>(value); }
    else if (value < 32) { output += "\\u00"; output += digits[value >> 4U]; output += digits[value & 15U]; }
    else output += static_cast<char>(value);
  }
  return output + '"';
}
std::string checked(soui::desktop::result status) {
  if (!status.ok) throw std::runtime_error(status.code + ": " + status.message);
  return "true";
}
std::string native_result(const soui::desktop::result& status) {
  return std::string("{\"ok\":") + (status.ok ? "true" : "false") +
    ",\"code\":" + json_string(status.code) + ",\"message\":" + json_string(status.message) + "}";
}
struct window_context {
  soui::desktop::application* window{nullptr}; // Borrowed; never owns its callbacks.
};
struct example_state {
  soui::desktop::options settings;
  // Retain handles until run() returns, never destroy a window in a callback.
  // The example bounds retained handles separately from the SDK's live limit.
  std::vector<std::shared_ptr<soui::desktop::application>> children;
};
std::vector<soui::desktop::menu_item> native_menu() {
  return {{"window.focus", "Show this window"}, {"window.close", "Close this window"},
    {"", "", true, false, true}, {"app.quit", "Quit all windows"}};
}
soui::desktop::callbacks make_callbacks(example_state& state,
                                       std::shared_ptr<window_context> context) {
  soui::desktop::callbacks callbacks;
  // This is the explicit method allowlist. Do not forward arbitrary page input
  // to shell/file/network APIs. Runtime protected methods still need permissions.
  callbacks.invoke = [&state, context](std::string_view method, std::string_view parameters) -> std::string {
    auto* window = context->window;
    if (!window) throw std::runtime_error("Window initialization is not complete");
    if (method == "app.echo") return std::string(parameters);
    if (method == "app.capabilities") {
      const auto caps = window->get_capabilities();
      return "{\"backend\":" + json_string(caps.backend) +
        ",\"clipboardRead\":" + (caps.clipboard_read ? "true" : "false") +
        ",\"clipboardWrite\":" + (caps.clipboard_write ? "true" : "false") +
        ",\"directoryDialog\":" + (caps.directory_dialog ? "true" : "false") +
        ",\"popupMenu\":" + (caps.popup_menu ? "true" : "false") +
        ",\"tray\":" + (caps.tray ? "true" : "false") + "}";
    }
    if (method == "window.title") return checked(window->set_title("SOUI · native title changed"));
    if (method == "window.resize") return checked(window->resize(1100, 760));
    if (method == "clipboard.write") return checked(window->clipboard_write("Hello from one SOUI application"));
    if (method == "clipboard.read") {
      const auto value = window->clipboard_read();
      (void)checked(value.status);
      return json_string(value.value);
    }
    if (method == "directory.choose") {
      const auto value = window->choose_directory();
      (void)checked(value.status);
      return json_string(value.value);
    }
    if (method == "app.reload") {
      return checked(window->dispatch([context] { (void)context->window->reload(); }));
    }
    if (method == "window.create") {
      if (state.children.size() >= 31)
        return native_result(soui::desktop::result::failure(
          "example_handle_limit", "Restart the example after creating 31 child windows."));
      auto child_context = std::make_shared<window_context>();
      auto settings = state.settings;
      settings.title = "SOUI · independent child window";
      auto child = window->create_window(std::move(settings), make_callbacks(state, child_context));
      if (!child.status.ok) return native_result(child.status);
      child_context->window = child.window.get();
      state.children.push_back(std::move(child.window));
      return "{\"ok\":true,\"message\":\"New window has its own Bridge session\"}";
    }
    if (method == "window.menu") return native_result(window->popup_menu(native_menu()));
    if (method == "window.tray") {
      soui::desktop::tray_options tray;
      tray.tooltip = "SOUI · this window";
      tray.menu = native_menu();
      // Backend support does not imply this desktop has a compatible tray host.
      return native_result(window->set_tray(tray));
    }
    if (method == "window.clearTray") return native_result(window->clear_tray());
    if (method == "window.close")
      return checked(window->dispatch([context] { (void)context->window->close(); }));
    if (method == "app.quit")
      return checked(window->dispatch([context] { (void)context->window->quit(); }));
    throw std::runtime_error("Method is not allowed by this application");
  };
  callbacks.ready = [context] {
    if (context->window)
      (void)context->window->emit("native.ready", "{\"message\":\"Native → Web events are connected\"}");
  };
  callbacks.command = [context](std::string_view command) {
    auto* window = context->window;
    if (!window) return;
    (void)window->emit("native.command", json_string(command));
    if (command == "window.focus") { (void)window->show(true); (void)window->focus(); }
    else if (command == "window.close") (void)window->close();
    else if (command == "app.quit") (void)window->quit();
  };
  callbacks.error = [](const soui::desktop::result& status) {
    std::cerr << status.code << ": " << status.message << '\n';
  };
  return callbacks;
}
}

int main(int argc, char** argv) {
  example_state state;
  state.settings.resource_root = argc > 1 ? std::filesystem::u8path(argv[1]) :
    std::filesystem::absolute(std::filesystem::u8path(argv[0])).parent_path() / "web";
  state.settings.title = "SOUI · one application";
  state.children.reserve(31); // No handle-storage allocation after native create.
  auto context = std::make_shared<window_context>();
  soui::desktop::application app(state.settings, make_callbacks(state, context));
  context->window = &app;
  const auto created = app.initialize();
  if (!created.ok) { std::cerr << created.code << ": " << created.message << '\n'; return 1; }
  const auto run = app.run();
  (void)app.quit();
  state.children.clear(); // The event loop and all business callbacks have returned.
  if (!run.ok) { std::cerr << run.code << ": " << run.message << '\n'; return 2; }
  return 0;
}
