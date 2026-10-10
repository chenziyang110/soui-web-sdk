#include "reference-business.h"
#include "generated/actions.hpp"
#include <json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>

namespace reference {
namespace {
using json = nlohmann::json;
std::uint32_t hash32(std::uint32_t value) noexcept {
  value ^= value >> 16; value *= 0x7feb352dU;
  value ^= value >> 15; value *= 0x846ca68bU; return value ^ (value >> 16);
}
soui::runtime::task_outcome reject_input() {
  return {soui::runtime::result_kind::failure, "{}", "E_REFERENCE_INPUT: Invalid fixed-fixture summary input."};
}
soui::runtime::task_outcome cancelled() {
  return {soui::runtime::result_kind::task_cancelled, "{}", "Reference summary cancelled by native worker."};
}
}
std::shared_ptr<const fixture> read_fixture(const std::filesystem::path& path) {
  if (std::filesystem::file_size(path) > 4096) throw std::runtime_error("E_REFERENCE_FIXTURE");
  std::ifstream input{path, std::ios::binary};
  if (!input) throw std::runtime_error("E_REFERENCE_FIXTURE");
  const std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  const auto value = json::parse(bytes, nullptr, false);
  const std::vector<std::string> groups{"素材", "项目", "缓存", "导出", "归档"};
  const std::vector<std::string> statuses{"ready", "indexed", "queued", "review"};
  if (!value.is_object() || value.size() != 6 || value.value("fixtureId", "") != "electron-smoothness-10k-v1" ||
      value.value("seed", 0) != 20260915 || value.value("rowCount", 0) != 10000 || value.value("rowHeightPx", 0) != 32 ||
      value.at("groups") != json(groups) || value.at("statuses") != json(statuses)) throw std::runtime_error("E_REFERENCE_FIXTURE");
  return std::make_shared<const fixture>(fixture{"electron-smoothness-10k-v1", 20260915U, groups, statuses});
}
bool register_business(soui::sdk::json_bridge& bridge, std::shared_ptr<business> owner) {
  if (!owner || !owner->data) return false;
  const auto data = owner->data;
  if (!reference_actions::register_DescribeFixture(bridge, [data](const reference_actions::DescribeFixtureInput&,
      const soui::runtime::bridge_request&) {
        reference_actions::DescribeFixtureOutput result;
        result.fixtureId = data->id; result.seed = data->seed; result.rowCount = 10000; result.rowHeightPx = 32;
        result.groups = data->groups; result.statuses = data->statuses; return result;
      }) || !reference_actions::register_SubmitForm(bridge, [owner](const reference_actions::SubmitFormInput& input,
      const soui::runtime::bridge_request&) {
        reference_actions::SubmitFormOutput result;
        result.accepted = input.name.find_first_not_of(" \t\r\n") != std::string::npos;
        result.message = result.accepted ? "C++ 已确认表单；本示例不写入文件。" : "名称不能为空。";
        if (result.accepted) {
          const auto sequence = owner->form_receipts.fetch_add(1, std::memory_order_relaxed);
          if (sequence == std::numeric_limits<std::uint64_t>::max()) std::terminate();
          result.receiptId = "reference-form-" + std::to_string(sequence + 1);
        }
        return result;
      })) return false;
  if (!bridge.register_task_handler("reference.summary", [data](const soui::runtime::task_invocation& invocation,
      const soui::runtime::task_control& control) {
        const auto input = json::parse(invocation.parameters_json, nullptr, false);
        if (!input.is_object() || input.size() != 3 || !input.contains("fixtureId") || !input.contains("category") ||
            !input.contains("durationMs") || !input["fixtureId"].is_string() || input["fixtureId"] != data->id ||
            !input["category"].is_string() || !input["durationMs"].is_number_integer()) return reject_input();
        const auto& duration = input["durationMs"];
        const bool duration_valid = duration.is_number_unsigned() ? duration.get<std::uint64_t>() >= 500 && duration.get<std::uint64_t>() <= 60000 :
            duration.get<std::int64_t>() >= 500 && duration.get<std::int64_t>() <= 60000;
        const auto category = input["category"].get<std::string>();
        if (!duration_valid || (category != "all" && std::find(data->groups.begin(), data->groups.end(), category) == data->groups.end()))
          return reject_input();
        const auto duration_ms = duration.get<std::uint32_t>();
        const auto began = std::chrono::steady_clock::now();
        const auto deadline = began + std::chrono::milliseconds{duration_ms};
        std::uint64_t total_size = 0; std::uint32_t matched = 0;
        std::vector<std::uint32_t> group_counts(data->groups.size(), 0);
        for (std::uint32_t index = 0; index < 10000; ++index) {
          if (index % 128 == 0 && control.cancellation_requested()) return cancelled();
          const auto value = hash32(data->seed + index * 2654435761U);
          const auto group = value % static_cast<std::uint32_t>(data->groups.size());
          if (category == "all" || category == data->groups[group]) { ++matched; ++group_counts[group]; total_size += 16 + value % 4096; }
        }
        std::uint32_t last_progress = 0;
        while (std::chrono::steady_clock::now() < deadline) {
          if (control.cancellation_requested()) return cancelled();
          const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
          const auto progress = static_cast<std::uint32_t>(std::min<std::int64_t>(99, elapsed * 100 / duration_ms));
          if (progress > last_progress) {
            last_progress = progress;
            if (!control.report_progress(progress, "Reference summary running in native SDK worker.")) return cancelled();
          }
          std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        if (control.cancellation_requested() || !control.report_progress(100, "Reference summary complete.")) return cancelled();
        auto groups = json::array();
        for (std::size_t index = 0; index < data->groups.size(); ++index)
          groups.push_back(json{{"group", data->groups[index]}, {"count", group_counts[index]}});
        return soui::runtime::task_outcome{soui::runtime::result_kind::success,
          json{{"fixtureId", data->id}, {"processed", 10000}, {"matched", matched}, {"totalSizeKb", total_size},
            {"groups", std::move(groups)}}.dump(), "Reference fixed-fixture summary complete."};
      })) return false;
  return bridge.enable_session_renewal() && bridge.set_session_capabilities({"bridge.describe", "bridge.probe", "bridge.session.renew",
      "reference.form.submit", "reference.fixture.describe", "task.start", "task.query", "task.cancel", "task.subscribe"});
}
bool register_local_business(soui::sdk::json_bridge& bridge) {
  soui::runtime::local_task_family_definition family;
  family.name = "reference.summary.local"; family.kind = "reference.summary";
  family.start_capability = "reference.summary.local.start";
  family.query_capability = "reference.summary.local.query";
  family.cancel_capability = "reference.summary.local.cancel";
  family.input_schema_json = R"schema({"type":"object","properties":{"fixtureId":{"type":"string","enum":["electron-smoothness-10k-v1"],"maxLength":64},"category":{"type":"string","enum":["all","素材","项目","缓存","导出","归档"],"maxLength":32},"durationMs":{"type":"integer","minimum":500,"maximum":60000}},"required":["fixtureId","category","durationMs"],"additionalProperties":false})schema";
  // Non-success terminal outcomes publish an empty safe object. Successful
  // outcomes always project all five fields; terminal state comes from SDK.
  family.result_schema_json = R"schema({"type":"object","properties":{"fixtureId":{"type":"string","enum":["electron-smoothness-10k-v1"],"maxLength":64},"processed":{"type":"integer","minimum":10000,"maximum":10000},"matched":{"type":"integer","minimum":0,"maximum":10000},"totalSizeKb":{"type":"integer","minimum":0,"maximum":41110000},"groups":{"type":"object","properties":{"素材":{"type":"integer","minimum":0,"maximum":10000},"项目":{"type":"integer","minimum":0,"maximum":10000},"缓存":{"type":"integer","minimum":0,"maximum":10000},"导出":{"type":"integer","minimum":0,"maximum":10000},"归档":{"type":"integer","minimum":0,"maximum":10000}},"required":["素材","项目","缓存","导出","归档"],"additionalProperties":false}},"additionalProperties":false})schema";
  family.project_result = [](const soui::runtime::task_outcome& outcome) {
    if (outcome.kind != soui::runtime::result_kind::success) return std::string{"{}"};
    const auto value = json::parse(outcome.result_json);
    const auto& original = value.at("groups");
    constexpr std::array<const char*, 5> names{"素材", "项目", "缓存", "导出", "归档"};
    if (!original.is_array() || original.size() != names.size()) throw std::runtime_error("E_REFERENCE_GROUPS");
    auto groups = json::object();
    for (const auto& item : original) {
      if (!item.is_object() || item.size() != 2 || !item.contains("group") || !item.contains("count") ||
          !item.at("group").is_string() || !item.at("count").is_number_integer()) throw std::runtime_error("E_REFERENCE_GROUPS");
      const auto name = item.at("group").get<std::string>();
      const auto& count = item.at("count");
      const bool bounded = count.is_number_unsigned() ? count.get<std::uint64_t>() <= 10000 :
          count.get<std::int64_t>() >= 0 && count.get<std::int64_t>() <= 10000;
      if (std::none_of(names.begin(), names.end(), [&name](const char* expected) { return name == expected; }) ||
          groups.contains(name) || !bounded) throw std::runtime_error("E_REFERENCE_GROUPS");
      groups[name] = count;
    }
    return json{{"fixtureId", value.at("fixtureId")}, {"processed", value.at("processed")},
      {"matched", value.at("matched")}, {"totalSizeKb", value.at("totalSizeKb")},
      {"groups", std::move(groups)}}.dump();
  };
  return bridge.allow_local_action("reference.fixture.describe") &&
      bridge.allow_local_action("reference.form.submit") && bridge.register_local_task_family(std::move(family));
}
} // namespace reference
