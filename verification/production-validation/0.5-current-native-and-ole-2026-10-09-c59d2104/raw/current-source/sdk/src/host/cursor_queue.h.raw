#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace soui::sdk::detail {

// These handles never leave the native queue owner. A descriptor contains no
// Win32 cursor and can safely be copied from the renderer owner.
enum class cursor_kind { standard, none, custom, unsupported };
struct cursor_descriptor {
  cursor_kind kind{cursor_kind::standard};
  std::uint32_t type{0}, width{0}, height{0}, stride{0};
  std::int32_t hotspot_x{0}, hotspot_y{0};
  std::vector<unsigned char> pixels;
};
struct cursor_global_observation {
  bool succeeded{false};
  std::uint32_t flags{0};
  void* handle{nullptr};
};
class cursor_platform {
 public:
  virtual ~cursor_platform() = default;
  virtual std::uint32_t thread_id() const noexcept = 0;
  virtual void* standard(std::uint32_t type) noexcept = 0;
  virtual void* create(const cursor_descriptor&) noexcept = 0;
  virtual void select(void*) noexcept = 0;
  virtual void* local_selection() const noexcept = 0;
  virtual cursor_global_observation global_selection() const noexcept = 0;
  virtual bool destroy(void*) noexcept = 0;
  virtual bool cleanup_unpublished() noexcept = 0;
  virtual unsigned unpublished_cursors() const noexcept = 0;
};
enum class cursor_queue_status {
  selected, deferred, wrong_thread, invalid, unsupported,
  allocation_failed, selection_failed, retirement_pending
};
struct cursor_queue_result {
  cursor_queue_status status{cursor_queue_status::invalid};
  bool selected{false};
};

// One instance per controlled UI thread, shared by that thread's endpoints.
// It retains at most two private handles (current + one awaiting retirement).
// A failed proof prevents another allocation; it never grows a quarantine.
class cursor_queue final {
 public:
  explicit cursor_queue(cursor_platform& platform) noexcept;
  cursor_queue(const cursor_queue&) = delete;
  cursor_queue& operator=(const cursor_queue&) = delete;
  [[nodiscard]] cursor_queue_result apply(const void* owner,
      std::uint64_t publication, const cursor_descriptor&) noexcept;
  [[nodiscard]] bool withdraw(const void* owner) noexcept;
  [[nodiscard]] bool retry_retirement() noexcept;
  [[nodiscard]] std::uint64_t enter_scope(const void* owner) noexcept;
  [[nodiscard]] bool leave_scope(const void* owner, std::uint64_t scope) noexcept;
  [[nodiscard]] unsigned retained_handles() const noexcept;
  [[nodiscard]] unsigned native_scopes() const noexcept { return scope_count_; }
  [[nodiscard]] bool on_owner_thread() const noexcept;

 private:
  [[nodiscard]] bool retire(void*& handle) noexcept;
  [[nodiscard]] bool select_verified(void* replacement) noexcept;
  cursor_platform& platform_;
  const std::uint32_t owner_thread_;
  const void* selected_owner_{nullptr};
  std::uint64_t selected_publication_{0};
  void* selected_handle_{nullptr};
  void* owned_current_{nullptr};
  void* retired_{nullptr};
  bool selection_unknown_{false};
  std::uint64_t next_scope_{0};
  struct scope_entry { const void* owner{nullptr}; std::uint64_t sequence{0}; };
  std::array<scope_entry, 64> scopes_{};
  unsigned scope_count_{0};
};

[[nodiscard]] bool valid_cursor_descriptor(const cursor_descriptor&) noexcept;

}  // namespace soui::sdk::detail
