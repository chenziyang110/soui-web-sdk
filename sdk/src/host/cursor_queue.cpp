#include "cursor_queue.h"

#include <limits>

namespace soui::sdk::detail {

bool valid_cursor_descriptor(const cursor_descriptor& value) noexcept {
  if (value.kind == cursor_kind::custom) {
    if (!value.width || !value.height || value.width > 256 || value.height > 256 ||
        value.stride != value.width * 4 || value.hotspot_x < 0 || value.hotspot_y < 0 ||
        static_cast<unsigned>(value.hotspot_x) >= value.width ||
        static_cast<unsigned>(value.hotspot_y) >= value.height ||
        value.pixels.size() != static_cast<std::size_t>(value.stride) * value.height)
      return false;
    // The fixed ABI promises premultiplied BGRA8. Reject a malformed payload
    // before it reaches CreateIconIndirect, rather than repairing its meaning.
    for (std::size_t i = 0; i < value.pixels.size(); i += 4)
      if (value.pixels[i] > value.pixels[i + 3] || value.pixels[i + 1] > value.pixels[i + 3] ||
          value.pixels[i + 2] > value.pixels[i + 3]) return false;
    return value.type == 43;
  }
  return (value.kind == cursor_kind::standard || value.kind == cursor_kind::none ||
          value.kind == cursor_kind::unsupported) && !value.width && !value.height &&
      !value.stride && !value.hotspot_x && !value.hotspot_y && value.pixels.empty() &&
      (value.kind != cursor_kind::standard || (value.type <= 42 && value.type != 37)) &&
      (value.kind != cursor_kind::none || value.type == 37);
}

cursor_queue::cursor_queue(cursor_platform& platform) noexcept
    : platform_(platform), owner_thread_(platform.thread_id()) {}
bool cursor_queue::on_owner_thread() const noexcept {
  return owner_thread_ != 0 && platform_.thread_id() == owner_thread_;
}
unsigned cursor_queue::retained_handles() const noexcept {
  return static_cast<unsigned>(owned_current_ != nullptr) + static_cast<unsigned>(retired_ != nullptr) +
      platform_.unpublished_cursors();
}
bool cursor_queue::select_verified(void* replacement) noexcept {
  platform_.select(replacement);
  return platform_.local_selection() == replacement;
}
bool cursor_queue::retire(void*& handle) noexcept {
  if (!handle) return true;
  if (scope_count_ || platform_.local_selection() == handle) return false;
  const auto global = platform_.global_selection();
  // CURSOR_SHOWING=1 and CURSOR_SUPPRESSED=2 are separate legal states.
  // NULL is only an absence of a global veto after the other P conditions.
  if (!global.succeeded || global.flags > 2 || global.handle == handle) return false;
  if (!platform_.destroy(handle)) return false;
  handle = nullptr;
  return true;
}
bool cursor_queue::retry_retirement() noexcept {
  if (!on_owner_thread() || scope_count_) return false;
  if (!platform_.cleanup_unpublished()) return false;
  if (!retired_ && !selection_unknown_) return true;
  const auto replacement = platform_.standard(0);
  if (!replacement || !select_verified(replacement)) return false;
  // Revoke the only cached selection permits before retirement. No native
  // callbacks, message dispatch, or allocation occurs between these steps.
  selected_owner_ = nullptr;
  selected_publication_ = 0;
  selected_handle_ = replacement;
  const bool old_done = retire(retired_);
  const bool current_done = retire(owned_current_);
  selection_unknown_ = !(old_done && current_done);
  return old_done && current_done;
}
cursor_queue_result cursor_queue::apply(const void* owner, std::uint64_t publication,
                                        const cursor_descriptor& value) noexcept {
  if (!on_owner_thread()) return {cursor_queue_status::wrong_thread, false};
  if (!owner || !publication || !valid_cursor_descriptor(value))
    return {cursor_queue_status::invalid, false};
  if (scope_count_) return {cursor_queue_status::deferred, false};
  if (!platform_.cleanup_unpublished()) return {cursor_queue_status::retirement_pending, false};
  if ((retired_ || selection_unknown_) && !retry_retirement())
    return {cursor_queue_status::retirement_pending, false};
  if (selected_owner_ == owner && selected_publication_ == publication) {
    const bool selected = select_verified(selected_handle_);
    return {selected ? cursor_queue_status::selected : cursor_queue_status::selection_failed, selected};
  }
  if (value.kind == cursor_kind::unsupported) return {cursor_queue_status::unsupported, false};
  void* replacement = nullptr;
  if (value.kind == cursor_kind::custom) replacement = platform_.create(value);
  else if (value.kind == cursor_kind::standard) replacement = platform_.standard(value.type);
  if (value.kind != cursor_kind::none && !replacement)
    return {cursor_queue_status::allocation_failed, false};

  const auto previous = owned_current_;
  owned_current_ = value.kind == cursor_kind::custom ? replacement : nullptr;
  retired_ = previous;
  selected_owner_ = owner;
  selected_publication_ = publication;
  selected_handle_ = replacement;
  const bool selected = select_verified(replacement);
  if (!selected) {
    // Both possible borrowers remain bounded and owned when selection cannot
    // be established. Retrying uses a shared replacement before any destroy.
    selection_unknown_ = true;
    selected_publication_ = 0;
    return {cursor_queue_status::selection_failed, false};
  }
  const bool old_done = retire(retired_);
  return {old_done ? cursor_queue_status::selected : cursor_queue_status::retirement_pending, true};
}
bool cursor_queue::withdraw(const void* owner) noexcept {
  if (!on_owner_thread() || scope_count_) return false;
  if (!platform_.cleanup_unpublished()) return false;
  if (selected_owner_ != owner) return (!retired_ && !selection_unknown_) || retry_retirement();
  const auto replacement = platform_.standard(0);
  if (!replacement || !select_verified(replacement)) return false;
  selected_owner_ = nullptr;
  selected_publication_ = 0;
  selected_handle_ = replacement;
  const bool old_done = retire(retired_);
  const bool current_done = retire(owned_current_);
  selection_unknown_ = !(old_done && current_done);
  return old_done && current_done;
}
std::uint64_t cursor_queue::enter_scope(const void* owner) noexcept {
  if (!on_owner_thread() || !owner || scope_count_ == scopes_.size() ||
      next_scope_ == std::numeric_limits<std::uint64_t>::max()) return 0;
  for (auto& entry : scopes_) if (!entry.sequence) {
    entry = {owner, ++next_scope_};
    ++scope_count_;
    return entry.sequence;
  }
  return 0;
}
bool cursor_queue::leave_scope(const void* owner, std::uint64_t scope) noexcept {
  if (!on_owner_thread() || !owner || !scope) return false;
  for (auto& entry : scopes_) if (entry.owner == owner && entry.sequence == scope) {
    entry = {};
    --scope_count_;
    return true;
  }
  return false;
}

}  // namespace soui::sdk::detail
