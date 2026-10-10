#pragma once

// SDK-internal capacity access. Not installed and not reachable through wire JSON.
#include <soui/host/engine_thread.h>
#include <array>
#include <cstdint>
#include <memory>

namespace soui::sdk { class dynamic_miniblink_adapter; }

namespace soui::sdk::detail {

enum class engine_lane : std::uint8_t { ordinary, control, renewal, cleanup };
enum class engine_admission_result : std::uint8_t { accepted, full, stopped, invalid };
struct engine_capacity_state;

// Capacity only: no Engine pointer, authorization, callback or executable work.
// A moved-from/consumed ticket has no credit. Release is safe after Engine death.
class engine_reply_ticket final {
 public:
  engine_reply_ticket() noexcept = default;
  ~engine_reply_ticket();
  engine_reply_ticket(const engine_reply_ticket&) = delete;
  engine_reply_ticket& operator=(const engine_reply_ticket&) = delete;
  engine_reply_ticket(engine_reply_ticket&&) noexcept;
  engine_reply_ticket& operator=(engine_reply_ticket&&) noexcept;
  [[nodiscard]] bool active() const noexcept { return active_; }
 private:
  friend class soui::sdk::engine_thread;
  friend class engine_admission;
  void release() noexcept;
  std::shared_ptr<engine_capacity_state> owner_;
  std::uint64_t generation_{0};
  engine_lane lane_{engine_lane::ordinary};
  bool active_{false};
};

struct engine_capacity_snapshot final {
  std::array<std::size_t, 4> queued{};
  std::array<std::size_t, 4> reserved{};
  std::size_t executing{0};
  std::uint64_t generation{0};
  engine_thread_state state{engine_thread_state::stopped};
};

class engine_admission final {
 public:
  static constexpr std::size_t ordinary_limit = 512;
  static constexpr std::size_t control_limit = 32;
  static constexpr std::size_t renewal_limit = 4;
  static constexpr std::size_t cleanup_limit = 32;
  static constexpr std::size_t queued_limit = engine_thread::queue_capacity;
  static_assert(ordinary_limit + control_limit + renewal_limit + cleanup_limit == queued_limit,
                "Engine lane credits must equal the physical FIFO capacity");
  [[nodiscard]] static engine_admission_result post(engine_thread&, engine_lane, engine_thread::task);
  [[nodiscard]] static engine_admission_result reserve_reply(engine_thread&, engine_lane, engine_reply_ticket&);
  [[nodiscard]] static engine_admission_result submit_reply(engine_thread&, engine_reply_ticket&, engine_thread::task);
  [[nodiscard]] static engine_capacity_snapshot observe(const engine_thread&);
  // Owner-only borrow of the actual mounted Host Engine; invalidated by teardown.
  // No callback, state setter, permission or wire endpoint is introduced.
  [[nodiscard]] static engine_thread* host_engine(dynamic_miniblink_adapter&) noexcept;
};

} // namespace soui::sdk::detail
