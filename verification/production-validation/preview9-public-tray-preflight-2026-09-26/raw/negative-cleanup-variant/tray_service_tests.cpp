#include <soui/runtime/services/desktop/tray_service.h>
#include <runtime/services/desktop/tray_platform.h>

#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <iostream>
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

}  // namespace

int main() {
  try {
    test_success_ownership_and_remove();
    test_invalid_configuration();
    test_tooltip_boundaries();
    test_setup_failures();
    test_add_failure_and_recovery_retry();
    test_version_rollback_and_pending_cleanup();
    test_remove_failure_blocks_activation();
    test_tooltip_commit_and_restart();
    test_rebuild_hint_preserves_uncertain_registration();
    test_event_decode_and_filters();
    test_thread_and_owner_lifetime();
    test_destructor_cleanup();
    test_return_focus();
    test_reentrant_operations_are_rejected();
    std::cout << "tray_service: 14 no-desktop contract groups passed (fake platform; no Explorer/GUI claim)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "tray_service contract failure: " << error.what() << '\n';
    return 1;
  }
}
