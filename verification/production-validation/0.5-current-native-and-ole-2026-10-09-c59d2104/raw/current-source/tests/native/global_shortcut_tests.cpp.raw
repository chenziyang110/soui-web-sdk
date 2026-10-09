#include <soui/runtime/services/desktop/global_shortcut.h>
#include <runtime/services/desktop/shortcut_platform.h>

#include <windows.h>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using soui::runtime::global_shortcut;
using soui::runtime::global_shortcut_options;
using soui::runtime::global_shortcut_response;
using soui::runtime::detail::global_shortcut_test_access;
using soui::runtime::detail::shortcut_native_result;

static_assert(!std::is_copy_constructible_v<global_shortcut>);
static_assert(!std::is_move_constructible_v<global_shortcut>);
static_assert(soui::runtime::shortcut_modifiers::alt == MOD_ALT);
static_assert(soui::runtime::shortcut_modifiers::control == MOD_CONTROL);
static_assert(soui::runtime::shortcut_modifiers::shift == MOD_SHIFT);
static_assert(soui::runtime::shortcut_modifiers::windows == MOD_WIN);

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void require_error(const global_shortcut_response& response, const char* code, std::uint32_t native_error = 0) {
  require(!response.ok() && response.code == code && response.native_error == native_error,
          "Expected explicit shortcut error and unchanged native error code");
}

struct native_call {
  bool registering;
  void* window;
  std::uint32_t id;
  std::uint32_t modifiers;
  std::uint32_t key;
};

// All HWND, thread/process values and registration operations are fake.
// This executable never calls RegisterHotKey or creates desktop objects.
class fake_platform final : public soui::runtime::detail::shortcut_platform {
 public:
  void* window{reinterpret_cast<void*>(0x101)};
  std::uint32_t thread{7};
  std::uint32_t process{11};
  soui::runtime::detail::shortcut_window_identity identity{true, 11, 7};
  shortcut_native_result registration{true, 0};
  shortcut_native_result removal{true, 0};
  mutable unsigned window_queries{0};
  std::vector<native_call> calls;
  std::function<void(bool)> nested;

  std::uint32_t current_thread_id() const noexcept override { return thread; }
  std::uint32_t current_process_id() const noexcept override { return process; }
  soui::runtime::detail::shortcut_window_identity window_identity(void* owner) const noexcept override {
    ++window_queries;
    return owner == window ? identity : soui::runtime::detail::shortcut_window_identity{};
  }
  shortcut_native_result register_key(void* owner, std::uint32_t id, std::uint32_t modifiers,
                                      std::uint32_t key) noexcept override {
    calls.push_back({true, owner, id, modifiers, key});
    if (nested) nested(true);
    return registration;
  }
  shortcut_native_result unregister_key(void* owner, std::uint32_t id) noexcept override {
    calls.push_back({false, owner, id, 0, 0});
    if (nested) nested(false);
    return removal;
  }
  global_shortcut_options options() const { return {window, 27, MOD_CONTROL | MOD_SHIFT, 'K', true}; }
  unsigned registration_count() const {
    unsigned count = 0;
    for (const auto& call : calls) if (call.registering) ++count;
    return count;
  }
  unsigned removal_count() const {
    return static_cast<unsigned>(calls.size()) - registration_count();
  }
};

std::intptr_t payload(std::uint32_t modifiers, std::uint32_t key) {
  return static_cast<std::intptr_t>((static_cast<std::uintptr_t>(key) << 16U) | modifiers);
}

void test_lifecycle_and_retirement() {
  fake_platform native;
  {
    auto shortcut = global_shortcut_test_access::create(native);
    const auto options = native.options();
    require(!shortcut->active() && !shortcut->cleanup_pending() && !shortcut->retired(), "Initial state must own nothing");
    require(shortcut->unregister_shortcut().ok() && !shortcut->retired() && native.calls.empty(),
            "No-op removal before registration must not retire an unused object");
    require(shortcut->register_shortcut(options).ok(), "Register must succeed");
    require(shortcut->active() && !shortcut->cleanup_pending() && !shortcut->retired(), "Active registration state");
    require(native.calls.size() == 1 && native.calls.front().window == options.owner_window &&
            native.calls.front().id == options.id && native.calls.front().key == options.virtual_key &&
            native.calls.front().modifiers == (options.modifiers | MOD_NOREPEAT), "Native identity and no-repeat defaults");
    require_error(shortcut->register_shortcut(options), "E_SHORTCUT_ACTIVE");
    require(native.registration_count() == 1, "Duplicate registration must not accumulate OS registrations");
    const auto event = shortcut->dispatch_message(WM_HOTKEY, options.id, payload(options.modifiers, options.virtual_key));
    require(event.handled && event.activated && event.response.ok(), "Matching message yields a value event");
    require(shortcut->unregister_shortcut().ok(), "Unregister succeeds");
    require(!shortcut->active() && !shortcut->cleanup_pending() && shortcut->retired(), "Successful removal retires this object");
    require_error(shortcut->register_shortcut(options), "E_SHORTCUT_RETIRED");
    auto new_identity = options; ++new_identity.id;
    require_error(shortcut->register_shortcut(new_identity), "E_SHORTCUT_RETIRED");
    require(shortcut->unregister_shortcut().ok() && native.removal_count() == 1, "Repeated removal is idempotent");
    const auto stale = shortcut->dispatch_message(WM_HOTKEY, options.id, payload(options.modifiers, options.virtual_key));
    require(!stale.handled && !stale.activated, "Queued messages cannot activate a retired registration");
  }
  require(native.removal_count() == 1, "Destruction must not unregister the retired identity a second time");
}

void test_invalid_input_and_owner() {
  fake_platform native;
  auto shortcut = global_shortcut_test_access::create(native);
  const auto valid = native.options();
  auto options = valid; options.id = 0xC000;
  require_error(shortcut->register_shortcut(options), "E_INVALID_ARGUMENT");
  options.id = 0xFFFFFFFF;
  require_error(shortcut->register_shortcut(options), "E_INVALID_ARGUMENT");
  for (const auto modifiers : {0x10U, static_cast<unsigned>(MOD_NOREPEAT), 0x80000000U}) {
    options = valid; options.modifiers = modifiers;
    require_error(shortcut->register_shortcut(options), "E_INVALID_ARGUMENT");
  }
  for (const auto key : {0U, static_cast<unsigned>(VK_F12), 255U, 0xFFFFFFFFU}) {
    options = valid; options.virtual_key = key;
    require_error(shortcut->register_shortcut(options), "E_INVALID_ARGUMENT");
  }
  options = valid; options.owner_window = nullptr;
  require_error(shortcut->register_shortcut(options), "E_INVALID_ARGUMENT");
  native.identity.valid = false;
  require_error(shortcut->register_shortcut(valid), "E_INVALID_ARGUMENT");
  native.identity.valid = true; native.identity.process_id = 99;
  require_error(shortcut->register_shortcut(valid), "E_INVALID_ARGUMENT");
  native.identity.process_id = native.process; native.identity.thread_id = 99;
  require_error(shortcut->register_shortcut(valid), "E_THREAD_AFFINITY");
  require(native.calls.empty() && !shortcut->active() && !shortcut->retired() && !shortcut->cleanup_pending(),
          "Invalid configuration must not call native registration or consume this object");
}

void test_boundaries_and_optional_repeat() {
  fake_platform native;
  {
    auto shortcut = global_shortcut_test_access::create(native);
    auto options = native.options(); options.id = 0; options.modifiers = 0;
    options.virtual_key = 1; options.no_repeat = false;
    require(shortcut->register_shortcut(options).ok(), "Lowest ID/key and no modifiers are accepted");
    require(native.calls.back().id == 0 && native.calls.back().key == 1 && native.calls.back().modifiers == 0,
            "Disabling no-repeat must omit that native flag");
    require(shortcut->dispatch_message(WM_HOTKEY, 0, payload(0, 1)).activated, "Unmodified key message can match");
    require(shortcut->unregister_shortcut().ok(), "Remove minimum registration");
  }
  {
    auto shortcut = global_shortcut_test_access::create(native);
    auto options = native.options(); options.id = 0xBFFF; options.modifiers = 0xF; options.virtual_key = 254;
    require(shortcut->register_shortcut(options).ok(), "Highest allowed ID/key and all four modifiers are accepted");
    require(native.calls.back().modifiers == (0xFU | MOD_NOREPEAT), "No-repeat is distinct from key combination modifiers");
    require(shortcut->dispatch_message(WM_HOTKEY, 0xBFFF, payload(0xF, 254)).activated, "Maximum valid combination matches");
  }
}

void test_native_registration_errors_and_retry() {
  fake_platform native;
  auto shortcut = global_shortcut_test_access::create(native);
  for (const auto error : {1409U, 5U, 0U, 0xF0000001U}) {
    native.registration = {false, error};
    const auto response = shortcut->register_shortcut(native.options());
    require_error(response, "E_SHORTCUT_REGISTER", error);
    require(response.message.find("conflict") == std::string::npos, "Do not invent a conflict diagnosis for all native errors");
    require(!shortcut->active() && !shortcut->cleanup_pending() && !shortcut->retired(), "Failed registration remains unused");
  }
  require(native.removal_count() == 0, "Failed registration owns nothing to unregister");
  native.registration = {true, 0};
  auto retry = native.options(); ++retry.id;
  require(shortcut->register_shortcut(retry).ok(), "A failed registration may retry with a different identity");
  require(shortcut->dispatch_message(WM_HOTKEY, retry.id, payload(retry.modifiers, retry.virtual_key)).activated,
          "Successful retry binds its own identity");
}

void test_unregister_failure_stops_events_and_retains_cleanup() {
  fake_platform native;
  auto shortcut = global_shortcut_test_access::create(native);
  const auto options = native.options();
  require(shortcut->register_shortcut(options).ok(), "Register for failed removal");
  native.removal = {false, 1419};
  require_error(shortcut->unregister_shortcut(), "E_SHORTCUT_UNREGISTER", 1419);
  require(!shortcut->active() && shortcut->cleanup_pending() && !shortcut->retired(), "Failed removal is inactive and unconfirmed");
  const auto event = shortcut->dispatch_message(WM_HOTKEY, options.id, payload(options.modifiers, options.virtual_key));
  require(!event.handled && !event.activated, "A lingering native registration must not activate after removal request");
  auto other = options; ++other.id;
  require_error(shortcut->register_shortcut(other), "E_SHORTCUT_ACTIVE");
  require(native.registration_count() == 1, "Pending cleanup must not be overwritten by another registration");
  native.removal = {false, 5};
  require_error(shortcut->unregister_shortcut(), "E_SHORTCUT_UNREGISTER", 5);
  native.removal = {true, 0};
  require(shortcut->unregister_shortcut().ok() && shortcut->retired() && !shortcut->cleanup_pending(), "Cleanup retry confirms retirement");
  require(native.removal_count() == 3 && native.calls.back().window == options.owner_window && native.calls.back().id == options.id,
          "Every cleanup attempt must retain the original identity");
}

void test_message_filters() {
  fake_platform native;
  auto shortcut = global_shortcut_test_access::create(native);
  const auto options = native.options();
  const auto expected = payload(options.modifiers, options.virtual_key);
  require(!shortcut->dispatch_message(WM_HOTKEY, options.id, expected).activated, "No event before registration");
  require(shortcut->register_shortcut(options).ok(), "Register for filtering");
  const auto ignored = [&](std::uint32_t message, std::uintptr_t id, std::intptr_t packed) {
    const auto event = shortcut->dispatch_message(message, id, packed);
    require(!event.handled && !event.activated && event.response.ok(), "Unrelated notification must be ignored");
  };
  ignored(WM_HOTKEY + 1, options.id, expected);
  ignored(WM_HOTKEY, options.id + 1, expected);
  ignored(WM_HOTKEY, static_cast<std::uintptr_t>(-1), expected);
  ignored(WM_HOTKEY, options.id, payload(options.modifiers ^ MOD_ALT, options.virtual_key));
  ignored(WM_HOTKEY, options.id, payload(options.modifiers | MOD_NOREPEAT, options.virtual_key));
  ignored(WM_HOTKEY, options.id, payload(options.modifiers, options.virtual_key + 1));
  if constexpr (sizeof(std::uintptr_t) > 4) {
    const auto high = static_cast<std::uintptr_t>(0x100000000ULL);
    ignored(WM_HOTKEY, high | options.id, expected);
    ignored(WM_HOTKEY, options.id, static_cast<std::intptr_t>(high | static_cast<std::uintptr_t>(expected)));
  }
  const auto event = shortcut->dispatch_message(WM_HOTKEY, options.id, expected);
  require(event.handled && event.activated && event.response.ok() && native.calls.size() == 1,
          "A matching message is a value event without a native side effect");
}

void test_wrong_thread_and_lost_owner() {
  fake_platform native;
  auto shortcut = global_shortcut_test_access::create(native);
  const auto options = native.options();
  native.thread = 99;
  require_error(shortcut->register_shortcut(options), "E_THREAD_AFFINITY");
  require(native.window_queries == 0 && native.calls.empty(), "Reject thread mismatch before inspecting HWND or native registration");
  native.thread = 7;
  require(shortcut->register_shortcut(options).ok(), "Register on owner thread");
  const auto queries = native.window_queries;
  native.thread = 99;
  require_error(shortcut->unregister_shortcut(), "E_THREAD_AFFINITY");
  require_error(shortcut->dispatch_message(WM_HOTKEY, options.id, payload(options.modifiers, options.virtual_key)).response,
                "E_THREAD_AFFINITY");
  native.thread = 7;
  require(shortcut->active() && native.calls.size() == 1 && native.window_queries == queries,
          "Wrong-thread calls must not mutate registration state");
  native.identity.valid = false;
  const auto event = shortcut->dispatch_message(WM_HOTKEY, options.id, payload(options.modifiers, options.virtual_key));
  require(event.handled && !event.activated, "Invalid owner suppresses matching activation");
  require_error(event.response, "E_INVALID_ARGUMENT");
  require_error(shortcut->unregister_shortcut(), "E_INVALID_ARGUMENT");
  require(!shortcut->active() && shortcut->cleanup_pending() && !shortcut->retired() && native.removal_count() == 0,
          "A lost owner cannot be silently treated as confirmed cleanup");
  native.identity.valid = true;
  require(shortcut->unregister_shortcut().ok(), "Retry with valid owner");
}

void test_reentrant_operations() {
  fake_platform native;
  auto shortcut = global_shortcut_test_access::create(native);
  const auto options = native.options();
  unsigned reentries = 0;
  bool all_busy = true;
  bool removal_is_inactive = true;
  native.nested = [&](bool registering) {
    ++reentries;
    all_busy = all_busy && shortcut->register_shortcut(options).code == "E_BUSY";
    all_busy = all_busy && shortcut->unregister_shortcut().code == "E_BUSY";
    const auto event = shortcut->dispatch_message(WM_HOTKEY, options.id, payload(options.modifiers, options.virtual_key));
    all_busy = all_busy && event.response.code == "E_BUSY" && !event.activated;
    if (!registering) removal_is_inactive = removal_is_inactive && !shortcut->active() && shortcut->cleanup_pending();
  };
  require(shortcut->register_shortcut(options).ok() && shortcut->active(), "Reentry must not corrupt the outer registration");
  require(shortcut->unregister_shortcut().ok() && shortcut->retired(), "Reentry must not corrupt outer cleanup");
  native.nested = {};
  require(all_busy && removal_is_inactive && reentries == 2 && native.calls.size() == 2,
          "Reentry must be rejected without nested native calls or activation during removal");
}

void test_destructor_best_effort() {
  fake_platform native;
  {
    auto unused = global_shortcut_test_access::create(native);
  }
  require(native.calls.empty(), "Unused destruction must not unregister somebody else's shortcut");
  {
    auto shortcut = global_shortcut_test_access::create(native);
    require(shortcut->register_shortcut(native.options()).ok(), "Register before destructor");
  }
  require(native.removal_count() == 1, "Active destruction attempts one native removal");
  native.removal = {false, 5};
  {
    auto shortcut = global_shortcut_test_access::create(native);
    auto options = native.options(); ++options.id;
    require(shortcut->register_shortcut(options).ok(), "Register pending destructor case");
    require_error(shortcut->unregister_shortcut(), "E_SHORTCUT_UNREGISTER", 5);
  }
  require(native.removal_count() == 3, "Pending cleanup gets one best-effort destructor retry");
  native.removal = {true, 0};
  {
    auto shortcut = global_shortcut_test_access::create(native);
    auto options = native.options(); options.id += 2;
    require(shortcut->register_shortcut(options).ok(), "Register invalid-lifetime case");
    native.identity.valid = false;
  }
  require(native.removal_count() == 3, "Destructor must not target an invalid owner HWND");
}

}  // namespace

int main() {
  try {
    test_lifecycle_and_retirement();
    test_invalid_input_and_owner();
    test_boundaries_and_optional_repeat();
    test_native_registration_errors_and_retry();
    test_unregister_failure_stops_events_and_retains_cleanup();
    test_message_filters();
    test_wrong_thread_and_lost_owner();
    test_reentrant_operations();
    test_destructor_best_effort();
    std::cout << "global_shortcut: 9 no-desktop contract groups passed (fake platform; no actual key/GUI claim)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "global_shortcut contract failure: " << error.what() << '\n';
    return 1;
  }
}
