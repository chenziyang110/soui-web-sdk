#pragma once
// Measurement consumers only. This does not expose a product CDP/Bridge API,
// own COM callbacks, change process policy, or authorize a remote connection.
#include <json.hpp>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace soui::scratch_diagnostic {
using json = nlohmann::json;
enum class page_script { setup, query, install, start, finish, abort, stop_workload };
// scripts and probe are built from the same pinned measurement inputs in each
// diagnostic consumer, never supplied by a web caller or arbitrary CDP method.
inline std::string fixed_page_script(page_script operation, const json& scripts, const std::string& probe) {
  if (scripts.at("schema") != "soui.scratch-diagnostic-page-scripts/v1") throw std::runtime_error("Diagnostic script schema differs");
  switch (operation) {
    case page_script::setup: return scripts.at("setup").get<std::string>();
    case page_script::query: return scripts.at("query").get<std::string>();
    case page_script::install:
      return "(()=>{if(window.__souiDiagnosticProbe)throw Error('Diagnostic probe already exists');window.__souiDiagnosticProbe=(" + probe + ")();return window.__souiDiagnosticProbe.warmup;})()";
    case page_script::start: return scripts.at("start").get<std::string>();
    case page_script::finish: return scripts.at("finish").get<std::string>();
    case page_script::abort: return scripts.at("abort").get<std::string>();
    case page_script::stop_workload: return scripts.at("stop-workload").get<std::string>();
  }
  throw std::runtime_error("Unknown fixed diagnostic script");
}
inline void validate_recipe(const json& value) {
  if (value.at("schema") != "soui.scratch-diagnostic-protocol/v1" || value.at("warmupMs") != 10000 ||
      value.at("captureMs") != 10000 || value.at("samplingIntervalUs") != 1000 ||
      value.at("timerProbeIntervalMs") != 16 || value.at("maxTraceBytes") != 67108864 ||
      value.at("maxResponseBytes") != 16777216 || value.at("traceOptions") != "record-until-full" ||
      value.at("traceCategories") != "devtools.timeline,v8,blink.user_timing,cc,gpu,viz,toplevel,renderer.scheduler")
    throw std::runtime_error("Diagnostic recipe differs from fixed v1 protocol");
}
inline json command_parameters(std::wstring_view method, const json& recipe) {
  validate_recipe(recipe);
  if (method == L"Profiler.enable" || method == L"Profiler.start" || method == L"Profiler.stop" ||
      method == L"Profiler.disable" || method == L"Tracing.getCategories" || method == L"Tracing.end") return json::object();
  if (method == L"Profiler.setSamplingInterval") return {{"interval", recipe.at("samplingIntervalUs")}};
  if (method == L"Tracing.start") return {{"categories", recipe.at("traceCategories")},
    {"options", recipe.at("traceOptions")}, {"bufferUsageReportingInterval", 1000}, {"transferMode", "ReportEvents"}};
  throw std::runtime_error("Command is outside fixed local diagnostic protocol");
}
inline bool fixed_command(std::wstring_view method, const json& params, const json& recipe) noexcept {
  try { return params == command_parameters(method, recipe); } catch (...) { return false; }
}
class bounded_trace_buffer final {
 public:
  static constexpr std::size_t maximum_bytes = 67108864;
  static constexpr std::size_t maximum_chunk_bytes = 16777216;
  // Retain raw params from dataCollected/bufferUsage/tracingComplete. Parse and
  // export only after collection; a dropped chunk is never a qualified trace.
  bool append(std::string kind, std::string raw) noexcept {
    if (kind != "Tracing.dataCollected" && kind != "Tracing.bufferUsage" && kind != "Tracing.tracingComplete") return false;
    if (raw.size() > maximum_chunk_bytes || raw.size() > maximum_bytes - bytes_ || chunks_.size() >= 8192) {
      overflow_ = true; return false;
    }
    try { const auto size = raw.size(); chunks_.push_back({std::move(kind), std::move(raw)}); bytes_ += size; return true; }
    catch (...) { allocation_failed_ = true; return false; }
  }
  struct chunk final { std::string method, raw; };
  const std::vector<chunk>& chunks() const noexcept { return chunks_; }
  std::size_t bytes() const noexcept { return bytes_; }
  bool overflow() const noexcept { return overflow_; }
  bool allocation_failed() const noexcept { return allocation_failed_; }
 private:
  std::vector<chunk> chunks_;
  std::size_t bytes_{};
  bool overflow_{}, allocation_failed_{};
};
} // namespace soui::scratch_diagnostic
