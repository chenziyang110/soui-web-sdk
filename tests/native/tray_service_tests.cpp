#include <soui/runtime/services/desktop/tray_service.h>
#include <runtime/services/desktop/tray_platform.h>
#include "../../demos/workbench/app/tray_callback_queue.h"
#include "../tray-consumer/tray_callback_queue.h"

#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using soui::runtime::tray_event;
using soui::runtime::tray_options;
using soui::runtime::tray_service;
using soui::runtime::detail::tray_native_data;
using soui::runtime::detail::tray_native_operation;
using soui::runtime::detail::tray_service_test_access;

static_assert(!std::is_copy_constructible_v<tray_service>);
static_assert(!std::is_move_constructible_v<tray_service>);

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void require_error(const soui::runtime::desktop_response& response, const char* code) {
  require(!response.ok() && response.code == code, "Expected explicit tray error code");
}

struct recorded_call { tray_native_operation operation; tray_native_data data; };
enum class focus_step { foreground, acquire, environment, activate, barrier, gui, wait, release, post, count };
struct focus_call { focus_step step; std::uint32_t requested; std::uint64_t began; };
struct recorded_wake { void* owner; std::uint32_t message; std::uintptr_t ticket; };

// Every HWND, HICON, process/thread identity and Shell call below is fake.
// The test uses only UTF conversion from Win32; it creates no desktop objects.
class fake_platform final : public soui::runtime::detail::tray_platform {
 public:
  void* window{reinterpret_cast<void*>(0x101)};
  void* original_icon{reinterpret_cast<void*>(0x202)};
  void* copied_icon{reinterpret_cast<void*>(0x303)};
  std::uint32_t thread{7};
  std::uint32_t process{11};
  std::uint32_t rebuilt{0xC123};
  soui::runtime::detail::tray_window_identity identity{true, false, false, 11, 7};
  bool copy_succeeds{true};
  bool add_succeeds{true};
  bool version_succeeds{true};
  bool remove_succeeds{true};
  bool update_succeeds{true};
  bool focus_succeeds{true};
  unsigned copies{0};
  unsigned registrations{0};
  mutable unsigned window_queries{0};
  bool original_destroyed{false};
  std::vector<void*> destroyed;
  std::vector<recorded_call> calls;
  std::function<void(tray_native_operation)> nested;
  void* shell{reinterpret_cast<void*>(0x404)};
  void* foreground{nullptr}; // Default legacy tests retain the original NIM path.
  soui::runtime::detail::tray_focus_target target{soui::runtime::detail::tray_focus_query::known,
      {shell, 91, 93, 12345}, reinterpret_cast<void*>(0x505)};
  soui::runtime::detail::tray_focus_identity observed_target{shell, 91, 93, 12345};
  soui::runtime::detail::tray_focus_query environment_query{soui::runtime::detail::tray_focus_query::known};
  bool target_alive{true}, same_session{true}, same_desktop{true}, menu_clear{true};
  bool activation_succeeds{true}, barrier_succeeds{true}, post_succeeds{true};
  std::vector<soui::runtime::detail::tray_focus_gui> gui_script;
  mutable std::size_t gui_index{0};
  mutable std::uint64_t now_ms{0};
  std::uint32_t actual_wait_ms{0}; // Zero means the requested duration.
  std::array<std::uint64_t, static_cast<std::size_t>(focus_step::count)> focus_costs{};
  mutable std::vector<focus_call> focus_calls;
  std::function<void(focus_step)> focus_nested;
  unsigned leases_opened{0}, leases_closed{0};
  std::vector<recorded_wake> wakes;

  void focus_step_called(focus_step step, std::uint32_t requested = 0) const noexcept {
    focus_calls.push_back({step, requested, now_ms});
    now_ms += focus_costs[static_cast<std::size_t>(step)];
    if (focus_nested) focus_nested(step);
  }
  unsigned focus_count(focus_step step) const {
    return static_cast<unsigned>(std::count_if(focus_calls.begin(), focus_calls.end(),
        [step](const focus_call& call) { return call.step == step; }));
  }

  std::uint32_t current_thread_id() const noexcept override { return thread; }
  std::uint32_t current_process_id() const noexcept override { return process; }
  soui::runtime::detail::tray_window_identity window_identity(void* owner) const noexcept override {
    ++window_queries;
    return owner == window ? identity : soui::runtime::detail::tray_window_identity{};
  }
  std::uint32_t taskbar_created_message() noexcept override { ++registrations; return rebuilt; }
  void* copy_icon(void* icon) noexcept override {
    ++copies;
    return copy_succeeds && icon == original_icon ? copied_icon : nullptr;
  }
  void destroy_icon(void* icon) noexcept override {
    original_destroyed = original_destroyed || icon == original_icon;
    destroyed.push_back(icon);
  }
  bool notify(tray_native_operation operation, const tray_native_data& data) noexcept override {
    calls.push_back({operation, data});
    if (nested) nested(operation);
    switch (operation) {
      case tray_native_operation::add: return add_succeeds;
      case tray_native_operation::set_version: return version_succeeds;
      case tray_native_operation::update_tooltip: return update_succeeds;
      case tray_native_operation::return_focus: return focus_succeeds;
      case tray_native_operation::remove: return remove_succeeds;
    }
    return false;
  }
  std::uint64_t focus_now_ms() const noexcept override { return now_ms; }
  void* focus_foreground() const noexcept override { focus_step_called(focus_step::foreground); return foreground; }
  soui::runtime::detail::tray_focus_target acquire_focus_target() noexcept override {
    focus_step_called(focus_step::acquire);
    if (target.process_lease) ++leases_opened;
    return target;
  }
  soui::runtime::detail::tray_focus_environment observe_focus_environment(
      void*, const soui::runtime::detail::tray_focus_target&) const noexcept override {
    focus_step_called(focus_step::environment);
    return {environment_query, identity, observed_target, foreground, target_alive, same_session, same_desktop, menu_clear};
  }
  void release_focus_target(soui::runtime::detail::tray_focus_target& value) noexcept override {
    focus_step_called(focus_step::release);
    if (value.process_lease) { ++leases_closed; value.process_lease = nullptr; }
  }
  bool activate_focus_target(void*) noexcept override {
    if (activation_succeeds) foreground = shell;
    focus_step_called(focus_step::activate);
    return activation_succeeds;
  }
  soui::runtime::detail::tray_focus_barrier send_focus_barrier(void*, std::uint32_t timeout) noexcept override {
    focus_step_called(focus_step::barrier, timeout);
    return {barrier_succeeds, 0};
  }
  soui::runtime::detail::tray_focus_gui focus_gui(std::uint32_t) const noexcept override {
    focus_step_called(focus_step::gui);
    if (gui_script.empty()) return {soui::runtime::detail::tray_focus_query::known, shell, shell};
    const auto index = gui_index < gui_script.size() ? gui_index++ : gui_script.size() - 1;
    return gui_script[index];
  }
  void wait_focus(std::uint32_t requested) noexcept override {
    focus_step_called(focus_step::wait, requested);
    now_ms += actual_wait_ms ? actual_wait_ms : requested;
  }
  bool post_taskbar_wake(void* owner, std::uint32_t message, std::uintptr_t ticket) noexcept override {
    focus_step_called(focus_step::post);
    wakes.push_back({owner, message, ticket});
    return post_succeeds;
  }
  unsigned count(tray_native_operation operation) const {
    unsigned value = 0;
    for (const auto& call : calls) if (call.operation == operation) ++value;
    return value;
  }
  tray_options options() const { return {window, original_icon, 37, WM_APP + 71, u8"资料同步"}; }
};

std::intptr_t notification(std::uint32_t id, std::uint32_t kind) {
  return static_cast<std::intptr_t>((id << 16U) | kind);
}

std::uintptr_t position(int x, int y) {
  return static_cast<std::uintptr_t>(static_cast<std::uint16_t>(x)) |
         (static_cast<std::uintptr_t>(static_cast<std::uint16_t>(y)) << 16U);
}

void test_success_ownership_and_remove() {
  fake_platform native;
  {
    auto service = tray_service_test_access::create(native);
    require(!service->installed() && !service->desired() && !service->cleanup_pending(), "Initial tray state");
    const auto options = native.options();
    require(service->install(options).ok(), "Install should succeed");
    require(service->installed() && service->desired() && !service->cleanup_pending(), "Confirmed installed state");
    require(native.calls.size() == 2 && native.calls[0].operation == tray_native_operation::add &&
            native.calls[1].operation == tray_native_operation::set_version, "Version must follow each ADD");
    require(native.calls[0].data.window == options.owner_window && native.calls[0].data.id == options.icon_id &&
            native.calls[0].data.callback_message == options.callback_message &&
            native.calls[0].data.icon == native.copied_icon && native.calls[0].data.tooltip == L"资料同步",
            "Installation must retain the correct identity, Unicode tooltip and owned icon copy");
    require_error(service->install(options), "E_TRAY_ACTIVE");
    require(native.copies == 1, "Duplicate installation must not allocate another icon");
    require(service->remove().ok(), "Remove should succeed");
    require(!service->installed() && !service->desired() && !service->cleanup_pending(), "Removed state");
    require(native.destroyed.size() == 1 && native.destroyed[0] == native.copied_icon, "Release the copied icon once");
    require(service->remove().ok(), "Repeated remove should be idempotent");
    (void)service->dispatch_message(native.rebuilt, 0, 0);
    require(native.calls.size() == 3, "Removed tray must not reappear after taskbar creation");
  }
  require(native.destroyed.size() == 1 && !native.original_destroyed, "Never free the caller's icon");
}

void test_invalid_configuration() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  const auto valid = native.options();
  auto options = valid;
  options.icon = nullptr; require_error(service->install(options), "E_INVALID_ARGUMENT");
  options = valid; options.icon_id = 0; require_error(service->install(options), "E_INVALID_ARGUMENT");
  options.icon_id = 65536; require_error(service->install(options), "E_INVALID_ARGUMENT");
  options = valid; options.callback_message = WM_APP - 1; require_error(service->install(options), "E_INVALID_ARGUMENT");
  options.callback_message = 0xC000; require_error(service->install(options), "E_INVALID_ARGUMENT");
  options = valid; options.owner_window = nullptr; require_error(service->install(options), "E_INVALID_ARGUMENT");
  native.identity.valid = false; require_error(service->install(valid), "E_INVALID_ARGUMENT");
  native.identity.valid = true; native.identity.child = true; require_error(service->install(valid), "E_INVALID_ARGUMENT");
  native.identity.child = false; native.identity.message_only = true; require_error(service->install(valid), "E_INVALID_ARGUMENT");
  native.identity.message_only = false; native.identity.process_id = 99; require_error(service->install(valid), "E_INVALID_ARGUMENT");
  native.identity.process_id = native.process; native.identity.thread_id = 99;
  require_error(service->install(valid), "E_THREAD_AFFINITY");
  require(native.copies == 0 && native.calls.empty() && !service->desired(), "Invalid input must not acquire Shell resources");
  native.identity.thread_id = native.thread;
  options = valid; options.icon_id = 65535; options.callback_message = 0xBFFF;
  require(service->install(options).ok(), "Maximum public ID and WM_APP message are valid");
}

void test_tooltip_boundaries() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  const std::vector<std::string> invalid{
    std::string("a\0b", 3), std::string("\xC3\x28", 2), std::string("\xC0\xAF", 2),
    std::string("\xED\xA0\x80", 3), std::string("\xF4\x90\x80\x80", 4), std::string(128, 'x')};
  for (const auto& text : invalid) {
    auto options = native.options(); options.tooltip = text;
    require_error(service->install(options), "E_INVALID_ARGUMENT");
  }
  std::string emoji;
  for (unsigned index = 0; index != 64; ++index) emoji += "\xF0\x9F\x98\x80";
  auto options = native.options(); options.tooltip = emoji;
  require_error(service->install(options), "E_INVALID_ARGUMENT");
  require(native.copies == 0 && native.calls.empty(), "Invalid text must fail before copying or calling Shell");
  emoji.resize(63 * 4); options.tooltip = emoji + "x";
  require(service->install(options).ok(), "127 UTF-16 units including surrogate pairs must fit exactly");
  require(native.calls.front().data.tooltip.size() == 127, "UTF-16 limit must not be UTF-8 byte length");
  require(service->update_tooltip("").ok(), "Empty tooltip is valid");
  const auto call_count = native.calls.size();
  for (const auto& text : invalid) require_error(service->update_tooltip(text), "E_INVALID_ARGUMENT");
  require(native.calls.size() == call_count, "Invalid update must not reach Shell");
}

void test_setup_failures() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  native.rebuilt = 0;
  require_error(service->install(native.options()), "E_TRAY_MESSAGE");
  require(native.copies == 0 && !service->desired(), "Missing recovery registration must stop installation");
  native.rebuilt = 0xC123; native.copy_succeeds = false;
  require_error(service->install(native.options()), "E_TRAY_ICON");
  require(native.calls.empty() && native.destroyed.empty(), "Failed CopyIcon must not add or free a borrowed icon");
}

void test_add_failure_and_recovery_retry() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  native.add_succeeds = false;
  require_error(service->install(native.options()), "E_TRAY_ADD");
  require(!service->installed() && service->desired() && !service->cleanup_pending(), "Failed ADD remains retryable");
  require(native.calls.size() == 1, "Failed ADD must not set version");
  require_error(service->update_tooltip("new"), "E_TRAY_NOT_INSTALLED");
  const auto retry = service->dispatch_message(native.rebuilt, 0, 0);
  require(retry.handled && !retry.response.ok() && service->desired() && !service->installed(), "Recovery failure must be reported");
  native.add_succeeds = true;
  require(service->dispatch_message(native.rebuilt, 0, 0).response.ok() && service->installed(), "Later recovery retry must succeed");
  require(native.copies == 1 && native.count(tray_native_operation::set_version) == 1, "Recovery reuses the owned icon and sets version");
}

void test_version_rollback_and_pending_cleanup() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  native.version_succeeds = false;
  require_error(service->install(native.options()), "E_TRAY_VERSION");
  require(!service->installed() && service->desired() && !service->cleanup_pending(), "Successful rollback is not an interactive tray");
  require(native.calls.size() == 3 && native.calls.back().operation == tray_native_operation::remove, "Version failure must roll back");
  native.remove_succeeds = false;
  const auto restored = service->dispatch_message(native.rebuilt, 0, 0);
  require_error(restored.response, "E_TRAY_CLEANUP_PENDING");
  require(!service->installed() && service->desired() && service->cleanup_pending(), "Rollback failure must remain visible");
  const auto ignored = service->dispatch_message(native.options().callback_message, 0, notification(37, NIN_SELECT));
  require(!ignored.handled && ignored.event == tray_event::none, "Unversioned residue must never activate");
  require_error(service->remove(), "E_TRAY_DELETE");
  require(!service->desired() && service->cleanup_pending() && native.destroyed.empty(), "Removal failure revokes desired state but retains cleanup data");
  const auto calls = native.calls.size();
  (void)service->dispatch_message(native.rebuilt, 0, 0);
  require(native.calls.size() == calls && service->cleanup_pending(), "A removed failed-cleanup icon cannot be revived or declared clean by a hint");
  require_error(service->install(native.options()), "E_TRAY_ACTIVE");
  native.remove_succeeds = true;
  require(service->remove().ok() && !service->cleanup_pending(), "Explicit cleanup retry must succeed");
  require(native.destroyed.size() == 1, "Retained copy is released after confirmed cleanup");
}

void test_remove_failure_blocks_activation() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Install for removal failure");
  native.remove_succeeds = false;
  require_error(service->remove(), "E_TRAY_DELETE");
  require(!service->installed() && !service->desired() && service->cleanup_pending(), "Delete failure must stop activation immediately");
  require(service->dispatch_message(native.options().callback_message, position(1, 2), notification(37, NIN_SELECT)).event == tray_event::none,
          "Lingering Shell callbacks are not active residency");
  native.remove_succeeds = true;
  require(service->remove().ok(), "Removal retry");
}

void test_tooltip_commit_and_restart() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Install for update");
  native.update_succeeds = false;
  require_error(service->update_tooltip("not committed"), "E_TRAY_MODIFY");
  require(service->installed() && service->desired(), "Update failure must retain confirmed residency");
  require(service->dispatch_message(native.rebuilt, 0, 0).response.ok(), "Restore after failed update");
  require(native.calls[native.calls.size() - 2].data.tooltip == L"资料同步", "Recovery must use last confirmed tooltip");
  native.update_succeeds = true;
  require(service->update_tooltip("committed").ok(), "Confirm tooltip update");
  require(service->dispatch_message(native.rebuilt, 0, 0).response.ok(), "Restore after confirmed update");
  require(native.calls[native.calls.size() - 2].data.tooltip == L"committed", "Confirmed tooltip survives recovery");
  require(native.count(tray_native_operation::add) == 3 && native.count(tray_native_operation::set_version) == 3,
          "Every recovery ADD must reset and confirm VERSION_4");
}

void test_rebuild_hint_preserves_uncertain_registration() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Install before duplicate rebuild hint");
  native.add_succeeds = false;
  require_error(service->dispatch_message(native.rebuilt, 0, 0).response, "E_TRAY_ADD");
  require(!service->installed() && service->desired() && service->cleanup_pending(),
          "Failed re-ADD must preserve the possible old registration");
  native.remove_succeeds = false;
  require_error(service->remove(), "E_TRAY_DELETE");
  require(native.count(tray_native_operation::remove) == 1 && native.destroyed.empty(),
          "Removal must attempt DELETE and retain its copy while the old registration is uncertain");
  native.remove_succeeds = true;
  require(service->remove().ok() && native.destroyed.size() == 1, "Confirmed cleanup retry releases the copy");

  native.add_succeeds = true; native.version_succeeds = false; native.remove_succeeds = false;
  require_error(service->install(native.options()), "E_TRAY_CLEANUP_PENDING");
  native.add_succeeds = false;
  require_error(service->dispatch_message(native.rebuilt, 0, 0).response, "E_TRAY_ADD");
  require(service->cleanup_pending(), "Failed recovery must preserve earlier rollback uncertainty");
  native.remove_succeeds = true;
  require(service->remove().ok() && native.destroyed.size() == 2,
          "The previous rollback residue still requires explicit successful cleanup");
}

void test_event_decode_and_filters() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  const auto options = native.options();
  require(!service->dispatch_message(options.callback_message, 0, notification(options.icon_id, NIN_SELECT)).handled,
          "No event before installation");
  require(service->install(options).ok(), "Install for event decoding");
  require(!service->dispatch_message(options.callback_message + 1, 0, notification(options.icon_id, NIN_SELECT)).handled,
          "Wrong callback message must be ignored");
  require(!service->dispatch_message(options.callback_message, 0, notification(options.icon_id + 1, NIN_SELECT)).handled,
          "Wrong ID must be ignored");
  for (const auto event : {NIN_SELECT, NIN_KEYSELECT}) {
    const auto result = service->dispatch_message(options.callback_message, position(-123, -32768), notification(options.icon_id, event));
    require(result.handled && result.response.ok() && result.event == tray_event::activated &&
            result.position_valid && result.x == -123 && result.y == -32768, "v4 activation must decode signed coordinates");
  }
  const auto menu = service->dispatch_message(options.callback_message, position(123, 456), notification(options.icon_id, WM_CONTEXTMENU));
  require(menu.handled && menu.event == tray_event::context_menu && !menu.position_valid && menu.x == 0 && menu.y == 0,
          "Context menu wParam has no documented coordinate meaning");
  const auto unknown = service->dispatch_message(options.callback_message, position(1, 2), notification(options.icon_id, WM_MOUSEMOVE));
  require(unknown.handled && unknown.event == tray_event::none && !unknown.position_valid, "Unexposed notifications must not invent events");
}

void test_thread_and_owner_lifetime() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  const auto options = native.options();
  native.thread = 99;
  require_error(service->install(options), "E_THREAD_AFFINITY");
  require(native.window_queries == 0 && native.calls.empty(), "Wrong thread must be rejected before any window or mutable state operations");
  native.thread = 7;
  require(service->install(options).ok(), "Install on creator thread");
  const auto calls = native.calls.size();
  native.thread = 99;
  require_error(service->update_tooltip("wrong thread"), "E_THREAD_AFFINITY");
  require_error(service->remove(), "E_THREAD_AFFINITY");
  require_error(service->return_focus(), "E_THREAD_AFFINITY");
  require_error(service->dispatch_message(native.rebuilt, 0, 0).response, "E_THREAD_AFFINITY");
  require_error(service->dispatch_message(options.callback_message, 0, notification(options.icon_id, NIN_SELECT)).response, "E_THREAD_AFFINITY");
  native.thread = 7;
  require(service->installed() && service->desired() && native.calls.size() == calls, "Wrong-thread calls must leave owner state untouched");
  native.identity.valid = false;
  require_error(service->return_focus(), "E_INVALID_ARGUMENT");
  const auto event = service->dispatch_message(options.callback_message, 0, notification(options.icon_id, NIN_SELECT));
  require_error(event.response, "E_INVALID_ARGUMENT");
  require(event.event == tray_event::none, "Destroyed owner must not activate");
  require_error(service->remove(), "E_INVALID_ARGUMENT");
  require(!service->desired() && service->cleanup_pending() && native.calls.size() == calls, "Lost owner removal must stay unconfirmed");
  native.identity.valid = true;
  require(service->remove().ok(), "Cleanup when the valid owner is available");
}

void test_destructor_cleanup() {
  fake_platform native;
  {
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Install for destructor");
  }
  require(native.count(tray_native_operation::remove) == 1 && native.destroyed.size() == 1 && !native.original_destroyed,
          "Destructor must make one cleanup attempt and release its copy");
  native.remove_succeeds = false;
  {
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Install for failing destructor cleanup");
  }
  require(native.destroyed.size() == 2, "Failed best-effort Shell cleanup must not leak the owned HICON");
}

void test_return_focus() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require_error(service->return_focus(), "E_TRAY_NOT_INSTALLED");
  require(native.calls.empty(), "Uninstalled focus return must not touch Shell");
  require(service->install(native.options()).ok(), "Install for focus return");
  require(service->return_focus().ok(), "Focus return must report Shell confirmation");
  require(native.calls.back().operation == tray_native_operation::return_focus &&
          native.calls.back().data.window == native.window && native.calls.back().data.id == native.options().icon_id,
          "Focus return must target this tray identity");
  native.focus_succeeds = false;
  require_error(service->return_focus(), "E_TRAY_FOCUS");
  require(service->installed() && service->desired() && !service->cleanup_pending(),
          "Focus failure must not change residency or cleanup state");
  require(service->remove().ok(), "Remove after focus failure");
  require_error(service->return_focus(), "E_TRAY_NOT_INSTALLED");
}

void test_reentrant_operations_are_rejected() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  const auto options = native.options();
  unsigned reentries = 0;
  bool all_busy = true;
  native.nested = [&](tray_native_operation operation) {
    if (operation != tray_native_operation::add && operation != tray_native_operation::remove) return;
    ++reentries;
    all_busy = all_busy && service->install(options).code == "E_BUSY";
    all_busy = all_busy && service->remove().code == "E_BUSY";
    all_busy = all_busy && service->update_tooltip("nested").code == "E_BUSY";
    all_busy = all_busy && service->return_focus().code == "E_BUSY";
    all_busy = all_busy && service->dispatch_message(native.rebuilt, 0, 0).response.code == "E_BUSY";
  };
  require(service->install(options).ok() && service->installed() && service->desired(), "Nested operations must not corrupt outer ADD");
  require(service->remove().ok() && !service->installed() && !service->desired(), "Nested operations must not corrupt outer DELETE");
  native.nested = {};
  require(reentries == 2 && all_busy && native.calls.size() == 3 && native.destroyed.size() == 1,
          "Every nested operation must reject E_BUSY without native work or state changes");
}

void test_focus_known_null_never_ready_keeps_native_uncalled() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Install before readiness test");
  native.foreground = native.window;
  native.actual_wait_ms = 15;
  native.gui_script = {{soui::runtime::detail::tray_focus_query::known, native.shell, nullptr}};
  require_error(service->return_focus(), "E_TRAY_FOCUS");
  require(native.count(tray_native_operation::return_focus) == 0, "No ready state must mean zero NIM");
  require(native.focus_count(focus_step::activate) == 1 && native.focus_count(focus_step::barrier) == 1,
          "Preparation effects are single attempt");
  require(native.leases_opened == native.leases_closed, "Timed-out preparation releases its metadata lease");
}

void test_focus_readiness_transition_preserves_native_false() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Install before readiness transition");
  native.foreground = native.window;
  native.actual_wait_ms = 15;
  native.focus_succeeds = false;
  native.gui_script = {{soui::runtime::detail::tray_focus_query::known, native.shell, nullptr},
                       {soui::runtime::detail::tray_focus_query::known, native.shell, native.shell}};
  require_error(service->return_focus(), "E_TRAY_FOCUS");
  require(native.focus_count(focus_step::wait) == 1, "Known NULL focus waits for a real readiness transition");
  require(native.count(tray_native_operation::return_focus) == 1, "Readiness permits one original NIM");
  require(native.leases_opened == native.leases_closed, "Prepared NIM releases metadata lease");
}

void test_focus_absolute_budget_includes_metadata_and_wait_overshoot() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Install before budget test");
  native.foreground = native.window;
  native.focus_costs[static_cast<std::size_t>(focus_step::acquire)] = 700;
  native.focus_costs[static_cast<std::size_t>(focus_step::barrier)] = 290;
  native.actual_wait_ms = 15;
  native.gui_script = {{soui::runtime::detail::tray_focus_query::known, native.shell, nullptr},
                       {soui::runtime::detail::tray_focus_query::known, native.shell, native.shell}};
  require_error(service->return_focus(), "E_TRAY_FOCUS");
  require(native.count(tray_native_operation::return_focus) == 0, "Readiness after the absolute budget cannot call NIM");
  for (const auto& call : native.focus_calls) {
    if (call.step == focus_step::barrier) require(call.requested == 300, "Barrier receives metadata-adjusted remaining budget");
    if (call.step == focus_step::wait) require(call.requested > 0 && call.requested <= 5 && call.requested <= 1000 - call.began,
                                             "Cooperative wait is bounded by both caps");
  }
  require(native.leases_opened == native.leases_closed, "Overshooting wait cannot leak the target lease");
}

void test_focus_legacy_foreground_selection() {
  for (void* foreground : {static_cast<void*>(nullptr), reinterpret_cast<void*>(0x909)}) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Legacy foreground setup");
    native.foreground = foreground; native.focus_succeeds = false;
    require_error(service->return_focus(), "E_TRAY_FOCUS");
    require(native.count(tray_native_operation::return_focus) == 1 && native.focus_count(focus_step::acquire) == 0 &&
            native.focus_count(focus_step::activate) == 0, "Foreign/unknown foreground keeps old NIM and BOOL");
  }
}

void test_focus_metadata_fallback_and_known_target_rejection() {
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Metadata fallback setup");
    native.foreground = native.window;
    if (scenario < 2) native.target.query = soui::runtime::detail::tray_focus_query::unavailable;
    if (scenario == 1) native.focus_nested = [&](focus_step step) {
      if (step == focus_step::acquire) native.foreground = reinterpret_cast<void*>(0x909);
    };
    if (scenario == 2) native.target.query = soui::runtime::detail::tray_focus_query::unsafe;
    if (scenario == 3) native.environment_query = soui::runtime::detail::tray_focus_query::unavailable;
    const auto result = service->return_focus();
    if (!scenario) require(result.ok(), "Only safe pre-activation missing metadata may use original NIM");
    else require_error(result, "E_TRAY_FOCUS");
    require(native.count(tray_native_operation::return_focus) == (scenario ? 0U : 1U) &&
            native.focus_count(focus_step::activate) == 0, "Unsafe/known-target unknown cannot silently fall back");
    require(native.leases_opened == 1 && native.leases_closed == 1, "Partial target acquisition is released exactly once");
  }
}

void test_focus_activation_barrier_and_gui_failures() {
  for (unsigned scenario = 0; scenario < 3; ++scenario) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Activation failure setup");
    native.foreground = native.window;
    if (!scenario) native.activation_succeeds = false;
    if (scenario == 1) native.barrier_succeeds = false;
    if (scenario == 2) native.gui_script = {{soui::runtime::detail::tray_focus_query::unavailable, nullptr, nullptr}};
    require_error(service->return_focus(), "E_TRAY_FOCUS");
    require(native.count(tray_native_operation::return_focus) == 0 && native.focus_count(focus_step::activate) == 1,
            "An attempted SFW forbids any unqualified fallback NIM");
    require(native.focus_count(focus_step::barrier) == (scenario ? 1U : 0U) && native.leases_opened == native.leases_closed,
            "Effects are not retried and every rejection releases its target");
  }
}

void test_focus_known_environment_eligibility() {
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Known environment eligibility setup");
    native.foreground = native.window;
    if (!scenario) native.target_alive = false;
    if (scenario == 1) native.same_session = false;
    if (scenario == 2) native.same_desktop = false;
    if (scenario == 3) native.menu_clear = false;
    require_error(service->return_focus(), "E_TRAY_FOCUS");
    require(native.focus_count(focus_step::acquire) == 1 && native.focus_count(focus_step::activate) == 0 &&
            native.count(tray_native_operation::return_focus) == 0 && native.leases_opened == native.leases_closed,
            "Known dead/cross-session/other-desktop/menu state is a rejection, never a missing-metadata fallback");
  }
}

void test_focus_target_identity_changes() {
  for (unsigned field = 0; field < 4; ++field) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Identity transition setup");
    native.foreground = native.window;
    native.focus_nested = [&](focus_step step) {
      if (step != focus_step::barrier) return;
      if (!field) native.observed_target.window = reinterpret_cast<void*>(0x808);
      if (field == 1) ++native.observed_target.process_id;
      if (field == 2) ++native.observed_target.thread_id;
      if (field == 3) ++native.observed_target.creation_time;
    };
    require_error(service->return_focus(), "E_TRAY_FOCUS");
    require(native.count(tray_native_operation::return_focus) == 0 && native.leases_closed == 1,
            "Actual identity fields are compared after native callouts");
  }
}

void test_focus_final_readiness_and_foreground_guard() {
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Final readiness setup");
    native.foreground = native.window;
    native.gui_script = {{soui::runtime::detail::tray_focus_query::known, native.shell, native.shell},
                         {soui::runtime::detail::tray_focus_query::known, native.shell, native.shell}};
    if (!scenario) native.gui_script[1].focus = nullptr;
    if (scenario == 1) native.gui_script[1].query = soui::runtime::detail::tray_focus_query::unavailable;
    native.focus_nested = [&](focus_step step) {
      if (step != focus_step::gui || native.focus_count(focus_step::gui) != 2) return;
      if (scenario == 2) native.foreground = reinterpret_cast<void*>(0x909);
      if (scenario == 3) native.now_ms = 1000;
      if (scenario == 4) native.identity.valid = false;
    };
    require_error(service->return_focus(), "E_TRAY_FOCUS");
    require(native.focus_count(focus_step::gui) == 2 && native.focus_count(focus_step::wait) == 0 &&
            native.count(tray_native_operation::return_focus) == 0, "The first ready sample is insufficient for admission");
    require(native.leases_opened == native.leases_closed, "Final-guard rejection closes its lease");
  }
}

void test_focus_clock_bounds_and_poll_limit() {
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Clock boundary setup");
    native.foreground = native.window;
    if (!scenario) native.now_ms = (std::numeric_limits<std::uint64_t>::max)() - 500;
    if (scenario == 1) {
      native.now_ms = 100;
      native.focus_nested = [&](focus_step step) { if (step == focus_step::barrier) native.now_ms = 50; };
    }
    if (scenario == 2) native.focus_costs[static_cast<std::size_t>(focus_step::acquire)] = 1000;
    if (scenario == 3) {
      native.actual_wait_ms = 1;
      native.gui_script = {{soui::runtime::detail::tray_focus_query::known, native.shell, nullptr}};
    }
    require_error(service->return_focus(), "E_TRAY_FOCUS");
    require(native.count(tray_native_operation::return_focus) == 0 && native.leases_opened == native.leases_closed,
            "Overflow/regression/expiry/excessive polls never enter NIM or leak");
    if (scenario == 3) require(native.focus_count(focus_step::gui) == 256 && native.now_ms < 1000,
                               "Poll limit is enforced even before time expires");
  }
}

void test_focus_lease_outlives_native_result_and_release_reentry() {
  for (const bool accepted : {false, true}) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Lease lifetime setup");
    native.foreground = native.window; native.focus_succeeds = accepted;
    bool held_at_native = false, release_after_native = false, release_busy = false, release_hint = false;
    native.nested = [&](tray_native_operation operation) {
      if (operation == tray_native_operation::return_focus)
        held_at_native = native.leases_opened == 1 && native.leases_closed == 0;
    };
    native.focus_nested = [&](focus_step step) {
      if (step != focus_step::release) return;
      release_after_native = native.count(tray_native_operation::return_focus) == 1;
      release_busy = service->remove().code == "E_BUSY" && service->installed() && service->desired() && !service->cleanup_pending();
      const auto hint = service->dispatch_message(native.rebuilt, 0, 0);
      release_hint = hint.handled && hint.response.code == "E_BUSY" && hint.event == tray_event::none;
    };
    const auto result = service->return_focus();
    require(result.ok() == accepted && (accepted || result.code == "E_TRAY_FOCUS"), "Release reentry cannot change the original BOOL");
    require(held_at_native && release_after_native && release_busy && release_hint && native.leases_closed == 1 && native.wakes.size() == 1,
            "Lease survives actual NIM, closes while guarded, and releases once");
    native.nested = {}; native.focus_nested = {};
  }
}

recorded_wake prime_pending_hint(fake_platform& native, tray_service& service) {
  const auto posts_before = native.wakes.size();
  const auto adds_before = native.count(tray_native_operation::add);
  const auto focus_before = native.count(tray_native_operation::return_focus);
  native.foreground = native.window;
  bool tuple = false, unchanged = false;
  native.focus_nested = [&](focus_step step) {
    if (step != focus_step::activate) return;
    const auto result = service.dispatch_message(native.rebuilt, 0, 0);
    tuple = result.handled && result.event == tray_event::none && result.response.code == "E_BUSY" &&
        result.response.kind == soui::runtime::result_kind::failure;
    unchanged = service.installed() && service.desired() && !service.cleanup_pending();
  };
  require_error(service.return_focus(), "E_TRAY_FOCUS");
  native.focus_nested = {};
  require(tuple && unchanged && native.count(tray_native_operation::return_focus) == focus_before && native.wakes.size() == posts_before + 1,
          "Busy hint is latched with the exact tuple and only one deferred post");
  require(native.count(tray_native_operation::add) == adds_before && native.leases_opened == native.leases_closed,
          "No ADD occurs inside focus/guard, and interrupted focus releases its lease");
  return native.wakes.back();
}

soui::runtime::tray_message_result deliver_wake(tray_service& service, const recorded_wake& wake) {
  return service.dispatch_message(wake.message, soui::runtime::detail::tray_taskbar_wake_tag,
                                 static_cast<std::intptr_t>(wake.ticket));
}

void test_focus_busy_remove_aborts_only_preparation() {
  for (const auto phase : {focus_step::activate, focus_step::barrier}) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Busy remove setup");
    native.foreground = native.window;
    bool busy = false, unchanged = false;
    native.focus_nested = [&](focus_step step) {
      if (step != phase) return;
      busy = service->remove().code == "E_BUSY";
      unchanged = service->installed() && service->desired() && !service->cleanup_pending();
    };
    require_error(service->return_focus(), "E_TRAY_FOCUS");
    native.focus_nested = {};
    require(busy && unchanged && native.count(tray_native_operation::return_focus) == 0 &&
            native.count(tray_native_operation::remove) == 0 && native.wakes.empty(), "Busy remove only requests private preparation abort");
    require(service->remove().ok() && !service->desired(), "Actual removal still needs a later nonbusy call");
  }
}

void test_pending_focus_selection_and_later_recovery() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Pending focus setup");
  const auto wake = prime_pending_hint(native, *service);
  const auto acquired = native.focus_count(focus_step::acquire);
  native.foreground = native.window;
  require_error(service->return_focus(), "E_TRAY_FOCUS");
  require(native.focus_count(focus_step::acquire) == acquired && native.count(tray_native_operation::return_focus) == 0 && native.wakes.size() == 1,
          "Pending blocks only owner preparation, without a post chain");
  native.foreground = nullptr; native.focus_succeeds = false;
  require_error(service->return_focus(), "E_TRAY_FOCUS");
  require(native.count(tray_native_operation::return_focus) == 1, "Pending cannot narrow unknown-foreground legacy NIM");
  native.foreground = reinterpret_cast<void*>(0x909); native.focus_succeeds = true;
  require(service->return_focus().ok() && native.count(tray_native_operation::return_focus) == 2 &&
          native.focus_count(focus_step::acquire) == acquired, "Pending cannot narrow foreign-foreground legacy NIM");
  const auto recovered = deliver_wake(*service, wake);
  require(recovered.handled && recovered.response.ok() && service->installed() &&
          native.count(tray_native_operation::add) == 2 && native.count(tray_native_operation::set_version) == 2,
          "A later nonbusy wake performs the actual ADD/VERSION");
}

void test_consumed_busy_wake_gets_one_replacement() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Consumed wake setup");
  const auto old = prime_pending_hint(native, *service);
  native.thread = 99;
  const auto wrong_thread = deliver_wake(*service, old);
  native.thread = 7;
  require(!wrong_thread.handled && wrong_thread.response.code == "E_THREAD_AFFINITY",
          "Wrong-thread delivery cannot consume the queued ticket");
  bool busy = false;
  native.nested = [&](tray_native_operation operation) {
    if (operation != tray_native_operation::update_tooltip) return;
    const auto event = deliver_wake(*service, old);
    busy = event.handled && event.event == tray_event::none && event.response.code == "E_BUSY";
  };
  require(service->update_tooltip("external drive").ok(), "External operation may consume the queued wake while busy");
  native.nested = {};
  require(busy && native.wakes.size() == 2 && native.wakes.back().ticket != old.ticket,
          "Consumed busy wake is invalidated and replaced after the outer guard");
  const auto stale = deliver_wake(*service, old);
  require(stale.handled && stale.response.ok() && native.wakes.size() == 2 && native.count(tray_native_operation::add) == 1,
          "Stale wake cannot turn into a native hint or clear the replacement");
  require(deliver_wake(*service, native.wakes.back()).response.ok() && native.count(tray_native_operation::add) == 2,
          "Replacement remains usable exactly once");
}

void test_post_failure_pending_and_native_hint_supersession() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Post failure setup");
  native.post_succeeds = false;
  const auto failed = prime_pending_hint(native, *service);
  const auto stale = deliver_wake(*service, failed);
  require(stale.handled && stale.response.ok() && native.wakes.size() == 1 && native.count(tray_native_operation::add) == 1,
          "Failed PostMessage has no queued ownership and cannot trigger a self retry");
  native.post_succeeds = true;
  require(service->update_tooltip("later external call").ok() && native.wakes.size() == 2,
          "A later external operation may schedule a pending hint");
  const auto queued = native.wakes.back();
  const auto actual_hint = service->dispatch_message(native.rebuilt, 0, 0);
  require(actual_hint.handled && actual_hint.response.ok() && native.count(tray_native_operation::add) == 2,
          "A real nonbusy hint can perform recovery before the queued wake");
  require(deliver_wake(*service, queued).response.ok() && native.count(tray_native_operation::add) == 2 && native.wakes.size() == 2,
          "Superseded queued wake is discarded without another ADD or post");
}

void test_wake_invalidation_across_remove_and_service_recreation() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Wake identity setup");
  const auto old = prime_pending_hint(native, *service);
  require(service->remove().ok(), "Remove cancels desired residency and deferred work");
  require(deliver_wake(*service, old).response.ok() && native.count(tray_native_operation::add) == 1,
          "Removed identity is never resurrected by its wake");
  require(service->install(native.options()).ok(), "Same object can install a fresh identity");
  const auto fresh = prime_pending_hint(native, *service);
  require(fresh.ticket && fresh.ticket != old.ticket, "Reinstall cannot reuse a wake ticket");
  require(deliver_wake(*service, old).response.ok() && native.count(tray_native_operation::add) == 2,
          "Old wake cannot attach to a reinstalled identity");
  // Destruction happens only after every member call has returned, as required.
  service.reset();
  service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "New service uses the still-live fake owner");
  const auto recreated = prime_pending_hint(native, *service);
  require(recreated.ticket != fresh.ticket && recreated.ticket != old.ticket,
          "Ticket allocation is shared across service instances");
  const auto adds = native.count(tray_native_operation::add);
  require(deliver_wake(*service, fresh).response.ok() && native.count(tray_native_operation::add) == adds,
          "Destroyed service's queued scalar cannot affect its replacement");
  require(deliver_wake(*service, recreated).response.ok() && native.count(tray_native_operation::add) == adds + 1,
          "Dropping stale tickets does not cancel the current one");
}

void test_recovery_reentry_and_failure_do_not_self_post() {
  for (const bool succeeds : {false, true}) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Recovery loop setup");
    const auto wake = prime_pending_hint(native, *service);
    native.add_succeeds = succeeds;
    bool busy = false, unchanged = false;
    native.nested = [&](tray_native_operation operation) {
      if (operation != tray_native_operation::add) return;
      const auto installed = service->installed();
      const auto cleanup = service->cleanup_pending();
      const auto hint = service->dispatch_message(native.rebuilt, 0, 0);
      busy = hint.handled && hint.response.code == "E_BUSY" && hint.event == tray_event::none;
      unchanged = service->installed() == installed && service->cleanup_pending() == cleanup && service->desired();
    };
    const auto result = deliver_wake(*service, wake);
    native.nested = {};
    require(result.response.ok() == succeeds && (succeeds || result.response.code == "E_TRAY_ADD"),
            "Deferred recovery reports its actual ADD outcome");
    require(busy && unchanged && native.wakes.size() == 1 && native.count(tray_native_operation::add) == 2,
            "Recovery reentry only leaves pending; it cannot start a post/ADD chain");
    require(service->remove().ok() && !service->desired(), "Explicit remove cancels even pending recovery failure");
    require(deliver_wake(*service, wake).response.ok() && native.count(tray_native_operation::add) == 2,
            "Cancelled recovery wake cannot revive an icon");
  }
}

void test_taskbar_busy_tuple_wrong_thread_and_native_bool() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Busy tuple setup");
  native.foreground = native.window;
  bool unrelated_busy = false, wrong_thread = false;
  native.focus_nested = [&](focus_step step) {
    if (step != focus_step::barrier) return;
    const auto unrelated = service->dispatch_message(native.options().callback_message, 0,
        notification(native.options().icon_id, WM_CONTEXTMENU));
    unrelated_busy = !unrelated.handled && unrelated.event == tray_event::none && unrelated.response.code == "E_BUSY";
    native.thread = 99;
    const auto wrong = service->dispatch_message(native.rebuilt, 0, 0);
    wrong_thread = !wrong.handled && wrong.response.code == "E_THREAD_AFFINITY";
    native.thread = 7;
  };
  require(service->return_focus().ok() && unrelated_busy && wrong_thread && native.wakes.empty(),
          "Unrelated/wrong-thread messages cannot latch a rebuild or cancel preparation");
  native.focus_nested = {};
  native.foreground = native.window;
  native.focus_succeeds = false;
  bool latched = false, remove_busy = false;
  native.nested = [&](tray_native_operation operation) {
    if (operation != tray_native_operation::return_focus) return;
    const auto hint = service->dispatch_message(native.rebuilt, 0, 0);
    latched = hint.handled && hint.response.code == "E_BUSY";
    remove_busy = service->remove().code == "E_BUSY";
  };
  require_error(service->return_focus(), "E_TRAY_FOCUS");
  native.nested = {};
  require(latched && remove_busy && native.count(tray_native_operation::return_focus) == 2 && native.wakes.size() == 1,
          "Hints inside an entered NIM defer but do not rewrite or retry its FALSE result");
}

void test_ticket_source_saturation_and_roundtrip() {
  const auto limit = static_cast<std::uintptr_t>((std::numeric_limits<std::intptr_t>::max)());
  soui::runtime::detail::tray_taskbar_ticket_source local(limit - 1);
  const auto last = local.reserve();
  require(last == limit - 1 && static_cast<std::uintptr_t>(static_cast<std::intptr_t>(last)) == last,
          "Last usable positive ticket roundtrips without x86 truncation");
  require(local.reserve() == 0 && local.reserve() == 0, "Exhaustion saturates; it cannot wrap to a stale value");
  soui::runtime::detail::tray_taskbar_ticket_source invalid(0);
  require(invalid.reserve() == 0, "Zero is never issued as a wake ticket");
}

void test_taskbar_hint_cancels_each_preparation_boundary() {
  for (const auto phase : {focus_step::foreground, focus_step::acquire, focus_step::environment,
                          focus_step::activate, focus_step::barrier, focus_step::gui, focus_step::wait}) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Boundary cancellation setup");
    native.foreground = native.window;
    if (phase == focus_step::wait)
      native.gui_script = {{soui::runtime::detail::tray_focus_query::known, native.shell, nullptr}};
    bool sent = false, tuple = false, unchanged = false;
    native.focus_nested = [&](focus_step step) {
      if (step != phase || sent) return;
      sent = true;
      const auto hint = service->dispatch_message(native.rebuilt, 0, 0);
      tuple = hint.handled && hint.event == tray_event::none && hint.response.kind == soui::runtime::result_kind::failure &&
          hint.response.code == "E_BUSY";
      unchanged = service->installed() && service->desired() && !service->cleanup_pending();
    };
    require_error(service->return_focus(), "E_TRAY_FOCUS");
    native.focus_nested = {};
    require(sent && tuple && unchanged && native.count(tray_native_operation::return_focus) == 0 && native.wakes.size() == 1 &&
            native.count(tray_native_operation::add) == 1 && native.leases_opened == native.leases_closed,
            "Every preparation boundary checks pending/abort before NIM and only schedules later recovery");
  }
}

void test_focus_last_environment_callout_rejects_late_changes() {
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Last environment boundary setup");
    native.foreground = native.window;
    bool injected = false, deferred_tuple = false;
    native.focus_nested = [&](focus_step step) {
      if (step != focus_step::environment || native.focus_count(focus_step::gui) != 2 || injected) return;
      injected = true;
      if (!scenario) {
        const auto hint = service->dispatch_message(native.rebuilt, 0, 0);
        deferred_tuple = hint.handled && hint.event == tray_event::none && hint.response.code == "E_BUSY";
      }
      if (scenario == 1) native.foreground = reinterpret_cast<void*>(0x909);
      if (scenario == 2) ++native.observed_target.creation_time;
      if (scenario == 3) native.now_ms = 1000;
    };
    require_error(service->return_focus(), "E_TRAY_FOCUS");
    native.focus_nested = {};
    require(injected && native.count(tray_native_operation::return_focus) == 0 && native.leases_opened == native.leases_closed,
            "Changes in the final observable environment callout still reject before NIM");
    if (!scenario) require(deferred_tuple && native.wakes.size() == 1 && native.count(tray_native_operation::add) == 1,
                           "Late pending hint is only deferred after the guarded preparation unwinds");
  }
}

}  // namespace

template<class Queue>
void check_callback_queue_boundaries() {
  Queue queue;
  typename Queue::value_type raw{};
  require(queue.capture(71, (std::numeric_limits<std::uintptr_t>::max)(),
      (std::numeric_limits<std::intptr_t>::min)(), 99, raw), "Capture full-width callback");
  require(queue.defer(71, raw) == Queue::result::direct && queue.size() == 0, "Idle callback stays direct");
  {
    typename Queue::call_scope outer(queue);
    { typename Queue::call_scope inner(queue); require(queue.call_depth() == 2, "Nested tray depth"); }
    require(queue.call_depth() == 1, "Inner guard only lowers depth");
    typename Queue::drain_scope blocked(queue);
    require(!blocked.next(raw), "Cannot drain a live tray member");
    for (unsigned index = 0; index != 64; ++index) {
      require(queue.capture(71, (std::numeric_limits<std::uintptr_t>::max)() - index,
          (std::numeric_limits<std::intptr_t>::min)() + static_cast<std::intptr_t>(index), index, raw), "Capture bounded input");
      require(queue.defer(71, raw) == Queue::result::queued, "Fill all 64 slots");
    }
    require(queue.capture(71, 1, 2, 3, raw) && queue.defer(71, raw) == Queue::result::overflow,
            "Full queue must report overflow without overwriting");
    raw.message = 0xC123;
    require(queue.defer(71, raw) == Queue::result::direct && queue.size() == 64,
            "Taskbar/non-callback messages bypass the FIFO even inside a tray call");
  }
  require(queue.call_depth() == 0 && queue.size() == 64, "Guard never drains on destruction");
  unsigned consumed = 0;
  {
    typename Queue::drain_scope batch(queue);
    typename Queue::drain_scope nested(queue);
    require(!nested.next(raw), "Reentrant drain cannot consume input");
    while (batch.next(raw)) {
      require(raw.first == (std::numeric_limits<std::uintptr_t>::max)() - consumed &&
              raw.second == (std::numeric_limits<std::intptr_t>::min)() + static_cast<std::intptr_t>(consumed) && raw.received_ms == consumed,
              "FIFO preserves message widths and order");
      ++consumed;
    }
  }
  require(consumed == 8 && queue.size() == 56, "One timer batch stops at eight");
  {
    typename Queue::call_scope guard(queue);
    for (unsigned index = 64; index != 72; ++index) {
      require(queue.capture(71, index, static_cast<std::intptr_t>(index), index, raw) &&
              queue.defer(71, raw) == Queue::result::queued, "Ring tail wraps without losing old entries");
    }
  }
  unsigned expected = 8;
  while (queue.size()) {
    typename Queue::drain_scope batch(queue);
    while (batch.next(raw)) {
      require(raw.received_ms == expected++, "Wrapped FIFO order");
    }
  }
  require(expected == 72, "All wrapped entries delivered once");
  {
    typename Queue::call_scope guard(queue);
    require(queue.capture(71, 1, -1, 100, raw) && queue.defer(71, raw) == Queue::result::queued, "Snapshot setup");
  }
  {
    typename Queue::drain_scope batch(queue);
    require(batch.next(raw), "Consume the initial snapshot item");
    { typename Queue::call_scope guard(queue);
      require(queue.capture(71, 2, -2, 101, raw) && queue.defer(71, raw) == Queue::result::queued, "Replay reentry is saved"); }
    require(!batch.next(raw) && queue.size() == 1, "Replay-created input stays for the next timer");
  }
  require(queue.revoke() == 1 && queue.size() == 0, "Exit clears pending input and reports dropped count");
  { typename Queue::call_scope guard(queue); require(queue.defer(71, raw) == Queue::result::revoked, "Exit cannot revive input"); }
  typename Queue::drain_scope stopped(queue);
  require(!stopped.next(raw), "Exited queue cannot replay");
}

template<class Queue>
void check_sdk_callback_queue_replay() {
  for (const bool accepted : {false, true}) {
    fake_platform native;
    auto service = tray_service_test_access::create(native);
    require(service->install(native.options()).ok(), "Caller replay setup");
    native.foreground = native.window; native.focus_succeeds = accepted;
    Queue queue;
    bool injected = false;
    native.focus_nested = [&](focus_step step) {
      if (step != focus_step::barrier || injected) return;
      injected = true;
      const auto busy = service->dispatch_message(native.options().callback_message, 0,
          notification(native.options().icon_id, WM_CONTEXTMENU));
      require(!busy.handled && busy.event == tray_event::none && busy.response.code == "E_BUSY",
              "SDK busy contract remains real and unchanged");
      for (const auto kind : {NIN_SELECT, NIN_KEYSELECT, WM_CONTEXTMENU, WM_MOUSEMOVE, 0x7777}) {
        typename Queue::value_type raw{};
        require(queue.capture(native.options().callback_message, position(-123, -32768),
            notification(native.options().icon_id, kind), 0, raw) &&
            queue.defer(native.options().callback_message, raw) == Queue::result::queued,
            "Caller saves original raw kind without guessing whether it is actionable");
      }
    };
    soui::runtime::desktop_response focused;
    { typename Queue::call_scope guard(queue); focused = service->return_focus(); }
    native.focus_nested = {};
    require(injected && focused.ok() == accepted && (accepted || focused.code == "E_TRAY_FOCUS") &&
            native.count(tray_native_operation::return_focus) == 1, "Queued callbacks cannot rewrite or retry native FALSE");
    unsigned delivered = 0;
    typename Queue::drain_scope batch(queue);
    typename Queue::value_type raw{};
    while (batch.next(raw)) {
      typename Queue::call_scope guard(queue);
      const auto replayed = service->dispatch_message(raw.message, raw.first, raw.second);
      require(replayed.handled && replayed.response.ok(), "Actual SDK accepts replay only after its member returned");
      ++delivered;
    }
    require(delivered == 5 && queue.size() == 0, "All original callbacks replay once");
  }
}

template<class Queue>
void check_sdk_callback_queue_taskbar_and_owner() {
  fake_platform native;
  auto service = tray_service_test_access::create(native);
  require(service->install(native.options()).ok(), "Caller Taskbar setup");
  native.foreground = native.window;
  Queue queue;
  bool injected = false;
  native.focus_nested = [&](focus_step step) {
    if (step != focus_step::barrier || injected) return;
    injected = true;
    typename Queue::value_type raw{};
    require(queue.capture(native.rebuilt, 0, 0, 0, raw) &&
            queue.defer(native.options().callback_message, raw) == Queue::result::direct, "Taskbar bypasses the caller FIFO");
    const auto hint = service->dispatch_message(raw.message, raw.first, raw.second);
    require(hint.handled && hint.response.code == "E_BUSY", "Immediate Taskbar still cancels SDK preparation");
    require(queue.capture(native.options().callback_message, 0, notification(native.options().icon_id, NIN_SELECT), 0, raw) &&
            queue.defer(native.options().callback_message, raw) == Queue::result::queued, "Callback remains saved independently");
  };
  { typename Queue::call_scope guard(queue); require_error(service->return_focus(), "E_TRAY_FOCUS"); }
  native.focus_nested = {};
  require(injected && native.count(tray_native_operation::return_focus) == 0 && native.wakes.size() == 1,
          "Caller FIFO cannot delay Taskbar or accidentally enter NIM");
  native.identity.valid = false;
  typename Queue::drain_scope batch(queue);
  typename Queue::value_type raw{};
  require(batch.next(raw), "Saved owner-error callback is available");
  typename Queue::call_scope guard(queue);
  require_error(service->dispatch_message(raw.message, raw.first, raw.second).response, "E_INVALID_ARGUMENT");
}

void test_workbench_callback_queue_boundaries() { check_callback_queue_boundaries<workbench_tray_callbacks::queue>(); }
void test_consumer_callback_queue_boundaries() { check_callback_queue_boundaries<consumer_tray_callbacks::queue>(); }
void test_workbench_sdk_callback_queue_replay() { check_sdk_callback_queue_replay<workbench_tray_callbacks::queue>(); }
void test_consumer_sdk_callback_queue_replay() { check_sdk_callback_queue_replay<consumer_tray_callbacks::queue>(); }
void test_workbench_sdk_callback_taskbar_and_owner() { check_sdk_callback_queue_taskbar_and_owner<workbench_tray_callbacks::queue>(); }
void test_consumer_sdk_callback_taskbar_and_owner() { check_sdk_callback_queue_taskbar_and_owner<consumer_tray_callbacks::queue>(); }

int main() {
  const char* current = "startup";
  unsigned passed = 0;
  try {
    const auto run = [&](const char* name, void (*test)()) { current = name; test(); ++passed; };
#define RUN_TRAY_TEST(test) run(#test, test)
    RUN_TRAY_TEST(test_success_ownership_and_remove);
    RUN_TRAY_TEST(test_invalid_configuration);
    RUN_TRAY_TEST(test_tooltip_boundaries);
    RUN_TRAY_TEST(test_setup_failures);
    RUN_TRAY_TEST(test_add_failure_and_recovery_retry);
    RUN_TRAY_TEST(test_version_rollback_and_pending_cleanup);
    RUN_TRAY_TEST(test_remove_failure_blocks_activation);
    RUN_TRAY_TEST(test_tooltip_commit_and_restart);
    RUN_TRAY_TEST(test_rebuild_hint_preserves_uncertain_registration);
    RUN_TRAY_TEST(test_event_decode_and_filters);
    RUN_TRAY_TEST(test_thread_and_owner_lifetime);
    RUN_TRAY_TEST(test_destructor_cleanup);
    RUN_TRAY_TEST(test_return_focus);
    RUN_TRAY_TEST(test_reentrant_operations_are_rejected);
    RUN_TRAY_TEST(test_focus_known_null_never_ready_keeps_native_uncalled);
    RUN_TRAY_TEST(test_focus_readiness_transition_preserves_native_false);
    RUN_TRAY_TEST(test_focus_absolute_budget_includes_metadata_and_wait_overshoot);
    RUN_TRAY_TEST(test_focus_legacy_foreground_selection);
    RUN_TRAY_TEST(test_focus_metadata_fallback_and_known_target_rejection);
    RUN_TRAY_TEST(test_focus_activation_barrier_and_gui_failures);
    RUN_TRAY_TEST(test_focus_known_environment_eligibility);
    RUN_TRAY_TEST(test_focus_target_identity_changes);
    RUN_TRAY_TEST(test_focus_final_readiness_and_foreground_guard);
    RUN_TRAY_TEST(test_focus_clock_bounds_and_poll_limit);
    RUN_TRAY_TEST(test_focus_lease_outlives_native_result_and_release_reentry);
    RUN_TRAY_TEST(test_focus_busy_remove_aborts_only_preparation);
    RUN_TRAY_TEST(test_pending_focus_selection_and_later_recovery);
    RUN_TRAY_TEST(test_consumed_busy_wake_gets_one_replacement);
    RUN_TRAY_TEST(test_post_failure_pending_and_native_hint_supersession);
    RUN_TRAY_TEST(test_wake_invalidation_across_remove_and_service_recreation);
    RUN_TRAY_TEST(test_recovery_reentry_and_failure_do_not_self_post);
    RUN_TRAY_TEST(test_taskbar_busy_tuple_wrong_thread_and_native_bool);
    RUN_TRAY_TEST(test_ticket_source_saturation_and_roundtrip);
    RUN_TRAY_TEST(test_taskbar_hint_cancels_each_preparation_boundary);
    RUN_TRAY_TEST(test_focus_last_environment_callout_rejects_late_changes);
    RUN_TRAY_TEST(test_workbench_callback_queue_boundaries);
    RUN_TRAY_TEST(test_consumer_callback_queue_boundaries);
    RUN_TRAY_TEST(test_workbench_sdk_callback_queue_replay);
    RUN_TRAY_TEST(test_consumer_sdk_callback_queue_replay);
    RUN_TRAY_TEST(test_workbench_sdk_callback_taskbar_and_owner);
    RUN_TRAY_TEST(test_consumer_sdk_callback_taskbar_and_owner);
#undef RUN_TRAY_TEST
    std::cout << "tray_service: " << passed << " no-desktop contract groups passed (fake platform; no Explorer/GUI claim)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "tray_service contract failure in " << current << ": " << error.what() << '\n';
    return 1;
  }
}
