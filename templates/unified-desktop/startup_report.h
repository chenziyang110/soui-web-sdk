#pragma once

#include <soui/desktop/app.h>

#include <limits>
#include <string>
#include <string_view>

namespace soui_app {

enum class startup_stage { resources, initialize, run, unexpected };

struct startup_report {
  std::string stage;
  std::string code;
  std::string socket_operation;
  int native_error{-1};
  std::string native_error_domain;
};

namespace startup_detail {

inline const char* stage_name(startup_stage stage) {
  switch (stage) {
    case startup_stage::resources: return "resources";
    case startup_stage::initialize: return "initialize";
    case startup_stage::run: return "run";
    case startup_stage::unexpected: return "unexpected";
  }
  return "unexpected";
}

inline const char* safe_stage(std::string_view stage) {
  for (const auto candidate : {"resources", "initialize", "run", "unexpected"})
    if (stage == candidate) return candidate;
  return "unexpected";
}

// Only fixed codes emitted by resource discovery, initialize/run and their
// native startup callbacks are public diagnostics. Never echo an unknown code.
inline const char* failure_code(std::string_view code) {
  constexpr const char* allowed[] = {
    "executable_path_unavailable", "resource_directory_unavailable",
    "already_initialized", "invalid_options", "invalid_root", "invalid_entry",
    "application_exists", "window_id_exhausted", "not_supported", "closed",
    "wrong_thread", "not_loop_owner", "already_running", "entropy_unavailable",
    "resource_server_started", "resource_server_limit", "resource_server_unavailable",
    "callback_failed", "invalid_bridge_message", "E_ARGUMENT", "E_STATE", "E_THREAD",
    "E_CLOSED", "E_DISPLAY", "E_PROFILE", "E_BRIDGE", "E_WEBKIT_RUNTIME",
    "E_WINDOWS", "E_WEBVIEW2_RUNTIME", "E_BACKEND", "E_WEBVIEW_PROCESS",
    "E_WEB_PROCESS", "E_CALLBACK", "E_LOAD", "E_JAVASCRIPT", "E_RELOAD",
    "E_UNSUPPORTED", "E_RESOURCE_ROOT", "E_UTF8", "E_APPLICATION_IN_USE",
    "E_APPLICATION", "E_WEBVIEW_CREATE", "E_NAVIGATION", "E_NAVIGATION_BLOCKED",
    "E_NAVIGATION_POLICY"
  };
  for (const auto candidate : allowed)
    if (code == candidate) return candidate;
  return "unclassified_error";
}

inline bool decimal_error(std::string_view digits, int& value) {
  if (digits.empty() || digits.size() > std::numeric_limits<int>::digits10 + 1U ||
      (digits.size() > 1 && digits.front() == '0')) return false;
  int parsed = 0;
  for (const char digit : digits) {
    if (digit < '0' || digit > '9') return false;
    const int number = digit - '0';
    if (parsed > ((std::numeric_limits<int>::max)() - number) / 10) return false;
    parsed = parsed * 10 + number;
  }
  value = parsed;
  return true;
}

inline void listener_error(std::string_view message, startup_report& report) {
  // Match resource_server.cpp's complete grammar, not a substring search for
  // errno. A new SDK message format must be reviewed before exposing its data.
  struct grammar { const char* prefix; const char* operation; const char* domain; };
  constexpr grammar formats[] = {
    {"Unable to bind the local resource listener (errno=", "bind", "errno"},
    {"Unable to listen on the local resource listener (errno=", "listen", "errno"},
    {"Unable to inspect the local resource listener (errno=", "inspect", "errno"},
    {"Unable to bind the local resource listener (WSAGetLastError=", "bind", "WSAGetLastError"},
    {"Unable to listen on the local resource listener (WSAGetLastError=", "listen", "WSAGetLastError"},
    {"Unable to inspect the local resource listener (WSAGetLastError=", "inspect", "WSAGetLastError"}
  };
  if (message.size() > 128 || message.size() < 2 ||
      message.substr(message.size() - 2) != ").") return;
  for (const auto& format : formats) {
    const std::string_view prefix(format.prefix);
    if (message.size() <= prefix.size() + 2 || message.substr(0, prefix.size()) != prefix)
      continue;
    int error = -1;
    if (!decimal_error(message.substr(prefix.size(), message.size() - prefix.size() - 2), error))
      return;
    report.socket_operation = format.operation;
    report.native_error = error;
    report.native_error_domain = format.domain;
    return;
  }
}

inline startup_report sanitized(const startup_report& report) {
  startup_report safe;
  safe.stage = safe_stage(report.stage);
  safe.code = report.code == "ok" ? "ok" : failure_code(report.code);
  // The aggregate is mutable. Recheck every string at the serialization boundary
  // so direct callers cannot introduce JSON, paths or secrets through its fields.
  if (safe.code == "resource_server_unavailable" && report.native_error >= 0 &&
      (report.socket_operation == "bind" || report.socket_operation == "listen" ||
       report.socket_operation == "inspect") &&
      (report.native_error_domain == "errno" || report.native_error_domain == "WSAGetLastError")) {
    safe.socket_operation = report.socket_operation;
    safe.native_error = report.native_error;
    safe.native_error_domain = report.native_error_domain;
  }
  return safe;
}

}  // namespace startup_detail

inline startup_report make_startup_report(startup_stage stage, const soui::desktop::result& result) {
  startup_report report;
  report.stage = startup_detail::stage_name(stage);
  report.code = result.ok ? "ok" : startup_detail::failure_code(result.code);
  if (!result.ok && report.code == "resource_server_unavailable")
    startup_detail::listener_error(result.message, report);
  return report;
}

inline std::string startup_report_json(const startup_report& report) {
  const auto safe = startup_detail::sanitized(report);
  // All string values above come from fixed ASCII allowlists, so no untrusted
  // strings reach JSON escaping or allocation. Output stays independent of input size.
  std::string json = "{\"schema\":\"soui.desktop.startup/1\",\"stage\":\"" + safe.stage +
    "\",\"code\":\"" + safe.code + "\"";
  if (safe.native_error >= 0) {
    json += ",\"socket_operation\":\"" + safe.socket_operation + "\",\"native_error\":" +
      std::to_string(safe.native_error) + ",\"native_error_domain\":\"" + safe.native_error_domain + "\"";
  }
  return json + "}";
}

inline std::string startup_report_text(const startup_report& report) {
  const auto safe = startup_detail::sanitized(report);
  std::string text = safe.code == "ok" ? "SOUI desktop diagnostic step succeeded.\n" :
    "SOUI application startup failed.\n";
  text += "Stage: " + safe.stage + "\nCode: " + safe.code + "\n";
  if (safe.native_error >= 0)
    text += "Resource listener: " + safe.socket_operation + " (" + safe.native_error_domain +
      "=" + std::to_string(safe.native_error) + ").\n";
  if (safe.code == "ok")
    text += "This does not verify page readiness or desktop interaction.\n";
  text += "Run this application with --diagnose for a sanitized report. "
    "Run the staged runtime doctor for deployment checks.";
  return text;
}

}  // namespace soui_app
