// Actual SDK lease implementation with deterministic module/API dependencies.
// No DLL is loaded, UI input injected or code executed for this delivery.
#include <windows.h>
#include <soui/diagnostics/input_frame_trace.h>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace fixture {
std::atomic<unsigned> pins_a{0}, pins_b{0}, retains_a{0}, releases_a{0}, retains_b{0}, releases_b{0};
bool fail_pin = false, fail_retain = false;
void __cdecl getter_a() {} void __cdecl getter_alias_a() {} void __cdecl getter_b() {}
int __cdecl retain_a() { if (fail_retain) return 2; ++retains_a; return 0; }
int __cdecl retain_b() { ++retains_b; return 0; }
void __cdecl release_a() { ++releases_a; }
void __cdecl release_b() { ++releases_b; }
HMODULE module_a() { return reinterpret_cast<HMODULE>(static_cast<UINT_PTR>(0x101)); }
HMODULE module_b() { return reinterpret_cast<HMODULE>(static_cast<UINT_PTR>(0x202)); }
BOOL WINAPI module(DWORD flags, LPCWSTR address, HMODULE* result) {
  if (fail_pin) return FALSE;
  const auto value = reinterpret_cast<const void*>(address);
  const bool a = value == reinterpret_cast<const void*>(getter_a) || value == reinterpret_cast<const void*>(getter_alias_a)
      || value == reinterpret_cast<const void*>(retain_a) || value == reinterpret_cast<const void*>(release_a);
  const bool b = value == reinterpret_cast<const void*>(getter_b) || value == reinterpret_cast<const void*>(retain_b)
      || value == reinterpret_cast<const void*>(release_b);
  if (!a && !b) return FALSE;
  *result = a ? module_a() : module_b();
  if (!(flags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT)) { if (a) ++pins_a; else ++pins_b; }
  return TRUE;
}
BOOL WINAPI free_module(HMODULE module) {
  if (module == module_a()) { if (!pins_a) return FALSE; --pins_a; return TRUE; }
  if (module == module_b()) { if (!pins_b) return FALSE; --pins_b; return TRUE; }
  return FALSE;
}
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
#define GetModuleHandleExW fixture::module
#define FreeLibrary fixture::free_module
#include "../../../sdk/src/host/native_trace_ui_lease.h"
#undef GetModuleHandleExW
#undef FreeLibrary
int main() {
  try {
    using lease = soui::sdk::detail::native_trace_ui_lease;
    using namespace fixture;
    // Failed initialization never supplies the Host's initialized process pin.
    check(!lease::acquire(nullptr, reinterpret_cast<const void*>(getter_a), retain_a, release_a)
        && pins_a == 0 && retains_a == 0, "missing initialized process pin registered a native writer");
    check(!lease::acquire(module_b(), reinterpret_cast<const void*>(getter_a), retain_a, release_a)
        && pins_a == 0 && retains_a == 0, "mismatched process module registered a native writer");
    // These fake process module identities model the separate permanently held
    // initialized-runtime references. pins_a/b count only the extra scoped refs.
    auto first = lease::acquire(module_a(), reinterpret_cast<const void*>(getter_a), retain_a, release_a);
    auto second = lease::acquire(module_a(), reinterpret_cast<const void*>(getter_a), retain_a, release_a);
    check(first && first == second && pins_a == 1 && retains_a == 1, "same actual UI/module/getter did not share one cold native retain");
    auto hot_call = first; first.reset(); second.reset();
    check(releases_a == 0 && pins_a == 1, "reentrant last Host unmount retired active call pin");
    hot_call.reset(); check(releases_a == 1 && pins_a == 0, "last actual UI call did not release native before DLL pin");

    auto a = lease::acquire(module_a(), reinterpret_cast<const void*>(getter_a), retain_a, release_a);
    auto b = lease::acquire(module_b(), reinterpret_cast<const void*>(getter_b), retain_b, release_b);
    auto alias = lease::acquire(module_a(), reinterpret_cast<const void*>(getter_alias_a), retain_a, release_a);
    check(a && b && alias && a != b && a != alias && pins_a == 2 && pins_b == 1, "different getter/module reused stale native lease");
    alias.reset(); a.reset(); b.reset();
    check(pins_a == 0 && pins_b == 0 && releases_a == retains_a && releases_b == retains_b, "same actual UI with multiple modules misclassified owner");

    const auto before = retains_a.load();
    check(!lease::acquire(module_a(), reinterpret_cast<const void*>(getter_a), retain_b, release_b) && pins_a == 0,
        "mixed module API functions escaped pinned getter identity");
    fail_pin = true;
    check(!lease::acquire(module_a(), reinterpret_cast<const void*>(getter_a), retain_a, release_a) && retains_a == before, "failed module pin called native retain");
    fail_pin = false; fail_retain = true;
    const auto released = releases_a.load();
    check(!lease::acquire(module_a(), reinterpret_cast<const void*>(getter_a), retain_a, release_a) && pins_a == 0 && releases_a == released,
        "failed native retain detached an unowned producer or leaked normal pin");
    fail_retain = false;

    auto wrong_thread = lease::acquire(module_a(), reinterpret_cast<const void*>(getter_a), retain_a, release_a);
    const auto foreign_release = releases_a.load();
    const auto lost = soui::sdk::diagnostics::input_frame_trace::process_recorder().snapshot().causal_lost;
    std::thread foreign([owned = std::move(wrong_thread)]() mutable { owned.reset(); }); foreign.join();
    check(releases_a == foreign_release && pins_a == 1
        && soui::sdk::diagnostics::input_frame_trace::process_recorder().snapshot().causal_lost > lost,
        "foreign destructor detached TLS/unloaded writer DLL or hid lifecycle loss");
    std::cout << "native UI trace lease SOURCE contract; foreign pin intentionally retained\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
