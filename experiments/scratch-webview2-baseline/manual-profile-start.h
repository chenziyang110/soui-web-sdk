#pragma once
#include <windows.h>
#include <stdexcept>

namespace soui::scratch_diagnostic {
inline ULONGLONG profile_budget_begin(ULONGLONG now, ULONGLONG original_epoch) {
  if (original_epoch > now) throw std::runtime_error("Diagnostic budget cannot begin in the future");
  return original_epoch ? original_epoch : now;
}
class manual_profile_start final {
 public:
  static constexpr UINT command_id = 0x5f71;
  explicit manual_profile_start(bool enabled = false) noexcept : enabled_(enabled) {}
  bool enabled() const noexcept { return enabled_; }
  bool requested() const noexcept { return requested_; }
  bool waiting() const noexcept { return enabled_ && !requested_; }
  bool request(bool ready, bool owner_foreground) noexcept {
    if (!waiting() || !ready || !owner_foreground) return false;
    requested_ = true;
    return true;
  }
  bool page_admitted(bool native_guard, bool page_focus) const noexcept {
    return !enabled_ || (requested_ && native_guard && page_focus);
  }
  void attach(HWND owner) const {
    if (!enabled_) return;
    const auto menu = CreateMenu();
    if (!menu) throw std::runtime_error("Manual-start menu creation failed");
    if (!AppendMenuW(menu, MF_STRING | MF_GRAYED, command_id, L"开始采集（等待页面准备）") || !SetMenu(owner, menu)) {
      DestroyMenu(menu);
      throw std::runtime_error("Manual-start menu attachment failed");
    }
    // SetMenu transferred ownership: DestroyWindow destroys the attached menu.
    if (!DrawMenuBar(owner)) throw std::runtime_error("Manual-start menu layout failed");
  }
  void reflect_readiness(HWND owner, bool ready) {
    if (!enabled_ || (ready == menu_ready_ && requested_ == menu_requested_)) return;
    const auto menu = GetMenu(owner);
    const auto flags = MF_BYCOMMAND | MF_STRING | ((ready && !requested_) ? MF_ENABLED : MF_GRAYED);
    if (!menu || !ModifyMenuW(menu, command_id, flags, command_id,
        requested_ ? L"采集中（请保持前台）" : L"开始采集（点击后保持前台约30秒）") || !DrawMenuBar(owner))
      throw std::runtime_error("Manual-start menu state failed");
    menu_ready_ = ready;
    menu_requested_ = requested_;
  }
 private:
  bool enabled_{}, requested_{}, menu_ready_{}, menu_requested_{};
};
inline bool profile_owner_guard(HWND window) noexcept {
  return GetForegroundWindow() == window && IsWindowVisible(window) && !IsIconic(window);
}
} // namespace soui::scratch_diagnostic
