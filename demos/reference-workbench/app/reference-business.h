#pragma once
#include <soui/json_bridge.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reference {
struct fixture final {
  std::string id;
  std::uint32_t seed{};
  std::vector<std::string> groups, statuses;
};
struct business final {
  explicit business(std::shared_ptr<const fixture> data) : data{std::move(data)} {}
  const std::shared_ptr<const fixture> data;
  std::atomic<std::uint64_t> form_receipts{0};
};
[[nodiscard]] std::shared_ptr<const fixture> read_fixture(const std::filesystem::path& path);
// App-owned immutable data and receipt state; callbacks retain shared ownership.
// The Bridge owns the only task_service and joins its real workers at destruction.
[[nodiscard]] bool register_business(soui::sdk::json_bridge& bridge, std::shared_ptr<business> owner);
// Native opt-in after ordinary registration, before endpoint factory/bootstrap.
// Reuses reference.summary in the Bridge's only service; adds no page grant.
[[nodiscard]] bool register_local_business(soui::sdk::json_bridge& bridge);
} // namespace reference
