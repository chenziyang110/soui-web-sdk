#pragma once

#include "engine_input_service.h"
#include <soui/host/interaction_protocol.h>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <utility>

namespace soui::sdk::detail {

class input_mailbox final : public engine_input_service {
 public:
  static constexpr std::size_t ordinary_limit = 64, edge_limit = 16;
  static constexpr std::size_t byte_limit = 256 * 1024, event_byte_limit = 64 * 1024;
  using handler = std::function<void(const input_packet*)>; // null = ordered recovery.
  explicit input_mailbox(handler dispatch) : dispatch_(std::move(dispatch)) {}

  [[nodiscard]] input_admission_result post(const input_packet& packet) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || quiescing_) return input_admission_result::stopped;
    observe_physical(packet);
    if (recovering_ || quarantined(packet)) return input_admission_result::recovering;
    if (packet.kind == input_kind::cancel) { request_recovery(); return input_admission_result::accepted; }
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
    tail_ = (tail_ + 1) % items_.size(); ++count_; bytes_ += bytes;
    if (edge) ++edges_; else ++ordinary_;
    pending_.store(true, std::memory_order_release);
    return input_admission_result::accepted;
  }

  [[nodiscard]] bool pending() const noexcept override { return pending_.load(std::memory_order_acquire); }
  void quiesce() noexcept override { std::lock_guard<std::mutex> lock(mutex_); quiescing_ = true; }
  void service() noexcept override {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
    for (unsigned count = 0; count < 8; ++count) {
      input_packet packet;
      bool recover = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        if (count_) {
          const bool edge = is_edge(items_[head_].kind);
          bytes_ -= items_[head_].text.size() * sizeof(char16_t);
          std::swap(packet, items_[head_]);
          head_ = (head_ + 1) % items_.size(); --count_;
          if (edge) --edges_; else --ordinary_;
        } else if (recovering_) recover = true;
        else { pending_.store(false, std::memory_order_release); return; }
      }
      // Neither Engine nor mailbox mutex is held across WKE/client reentry.
      try { if (dispatch_) dispatch_(recover ? nullptr : &packet); }
      catch (...) { std::lock_guard<std::mutex> lock(mutex_); request_recovery(); }
      if (recover) {
        std::lock_guard<std::mutex> lock(mutex_);
        quarantined_keys_ = physical_keys_;
        quarantined_pointer_ = physical_pointer_;
        quarantined_ime_ = physical_ime_;
        recovering_ = false;
      }
      if (std::chrono::steady_clock::now() >= deadline) return;
    }
  }
  void close() noexcept override {
    // Owner-only: no service callback can be concurrently using dispatch_.
    std::array<input_packet, ordinary_limit + edge_limit> retired;
    handler released;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true; pending_.store(false, std::memory_order_release);
      items_.swap(retired); released.swap(dispatch_);
      head_ = tail_ = count_ = ordinary_ = edges_ = bytes_ = 0;
      recovering_ = false;
    }
  }

 private:
  static bool is_edge(input_kind kind) noexcept {
    return kind != input_kind::pointer_move && kind != input_kind::wheel && kind != input_kind::ime_update;
  }
  void request_recovery() noexcept { recovering_ = true; pending_.store(true, std::memory_order_release); }
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
  handler dispatch_;
  std::atomic<bool> pending_{false};
  std::size_t head_{0}, tail_{0}, count_{0}, ordinary_{0}, edges_{0}, bytes_{0};
  bool closed_{false}, quiescing_{false}, recovering_{false};
  std::array<bool, 256> physical_keys_{}, quarantined_keys_{};
  bool physical_pointer_{false}, quarantined_pointer_{false}, physical_ime_{false}, quarantined_ime_{false};
};

}  // namespace soui::sdk::detail
