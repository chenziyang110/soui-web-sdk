#pragma once

#include <soui/diagnostics/input_frame_trace.h>
#include <cstddef>
#include <cstdint>

namespace soui::sdk::detail::webview2_output_timing {
namespace trace = diagnostics::input_frame_trace;
enum class origin : std::uint8_t { bridge_reply = 1, admission_rejection = 2, event_sink = 3 };
enum class executor : std::uint8_t { owner = 1, bridge_worker = 2, other = 3 };
enum class preparation : std::uint8_t { pending = 0, disabled = 1, cached = 2, limit_fallback = 3, exception_fallback = 4 };
struct metadata final {
  origin source{origin::bridge_reply};
  executor thread{executor::other};
  preparation prepared{preparation::pending};
  std::uint32_t raw_bytes{0};
};
// Private v1 discriminator. This is not a new public trace ABI or input link.
inline constexpr std::uint64_t signature = 0x57564f0100000000ULL;
inline constexpr std::uint64_t payload_mask = 0x03ffffffULL;
// A response envelope stores raw as a JSON string, so encoded.size() cannot
// be smaller than raw.size(). Events parse raw and may discard whitespace.
inline constexpr bool should_prepare(bool enabled, bool is_event, std::size_t raw_bytes, std::size_t limit) noexcept {
  return enabled && limit && raw_bytes <= limit && (is_event || raw_bytes < limit / sizeof(wchar_t));
}
inline constexpr bool valid(metadata value) noexcept {
  return static_cast<unsigned>(value.source) >= 1 && static_cast<unsigned>(value.source) <= 3 &&
      static_cast<unsigned>(value.thread) >= 1 && static_cast<unsigned>(value.thread) <= 3 &&
      static_cast<unsigned>(value.prepared) <= 4 && value.raw_bytes <= 64U * 1024U;
}
inline constexpr std::uint64_t encode(metadata value) noexcept {
  return valid(value) ? signature | value.raw_bytes |
      (static_cast<std::uint64_t>(value.source) << 17) |
      (static_cast<std::uint64_t>(value.thread) << 20) |
      (static_cast<std::uint64_t>(value.prepared) << 23) : 0;
}
inline bool decode(std::uint64_t packed, metadata& value) noexcept {
  if ((packed & ~payload_mask) != signature) return false;
  const metadata decoded{static_cast<origin>((packed >> 17) & 7U),
      static_cast<executor>((packed >> 20) & 7U), static_cast<preparation>((packed >> 23) & 7U),
      static_cast<std::uint32_t>(packed & 0x1ffffU)};
  if (!valid(decoded)) return false;
  value = decoded; return true;
}
inline bool make_event(trace::event& target, trace::stage point, std::uint64_t task,
    std::uint64_t generation, metadata details, trace::reason why = trace::reason::none) noexcept {
  if (!task || !generation || !valid(details)) return false;
  const bool cancelled = point == trace::stage::task_cancelled;
  if (!cancelled && point != trace::stage::task_posted && point != trace::stage::task_begin &&
      point != trace::stage::task_end) return false;
  if (!cancelled && why != trace::reason::none) return false;
  trace::terminal outcome = trace::terminal::none;
  if (cancelled) {
    switch (why) {
      case trace::reason::capacity: case trace::reason::invalid: case trace::reason::post_failed:
        outcome = trace::terminal::rejected; break;
      case trace::reason::stale_generation: case trace::reason::navigation:
        outcome = trace::terminal::stale; break;
      case trace::reason::stopped: case trace::reason::cancelled: case trace::reason::destroyed:
        outcome = trace::terminal::cancelled; break;
      default: return false;
    }
  }
  target = {}; // Never inherit current_context or manufacture input causality.
  target.point = point; target.task_id = task; target.document_generation = generation;
  target.value = encode(details); target.why = why; target.outcome = outcome;
  return true;
}
template<class Sink>
bool emit(bool enabled, Sink&& sink, trace::stage point, std::uint64_t task,
    std::uint64_t generation, metadata details, trace::reason why = trace::reason::none) {
  if (!enabled) return false;
  trace::event event;
  if (!make_event(event, point, task, generation, details, why)) return false;
  sink(event); return true;
}
} // namespace soui::sdk::detail::webview2_output_timing
