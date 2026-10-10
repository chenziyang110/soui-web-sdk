#include "../../sdk/src/host/cursor_queue.h"

#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace soui::sdk::detail;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct fake_platform final : cursor_platform {
  unsigned thread{7}, created{0}, destroyed{0}, selections{0};
  bool fail_create{false}, fail_select{false}, fail_destroy{false};
  bool global_succeeds{true}, global_follows_local{true};
  unsigned global_flags{1};
  void* global_handle{nullptr};
  void* local{reinterpret_cast<void*>(1)};
  std::set<void*> owned;
  std::uint32_t thread_id() const noexcept override { return thread; }
  void* standard(std::uint32_t type) noexcept override { return reinterpret_cast<void*>(std::uintptr_t(1 + type)); }
  void* create(const cursor_descriptor&) noexcept override {
    if (fail_create) return nullptr;
    auto handle = reinterpret_cast<void*>(std::uintptr_t(100 + ++created));
    owned.insert(handle);
    return handle;
  }
  void select(void* handle) noexcept override { ++selections; if (!fail_select) local = handle; }
  void* local_selection() const noexcept override { return local; }
  cursor_global_observation global_selection() const noexcept override {
    return {global_succeeds, global_flags, global_follows_local ? local : global_handle};
  }
  bool destroy(void* handle) noexcept override {
    if (fail_destroy || local == handle || !owned.count(handle)) return false;
    if (!global_follows_local && global_handle == handle) return false;
    owned.erase(handle); ++destroyed; return true;
  }
  bool cleanup_unpublished() noexcept override { return true; }
  unsigned unpublished_cursors() const noexcept override { return 0; }
};
cursor_descriptor custom(unsigned width = 17, unsigned color = 64) {
  cursor_descriptor value;
  value.kind = cursor_kind::custom; value.type = 43;
  value.width = width; value.height = 13; value.stride = width * 4;
  value.hotspot_x = 3; value.hotspot_y = 5;
  value.pixels.resize(value.stride * value.height);
  for (std::size_t i = 0; i < value.pixels.size(); i += 4) {
    value.pixels[i] = static_cast<unsigned char>(color);
    value.pixels[i + 3] = 255;
  }
  return value;
}
int owner_a, owner_b;

void same_type_update() {
  fake_platform api; cursor_queue queue(api);
  require(queue.apply(&owner_a, 1, custom()).selected, "custom A selection");
  const auto a = api.local;
  require(queue.apply(&owner_a, 2, custom(17, 96)).selected, "same type custom B selection");
  require(api.local != a && api.created == 2 && api.destroyed == 1 && queue.retained_handles() == 1,
          "same-type pixels must replace and retire");
  require(queue.withdraw(&owner_a) && api.owned.empty(), "close retires B");
}
void repeat_publication() {
  fake_platform api; cursor_queue queue(api); const auto value = custom();
  require(queue.apply(&owner_a, 8, value).selected, "first publication");
  for (unsigned i = 0; i < 100; ++i) require(queue.apply(&owner_a, 8, value).selected, "cached selection");
  require(api.created == 1 && api.destroyed == 0, "cached publication does not allocate");
  require(queue.withdraw(&owner_a), "cached close");
}
void suppressed_retirement() {
  fake_platform api; api.global_follows_local = false; api.global_flags = 2;
  cursor_queue queue(api);
  require(queue.apply(&owner_a, 1, custom()).selected, "suppressed A local selection");
  require(queue.apply(&owner_a, 2, custom(17, 1)).selected, "suppressed B local selection");
  require(api.destroyed == 1 && queue.retained_handles() == 1, "P allows suppressed NULL without quarantine");
  require(queue.withdraw(&owner_a) && api.owned.empty(), "suppressed close proof");
}
void global_old_veto() {
  fake_platform api; cursor_queue queue(api);
  require(queue.apply(&owner_a, 1, custom()).selected, "old global A");
  api.global_follows_local = false; api.global_handle = api.local;
  const auto b = queue.apply(&owner_a, 2, custom(17, 2));
  require(b.selected && b.status == cursor_queue_status::retirement_pending, "local B with global A veto");
  require(!queue.apply(&owner_a, 3, custom(17, 3)).selected && api.created == 2 &&
              api.destroyed == 1 && queue.retained_handles() == 1 &&
              api.owned.size() == 1 && api.owned.count(api.global_handle) == 1,
          "veto blocks a third allocation while independently proven B is retired");
  api.global_handle = nullptr; api.global_flags = 2;
  require(queue.apply(&owner_a, 3, custom(17, 3)).selected && api.created == 3 && api.destroyed == 2,
          "proof recovery reclaims both before allocation");
  require(queue.withdraw(&owner_a) && api.owned.empty(), "recovery close");
}
void second_retirement_veto() {
  fake_platform api; cursor_queue queue(api);
  require(queue.apply(&owner_a, 1, custom()).selected, "A");
  api.global_succeeds = false;
  require(queue.apply(&owner_a, 2, custom()).selected, "B while query unavailable");
  api.global_succeeds = true; api.global_follows_local = false; api.global_handle = api.local;
  require(!queue.retry_retirement() && queue.retained_handles() == 1, "A retired but B still globally borrowed");
  require(!queue.apply(&owner_a, 3, custom()).selected && api.created == 2,
          "remaining current handle still blocks allocation");
  api.global_handle = nullptr;
  require(queue.apply(&owner_a, 3, custom()).selected && api.created == 3, "B proof becomes available");
  require(queue.withdraw(&owner_a), "close C");
}
void global_query_and_flags() {
  fake_platform api; cursor_queue queue(api);
  require(queue.apply(&owner_a, 1, custom()).selected, "A query test");
  api.global_succeeds = false;
  require(queue.apply(&owner_a, 2, custom()).selected && queue.retained_handles() == 2, "unknown query retains");
  api.global_succeeds = true; api.global_flags = 3;
  require(!queue.apply(&owner_a, 3, custom()).selected && api.created == 2, "unknown flag combination is not proof");
  api.global_flags = 0;
  require(queue.apply(&owner_a, 3, custom()).selected, "hidden legal state can complete P");
  require(queue.withdraw(&owner_a), "query close");
}
void destruction_failure() {
  fake_platform api; cursor_queue queue(api);
  require(queue.apply(&owner_a, 1, custom()).selected, "destroy A");
  api.fail_destroy = true;
  require(queue.apply(&owner_a, 2, custom()).selected, "destroy B selected");
  for (unsigned i = 0; i < 10; ++i) require(!queue.apply(&owner_a, 3 + i, custom()).selected, "destroy failure gate");
  require(api.created == 2 && api.destroyed == 0 && queue.retained_handles() == 2, "failed destroy ownership retained bounded");
  api.fail_destroy = false;
  require(queue.withdraw(&owner_a) && api.destroyed == 2 && api.owned.empty(), "retry destroys each once");
}
void selection_failure() {
  fake_platform api; cursor_queue queue(api);
  require(queue.apply(&owner_a, 1, custom()).selected, "select A");
  api.fail_select = true;
  require(queue.apply(&owner_a, 2, custom()).status == cursor_queue_status::selection_failed, "B selection ambiguous");
  require(!queue.apply(&owner_a, 3, custom()).selected && api.created == 2, "ambiguous two handles gate allocation");
  api.fail_select = false;
  require(queue.withdraw(&owner_a) && api.owned.empty(), "ambiguous selection closes only after replacement");
}
void first_selection_failure() {
  fake_platform api;
  api.local = reinterpret_cast<void*>(9); // Distinct from the shared replacement.
  api.fail_select = true; cursor_queue queue(api);
  require(!queue.apply(&owner_a, 1, custom()).selected && queue.retained_handles() == 1, "first custom selection fails");
  require(!queue.apply(&owner_a, 2, custom()).selected && api.created == 1, "first failure also gates allocation");
  api.fail_select = false;
  require(queue.withdraw(&owner_a) && api.owned.empty(), "first failure close");
}
void already_selected_replacement_proves_retirement() {
  fake_platform api; api.fail_select = true; cursor_queue queue(api);
  require(!queue.apply(&owner_a, 1, custom()).selected && queue.retained_handles() == 1,
          "failed initial selection retains the unpublished owned cursor");
  require(!queue.apply(&owner_a, 2, custom()).selected && api.created == 2 &&
              api.destroyed == 1 && queue.retained_handles() == 1 && api.owned.size() == 1,
          "verified existing shared replacement retires the old failure before another allocation");
  api.fail_select = false;
  require(queue.withdraw(&owner_a) && api.destroyed == 2 && api.owned.empty(),
          "each failed selection releases only after the full retirement proof");
}
void nested_native_scopes() {
  fake_platform api; cursor_queue queue(api);
  require(queue.apply(&owner_a, 1, custom()).selected, "scope A");
  const auto first = queue.enter_scope(&owner_a), second = queue.enter_scope(&owner_b);
  require(first && second && first != second, "nested unique scopes");
  require(queue.apply(&owner_a, 2, custom()).status == cursor_queue_status::deferred && api.created == 1,
          "modal pump only coalesces descriptors");
  require(!queue.leave_scope(&owner_a, second) && queue.native_scopes() == 2, "wrong owner cannot release lease");
  require(queue.leave_scope(&owner_a, first) && !queue.withdraw(&owner_a), "outer scope still pins");
  require(queue.leave_scope(&owner_b, second) && !queue.leave_scope(&owner_b, second), "exact once leave");
  require(queue.apply(&owner_a, 2, custom()).selected && api.destroyed == 1, "after outer leave replacement retires");
  require(queue.withdraw(&owner_a), "scope close");
}
void bounded_scopes() {
  fake_platform api; cursor_queue queue(api); std::vector<std::uint64_t> scopes;
  for (unsigned i = 0; i < 64; ++i) { auto s = queue.enter_scope(&owner_a); require(s != 0, "scope admission"); scopes.push_back(s); }
  require(!queue.enter_scope(&owner_a) && queue.native_scopes() == 64, "scope capacity exact");
  for (auto s : scopes) require(queue.leave_scope(&owner_a, s), "bounded scope release");
  require(queue.native_scopes() == 0, "all scopes released");
}
void other_queue_rejected() {
  fake_platform api; cursor_queue queue(api); api.thread = 9;
  require(queue.apply(&owner_a, 1, custom()).status == cursor_queue_status::wrong_thread &&
      !queue.enter_scope(&owner_a) && !queue.withdraw(&owner_a) && !queue.retry_retirement(), "foreign queue rejected");
  require(api.created == 0 && api.selections == 0 && api.destroyed == 0, "foreign rejection before OS calls");
}
void multiple_views_and_none() {
  fake_platform api; cursor_queue queue(api);
  require(queue.apply(&owner_a, 1, custom()).selected, "view A");
  require(queue.apply(&owner_b, 1, custom(17, 5)).selected && api.destroyed == 1, "view B owns new queue selection");
  require(queue.withdraw(&owner_a) && api.owned.size() == 1, "closing inactive A preserves B");
  cursor_descriptor none; none.kind = cursor_kind::none; none.type = 37;
  require(queue.apply(&owner_b, 2, none).selected && api.local == nullptr && api.owned.empty(), "none replaces then retires B");
  cursor_descriptor standard; standard.type = 3;
  require(queue.apply(&owner_a, 2, standard).selected && api.created == 2 && api.destroyed == 2,
          "standard selection never destroys shared resources");
  require(queue.withdraw(&owner_a), "standard close");
}
void descriptor_validation() {
  fake_platform api; cursor_queue queue(api); auto value = custom();
  value.width = 257; require(!queue.apply(&owner_a, 1, value).selected, "oversized cursor rejected");
  value = custom(); value.hotspot_x = 17; require(!queue.apply(&owner_a, 1, value).selected, "invalid hotspot rejected");
  value = custom(); value.pixels[3] = 1; require(!queue.apply(&owner_a, 1, value).selected, "nonpremultiplied data rejected");
  value = custom(); value.stride += 4; require(!queue.apply(&owner_a, 1, value).selected, "noncanonical stride rejected");
  cursor_descriptor unsupported; unsupported.kind = cursor_kind::unsupported; unsupported.type = 43;
  require(queue.apply(&owner_a, 1, unsupported).status == cursor_queue_status::unsupported, "external cursor unsupported");
  require(api.created == 0 && api.selections == 0, "validation before resource acquisition");
}
}  // namespace
int main() {
  const std::vector<std::pair<const char*, std::function<void()>>> cases{
      {"same-type custom replacement", same_type_update}, {"cached publication", repeat_publication},
      {"suppressed retirement P", suppressed_retirement}, {"global old-handle veto", global_old_veto},
      {"second retirement veto", second_retirement_veto}, {"query and flag proof", global_query_and_flags},
      {"failed destruction bounded retry", destruction_failure}, {"selection failure", selection_failure},
      {"first selection failure", first_selection_failure},
      {"existing replacement retirement proof", already_selected_replacement_proves_retirement},
      {"nested native scopes", nested_native_scopes},
      {"bounded native scopes", bounded_scopes}, {"foreign queue rejection", other_queue_rejected},
      {"multiple views and none", multiple_views_and_none}, {"descriptor validation", descriptor_validation}};
  unsigned passed = 0;
  for (const auto& item : cases) try { item.second(); ++passed; std::cout << "PASS " << item.first << '\n'; }
    catch (const std::exception& error) { std::cerr << "FAIL " << item.first << ": " << error.what() << '\n'; return 1; }
  std::cout << "Cursor queue: " << passed << '/' << cases.size() << " passed\n";
  return 0;
}
