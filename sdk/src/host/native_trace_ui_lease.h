#pragma once
#ifdef _WIN32
#include <windows.h>
#include <soui/diagnostics/input_frame_trace.h>
#include <array>
#include <memory>

namespace soui::sdk::detail {
class native_trace_ui_lease;
struct native_trace_thread_identity final {};
// Trivial TLS query only. A live token pins the unique cold-created cookie,
// so OS thread-ID/TLS-address reuse cannot authorize a foreign destructor.
inline thread_local const native_trace_thread_identity* native_trace_thread_cookie = nullptr;
struct native_trace_ui_cache final {
  std::shared_ptr<native_trace_thread_identity> owner = std::make_shared<native_trace_thread_identity>();
  std::array<std::weak_ptr<native_trace_ui_lease>, 8> slots;
  native_trace_ui_cache() { native_trace_thread_cookie = owner.get(); }
  ~native_trace_ui_cache() { native_trace_thread_cookie = nullptr; }
};

class native_trace_ui_lease final {
 public:
  using retain_fn = int(__cdecl*)();
  using release_fn = void(__cdecl*)();
  native_trace_ui_lease() = default;
  ~native_trace_ui_lease() {
    if (!module_) return;
    if (owner_.get() != native_trace_thread_cookie) {
      diagnostics::input_frame_trace::note_causal_loss();
      // Deliberately retain the acquired DLL reference. Never call another
      // actual thread's native TLS release or unload code owning its writer.
      return;
    }
    if (retained_) release_();
    // release pauses the native TLS writer; it does not destroy its TLS
    // registration. The initialized runtime's separate process-lifetime
    // LoadLibrary reference, required by acquire(), still keeps that code live.
    if (!::FreeLibrary(module_)) diagnostics::input_frame_trace::note_causal_loss();
  }
  native_trace_ui_lease(const native_trace_ui_lease&) = delete;
  native_trace_ui_lease& operator=(const native_trace_ui_lease&) = delete;

  // Cold only, after successful acquire_runtime and before input activation.
  // process_pinned_module must be the initialized runtime's LoadLibrary handle,
  // whose original reference is never released, including finalization/mount
  // failure. This scoped pin alone cannot support a generally unloadable DLL:
  // native detach only pauses TLS storage until the actual UI thread exits.
  // Hot callers only copy an existing token; first input/record never attach.
  static std::shared_ptr<native_trace_ui_lease> acquire(HMODULE process_pinned_module,
      const void* getter, retain_fn retain, release_fn release) {
    if (!process_pinned_module || !getter || !retain || !release) return {};
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(getter), &module)) {
      diagnostics::input_frame_trace::note_causal_loss(); return {};
    }
    struct module_pin {
      HMODULE value;
      ~module_pin() { if (value && !::FreeLibrary(value)) diagnostics::input_frame_trace::note_causal_loss(); }
    } pin{module};
    if (module != process_pinned_module) {
      diagnostics::input_frame_trace::note_causal_loss(); return {};
    }
    HMODULE retain_module = nullptr, release_module = nullptr;
    constexpr DWORD query = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!::GetModuleHandleExW(query, reinterpret_cast<LPCWSTR>(retain), &retain_module)
        || !::GetModuleHandleExW(query, reinterpret_cast<LPCWSTR>(release), &release_module)
        || retain_module != module || release_module != module) {
      diagnostics::input_frame_trace::note_causal_loss(); return {};
    }
    // Only the acquired FROM_ADDRESS reference is freed. Unchanged-refcount
    // module identities above are never passed to FreeLibrary.
    static thread_local native_trace_ui_cache cache;
    std::weak_ptr<native_trace_ui_lease>* vacant = nullptr;
    for (auto& slot : cache.slots) {
      if (const auto current = slot.lock()) {
        if (current->getter_ == getter && current->module_ == module && current->retain_ == retain && current->release_ == release
            && current->owner_.get() == native_trace_thread_cookie)
          return current;
      } else if (!vacant) vacant = &slot;
    }
    if (!vacant) { diagnostics::input_frame_trace::note_causal_loss(); return {}; }
    auto result = std::make_shared<native_trace_ui_lease>(); // Allocate all ownership before native retain.
    result->owner_ = cache.owner;
    result->getter_ = getter; result->retain_ = retain; result->release_ = release;
    result->module_ = module; pin.value = nullptr;
    if (retain() != 0) { diagnostics::input_frame_trace::note_causal_loss(); return {}; }
    result->retained_ = true;
    *vacant = result;
    return result;
  }
 private:
  std::shared_ptr<native_trace_thread_identity> owner_;
  const void* getter_{nullptr};
  HMODULE module_{nullptr};
  retain_fn retain_{nullptr}; release_fn release_{nullptr};
  bool retained_{false};
};
} // namespace soui::sdk::detail
#endif
