#pragma once

#include "engine_input_service.h"
#include "input_trace_hooks.h"
#include <soui/host/interaction_protocol.h>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace soui::sdk::detail {

class input_mailbox final : public engine_input_service {
 public:
  static constexpr std::size_t ordinary_limit = 64, edge_limit = 16;
  static constexpr std::size_t byte_limit = 256 * 1024, event_byte_limit = 64 * 1024;
  static constexpr bool valid_native_text_bytes(std::int64_t bytes) noexcept {
    return bytes >= 0 && bytes <= static_cast<std::int64_t>(event_byte_limit) && bytes % sizeof(char16_t) == 0;
  }
  using handler = std::function<void(const input_packet*)>; // null = ordered recovery.
  // Called with mailbox mutex held, after payload copy. Engine publishes head
  // and pending in the same mutex transaction that assigns the ordinal.
  using order_committer = std::function<std::uint64_t(std::atomic<std::uint64_t>&, std::atomic<bool>&, bool, bool)>;
  using cutoff_reader = std::function<std::uint64_t()>;
  explicit input_mailbox(handler dispatch, order_committer commit = {}, cutoff_reader cutoff = {}, std::uint64_t view_id = 0)
      : dispatch_(std::make_shared<handler>(std::move(dispatch))), commit_(std::move(commit)), cutoff_(std::move(cutoff)), trace_view_id_(view_id) {}

  [[nodiscard]] input_admission_result post(const input_packet& packet) noexcept {
    input_packet retired; // Rejected payload retirement follows mutex release.
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || quiescing_) return input_admission_result::stopped;
    observed_generation_ = packet.generation;
    if (blocked_) {
      if (packet.generation.view == blocked_generation_.view && packet.generation.document == blocked_generation_.document) {
        observe_physical(packet);
        return input_admission_result::stopped;
      }
      blocked_ = false;
      quarantined_keys_ = physical_keys_;
      quarantined_pointer_ = physical_pointer_;
      quarantined_ime_ = physical_ime_;
    }
    observe_physical(packet);
    if (recovering_ || quarantined(packet)) return input_admission_result::recovering;
    if (packet.kind == input_kind::cancel) {
      if (!request_recovery()) return input_admission_result::stopped;
      recovery_generation_ = packet.generation; recovery_sequence_ = packet.sequence;
      return input_admission_result::accepted;
    }
    const bool edge = is_edge(packet.kind);
    if (packet.text.size() > event_byte_limit / sizeof(char16_t)) {
      request_recovery(); return input_admission_result::capacity;
    }
    const auto bytes = packet.text.size() * sizeof(char16_t);
    if (bytes > byte_limit - bytes_
        || (edge ? edges_ == edge_limit : ordinary_ == ordinary_limit)) {
      request_recovery();
      return input_admission_result::capacity;
    }
    try {
      items_[tail_] = packet;
    } catch (...) { request_recovery(); return input_admission_result::no_memory; }
    const auto order = commit_order(false);
    if (!order) { std::swap(retired, items_[tail_]); request_recovery(); return input_admission_result::stopped; }
    // No throwing work between publication and completing the fixed slot.
    orders_[tail_] = order;
    tail_ = (tail_ + 1) % items_.size(); ++count_; bytes_ += bytes;
    queued_qpc_[(tail_ + items_.size() - 1) % items_.size()] = input_trace::now_qpc();
    if (edge) ++edges_; else ++ordinary_;
    return input_admission_result::accepted;
  }

  [[nodiscard]] bool pending() const noexcept override { return pending_.load(std::memory_order_acquire); }
  [[nodiscard]] std::uint64_t oldest_order() const noexcept override { return published_head_.load(std::memory_order_acquire); }
  void block_document(interaction_generation generation) noexcept {
    std::lock_guard<std::mutex> lock(mutex_); blocked_generation_ = generation; blocked_ = true;
  }
  void quiesce() noexcept override { std::lock_guard<std::mutex> lock(mutex_); quiescing_ = true; }
  void service() noexcept override { service_before(0); }
  void service_before(std::uint64_t exclusive_order) noexcept override {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
    for (unsigned count = 0; count < 8; ++count) {
      input_packet packet;
      std::shared_ptr<handler> dispatch; // Cold-owned callable pin; copying it never allocates.
      bool recover = false;
      std::uint64_t queued_qpc = 0;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        // A FIFO may be appended while a no-FIFO batch is running. Recheck
        // before every pop; Engine reads only atomic heads in the other direction.
        auto cutoff = exclusive_order;
        if (!cutoff && cutoff_) {
          try { cutoff = cutoff_(); } catch (...) { return; }
        }
        if (count_) {
          if (!input_order_precedes(orders_[head_], cutoff)) return;
          const bool edge = is_edge(items_[head_].kind);
          bytes_ -= items_[head_].text.size() * sizeof(char16_t);
          std::swap(packet, items_[head_]);
          queued_qpc = queued_qpc_[head_]; queued_qpc_[head_] = 0;
          orders_[head_] = 0;
          head_ = (head_ + 1) % items_.size(); --count_;
          if (edge) --edges_; else --ordinary_;
          publish_head();
        } else if (recovering_) {
          if (!input_order_precedes(recovery_order_, cutoff)) return;
          recover = true; packet.kind = input_kind::cancel;
          packet.generation = recovery_generation_; packet.sequence = recovery_sequence_;
          recovery_sequence_ = 0; // The in-flight marker now exclusively owns its terminal.
        }
        else { publish_head(); return; }
        dispatch = dispatch_;
      }
      const auto began = input_trace::now_qpc();
      if (queued_qpc && began >= queued_qpc)
        input_trace::observe_duration(input_trace::duration_metric::input_queue, began - queued_qpc);
      // Neither Engine nor mailbox mutex is held across WKE/client reentry.
      try { if (dispatch && *dispatch) (*dispatch)(recover ? nullptr : &packet); }
      catch (...) {
        input_trace::note_causal_loss();
        if (!recover) (void)input_trace::record(input_event(packet, input_trace::stage::input_terminal,
            input_trace::reason::cancelled, input_trace::terminal::cancelled, trace_view_id_));
        std::lock_guard<std::mutex> lock(mutex_);
        if (!closed_) {
          if (recover) { blocked_generation_ = observed_generation_; blocked_ = true; }
          request_recovery();
        }
      }
      bool closed = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        closed = closed_;
        if (recover && !closed) {
          quarantined_keys_ = physical_keys_;
          quarantined_pointer_ = physical_pointer_;
          quarantined_ime_ = physical_ime_;
          recovering_ = false;
          recovery_order_ = 0;
          publish_head();
        }
      }
      if (recover) {
        input_trace::add(input_trace::counter::input_recoveries);
        if (packet.sequence) (void)input_trace::record(input_event(packet, input_trace::stage::input_terminal,
            closed ? input_trace::reason::destroyed : input_trace::reason::recovery, input_trace::terminal::cancelled, trace_view_id_));
      }
      if (closed) return; // Reentrant close cannot reopen pending/quarantine state.
      if (std::chrono::steady_clock::now() >= deadline) return;
    }
  }
  void close() noexcept override {
    // Owner-only: no service callback can be concurrently using dispatch_.
    std::array<input_packet, ordinary_limit + edge_limit> retired;
    std::shared_ptr<handler> released;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true; pending_.store(false, std::memory_order_release);
      published_head_.store(0, std::memory_order_release);
      for (std::size_t index = 0; index < count_; ++index) {
        const auto& packet = items_[(head_ + index) % items_.size()];
        (void)input_trace::record(input_event(packet, input_trace::stage::input_terminal,
            input_trace::reason::destroyed, input_trace::terminal::cancelled, trace_view_id_));
      }
      if (recovery_sequence_) {
        input_packet marker; marker.kind = input_kind::cancel; marker.sequence = recovery_sequence_; marker.generation = recovery_generation_;
        (void)input_trace::record(input_event(marker, input_trace::stage::input_terminal,
            input_trace::reason::destroyed, input_trace::terminal::cancelled, trace_view_id_));
      }
      items_.swap(retired); released.swap(dispatch_);
      head_ = tail_ = count_ = ordinary_ = edges_ = bytes_ = 0;
      recovering_ = false;
      recovery_sequence_ = 0;
      recovery_order_ = 0;
    }
  }

 private:
  static bool is_edge(input_kind kind) noexcept {
    return kind != input_kind::pointer_move && kind != input_kind::wheel && kind != input_kind::ime_update;
  }
  std::uint64_t commit_order(bool recovery) noexcept {
    const bool first = !count_ && !recovering_;
    if (commit_) {
      try { return commit_(published_head_, pending_, first, recovery); }
      catch (...) { return 0; }
    }
    // Standalone owner/source fixture uses the same no-wrap contract.
    auto order = local_order_ < (std::numeric_limits<std::uint64_t>::max)() - 1 ? ++local_order_ : 0;
    if (!order && recovery) order = (std::numeric_limits<std::uint64_t>::max)();
    if (order) { if (first) published_head_.store(order, std::memory_order_release); pending_.store(true, std::memory_order_release); }
    return order;
  }
  bool request_recovery() noexcept {
    if (recovering_) return true;
    const auto order = commit_order(true);
    if (!order) return false;
    recovery_order_ = order; recovering_ = true;
    return true;
  }
  void publish_head() noexcept {
    const auto order = count_ ? orders_[head_] : (recovering_ ? recovery_order_ : 0);
    published_head_.store(order, std::memory_order_release);
    pending_.store(order != 0, std::memory_order_release);
  }
  void observe_physical(const input_packet& packet) noexcept {
    if (packet.kind == input_kind::key_down && packet.key_code < physical_keys_.size()) physical_keys_[packet.key_code] = true;
    if (packet.kind == input_kind::key_up && packet.key_code < physical_keys_.size()) {
      physical_keys_[packet.key_code] = false; quarantined_keys_[packet.key_code] = false;
    }
    if (packet.kind == input_kind::pointer_down) physical_pointer_ = true;
    if (packet.kind == input_kind::pointer_up || packet.kind == input_kind::capture_lost) physical_pointer_ = false;
    if (packet.kind == input_kind::ime_start) physical_ime_ = true;
    if (packet.kind == input_kind::ime_end) { physical_ime_ = false; quarantined_ime_ = false; }
  }
  bool quarantined(const input_packet& packet) noexcept {
    if (packet.kind == input_kind::key_down && packet.key_code < quarantined_keys_.size()) return quarantined_keys_[packet.key_code];
    if (packet.kind == input_kind::character) for (bool key : quarantined_keys_) if (key) return true;
    if (packet.kind == input_kind::pointer_up || packet.kind == input_kind::capture_lost) quarantined_pointer_ = false;
    if ((packet.kind == input_kind::pointer_down || packet.kind == input_kind::pointer_move) && quarantined_pointer_) return true;
    return quarantined_ime_ && (packet.kind == input_kind::ime_update || packet.kind == input_kind::ime_commit);
  }
  std::mutex mutex_;
  std::array<input_packet, ordinary_limit + edge_limit> items_{};
  std::array<std::uint64_t, ordinary_limit + edge_limit> orders_{};
  std::array<std::uint64_t, ordinary_limit + edge_limit> queued_qpc_{};
  std::shared_ptr<handler> dispatch_;
  order_committer commit_;
  cutoff_reader cutoff_;
  const std::uint64_t trace_view_id_;
  std::atomic<bool> pending_{false};
  std::atomic<std::uint64_t> published_head_{0};
  std::size_t head_{0}, tail_{0}, count_{0}, ordinary_{0}, edges_{0}, bytes_{0};
  bool closed_{false}, quiescing_{false}, recovering_{false};
  bool blocked_{false};
  interaction_generation blocked_generation_;
  interaction_generation observed_generation_;
  interaction_generation recovery_generation_;
  std::uint64_t recovery_sequence_{0};
  std::uint64_t recovery_order_{0}, local_order_{0};
  std::array<bool, 256> physical_keys_{}, quarantined_keys_{};
  bool physical_pointer_{false}, quarantined_pointer_{false}, physical_ime_{false}, quarantined_ime_{false};
};

}  // namespace soui::sdk::detail
