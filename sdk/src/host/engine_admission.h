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
#ifdef SOUI_HOST_STAGE0_DEVTOOLS_TEST_ONLY
struct stage0_host_context;
// Sticky milestones, not an ordered history or an authorization snapshot.
enum class stage0_startup_event : std::uint8_t {
  load_requested, load_returned, navigation_entered, navigation_invalid,
  navigation_allowed, navigation_denied, context_callback, context_other_world,
  context_other_frame, context_superseded, context_ack_rejected, context_ack_returned,
  ack_entered, ack_view_rejected, ack_invalidation_rejected, ack_advance_rejected,
  ack_owner_changed, ack_pending_changed, ack_sync_rejected, ack_identity_changed,
  ack_sink_rejected, ack_commit_rejected, ack_final_revoked, ack_completed, ack_exception,
  ready_callback, ready_callback_receipt_rejected, ready_other_frame, ready_callback_superseded,
  ready_entered, ready_receipt_rejected, ready_input_closed, ready_bindings_rejected,
  ready_superseded, ready_committed,
  count
};
static_assert(static_cast<unsigned>(stage0_startup_event::count) <= 64);
#endif

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
#ifdef SOUI_HOST_STAGE0_DEVTOOLS_TEST_ONLY
  // Atomic scalars only: no locks, posting, callbacks, native calls or permission.
  static void note_stage0_startup(dynamic_miniblink_adapter&, stage0_startup_event) noexcept;
  [[nodiscard]] static std::uint64_t stage0_startup_events(const dynamic_miniblink_adapter&) noexcept;
  // Actual mounted SDK Host only; no view/Runtime is manufactured. Call on the
  // retained owner before any close/document transition. Not a public API.
  [[nodiscard]] static bool stage0_context(dynamic_miniblink_adapter&, stage0_host_context&) noexcept;
  // On the original engine task, immediately before using the borrowed view.
  [[nodiscard]] static bool stage0_context_current(dynamic_miniblink_adapter&,
                                                  const stage0_host_context&) noexcept;
#endif
#ifdef SOUI_HOST_UNMOUNT_TEST_ONLY
  // Only the dedicated real-source unit target defines this. No view, DLL,
  // window, permission or installed SDK entry is created by this fixture.
  static void bind_unmount_fixture(dynamic_miniblink_adapter&, engine_thread&,
                                   std::unique_ptr<engine_thread> bridge = {});
  static void bind_reload_fixture(dynamic_miniblink_adapter&, engine_thread&, void* opaque_view);
  [[nodiscard]] static engine_thread* unmount_fixture_engine(dynamic_miniblink_adapter&) noexcept;
#endif
};

} // namespace soui::sdk::detail
