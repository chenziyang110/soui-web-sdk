#include <soui/host/interaction_protocol.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using soui::sdk::engine_input_sink;
using soui::sdk::independent_input_sink;
using soui::sdk::input_kind;
using soui::sdk::input_packet;
using soui::sdk::interaction_generation;
using soui::sdk::interaction_router;
using soui::sdk::route_disposition;
using soui::sdk::route_result;
using soui::sdk::scroll_sample;

constexpr interaction_generation generation{7, 11, 3};

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

bool same_sample(const scroll_sample& a, const scroll_sample& b) {
  return a.scene_version == b.scene_version && a.view_id == b.view_id &&
      a.node_id == b.node_id && a.node_generation == b.node_generation &&
      a.scroll_epoch == b.scroll_epoch && a.sample_sequence == b.sample_sequence &&
      a.scroll_sequence == b.scroll_sequence && a.consumed_total_x == b.consumed_total_x &&
      a.consumed_total_y == b.consumed_total_y && a.offset_x == b.offset_x &&
      a.offset_y == b.offset_y;
}

scroll_sample make_sample(std::uint64_t seed) {
  scroll_sample value{};
  value.scene_version = seed + 1;
  value.view_id = seed + 2;
  value.node_id = seed + 3;
  value.node_generation = seed + 4;
  value.scroll_epoch = seed + 5;
  value.sample_sequence = seed + 6;
  value.scroll_sequence = seed + 7;
  value.consumed_total_x = static_cast<double>(seed) + 0.25;
  value.consumed_total_y = static_cast<double>(seed) + 0.5;
  value.offset_x = static_cast<double>(seed) + 0.75;
  value.offset_y = static_cast<double>(seed) + 1.0;
  return value;
}

enum class capture_mode { accept, reject_untouched, reject_partial };

struct fast_sink final : independent_input_sink {
  capture_mode mode{capture_mode::reject_untouched};
  scroll_sample fresh{make_sample(100)};
  unsigned captures{};

  bool submit_wheel(const input_packet& input, scroll_sample& sample) override {
    // A wheel sample is not a successful pointer-down barrier capture.
    sample.scene_version = 19;
    sample.scroll_sequence = input.sequence;
    sample.offset_x = 4.0;
    sample.offset_y = 125.0;
    return true;
  }

  bool capture_scroll_barrier(const input_packet&, scroll_sample& sample) override {
    ++captures;
    if (mode == capture_mode::reject_partial) {
      // Failed capture output remains invalid even when some fields were written.
      sample.scene_version = 777;
      sample.node_id = 991;
      sample.offset_y = 8192.0;
      return false;
    }
    if (mode == capture_mode::reject_untouched) return false;
    sample = fresh;
    return true;
  }
};

struct engine_sink final : engine_input_sink {
  std::vector<input_packet> packets;

  bool post_input(const input_packet& input) override {
    packets.push_back(input);
    return true;
  }
};

struct fixture {
  interaction_router router;
  fast_sink fast;
  engine_sink engine;

  fixture() { router.activate(generation); }

  input_packet packet(input_kind kind, std::uint64_t sequence) const {
    input_packet value{};
    value.kind = kind;
    value.generation = generation;
    value.sequence = sequence;
    value.x = 580;
    value.y = 410;
    return value;
  }

  route_result send(input_kind kind, std::uint64_t sequence) {
    return router.route(packet(kind, sequence), fast, engine);
  }

  void wheel(std::uint64_t sequence) {
    const auto result = send(input_kind::wheel, sequence);
    require(result.disposition == route_disposition::independent,
            "accepted wheel must use the independent sink");
    require(engine.packets.empty(), "accepted wheel must not reach the engine sink");
    require(result.visual_sample.scene_version == 19,
            "accepted wheel must leave a nonzero cached sample");
  }

  route_result down(std::uint64_t sequence) {
    const auto result = send(input_kind::pointer_down, sequence);
    require(result.disposition == route_disposition::engine,
            "pointer down must reach the engine sink");
    return result;
  }

  const scroll_sample& emitted_barrier() const {
    require(!engine.packets.empty(), "engine sink must contain an input packet");
    return engine.packets.back().barrier;
  }
};

void test_wheel_then_false_untouched() {
  fixture value;
  value.wheel(1);
  value.down(2);
  require(same_sample(value.emitted_barrier(), {}),
          "failed capture must not inherit the wheel barrier");
}

void test_wheel_then_false_partial() {
  fixture value;
  value.wheel(1);
  value.fast.mode = capture_mode::reject_partial;
  value.down(2);
  require(same_sample(value.emitted_barrier(), {}),
          "failed capture must not leak partially written output");
}

void test_prior_success_then_no_target() {
  fixture value;
  value.fast.mode = capture_mode::accept;
  value.down(1);
  value.fast.mode = capture_mode::reject_untouched;
  value.down(2);
  require(same_sample(value.emitted_barrier(), {}),
          "no target must not inherit a prior valid barrier");
}

void test_prior_success_then_partial_failure() {
  fixture value;
  value.fast.mode = capture_mode::accept;
  value.down(1);
  value.fast.mode = capture_mode::reject_partial;
  value.down(2);
  require(same_sample(value.emitted_barrier(), {}),
          "failed capture must not combine cached and partially written fields");
}

void test_no_history_partial_failure() {
  fixture value;
  value.fast.mode = capture_mode::reject_partial;
  value.down(1);
  require(same_sample(value.emitted_barrier(), {}),
          "partially written failure output must remain invalid without history");
}

void test_fresh_success_all_fields_preserved() {
  fixture value;
  value.fast.mode = capture_mode::accept;
  value.down(1);
  require(same_sample(value.emitted_barrier(), value.fast.fresh),
          "successful capture must preserve every barrier field");
}

void test_fresh_success_replaces_all_old_fields() {
  fixture value;
  value.fast.mode = capture_mode::accept;
  value.down(1);
  value.fast.fresh = make_sample(500);
  value.down(2);
  require(same_sample(value.emitted_barrier(), value.fast.fresh),
          "new successful capture must replace every old field");
}

void test_failure_does_not_poison_cached_sample() {
  fixture value;
  value.fast.mode = capture_mode::accept;
  value.down(1);
  const auto cached = value.fast.fresh;
  value.fast.mode = capture_mode::reject_partial;
  const auto result = value.down(2);
  require(same_sample(result.visual_sample, cached),
          "failed capture must preserve the valid cached sample");
}

void test_up_submission_order_without_recapture() {
  fixture value;
  value.fast.mode = capture_mode::accept;
  value.down(1);
  const auto up = value.send(input_kind::pointer_up, 2);
  require(up.disposition == route_disposition::engine && value.fast.captures == 1 &&
              value.engine.packets.size() == 2 &&
              value.engine.packets[0].kind == input_kind::pointer_down &&
              value.engine.packets[0].sequence == 1 &&
              value.engine.packets[1].kind == input_kind::pointer_up &&
              value.engine.packets[1].sequence == 2 && same_sample(value.emitted_barrier(), {}),
          "pointer up must follow down once without recapturing a barrier");
}

void test_caller_barrier_cleared_on_failed_down() {
  fixture value;
  auto input = value.packet(input_kind::pointer_down, 1);
  input.barrier = make_sample(900);
  const auto result = value.router.route(input, value.fast, value.engine);
  require(result.disposition == route_disposition::engine &&
              same_sample(value.emitted_barrier(), {}),
          "failed pointer down must not inherit the caller's barrier");
}

void test_stale_generation_has_no_side_effect() {
  fixture value;
  auto input = value.packet(input_kind::pointer_down, 1);
  input.generation.document -= 1;
  const auto result = value.router.route(input, value.fast, value.engine);
  require(result.disposition == route_disposition::rejected_stale &&
              value.fast.captures == 0 && value.engine.packets.empty(),
          "stale input must not reach capture or the engine sink");
}

void test_later_success_survives_failed_partial_capture() {
  fixture value;
  value.fast.mode = capture_mode::reject_partial;
  value.down(1);
  value.fast.mode = capture_mode::accept;
  value.fast.fresh = make_sample(700);
  value.down(2);
  require(same_sample(value.emitted_barrier(), value.fast.fresh),
          "failed capture must not poison a later successful barrier");
}

struct test_case {
  const char* name;
  void (*run)();
};

}  // namespace

int main() {
  const test_case cases[] = {
      {"wheel_then_false_untouched", test_wheel_then_false_untouched},
      {"wheel_then_false_partial", test_wheel_then_false_partial},
      {"prior_success_then_no_target", test_prior_success_then_no_target},
      {"prior_success_then_partial_failure", test_prior_success_then_partial_failure},
      {"no_history_partial_failure", test_no_history_partial_failure},
      {"fresh_success_all_fields_preserved", test_fresh_success_all_fields_preserved},
      {"fresh_success_replaces_all_old_fields", test_fresh_success_replaces_all_old_fields},
      {"failure_does_not_poison_cached_sample", test_failure_does_not_poison_cached_sample},
      {"up_submission_order_without_recapture", test_up_submission_order_without_recapture},
      {"caller_barrier_cleared_on_failed_down", test_caller_barrier_cleared_on_failed_down},
      {"stale_generation_has_no_side_effect", test_stale_generation_has_no_side_effect},
      {"later_success_survives_failed_partial_capture",
       test_later_success_survives_failed_partial_capture},
  };
  unsigned total{};
  unsigned failed{};
  for (const auto& test : cases) {
    ++total;
    try {
      test.run();
      std::cout << "PASS " << test.name << '\n';
    } catch (const std::exception& error) {
      ++failed;
      std::cout << "FAIL " << test.name << ": " << error.what() << '\n';
    } catch (...) {
      ++failed;
      std::cout << "FAIL " << test.name << ": unexpected exception\n";
    }
  }
  std::cout << "interaction_router_tests summary count=" << total
            << " passed=" << total - failed << " failed=" << failed << '\n';
  return failed == 0 ? 0 : 1;
}
