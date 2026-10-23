#include <soui/desktop/app.h>
#include "startup_report.h"
#include "startup_dialog.h"
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
soui_app::startup_report start_application(bool diagnose, bool& failure_reported) {
  using soui_app::make_startup_report;
  using soui_app::startup_stage;
  const auto resources = soui::desktop::application_resource_directory();
  if (!resources.status.ok) return make_startup_report(startup_stage::resources, resources.status);
  soui::desktop::options settings;
  settings.resource_root = resources.value;
  settings.title = "@APP_NAME@";
  settings.visible = !diagnose;
  // Explicit template choice; the SDK default remains file:// with this service disabled.
  settings.enable_resource_server = true;

  bool runtime_failed = false;
  auto runtime_failure = make_startup_report(startup_stage::run, soui::desktop::result::success());
  soui::desktop::callbacks handlers;
  if (!diagnose) {
    handlers.invoke = [](std::string_view method, std::string_view) -> std::string {
      if (method == "app.info")
        return R"({"name":"@APP_NAME@","identifier":"@APP_IDENTIFIER@","version":"@APP_VERSION@"})";
      throw std::runtime_error("Method is not allowed by this application.");
    };
    handlers.error = [&](const soui::desktop::result& error) {
      if (runtime_failed) return;
      runtime_failure = make_startup_report(startup_stage::run, error);
      runtime_failed = true;
      // Preserve immediate diagnostics while the window/event loop remains live.
      // Never start a second modal loop from a native error callback.
      std::cerr << soui_app::startup_report_text(runtime_failure) << '\n';
      failure_reported = true;
    };
  }
  soui::desktop::application app(settings, std::move(handlers));
  const auto initialized = app.initialize();
  if (!initialized.ok) return make_startup_report(startup_stage::initialize, initialized);
  if (diagnose) {
    // Hidden initialization only: no business callbacks or application event loop.
    // This does not establish page load, Bridge, GUI or runtime acceptance.
    const auto stopped = app.quit();
    return make_startup_report(startup_stage::initialize, stopped);
  }
  const auto run = app.run();
  const auto stopped = app.quit();
  if (runtime_failed) return runtime_failure;
  if (!run.ok) return make_startup_report(startup_stage::run, run);
  return make_startup_report(startup_stage::run, stopped);
}
}  // namespace

int main(int argc, char** argv) {
  const bool diagnose = argc == 2 && std::string_view(argv[1]) == "--diagnose";
  if (argc != 1 && !diagnose) {
    std::cerr << "Usage: @APP_NAME@ [--diagnose]\n";
    return 2;
  }
  soui_app::startup_report report;
  bool failure_reported = false;
  try { report = start_application(diagnose, failure_reported); }
  catch (...) {
    report = soui_app::make_startup_report(soui_app::startup_stage::unexpected,
        soui::desktop::result::failure("unclassified_error", ""));
  }
  // start_application has released the app before invoking a separate error UI.
  // Never include raw SDK messages, exception text, resource URLs or user paths.
  if (diagnose) std::cout << soui_app::startup_report_json(report) << '\n';
  else if (report.code != "ok") {
    const auto text = soui_app::startup_report_text(report);
    if (!failure_reported) std::cerr << text << '\n';
    (void)soui_app::show_startup_error(text); // Best effort; stderr remains available.
  }
  return report.code == "ok" ? 0 : 1;
}
