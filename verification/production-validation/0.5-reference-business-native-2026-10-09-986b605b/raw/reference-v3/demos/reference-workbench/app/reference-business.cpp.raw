#include "reference-business.h"
#include "generated/actions.hpp"
#include <json.hpp>
#include <algorithm>
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
} // namespace reference
