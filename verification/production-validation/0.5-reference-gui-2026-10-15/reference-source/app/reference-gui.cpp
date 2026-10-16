#include "reference-gui.h"
#include <json.hpp>
#include <array>
#include <stdexcept>
#include <utility>

namespace reference {
namespace {
using json = nlohmann::json;
using definition = soui::runtime::gui_command_definition;
using operation = soui::runtime::gui_operation;
constexpr std::array<const char*, 9> families{
  "reference.gui.filter", "reference.gui.dialog_open", "reference.gui.dialog_close",
  "reference.gui.form_set", "reference.gui.form_submit", "reference.gui.summary_start",
  "reference.gui.summary_query", "reference.gui.summary_cancel", "reference.gui.status"};
json object(json properties = json::object(), json required = json::array()) {
  return {{"type", "object"}, {"properties", std::move(properties)},
    {"required", std::move(required)}, {"additionalProperties", false}};
}
json text(unsigned bound) { return {{"type", "string"}, {"maxLength", bound}}; }
json boolean() { return {{"type", "boolean"}}; }
json integer(unsigned bound) { return {{"type", "integer"}, {"minimum", 0}, {"maximum", bound}}; }
json categories(bool all) {
  json values = {"素材", "项目", "缓存", "导出", "归档"};
  if (all) values.push_back("all");
  return {{"type", "string"}, {"enum", std::move(values)}, {"maxLength", 32}};
}
definition row(const char* family, const char* id, const char* action,
    json input, json output, std::vector<std::string> allowed, const char* effect = "") {
  definition result;
  result.family_name = family; result.resource = "reference.workbench";
  result.id = id; result.action = action;
  result.start_capability = result.family_name + ".start";
  result.query_capability = result.family_name + ".query";
  result.cancel_capability = result.family_name + ".cancel";
  result.input_schema_json = input.dump(); result.result_schema_json = output.dump();
  result.effect_method = effect;
  result.project_result = [allowed = std::move(allowed)](const soui::runtime::task_outcome& outcome) {
    const auto original = json::parse(outcome.result_json);
    if (!original.is_object()) throw std::runtime_error("E_REFERENCE_GUI_RESULT");
    auto projected = json::object();
    for (const auto& name : allowed) projected[name] = original.at(name);
    // Never echo a component snapshot, form draft, arbitrary message or input.
    // The SDK independently validates this projection and preserves its own
    // effect receipt; this page result does not replace the native receipt.
    return projected.dump();
  };
  return result;
}
std::vector<definition> rows() {
  const auto id = text(128);
  const auto changed = object({{"changed", boolean()}}, {"changed"});
  const auto opened = object({{"open", boolean()}}, {"open"});
  std::vector<definition> result;
  result.reserve(families.size());
  result.push_back(row(families[0], "reference.filter", "set",
    object({{"query", text(128)}, {"category", categories(true)}}), changed, {"changed"}));
  result.push_back(row(families[1], "reference.dialog", "open", object(), opened, {"open"}));
  result.push_back(row(families[2], "reference.dialog", "close", object(), opened, {"open"}));
  result.push_back(row(families[3], "reference.form", "set",
    object({{"name", text(64)}, {"notes", text(256)}}, {"name", "notes"}), changed, {"changed"}));
  auto name = text(64); name["minLength"] = 1;
  result.push_back(row(families[4], "reference.form", "submit",
    object({{"name", name}, {"notes", text(256)}, {"category", categories(false)}}, {"name", "notes", "category"}),
    object({{"accepted", boolean()}}, {"accepted"}), {"accepted"}, "reference.form.submit"));
  auto duration = integer(60000); duration["minimum"] = 500;
  auto fixture = text(64); fixture["enum"] = {"electron-smoothness-10k-v1"};
  result.push_back(row(families[5], "reference.agent-task", "start",
    object({{"fixtureId", fixture}, {"category", categories(true)}, {"durationMs", duration}},
      {"fixtureId", "category", "durationMs"}),
    object({{"admitted", boolean()}, {"taskId", id}, {"duplicate", boolean()}}, {"admitted", "taskId", "duplicate"}),
    {"admitted", "taskId", "duplicate"}, "reference.summary.local.start"));
  const auto selector = object({{"taskId", id}}, {"taskId"});
  result.push_back(row(families[6], "reference.agent-task", "query", selector,
    object({{"observed", boolean()}, {"taskId", id}, {"state", text(16)}, {"progress", integer(100)},
      {"sequence", text(20)}}, {"observed", "taskId", "state", "progress", "sequence"}),
    {"observed", "taskId", "state", "progress", "sequence"}, "reference.summary.local.query"));
  result.push_back(row(families[7], "reference.agent-task", "cancel", selector,
    object({{"requestAcknowledged", boolean()}, {"taskId", id}, {"cancelRequested", boolean()}},
      {"requestAcknowledged", "taskId", "cancelRequested"}),
    {"requestAcknowledged", "taskId", "cancelRequested"}, "reference.summary.local.cancel"));
  auto snapshot = row(families[8], "reference.status", "", object(),
    object({{"ready", boolean()}, {"taskState", text(16)}, {"progress", integer(100)},
      {"agentTaskState", text(16)}, {"agentTaskProgress", integer(100)},
      {"filteredRows", integer(10000)}, {"selectedCount", integer(100)}, {"dialogOpen", boolean()}},
      {"ready", "taskState", "progress", "agentTaskState", "agentTaskProgress", "filteredRows", "selectedCount", "dialogOpen"}), {});
  snapshot.operation = operation::snapshot;
  snapshot.project_result = [](const soui::runtime::task_outcome& outcome) {
    const auto state = json::parse(outcome.result_json).at("state");
    auto projected = json::object();
    for (const auto* key : {"ready", "taskState", "progress", "agentTaskState", "agentTaskProgress", "filteredRows", "selectedCount", "dialogOpen"})
      projected[key] = state.at(key);
    return projected.dump();
  };
  result.push_back(std::move(snapshot));
  return result;
}
} // namespace
std::vector<std::string> gui_command_methods() {
  std::vector<std::string> methods;
  methods.reserve(families.size() * 3);
  for (const auto* family : families) for (const auto* suffix : {".start", ".query", ".cancel"})
    methods.emplace_back(std::string{family} + suffix);
  return methods;
}
bool register_gui_business(soui::sdk::json_bridge& bridge) {
  if (!bridge.enable_gui_commands(rows())) return false;
  // register_business uses an explicit page grant. GUI controls must be added
  // explicitly; local families remain absent from the page grant.
  return bridge.set_session_capabilities({"bridge.describe", "bridge.probe", "bridge.session.renew",
    "reference.form.submit", "reference.fixture.describe", "task.start", "task.query", "task.cancel", "task.subscribe",
    "gui.bind", "gui.effect", "gui.ack"});
}
} // namespace reference
