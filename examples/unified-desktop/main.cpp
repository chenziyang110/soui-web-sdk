#include <soui/desktop/app.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

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
}

int main(int argc, char** argv) {
  soui::desktop::options options;
  options.resource_root = argc > 1 ? std::filesystem::u8path(argv[1]) :
    std::filesystem::absolute(std::filesystem::u8path(argv[0])).parent_path() / "web";
  options.title = "SOUI · one application";
  soui::desktop::application* window = nullptr;
  soui::desktop::callbacks callbacks;
  // This is the explicit method allowlist. Do not forward arbitrary page input
  // to shell/file/network APIs. Runtime protected methods still need permissions.
  callbacks.invoke = [&](std::string_view method, std::string_view parameters) -> std::string {
    if (method == "app.echo") return std::string(parameters);
    if (method == "app.capabilities") {
      const auto caps = window->get_capabilities();
      return "{\"backend\":" + json_string(caps.backend) +
        ",\"clipboardRead\":" + (caps.clipboard_read ? "true" : "false") +
        ",\"clipboardWrite\":" + (caps.clipboard_write ? "true" : "false") +
        ",\"directoryDialog\":" + (caps.directory_dialog ? "true" : "false") + "}";
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
      return checked(window->dispatch([&] { (void)window->reload(); }));
    }
    throw std::runtime_error("Method is not allowed by this application");
  };
  callbacks.ready = [&] { (void)window->emit("native.ready", "{\"message\":\"Native → Web events are connected\"}"); };
  callbacks.error = [](const soui::desktop::result& status) {
    std::cerr << status.code << ": " << status.message << '\n';
  };
  soui::desktop::application app(std::move(options), std::move(callbacks));
  window = &app;
  const auto created = app.initialize();
  if (!created.ok) { std::cerr << created.code << ": " << created.message << '\n'; return 1; }
  const auto run = app.run();
  if (!run.ok) { std::cerr << run.code << ": " << run.message << '\n'; return 2; }
  return 0;
}
