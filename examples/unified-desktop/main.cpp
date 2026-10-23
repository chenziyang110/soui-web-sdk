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
  bool close_protected{false}; // This window's current document, not group state.
};
struct example_state {
  soui::desktop::options settings;
  // Retain handles until run() returns, never destroy a window in a callback.
  // The example bounds retained handles separately from the SDK's live limit.
  std::vector<std::shared_ptr<soui::desktop::application>> children;
};
std::vector<soui::desktop::menu_item> native_menu() {
  // Parents have no command ID; disabled groups disable every descendant.
  return {{"", "This window", true, false, false, {
      {"window.focus", "Show this window"},
      {"", "Close options", true, false, false, {
        {"window.close", "Close this window (checks unsaved changes)"}}}}},
    {"", "Unavailable workspace actions", false, false, false, {
      {"workspace.publish", "Publish workspace"},
      {"", "Maintenance", true, false, false, {
        {"workspace.reset", "Reset workspace"}}}}},
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
        ",\"clipboardRead\":" + (caps.clipboard_read && state.settings.services.clipboard_read ? "true" : "false") +
        ",\"clipboardWrite\":" + (caps.clipboard_write && state.settings.services.clipboard_write ? "true" : "false") +
        ",\"directoryDialog\":" + (caps.directory_dialog && state.settings.services.directory_dialog ? "true" : "false") +
        ",\"popupMenu\":" + (caps.popup_menu ? "true" : "false") +
        ",\"tray\":" + (caps.tray ? "true" : "false") +
        ",\"globalShortcuts\":" + (caps.global_shortcuts ? "true" : "false") +
        ",\"resourceServer\":" + (state.settings.enable_resource_server ? "true" : "false") + "}";
    }
    // Window properties, clipboard and directory selection now use the SDK's
    // desktop.* services. Only example-specific business methods belong here.
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
    // Fixed, host-owned business binding only. Nothing registers at startup.
    if (method == "window.registerRevealShortcut") {
      return native_result(window->register_global_shortcut("window.reveal",
        {soui::desktop::shortcut_key::f9,
         soui::desktop::shortcut_modifiers::primary | soui::desktop::shortcut_modifiers::shift}));
    }
    if (method == "window.unregisterRevealShortcut")
      return native_result(window->unregister_global_shortcut("window.reveal"));
    if (method == "window.setCloseProtection") {
      // This example business method accepts a JSON boolean, not truthy strings
      // or arbitrary objects. It does not grant a standard SDK close permission.
      const auto first = parameters.find_first_not_of(" \t\r\n");
      const auto last = parameters.find_last_not_of(" \t\r\n");
      const auto value = first == std::string_view::npos ? std::string_view{} :
        parameters.substr(first, last - first + 1);
      if (value != "true" && value != "false")
        throw std::runtime_error("window.setCloseProtection requires a JSON boolean");
      context->close_protected = value == "true";
      return context->close_protected ? "true" : "false";
    }
    if (method == "window.close")
      return checked(window->request_close());
    if (method == "app.quit")
      return checked(window->request_quit());
    throw std::runtime_error("Method is not allowed by this application");
  };
  callbacks.ready = [context] {
    // Reload/navigation starts a new document with an unchecked UI. Never carry
    // the old document's business protection flag into that new session.
    context->close_protected = false;
    if (context->window)
      (void)context->window->emit("native.ready", "{\"message\":\"Native → Web events are connected\"}");
  };
  callbacks.close_requested = [context](soui::desktop::window_id) {
    if (!context->close_protected) return true;
    if (context->window)
      (void)context->window->emit("app.closeBlocked",
        "{\"message\":\"This window has simulated unsaved changes. Uncheck close protection here, then try closing or quitting again.\"}");
    // Cancel synchronously. Do not wait on a page Promise in a native callback.
    return false;
  };
  callbacks.command = [context](std::string_view command) {
    auto* window = context->window;
    if (!window) return;
    (void)window->emit("native.command", json_string(command));
    if (command == "window.focus") { (void)window->show(true); (void)window->focus(); }
    else if (command == "window.close") (void)window->request_close();
    else if (command == "app.quit") (void)window->request_quit();
  };
  callbacks.global_shortcut = [context](soui::desktop::window_id, std::string_view id) {
    // Native business action, not a gesture proof or an automatic page event.
    if (id == "window.reveal" && context->window) {
      (void)context->window->show(true);
      (void)context->window->focus();
    }
  };
  callbacks.error = [](const soui::desktop::result& status) {
    std::cerr << status.code << ": " << status.message << '\n';
  };
  return callbacks;
}
}

int main(int argc, char** argv) {
  const bool default_http = argc == 2 && std::string_view(argv[1]) == "--resource-server";
  if (argc > 3 || (argc == 3 && (std::string_view(argv[2]) != "--resource-server" ||
      std::string_view(argv[1]) == "--resource-server"))) {
    std::cerr << "Usage: soui_desktop_example [resource-root] [--resource-server]\n";
    return 2;
  }
  example_state state;
  if (argc > 1 && !default_http) {
    state.settings.resource_root = std::filesystem::u8path(argv[1]);
  } else {
    const auto resources = soui::desktop::application_resource_directory();
    if (!resources.status.ok) {
      std::cerr << resources.status.code << ": " << resources.status.message << '\n';
      return 1;
    }
    state.settings.resource_root = resources.value;
  }
  state.settings.title = "SOUI · one application";
  state.settings.enable_asset_read = true;
  state.settings.enable_window_controls = true;
  // Independent, default-off grants for this trusted example page. These are
  // not proof of a native user gesture and do not accept OS permission prompts.
  state.settings.services.window_properties = true;
  state.settings.services.clipboard_read = true;
  state.settings.services.clipboard_write = true;
  state.settings.services.directory_dialog = true;
  state.settings.enable_resource_server = default_http || argc == 3;
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
