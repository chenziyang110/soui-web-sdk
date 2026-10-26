#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <type_traits>

namespace content { namespace input_frame_trace {

constexpr std::uint32_t abi_version = 1;
constexpr std::size_t maximum_threads = 32;
constexpr std::size_t events_per_thread = 1024;
constexpr std::size_t duration_buckets = 64;
// Diagnostic correlation only; never input authority. x64 upper bits must be zero.
constexpr std::uint32_t decode_injection_tag(std::uint64_t tag) noexcept {
  return tag <= 0xffffffffULL && (tag & 0xfff00000ULL) == 0x53400000ULL &&
      (tag & 0xfffffULL) ? static_cast<std::uint32_t>(tag & 0xfffffULL) : 0U;
}

enum class mode : std::uint32_t { scalar = 0, detailed = 1 };
enum class stage : std::uint32_t {
  host_receive = 1, admission = 2, engine_begin = 3, handler_return = 4,
  microtasks_complete = 5, commit_complete = 6, resources_ready = 7,
  submit = 8, desktop_observed = 9, input_terminal = 10,
  task_posted = 11, task_begin = 12, task_end = 13, task_cancelled = 14,
  wake_requested = 15, wake_consumed = 16, bridge_request = 17,
  bridge_response = 18, layout_begin = 19, layout_end = 20,
  record_begin = 21, record_end = 22, composite_begin = 23,
  composite_end = 24, frame_coverage = 25, resource_wait = 26,
  scene_pending = 27, scene_activated = 28,
};
constexpr std::size_t stage_count = 29; // zero is deliberately unused.

enum class terminal : std::uint32_t {
  none = 0, visible = 1, coalesced = 2, cancelled = 3, stale = 4,
  not_visible = 5, rejected = 6,
};
enum class reason : std::uint32_t {
  none = 0, accepted = 1, capacity = 2, stopped = 3, stale_generation = 4,
  superseded = 5, navigation = 6, destroyed = 7, hidden = 8,
  pacing = 9, raster = 10, missing_action = 11, inflight_scene = 12,
  post_failed = 13, invalid = 14, recovery = 15, cancelled = 16,
};
enum event_flag : std::uint32_t {
  coverage_valid = 1U, content_marker_same_submission = 2U,
  microtask_causality_established = 4U,
};
enum class counter : std::uint32_t {
  copy_operations, copy_bytes, raster_bytes, lock_contentions,
  scene_pending, scene_activated, resource_notifications, pacing_wakes,
  client_callbacks, buffer_leases, admission_accepted, admission_rejected,
  admission_coalesced, input_recoveries, causal_ranges_dropped,
  causal_context_dropped, count,
};
enum class duration_metric : std::uint32_t {
  lock_wait, lock_hold, client_callback, input_queue, task_queue,
  layout, record, resource_wait, composite, count,
};
constexpr auto counter_count = static_cast<std::size_t>(counter::count);
constexpr auto duration_metric_count = static_cast<std::size_t>(duration_metric::count);

#pragma pack(push, 8)
struct identity final {
  std::uint32_t version{abi_version};
  std::uint32_t process_id{0};
  std::uint64_t run_id_high{0}, run_id_low{0};
  std::uint64_t process_incarnation{0}; // actual process creation FILETIME.
  std::uint64_t qpc_frequency{0};
};

// Zero IDs mean unknown. A coverage range is contiguous, inclusive and explicit;
// disjoint coverage requires separate events. Never infer missing causal stages.
struct event final {
  std::uint32_t struct_size{sizeof(event)};
  std::uint32_t version{abi_version};
  stage point{stage::host_receive};
  reason why{reason::none};
  terminal outcome{terminal::none};
  std::uint32_t flags{0}, thread_id{0}, reserved{0};
  std::uint64_t qpc{0}, view_id{0};
  std::uint64_t view_generation{0}, document_generation{0}, geometry_generation{0};
  std::uint64_t input_first{0}, input_last{0}, task_id{0}, frame_id{0};
  std::uint64_t action_first{0}, action_last{0}, value{0}, related_id{0};
};
#pragma pack(pop)
static_assert(std::is_standard_layout<event>::value &&
              std::is_trivially_copyable<event>::value, "Trace event must remain POD-like");
static_assert(sizeof(event) == 136, "Trace ABI layout changed");
static_assert(sizeof(identity) == 40, "Trace identity ABI layout changed");
static_assert(offsetof(event, qpc) == 32 && offsetof(event, input_first) == 72 &&
              offsetof(event, related_id) == 128, "Trace event offsets changed");
static_assert(offsetof(identity, process_incarnation) == 24 &&
              offsetof(identity, qpc_frequency) == 32, "Trace identity offsets changed");

struct scalar_snapshot final {
  identity session;
  mode recording_mode{mode::scalar};
  std::uint32_t registered_threads{0}, active_producers{0}, pending_registrations{0};
  std::uint64_t attempted{0}, recorded{0}, dropped{0}, queued{0};
  std::uint64_t unregistered_writes{0}, registration_rejections{0}, causal_lost{0};
  bool stopped{false}, quiescent{false}, counter_saturated{false};
  bool owner_violation{false}, invalid_event{false}, clock_invalid{false};
  std::array<std::uint64_t, stage_count> stages{};
  std::array<std::uint64_t, counter_count> counters{};
  std::array<std::array<std::uint64_t, duration_buckets>, duration_metric_count> durations{};
};
enum class record_result : std::uint32_t {
  recorded, scalar_only, stopped, full, invalid, wrong_thread, unregistered,
};
struct drain_result final { std::size_t count{0}; bool busy{false}; };
namespace detail { struct state; }

class writer final {
 public:
  writer() noexcept = default;
  ~writer();
  writer(const writer&) = delete;
  writer& operator=(const writer&) = delete;
  writer(writer&& other) noexcept;
  writer& operator=(writer&& other) noexcept;
  explicit operator bool() const noexcept;
  record_result record(event value) noexcept;
  void add(counter metric, std::uint64_t amount = 1) noexcept;
  void observe_duration(duration_metric metric, std::uint64_t ticks) noexcept;
 private:
  friend class recorder;
  writer(std::shared_ptr<detail::state> storage, std::size_t slot) noexcept;
  void release() noexcept;
  std::shared_ptr<detail::state> storage_;
  std::size_t slot_{maximum_threads};
};

class recorder final {
 public:
  recorder();
  ~recorder();
  recorder(const recorder&) = delete;
  recorder& operator=(const recorder&) = delete;
  recorder(recorder&&) = delete;
  recorder& operator=(recorder&&) = delete;
  // Cold, before any registration. A process/run is intentionally single-session.
  bool configure(identity session, mode requested) noexcept;
  writer attach_current_thread();
  void stop() noexcept; // Nonblocking. Existing writer handles pin all storage.
  bool quiescent() const noexcept;
  scalar_snapshot snapshot() const noexcept;
  // Exactly one consumer. Busy rejects; count and scan are both bounded.
  drain_result drain(event* output, std::size_t capacity) noexcept;
  void note_unregistered_write() noexcept;
  void note_causal_loss(std::uint64_t amount = 1) noexcept;
 private:
  std::shared_ptr<detail::state> storage_;
};

// Actual process identity/QPC only. run_id is supplied by the run supervisor.
identity current_identity(std::uint64_t run_id_high,
                                        std::uint64_t run_id_low) noexcept;
std::uint64_t now_qpc() noexcept;
recorder& process_recorder() noexcept;
bool register_current_thread(); // Cold; never called by record.
void detach_current_thread() noexcept;
record_result record(event value) noexcept;
void add(counter metric, std::uint64_t amount = 1) noexcept;
void observe_duration(duration_metric metric, std::uint64_t ticks) noexcept;
// Constant-initialized TLS POD only; no lazy allocation or implicit registration.
bool current_context(event& output) noexcept;
bool set_current_context(const event& value) noexcept;
void clear_current_context() noexcept;
std::uint64_t next_task_id() noexcept; // zero => diagnostic loss, never wrap/reuse.
void note_causal_loss(std::uint64_t amount = 1) noexcept;
class context_scope final {
 public:
  explicit context_scope(const event& value) noexcept;
  ~context_scope();
  context_scope(const context_scope&) = delete;
  context_scope& operator=(const context_scope&) = delete;
  context_scope(context_scope&&) = delete;
  context_scope& operator=(context_scope&&) = delete;
 private:
  event previous_;
  bool previous_valid_{false}, installed_{false};
};

// Caller-driven, nonengine export. No automatic thread, timer or queue posts.
// begin/drain may run while producers live; finish refuses until stopped,
// all producers released and every buffered event drained. No file is opened.
class exporter final {
 public:
  exporter(std::ostream& output, recorder& source) noexcept;
  exporter(const exporter&) = delete;
  exporter& operator=(const exporter&) = delete;
  bool begin();
  drain_result drain(std::size_t budget = 256);
  bool finish();
 private:
  std::ostream& output_;
  recorder& source_;
  bool begun_{false}, finished_{false};
};

} } // namespace content::input_frame_trace
