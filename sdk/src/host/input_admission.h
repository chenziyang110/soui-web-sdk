#pragma once

#include "engine_admission.h"
#include <soui/host/interaction_protocol.h>
#include <array>
#include <mutex>
#include <utility>

namespace soui::sdk::detail {

// Host-private capacity leases, not an input-priority queue. Every callback uses
// the existing FIFO and ordinary credits; reply/control/cleanup limits stay put.
class input_admission final {
 public:
  [[nodiscard]] engine_admission_result post(engine_thread& engine,
      const input_packet& packet, engine_thread::task work) {
    if (!work || !packet.generation.view || !packet.generation.document
        || static_cast<std::uint32_t>(packet.kind) > static_cast<std::uint32_t>(input_kind::ime_end)
        || ((packet.kind == input_kind::key_down || packet.kind == input_kind::key_up)
            && packet.key_code >= keys_.size()))
      return engine_admission_result::invalid;
    // work is a function parameter: rejected captures are destroyed only after
    // this local lock is released. All engine calls below retain rejected work.
    std::lock_guard<std::mutex> lock(mutex_);
    if (engine_ != &engine || generation_.view != packet.generation.view
        || generation_.document != packet.generation.document) {
      reset_slots();
      engine_ = &engine;
      engine_generation_ = 0;
      generation_ = packet.generation;
    }
    // Motion/character updates need no ticket lookup or extra Engine snapshot.
    // Any paired edge verifies the epoch before touching its held completions.
    const bool ordinary_update = packet.kind == input_kind::pointer_move || packet.kind == input_kind::wheel
        || packet.kind == input_kind::character || packet.kind == input_kind::ime_update;
    if (!ordinary_update) {
      const auto observed = engine_admission::observe(engine);
      if (engine_generation_ != observed.generation) {
        reset_slots();
        engine_generation_ = observed.generation;
      }
    }
    // Geometry alone never cancels an accepted key or pointer gesture.
    auto result = engine_admission_result::invalid;
    switch (packet.kind) {
      case input_kind::key_down: {
        if (packet.key_code >= keys_.size()) break;
        engine_reply_ticket* slots[]{&keys_[packet.key_code], &blur_};
        result = start(engine, work, slots, 2);
        break;
      }
      case input_kind::key_up:
        if (packet.key_code < keys_.size()) result = finish(engine, work, keys_[packet.key_code]);
        break;
      case input_kind::pointer_down: {
        engine_reply_ticket* slots[]{&pointer_up_, &blur_};
        result = start(engine, work, slots, 2);
        break;
      }
      case input_kind::pointer_up:
        result = finish(engine, work, pointer_up_);
        break;
      case input_kind::focus: {
        engine_reply_ticket* slots[]{&blur_};
        result = start(engine, work, slots, 1);
        break;
      }
      case input_kind::blur:
        result = finish(engine, work, blur_);
        if (result == engine_admission_result::accepted)
          reset_slots(); // Accepted FIFO blur ends this session without synthesizing clicks.
        break;
      case input_kind::ime_start: {
        engine_reply_ticket* slots[]{&ime_commit_, &ime_end_, &blur_};
        result = start(engine, work, slots, 3);
        break;
      }
      case input_kind::ime_commit:
        // A composition may emit more than one result. Only its first commit
        // has a held credit; additional text is ordinary and reports real full.
        result = finish(engine, work, ime_commit_);
        break;
      case input_kind::ime_end:
        result = finish(engine, work, ime_end_);
        if (result == engine_admission_result::accepted)
          ime_commit_ = engine_reply_ticket{};
        break;
      default:
        result = engine_admission::try_post(engine, engine_lane::ordinary, work);
        break;
    }
    if (result == engine_admission_result::invalid || result == engine_admission_result::stopped) {
      // A stale Engine epoch must never leave slots that block a later session.
      reset_slots();
      engine_ = nullptr;
      engine_generation_ = 0;
    }
    return result;
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    reset_slots();
    engine_ = nullptr;
    engine_generation_ = 0;
    generation_ = {};
  }

 private:
  [[nodiscard]] engine_admission_result start(engine_thread& engine, engine_thread::task& work,
      engine_reply_ticket* const* slots, std::size_t count) {
    std::array<engine_reply_ticket, 3> fresh;
    std::array<const engine_reply_ticket*, 3> guards{};
    for (std::size_t i = 0; i < count; ++i) {
      if (slots[i]->active()) guards[i] = slots[i];
      else {
        const auto reserved = engine_admission::reserve_reply(engine, engine_lane::ordinary, fresh[i]);
        if (reserved != engine_admission_result::accepted) return reserved;
        guards[i] = &fresh[i];
      }
    }
    const auto posted = engine_admission::try_post_guarded(engine, work, guards.data(), count);
    if (posted != engine_admission_result::accepted) return posted;
    const auto actual_generation = guards[0]->generation_;
    if (actual_generation != engine_generation_) {
      // Engine restart between observe/reserve/admit is legal only if every
      // completion guard belongs to the actual new epoch. Keep those guards,
      // retire unrelated old leases, then publish the new binding.
      for (std::size_t i = 0; i < count; ++i)
        if (!fresh[i].active()) fresh[i] = std::move(*slots[i]);
      reset_slots();
      engine_generation_ = actual_generation;
    }
    for (std::size_t i = 0; i < count; ++i)
      if (fresh[i].active()) *slots[i] = std::move(fresh[i]);
    return posted;
  }

  [[nodiscard]] static engine_admission_result finish(engine_thread& engine,
      engine_thread::task& work, engine_reply_ticket& slot) {
    const auto result = slot.active()
        ? engine_admission::try_submit_reply(engine, slot, work)
        : engine_admission::try_post(engine, engine_lane::ordinary, work);
    if (result == engine_admission_result::accepted) slot = engine_reply_ticket{};
    return result;
  }

  void reset_slots() noexcept {
    for (auto& key : keys_) key = engine_reply_ticket{};
    pointer_up_ = engine_reply_ticket{};
    blur_ = engine_reply_ticket{};
    ime_commit_ = engine_reply_ticket{};
    ime_end_ = engine_reply_ticket{};
  }

  std::mutex mutex_;
  engine_thread* engine_{nullptr}; // Borrowed only during post; reset before Host teardown.
  std::uint64_t engine_generation_{0};
  interaction_generation generation_;
  std::array<engine_reply_ticket, 256> keys_;
  engine_reply_ticket pointer_up_, blur_, ime_commit_, ime_end_;
};

} // namespace soui::sdk::detail
