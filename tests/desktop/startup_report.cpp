#include "../../templates/unified-desktop/startup_report.h"

#include <iostream>
#include <limits>
#include <sstream>
#include <string>

// Exercise the actual template entry without linking any SDK implementation or
// native error UI. These fake methods only observe policy and lifecycle ordering.
#define main generated_startup_main
#include "../../templates/unified-desktop/main.cpp"
#undef main

namespace startup_fake {
struct state {
  soui::desktop::result resources{soui::desktop::result::success()};
  soui::desktop::result initialized{soui::desktop::result::success()};
  soui::desktop::result ran{soui::desktop::result::success()};
  soui::desktop::result stopped{soui::desktop::result::success()};
  soui::desktop::callbacks handlers;
  int created{0}, destroyed{0}, initialized_calls{0}, run_calls{0}, quit_calls{0}, dialogs{0};
  bool visible{true}, resource_server{false}, had_handlers{false};
  bool dialog_before_teardown{false}, throw_initialize{false}, report_runtime_error{false};
  bool duplicate_error_suppressed{false}, dialog_during_run{false}, dialog_used_first_error{false};
  std::string error_observed_inside_run;
};
state current;
}  // namespace startup_fake

namespace soui::desktop {
struct application::implementation {};
application::application(options settings, callbacks handlers) {
  auto& fake = startup_fake::current;
  ++fake.created;
  fake.visible = settings.visible;
  fake.resource_server = settings.enable_resource_server;
  fake.had_handlers = handlers.invoke || handlers.ready || handlers.closed || handlers.error ||
    handlers.command || handlers.lifecycle || handlers.state_changed || handlers.close_requested ||
    handlers.global_shortcut;
  fake.handlers = std::move(handlers);
}
application::~application() { ++startup_fake::current.destroyed; }
result application::initialize() {
  auto& fake = startup_fake::current;
  ++fake.initialized_calls;
  if (fake.throw_initialize) throw std::runtime_error("PRIVATE_EXCEPTION_PATH");
  return fake.initialized;
}
result application::run() {
  auto& fake = startup_fake::current;
  ++fake.run_calls;
  if (fake.report_runtime_error && fake.handlers.error) {
    fake.handlers.error(result::failure("E_LOAD", "PRIVATE_RUNTIME_TOKEN"));
    const auto* captured = dynamic_cast<std::stringbuf*>(std::cerr.rdbuf());
    if (captured) fake.error_observed_inside_run = captured->str();
    fake.handlers.error(result::failure("E_WEB_PROCESS", "PRIVATE_SECOND_TOKEN"));
    fake.duplicate_error_suppressed = captured && captured->str() == fake.error_observed_inside_run;
    fake.dialog_during_run = fake.dialogs != 0;
  }
  return fake.ran;
}
result application::quit() {
  ++startup_fake::current.quit_calls;
  return startup_fake::current.stopped;
}
path_result application_resource_directory() {
  return {startup_fake::current.resources, "mock-web"};
}
}  // namespace soui::desktop

namespace soui_app {
bool show_startup_error(const std::string& text) noexcept {
  auto& fake = startup_fake::current;
  ++fake.dialogs;
  fake.dialog_before_teardown = fake.created != fake.destroyed;
  fake.dialog_used_first_error = text.find("Code: E_LOAD\n") != std::string::npos;
  return false;  // Unavailable UI must leave the stderr diagnostic intact.
}
}  // namespace soui_app

namespace {
using soui::desktop::result;
using namespace soui_app;
int failures = 0;

#define CHECK(condition) do { if (!(condition)) { \
  ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
  } } while (false)

void check_no_native(const startup_report& report) {
  CHECK(report.native_error == -1);
  CHECK(report.native_error_domain.empty());
  CHECK(report.socket_operation.empty());
  CHECK(startup_report_json(report).find("native_error") == std::string::npos);
}

void cases() {
  const std::string secret = "PRIVATE_TOKEN_123 /home/private/web http://127.0.0.1:1234/token/";
  for (const auto stage : {startup_stage::resources, startup_stage::initialize,
                          startup_stage::run, startup_stage::unexpected}) {
    for (const auto code : {"resource_directory_unavailable", "E_DISPLAY", "E_WEBKIT_RUNTIME",
                            "E_WEBVIEW2_RUNTIME", "E_PROFILE", "E_WEBVIEW_CREATE"}) {
      const auto report = make_startup_report(stage, result::failure(code, secret));
      CHECK(report.code == code);
      CHECK(startup_report_json(report).find(secret) == std::string::npos);
      CHECK(startup_report_text(report).find(secret) == std::string::npos);
      CHECK(startup_report_text(report).find("--diagnose") != std::string::npos);
      CHECK(startup_report_text(report).find("doctor") != std::string::npos);
      check_no_native(report);
    }
  }
  for (const auto& code : {secret, std::string("E_DISPLAY\"},\"secret\":\"value"),
                           std::string("ok"), std::string(1024 * 1024, 'x')}) {
    const auto report = make_startup_report(startup_stage::unexpected, result::failure(code, secret));
    CHECK(report.code == "unclassified_error");
    CHECK(startup_report_json(report) ==
      "{\"schema\":\"soui.desktop.startup/1\",\"stage\":\"unexpected\",\"code\":\"unclassified_error\"}");
  }
  CHECK(make_startup_report(static_cast<startup_stage>(99), result::failure("E_STATE", "")).stage ==
        "unexpected");
  const auto success = make_startup_report(startup_stage::initialize, {true, secret, secret});
  CHECK(startup_report_json(success) ==
    "{\"schema\":\"soui.desktop.startup/1\",\"stage\":\"initialize\",\"code\":\"ok\"}");
  check_no_native(success);

  for (const auto operation : {"bind", "listen on", "inspect"}) {
    for (const auto domain : {"errno", "WSAGetLastError"}) {
      for (const auto number : {0, 13, (std::numeric_limits<int>::max)()}) {
        const std::string message = std::string("Unable to ") + operation +
          " the local resource listener (" + domain + "=" + std::to_string(number) + ").";
        const auto report = make_startup_report(startup_stage::initialize,
          result::failure("resource_server_unavailable", message));
        CHECK(report.native_error == number);
        CHECK(report.native_error_domain == domain);
        CHECK(report.socket_operation == (std::string(operation) == "listen on" ? "listen" : operation));
        CHECK(startup_report_json(report).size() < 256);
        CHECK(startup_report_text(report).size() < 512);
      }
    }
  }
  const std::string valid = "Unable to bind the local resource listener (errno=13).";
  const auto parsed = make_startup_report(startup_stage::initialize,
    result::failure("resource_server_unavailable", valid));
  CHECK(startup_report_json(parsed) ==
    "{\"schema\":\"soui.desktop.startup/1\",\"stage\":\"initialize\","
    "\"code\":\"resource_server_unavailable\",\"socket_operation\":\"bind\","
    "\"native_error\":13,\"native_error_domain\":\"errno\"}");
  for (const auto& message : {valid + secret, secret + valid, valid + "\n", valid + '\0',
      std::string("Unable to bind the local resource listener (errno=-1)."),
      std::string("Unable to bind the local resource listener (errno=+13)."),
      std::string("Unable to bind the local resource listener (errno=013)."),
      std::string("Unable to bind the local resource listener (errno= 13)."),
      std::string("Unable to bind the local resource listener (errno=13x)."),
      std::string("Unable to bind the local resource listener (errno=)."),
      std::string("Unable to bind the local resource listener (errno=") +
        std::to_string(static_cast<unsigned long long>((std::numeric_limits<int>::max)()) + 1) + ").",
      std::string("Unable to bind the local resource listener (errno=999999999999999999999)."),
      std::string("Unable to bind the local resource listener (GetLastError=13)."),
      std::string("Unable to listen the local resource listener (errno=13)."),
      std::string("Unable to start the local resource listener (errno=13)."),
      std::string("Unable to bind the local resource listener (errno=13)"),
      std::string("Unable to bind the local resource listener (errno=13). "),
      std::string(1024 * 1024, 'x')}) {
    check_no_native(make_startup_report(startup_stage::initialize,
      result::failure("resource_server_unavailable", message)));
  }
  check_no_native(make_startup_report(startup_stage::initialize, result::failure("E_WINDOWS", valid)));
  check_no_native(make_startup_report(startup_stage::initialize, {true, "resource_server_unavailable", valid}));

  // Direct aggregate construction/mutation cannot bypass serialization policy.
  startup_report forged{secret, secret, secret, 13, secret};
  CHECK(startup_report_json(forged) ==
    "{\"schema\":\"soui.desktop.startup/1\",\"stage\":\"unexpected\",\"code\":\"unclassified_error\"}");
  CHECK(startup_report_text(forged).find(secret) == std::string::npos);
  forged = parsed;
  forged.native_error_domain = secret;
  CHECK(startup_report_json(forged).find("native_error") == std::string::npos);
  forged = parsed;
  forged.socket_operation = secret;
  CHECK(startup_report_text(forged).find(secret) == std::string::npos);
  forged = parsed;
  forged.native_error = -2;
  CHECK(startup_report_json(forged).find("native_error") == std::string::npos);
  forged = parsed;
  forged.code = "ok";
  CHECK(startup_report_json(forged).find("native_error") == std::string::npos);
  forged = {std::string(1024 * 1024, 'x'), std::string(1024 * 1024, 'y'),
            std::string(1024 * 1024, 'z'), 1, std::string(1024 * 1024, 'w')};
  CHECK(startup_report_json(forged).size() < 256);
  CHECK(startup_report_text(forged).size() < 512);
}

struct captured_output { int status; std::string out, error; };

captured_output invoke_main(const char* argument = nullptr) {
  char name[] = "fixture";
  std::string option = argument ? argument : "";
  char* argv[] = {name, option.data(), nullptr};
  std::ostringstream out, error;
  auto* old_out = std::cout.rdbuf(out.rdbuf());
  auto* old_error = std::cerr.rdbuf(error.rdbuf());
  const int status = generated_startup_main(argument ? 2 : 1, argv);
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_error);
  return {status, out.str(), error.str()};
}

void entry_cases() {
  using startup_fake::current;
  current = {};
  auto output = invoke_main("--diagnose");
  CHECK(output.status == 0);
  CHECK(output.out == "{\"schema\":\"soui.desktop.startup/1\",\"stage\":\"initialize\",\"code\":\"ok\"}\n");
  CHECK(output.error.empty());
  CHECK(!current.visible && current.resource_server && !current.had_handlers);
  CHECK(current.initialized_calls == 1 && current.run_calls == 0 && current.quit_calls == 1);
  CHECK(current.created == 1 && current.destroyed == 1 && current.dialogs == 0);

  current = {};
  current.initialized = result::failure("E_DISPLAY", "PRIVATE_INITIALIZE_PATH");
  output = invoke_main("--diagnose");
  CHECK(output.status == 1 && output.error.empty());
  CHECK(output.out == "{\"schema\":\"soui.desktop.startup/1\",\"stage\":\"initialize\",\"code\":\"E_DISPLAY\"}\n");
  CHECK(current.run_calls == 0 && current.quit_calls == 0 && current.dialogs == 0);
  CHECK(current.destroyed == 1 && !current.had_handlers);

  current = {};
  current.initialized = result::failure("ok", "PRIVATE_INITIALIZE_PATH");
  output = invoke_main();
  CHECK(output.status == 1 && output.out.empty());
  CHECK(output.error.find("unclassified_error") != std::string::npos);
  CHECK(output.error.find("PRIVATE_") == std::string::npos);
  CHECK(current.visible && current.had_handlers && current.run_calls == 0);
  CHECK(current.dialogs == 1 && !current.dialog_before_teardown && current.destroyed == 1);

  current = {};
  output = invoke_main();
  CHECK(output.status == 0 && output.out.empty() && output.error.empty());
  CHECK(current.visible && current.had_handlers && current.resource_server);
  CHECK(current.run_calls == 1 && current.quit_calls == 1 && current.destroyed == 1 && current.dialogs == 0);

  current = {};
  current.report_runtime_error = true;
  current.ran = result::failure("E_WINDOWS", "PRIVATE_RUN_RETURN_TOKEN");
  output = invoke_main();
  CHECK(output.status == 1 && output.error.find("E_LOAD") != std::string::npos);
  CHECK(output.error.find("PRIVATE_") == std::string::npos);
  CHECK(current.error_observed_inside_run.find("Code: E_LOAD\n") != std::string::npos);
  CHECK(current.duplicate_error_suppressed && !current.dialog_during_run);
  CHECK(output.error == current.error_observed_inside_run);
  CHECK(output.error.find("E_WEB_PROCESS") == std::string::npos);
  CHECK(output.error.find("E_WINDOWS") == std::string::npos);
  CHECK(current.dialog_used_first_error);
  CHECK(current.dialogs == 1 && !current.dialog_before_teardown);

  current = {};
  current.resources = result::failure("resource_directory_unavailable", "PRIVATE_RESOURCE_PATH");
  output = invoke_main("--diagnose");
  CHECK(output.status == 1 && output.out.find("\"stage\":\"resources\"") != std::string::npos);
  CHECK(output.out.find("PRIVATE_") == std::string::npos && output.error.empty());
  CHECK(current.created == 0 && current.dialogs == 0);

  current = {};
  current.throw_initialize = true;
  output = invoke_main("--diagnose");
  CHECK(output.status == 1 && output.out.find("\"stage\":\"unexpected\"") != std::string::npos);
  CHECK(output.out.find("PRIVATE_") == std::string::npos && output.error.empty());
  CHECK(current.destroyed == 1 && current.dialogs == 0);

  current = {};
  output = invoke_main("--not-supported");
  CHECK(output.status == 2 && output.out.empty() && !output.error.empty());
  CHECK(current.created == 0 && current.dialogs == 0);
}
}  // namespace

int main() {
  cases();
  entry_cases();
  if (failures) return 1;
  std::cout << "Startup report policy checks passed\n";
  return 0;
}
