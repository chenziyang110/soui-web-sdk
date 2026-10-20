#include <soui/desktop/app.h>
#include <iostream>
#include <stdexcept>

// All platform-specific window, resource-origin and Bridge code stays in the SDK.
int main() {
  const auto resources = soui::desktop::application_resource_directory();
  if (!resources.status.ok) {
    std::cerr << resources.status.code << ": " << resources.status.message << '\n';
    return 1;
  }
  soui::desktop::options settings;
  settings.resource_root = resources.value;
  settings.title = "@APP_NAME@";
  // Explicit template choice; the SDK default remains file:// with this service disabled.
  settings.enable_resource_server = true;

  bool runtime_failed = false;
  soui::desktop::callbacks handlers;
  handlers.invoke = [](std::string_view method, std::string_view) -> std::string {
    if (method == "app.info")
      return R"({"name":"@APP_NAME@","identifier":"@APP_IDENTIFIER@","version":"@APP_VERSION@"})";
    // Do not forward arbitrary page requests to filesystem, shell or network APIs.
    throw std::runtime_error("Method is not allowed by this application.");
  };
  handlers.error = [&runtime_failed](const soui::desktop::result& error) {
    runtime_failed = true;
    std::cerr << error.code << ": " << error.message << '\n';
  };
  soui::desktop::application app(settings, std::move(handlers));
  const auto initialized = app.initialize();
  if (!initialized.ok) {
    std::cerr << initialized.code << ": " << initialized.message << '\n';
    return 1;
  }
  const auto run = app.run();
  (void)app.quit();
  if (!run.ok) {
    std::cerr << run.code << ": " << run.message << '\n';
    return 1;
  }
  return runtime_failed ? 1 : 0;
}
