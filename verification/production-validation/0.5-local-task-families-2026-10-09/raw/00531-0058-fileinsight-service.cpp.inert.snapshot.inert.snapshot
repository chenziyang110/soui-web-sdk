#include <soui/runtime/services/fileinsight-service/fileinsight-service.h>

#include <string>
#include <utility>

#include "../../../../../third_party/include/json/json.hpp"

namespace soui::modules::fileinsight {
namespace {

using json = nlohmann::json;

std::string string_param(const json& params, const char* name) {
  const auto found = params.find(name);
  return found != params.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::size_t size_param(const json& params, const char* name, std::size_t fallback) {
  const auto found = params.find(name);
  if (found == params.end() || !found->is_number_unsigned()) return fallback;
  return found->get<std::size_t>();
}

std::string error_json(const char* code, const char* message) {
  return json{{"error", code}, {"message", message}}.dump();
}

json page_json(const soui::runtime::scan_response& page) {
  json entries = json::array();
  for (const auto& entry : page.entries) {
    entries.push_back({{"path", entry.relative_path}, {"directory", entry.directory}, {"size", entry.size}});
  }
  json groups = json::array();
  for (const auto& group : page.groups) {
    groups.push_back({{"extension", group.extension}, {"fileCount", group.file_count},
                      {"totalBytes", group.total_bytes}});
  }
  const auto state = page.kind == soui::runtime::result_kind::timeout_observed
                         ? "running"
                         : page.kind == soui::runtime::result_kind::task_cancelled
                               ? "cancelled"
                               : page.kind == soui::runtime::result_kind::partial ? "partial" : "succeeded";
  // The scanner cannot know the total number of entries before traversal, so
  // a running page carries 0 as an explicit "unknown" progress value. The
  // React surface renders that value as an indeterminate state, never 0% or
  // an invented percentage.
  const auto progress = page.kind == soui::runtime::result_kind::timeout_observed ? 0U : 100U;
  return {{"taskId", page.task_id}, {"state", state}, {"progress", progress},
          {"entries", std::move(entries)}, {"cursor", page.next_cursor}, {"hasMore", page.has_more},
          {"message", page.message}, {"skipped", page.skipped_count}, {"errors", page.error_count},
          {"issues", page.issues}, {"fileCount", page.file_count}, {"totalBytes", page.total_bytes},
          {"groups", std::move(groups)}};
}

}  // namespace

void FileinsightService::register_methods(soui::runtime::capability_registry& registry,
                                          soui::runtime::directory_scanner& scanner) {
  (void)registry.register_method("fileinsight.scan.start", "filesystem.scan",
      [&scanner](std::string_view params_text) -> std::string {
        const auto params = json::parse(params_text.begin(), params_text.end(), nullptr, false);
        if (!params.is_object()) return error_json("E_INVALID_PARAMS", "params must be an object.");
        const soui::runtime::scan_request_context context{
            string_param(params, "sessionId"), string_param(params, "origin"), string_param(params, "token")};
        const auto root = string_param(params, "rootToken");
        const auto filter = string_param(params, "filter");
        const auto operation = string_param(params, "operationId");
        if (root.empty() || operation.empty()) return error_json("E_INVALID_PARAMS", "rootToken and operationId are required.");
        const auto started = scanner.start(context, operation, root, filter, "fileinsight.scan.start");
        if (!started.ok()) return error_json(started.code.c_str(), started.message.c_str());
        return json{{"taskId", started.task_id}, {"accepted", true}}.dump();
      });
  (void)registry.register_method("fileinsight.scan.query", "task.query",
      [&scanner](std::string_view params_text) -> std::string {
        const auto params = json::parse(params_text.begin(), params_text.end(), nullptr, false);
        if (!params.is_object()) return error_json("E_INVALID_PARAMS", "params must be an object.");
        const soui::runtime::scan_request_context context{
            string_param(params, "sessionId"), string_param(params, "origin"), string_param(params, "token")};
        const auto task_id = string_param(params, "taskId");
        if (task_id.empty()) return error_json("E_INVALID_PARAMS", "taskId is required.");
        const auto page = scanner.read_page(context, task_id, string_param(params, "cursor"),
                                            size_param(params, "pageSize", 100));
        if (page.kind == soui::runtime::result_kind::failure) return error_json(page.code.c_str(), page.message.c_str());
        return page_json(page).dump();
      });
}

}  // namespace soui::modules::fileinsight
