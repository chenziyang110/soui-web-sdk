#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace soui::runtime {

// Immutable, explicitly limited soui.action/1 contract. This is not a complete
// JSON Schema implementation. Parsing rejects unsupported keywords and types.
class action_contract final {
 public:
  static constexpr std::size_t max_schema_bytes{8U * 1024U};
  static constexpr std::size_t max_contract_bytes{16U * 1024U};
  static constexpr std::size_t max_depth{8U};
  static constexpr std::size_t max_properties{64U};
  static constexpr std::size_t max_items{1024U};
  static constexpr std::size_t max_string_length{32768U};

  // Returns null for malformed, unbounded or unsupported contracts.
  [[nodiscard]] static std::shared_ptr<const action_contract> parse(std::string_view text);
  [[nodiscard]] bool validates_input(std::string_view text) const;
  [[nodiscard]] bool validates_output(std::string_view text) const;
  [[nodiscard]] const std::string& encoded() const noexcept;

 private:
  struct impl;
  explicit action_contract(std::shared_ptr<const impl> state);
  std::shared_ptr<const impl> state_;
};

}  // namespace soui::runtime
