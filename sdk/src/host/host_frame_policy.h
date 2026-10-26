#pragma once
namespace soui::sdk::detail {
struct host_frame_policy final {
  static constexpr int main_rate(int target, bool fractional_capability, bool explicit_main,
      bool gpu, bool cpu_interval_override, bool legacy_compositor) noexcept {
    return target > 0 && (explicit_main || (fractional_capability && !legacy_compositor && (gpu || !cpu_interval_override))) ? target : 0;
  }
  static constexpr bool fractional_cpu(bool fractional_capability, bool managed, bool gpu, int main_rate) noexcept {
    return fractional_capability && managed && !gpu && main_rate > 0;
  }
  static constexpr int rounded_interval(int rate) noexcept {
    return rate > 0 ? 1000 / rate + (1000 % rate != 0 ? 1 : 0) : 17;
  }
};
} // namespace soui::sdk::detail
