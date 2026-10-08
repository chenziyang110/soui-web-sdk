#include <soui/runtime/services/desktop/menu_service.h>
#include <runtime/services/desktop/menu_platform.h>

#include <windows.h>

#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using namespace soui::runtime;
using namespace soui::runtime::detail;
static_assert(!std::is_copy_constructible_v<menu_service>);
static_assert(!std::is_move_constructible_v<menu_service>);

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void error(const desktop_response& result, const char* code) {
  require(!result.ok() && result.code == code, "Expected explicit menu failure");
}
menu_item command(std::uint32_t id, std::string text = "Command", bool enabled = true) {
  return {menu_item_kind::command, id, std::move(text), enabled, false, {}};
}
menu_item submenu(std::vector<menu_item> children, bool enabled = true) {
  return {menu_item_kind::submenu, 0, u8"子菜单", enabled, false, std::move(children)};
}
menu_item separator() { return {menu_item_kind::separator, 0, {}, true, false, {}}; }

struct inserted_item {
  void* parent;
  unsigned position;
  menu_item_kind kind;
  std::uint32_t id;
  std::wstring label;
  bool enabled;
  bool checked;
  void* child;
};

class fake_platform final : public menu_platform {
 public:
  struct node { bool alive{true}; void* parent{nullptr}; std::vector<void*> children; };
  void* owner{reinterpret_cast<void*>(0x101)};
  std::uint32_t thread{7}, process{11};
  menu_window_identity identity{true, 11, 7};
  std::map<void*, node> nodes;
  std::vector<void*> destroy_calls;
  std::vector<inserted_item> inserts;
  unsigned creates{0}, insert_attempts{0}, tracks{0}, ends{0}, foregrounds{0}, finishes{0};
  mutable unsigned queries{0};
  unsigned fail_create_at{0}, fail_insert_at{0};
  bool fail_destroy{false}, fail_foreground{false}, fail_finish{false}, fail_end{false};
  bool invalid_destroy{false}, invalid_transfer{false};
  menu_track_result tracked{0, 0};
  std::uint32_t track_flags{0};
  int track_x{0}, track_y{0};
  void* tracked_owner{nullptr};
  std::function<void()> during_create, during_insert, during_track, during_end, during_destroy, during_foreground;

  std::uint32_t current_thread_id() const noexcept override { return thread; }
  std::uint32_t current_process_id() const noexcept override { return process; }
  menu_window_identity window_identity(void* candidate) const noexcept override {
    ++queries;
    return candidate == owner ? identity : menu_window_identity{};
  }
  menu_create_result create_popup() noexcept override {
    ++creates;
    if (during_create) during_create();
    if (creates == fail_create_at) return {nullptr, 71};
    auto handle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1000 + creates));
    nodes.emplace(handle, node{});
    return {handle, 0};
  }
  menu_native_status insert(void* parent, unsigned position, const menu_native_item& item) noexcept override {
    ++insert_attempts;
    if (during_insert) during_insert();
    if (insert_attempts == fail_insert_at) return {false, 72};
    inserts.push_back({parent, position, item.kind, item.id, std::wstring(item.label), item.enabled, item.checked, item.submenu});
    if (item.submenu) {
      auto& child = nodes.at(item.submenu);
      invalid_transfer = invalid_transfer || child.parent != nullptr || !child.alive;
      child.parent = parent;
      nodes.at(parent).children.push_back(item.submenu);
    }
    return {true, 0};
  }
  void erase_tree(void* root) {
    auto& value = nodes.at(root);
    invalid_destroy = invalid_destroy || !value.alive;
    value.alive = false;
    for (const auto child : value.children) erase_tree(child);
  }
  menu_native_status destroy(void* root) noexcept override {
    destroy_calls.push_back(root);
    if (during_destroy) during_destroy();
    const auto found = nodes.find(root);
    if (found == nodes.end() || !found->second.alive || found->second.parent) {
      invalid_destroy = true;
      return {false, 99};
    }
    if (fail_destroy) return {false, 73};
    erase_tree(root);
    return {true, 0};
  }
  menu_native_status foreground(void* candidate) noexcept override {
    ++foregrounds;
    if (during_foreground) during_foreground();
    return {candidate == owner && !fail_foreground, fail_foreground ? 74U : 0U};
  }
  menu_native_status finish(void* candidate) noexcept override {
    ++finishes;
    return {candidate == owner && !fail_finish, fail_finish ? 75U : 0U};
  }
  menu_track_result track(void*, void* candidate, int x, int y, std::uint32_t flags) noexcept override {
    ++tracks; track_x = x; track_y = y; track_flags = flags; tracked_owner = candidate;
    if (during_track) during_track();
    return tracked;
  }
  menu_native_status end_menu() noexcept override {
    ++ends;
    if (during_end) during_end();
    return {!fail_end, fail_end ? 76U : 0U};
  }
  unsigned alive() const {
    unsigned count = 0;
    for (const auto& entry : nodes) if (entry.second.alive) ++count;
    return count;
  }
};

void test_tree_selection_and_ownership() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  auto checked = command(2, u8"勾选命令"); checked.checked = true;
  native.tracked.command = 2;
  const auto result = menu->show(native.owner, {command(1), separator(), submenu({checked, submenu({command(3)})})}, -55, 123);
  require(result.response.ok() && result.has_selection && result.command_id == 2, "Return a validated selected command");
  require(native.creates == 3 && native.destroy_calls.size() == 1 && native.alive() == 0 &&
          !native.invalid_destroy && !native.invalid_transfer && !menu->cleanup_pending(),
          "Attached descendants must be destroyed only through their ownership root");
  require(native.track_x == -55 && native.track_y == 123 && native.tracked_owner == native.owner &&
          native.track_flags == (TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON), "Track flags and signed coordinates");
  bool saw_checked = false;
  for (const auto& item : native.inserts)
    if (item.id == 2) saw_checked = item.checked && item.label == L"勾选命令";
  require(saw_checked && native.foregrounds == 1 && native.finishes == 1, "Unicode, checked state and native foreground/finish flow");
}

void test_shape_validation_before_resources() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  auto invalid = [&](const std::vector<menu_item>& items) { error(menu->show(native.owner, items, 0, 0).response, "E_INVALID_ARGUMENT"); };
  invalid({}); invalid({command(0)}); invalid({command(65536)}); invalid({command(1), submenu({command(1)})});
  auto item = command(1); item.children.push_back(command(2)); invalid({item});
  item = separator(); item.label = "bad"; invalid({item});
  item = separator(); item.id = 4; invalid({item});
  item = separator(); item.checked = true; invalid({item});
  item = separator(); item.enabled = false; invalid({item});
  item = separator(); item.children.push_back(command(2)); invalid({item});
  invalid({submenu({})}); item = submenu({command(2)}); item.id = 1; invalid({item});
  item = command(1); item.kind = static_cast<menu_item_kind>(99); invalid({item});
  invalid({command(1, std::string("a\0b", 3))}); invalid({command(1, std::string("\xC3\x28", 2))});
  invalid({command(1, std::string("\xED\xA0\x80", 3))});
  require(native.creates == 0 && native.foregrounds == 0, "Validate the entire input before acquiring any HMENU");
  native.tracked.command = 65535;
  require(menu->show(native.owner, {command(65535)}, 0, 0).has_selection, "Maximum command ID is supported");
}

void test_item_and_depth_limits() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  std::vector<menu_item> items;
  for (unsigned id = 1; id <= 256; ++id) items.push_back(command(id));
  require(menu->show(native.owner, items, 0, 0).response.ok(), "256 items are valid");
  const auto acquired = native.creates;
  items.push_back(command(257));
  error(menu->show(native.owner, items, 0, 0).response, "E_MENU_LIMIT");
  require(native.creates == acquired, "Item limit failure must be before resource allocation");
  std::vector<menu_item> deep{command(1)};
  for (unsigned depth = 1; depth < 8; ++depth) deep = {submenu(std::move(deep))};
  require(menu->show(native.owner, deep, 0, 0).response.ok(), "Eight levels are valid");
  const auto before = native.creates;
  deep = {submenu(std::move(deep))};
  error(menu->show(native.owner, deep, 0, 0).response, "E_MENU_LIMIT");
  require(native.creates == before && native.alive() == 0, "Nine levels must fail without resources");
}

void test_utf16_and_total_text_limits() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  const std::string ascii(1024, 'x');
  require(menu->show(native.owner, {command(1, ascii)}, 0, 0).response.ok(), "1024 UTF-16 units allowed");
  error(menu->show(native.owner, {command(1, ascii + "x")}, 0, 0).response, "E_MENU_LIMIT");
  std::string emoji;
  for (unsigned count = 0; count < 512; ++count) emoji += "\xF0\x9F\x98\x80";
  require(menu->show(native.owner, {command(1, emoji)}, 0, 0).response.ok(), "512 surrogate pairs are 1024 UTF-16 units");
  error(menu->show(native.owner, {command(1, emoji + "x")}, 0, 0).response, "E_MENU_LIMIT");
  std::vector<menu_item> items;
  for (unsigned id = 1; id <= 32; ++id) items.push_back(command(id, ascii));
  require(menu->show(native.owner, items, 0, 0).response.ok(), "64 KiB total UTF-16 text is valid");
  const auto acquired = native.creates;
  items.push_back(command(33, "x"));
  error(menu->show(native.owner, items, 0, 0).response, "E_MENU_LIMIT");
  require(native.creates == acquired, "Total text limit must be validated before native creation");
}

void test_owner_and_thread_validation() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  const std::vector<menu_item> items{command(1)};
  native.thread = 99;
  error(menu->show(native.owner, items, 0, 0).response, "E_THREAD_AFFINITY");
  error(menu->cleanup(), "E_THREAD_AFFINITY"); error(menu->cancel(), "E_THREAD_AFFINITY");
  require(native.queries == 0 && native.creates == 0 && native.ends == 0, "Wrong thread must fail before mutable/native work");
  native.thread = 7;
  error(menu->show(nullptr, items, 0, 0).response, "E_INVALID_ARGUMENT");
  native.identity.valid = false; error(menu->show(native.owner, items, 0, 0).response, "E_INVALID_ARGUMENT");
  native.identity.valid = true; native.identity.process_id = 99;
  error(menu->show(native.owner, items, 0, 0).response, "E_INVALID_ARGUMENT");
  native.identity.process_id = 11; native.identity.thread_id = 99;
  error(menu->show(native.owner, items, 0, 0).response, "E_THREAD_AFFINITY");
  require(native.creates == 0, "Invalid owner must not acquire menu handles");
}

void test_creation_failure_reclaims_partial_tree() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  native.fail_create_at = 3;
  const auto result = menu->show(native.owner, {submenu({submenu({command(1)})})}, 0, 0);
  error(result.response, "E_MENU_BUILD");
  require(result.native_error == 71 && native.destroy_calls.size() == 2 && native.alive() == 0 && !native.invalid_destroy,
          "Failed recursive creation must clean each independent root once");
  require(native.tracks == 0 && !menu->cleanup_pending(), "A partial menu must never track");
}

void test_attachment_failure_preserves_two_roots() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  native.fail_insert_at = 2; // First inserts a child command; second attaches that child to root.
  const auto result = menu->show(native.owner, {submenu({command(1)})}, 0, 0);
  error(result.response, "E_MENU_BUILD");
  require(result.native_error == 72 && native.destroy_calls.size() == 2 && native.alive() == 0 && !native.invalid_destroy,
          "Failed attachment must not transfer submenu ownership");
}

void test_failed_destroy_retry_blocks_new_show() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  native.tracked.command = 1; native.fail_destroy = true;
  const auto result = menu->show(native.owner, {submenu({command(1)})}, 0, 0);
  error(result.response, "E_MENU_CLEANUP_PENDING");
  require(result.native_error == 73 && !result.has_selection && result.command_id == 0 && menu->cleanup_pending() && native.alive() == 2,
          "Failed destruction must suppress execution and retain the ownership root");
  const auto acquired = native.creates;
  error(menu->show(native.owner, {command(2)}, 0, 0).response, "E_MENU_CLEANUP_PENDING");
  require(native.creates == acquired, "A pending root cannot be overwritten by another show");
  error(menu->cleanup(), "E_MENU_CLEANUP_PENDING");
  native.fail_destroy = false;
  require(menu->cleanup().ok() && !menu->cleanup_pending() && native.alive() == 0 && !native.invalid_destroy,
          "Retry cleanup must destroy the parent and its attached descendants once");
  const auto attempts = native.destroy_calls.size();
  require(menu->cleanup().ok() && native.destroy_calls.size() == attempts, "Cleanup after success is idempotent");
}

void test_build_and_cleanup_failure_diagnostics() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  native.fail_insert_at = 2; native.fail_destroy = true;
  const auto result = menu->show(native.owner, {submenu({command(1)})}, 0, 0);
  error(result.response, "E_MENU_CLEANUP_PENDING");
  require(result.response.message.find("E_MENU_BUILD") != std::string::npos &&
          result.response.message.find("native_error=72") != std::string::npos && result.native_error == 73,
          "Cleanup failure must not erase the original attachment diagnostic");
  native.fail_destroy = false;
  require(menu->cleanup().ok() && native.alive() == 0 && !native.invalid_destroy, "Both failed-build ownership roots remain retryable");
}

void test_foreground_and_finish_failures() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  native.fail_foreground = true;
  auto result = menu->show(native.owner, {command(1)}, 0, 0);
  error(result.response, "E_MENU_FOREGROUND");
  require(result.native_error == 74 && native.tracks == 0 && native.alive() == 0 && native.finishes == 0,
          "Foreground denial must clean resources without tracking");
  native.fail_foreground = false; native.fail_finish = true; native.tracked.command = 1;
  result = menu->show(native.owner, {command(1)}, 0, 0);
  error(result.response, "E_MENU_FINISH");
  require(result.native_error == 75 && !result.has_selection && native.tracks == 1 && native.alive() == 0,
          "Failed completion message must be visible and still clean resources");
}

void test_disabled_ancestor_and_unknown_selection() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  native.tracked.command = 1;
  error(menu->show(native.owner, {submenu({submenu({command(1)})}, false)}, 0, 0).response, "E_MENU_SELECTION");
  error(menu->show(native.owner, {command(1, "disabled", false)}, 0, 0).response, "E_MENU_SELECTION");
  native.tracked.command = 99;
  error(menu->show(native.owner, {command(1)}, 0, 0).response, "E_MENU_SELECTION");
  require(native.alive() == 0 && !native.invalid_destroy, "Rejected choices still clean the entire tree");
}

void test_no_selection_is_not_proven_cancel() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  auto result = menu->show(native.owner, {command(1)}, 0, 0);
  require(result.response.ok() && !result.has_selection && result.command_id == 0 &&
          result.response.kind != result_kind::user_cancelled && result.native_error == 0,
          "Zero TrackPopup result must not be called confirmed user cancellation");
  native.tracked.error = 87;
  result = menu->show(native.owner, {command(1)}, 0, 0);
  error(result.response, "E_MENU_TRACK");
  require(result.native_error == 87 && !result.has_selection && native.alive() == 0,
          "Retain a no-selection last-error diagnostic without executing anything");
}

void test_cancel_scope_and_reentry() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  auto other = menu_service_test_access::create(native);
  const std::vector<menu_item> items{command(1)};
  error(menu->cancel(), "E_MENU_NOT_TRACKING");
  require(native.ends == 0, "Idle cancel cannot end another native menu");
  bool nested_valid = true;
  native.during_create = [&] {
    nested_valid = nested_valid && menu->show(native.owner, items, 0, 0).response.code == "E_BUSY";
    nested_valid = nested_valid && menu->cleanup().code == "E_BUSY";
    nested_valid = nested_valid && menu->cancel().code == "E_MENU_NOT_TRACKING";
  };
  native.during_track = [&] {
    nested_valid = nested_valid && menu->show(native.owner, items, 0, 0).response.code == "E_BUSY";
    nested_valid = nested_valid && other->show(native.owner, items, 0, 0).response.code == "E_BUSY";
    nested_valid = nested_valid && menu->cleanup().code == "E_BUSY";
    nested_valid = nested_valid && other->cancel().code == "E_MENU_NOT_TRACKING";
    nested_valid = nested_valid && menu->cancel().ok();
  };
  native.during_end = [&] { nested_valid = nested_valid && menu->cancel().code == "E_BUSY"; };
  native.during_destroy = [&] {
    nested_valid = nested_valid && menu->show(native.owner, items, 0, 0).response.code == "E_BUSY";
    nested_valid = nested_valid && menu->cleanup().code == "E_BUSY";
    nested_valid = nested_valid && menu->cancel().code == "E_MENU_NOT_TRACKING";
  };
  const auto result = menu->show(native.owner, items, 0, 0);
  require(result.response.ok() && nested_valid && native.ends == 1 && native.creates == 1 && native.alive() == 0,
          "Only this service's tracking phase may reenter EndMenu, once at a time");
  native.during_create = {}; native.during_track = {}; native.during_end = {}; native.during_destroy = {};
  require(other->show(native.owner, items, 0, 0).response.ok(), "Thread tracking ownership must release after return");
}

void test_failed_cancel_is_observable() {
  fake_platform native;
  auto menu = menu_service_test_access::create(native);
  desktop_response cancellation;
  native.fail_end = true;
  native.during_track = [&] { cancellation = menu->cancel(); };
  const auto result = menu->show(native.owner, {command(1)}, 0, 0);
  error(cancellation, "E_MENU_CANCEL");
  require(cancellation.value == "76" && native.ends == 1 && native.alive() == 0 && !result.has_selection,
          "A failed EndMenu must preserve its native diagnostic and not leak the tree");
  native.during_track = {};
}

void test_destructor_only_cleans_owned_roots() {
  fake_platform native;
  const auto borrowed = native.create_popup().handle;
  {
    auto menu = menu_service_test_access::create(native);
    native.fail_destroy = true;
    error(menu->show(native.owner, {submenu({command(1)})}, 0, 0).response, "E_MENU_CLEANUP_PENDING");
    native.fail_destroy = false;
  }
  require(native.alive() == 1 && native.nodes.at(borrowed).alive && !native.invalid_destroy,
          "Destructor retries its own pending roots but never destroys an unrelated menu");
  require(native.destroy(borrowed).ok, "Test fixture releases the unrelated fake menu");
}

}  // namespace

int main() {
  try {
    test_tree_selection_and_ownership();
    test_shape_validation_before_resources();
    test_item_and_depth_limits();
    test_utf16_and_total_text_limits();
    test_owner_and_thread_validation();
    test_creation_failure_reclaims_partial_tree();
    test_attachment_failure_preserves_two_roots();
    test_failed_destroy_retry_blocks_new_show();
    test_build_and_cleanup_failure_diagnostics();
    test_foreground_and_finish_failures();
    test_disabled_ancestor_and_unknown_selection();
    test_no_selection_is_not_proven_cancel();
    test_cancel_scope_and_reentry();
    test_failed_cancel_is_observable();
    test_destructor_only_cleans_owned_roots();
    std::cout << "menu_service: 15 no-desktop contract groups passed (fake platform; no GUI claim)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "menu_service contract failure: " << error.what() << '\n';
    return 1;
  }
}
