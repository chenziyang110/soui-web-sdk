#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>

namespace soui::sdk {

struct interaction_generation {
  std::uint64_t view{0};
  std::uint64_t document{0};
  std::uint64_t geometry{0};

  [[nodiscard]] bool operator==(const interaction_generation& other) const noexcept {
    return view == other.view && document == other.document && geometry == other.geometry;
  }
};

enum class input_kind : std::uint32_t {
  wheel,
  pointer_move,
  pointer_down,
  pointer_up,
  key_down,
  key_up,
  character,
  focus,
  blur,
  ime_start,
  ime_update,
  ime_commit,
  ime_end,
  capture_lost,
  cancel,
};

struct scroll_sample {
  std::uint64_t scene_version{0};
  std::uint64_t view_id{0};
  std::uint64_t node_id{0};
  std::uint64_t node_generation{0};
  std::uint64_t scroll_epoch{0};
  std::uint64_t sample_sequence{0};
  std::uint64_t scroll_sequence{0};
  double consumed_total_x{0.0};
  double consumed_total_y{0.0};
  double offset_x{0.0};
  double offset_y{0.0};
};

struct input_packet {
  input_kind kind{input_kind::pointer_move};
  interaction_generation generation;
  std::uint64_t sequence{0};
  std::uint64_t gesture{0};
  double timestamp_seconds{0.0};
  int x{0};
  int y{0};
  double delta_x{0.0};
  double delta_y{0.0};
  std::uint32_t modifiers{0};
  std::uint32_t key_code{0};
  int selection_start{0};
  int selection_end{0};
  bool system_key{false};
  bool double_click{false};
  std::u16string text;
  scroll_sample barrier;
};

// Make a newly-started wheel gesture visible on the next compositor frame,
// then preserve smooth motion for the rest of the burst. Windows emits
// separate WM_MOUSEWHEEL messages for a single physical gesture, so the gap
// is the only stable boundary available at the host layer.
class wheel_smoothing_policy final {
 public:
  [[nodiscard]] bool should_smooth(double timestamp_seconds) noexcept {
    constexpr double gesture_gap_seconds = 0.120;
    const bool valid = std::isfinite(timestamp_seconds) && timestamp_seconds > 0.0;
    const bool continuing = valid && last_timestamp_seconds_ > 0.0
        && timestamp_seconds >= last_timestamp_seconds_
        && timestamp_seconds - last_timestamp_seconds_ <= gesture_gap_seconds;
    last_timestamp_seconds_ = valid ? timestamp_seconds : 0.0;
    return continuing;
  }

  void reset() noexcept { last_timestamp_seconds_ = 0.0; }

 private:
  double last_timestamp_seconds_{0.0};
};

class independent_input_sink {
 public:
  virtual ~independent_input_sink() = default;
  [[nodiscard]] virtual bool submit_wheel(const input_packet& packet,
                                          scroll_sample& sample) = 0;
  [[nodiscard]] virtual bool capture_scroll_barrier(
      const input_packet& packet, scroll_sample& sample) = 0;
};

enum class input_admission_result : std::uint8_t {
  accepted, coalesced, stale, capacity, stopped, invalid, no_memory, recovering, cancelled,
};

class engine_input_sink {
 public:
  virtual ~engine_input_sink() = default;
  [[nodiscard]] virtual bool post_input(const input_packet& packet) = 0;
  [[nodiscard]] virtual input_admission_result post_input_typed(const input_packet& packet) {
    return post_input(packet) ? input_admission_result::accepted : input_admission_result::capacity;
  }
};

enum class route_disposition {
  independent,
  engine,
  rejected_inactive,
  rejected_stale,
  rejected_out_of_order,
  rejected_sink,
};

struct route_result {
  route_disposition disposition{route_disposition::rejected_inactive};
  std::uint64_t accepted_sequence{0};
  scroll_sample visual_sample;
  input_admission_result admission{input_admission_result::invalid};
};

class interaction_router final {
 public:
  void activate(interaction_generation generation);
  void deactivate();
  [[nodiscard]] route_result route(const input_packet& packet,
                                   independent_input_sink& independent,
                                   engine_input_sink& engine);
  [[nodiscard]] interaction_generation generation() const noexcept;
  [[nodiscard]] std::uint64_t last_sequence() const noexcept;

 private:
  mutable std::mutex mutex_;
  bool active_{false};
  interaction_generation generation_;
  std::uint64_t last_sequence_{0};
  scroll_sample visual_sample_;
};

}  // namespace soui::sdk
