#define ENABLE_WKE 0
#include "../../third_party/include/miniblink/wke.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct export_state {
  HMODULE module{reinterpret_cast<HMODULE>(1)};
  const char* missing{nullptr};
  bool ready{true};
  bool broker{true};
  bool initialized{true};
  bool second_ready{false};
  unsigned initialize_calls{0};
  unsigned readiness_queries{0};
  unsigned broker_queries{0};
  bool wrong_module{false};
  bool nonnull_settings{false};
} exports;

void WKE_CALL_TYPE initialize(const wkeSettings* settings) {
  ++exports.initialize_calls;
  exports.nonnull_settings = settings != nullptr;
}
bool WKE_CALL_TYPE is_initialized() { return exports.initialized; }
void WKE_CALL_TYPE ready_first(wkeWebView, wkeDocumentReady2Callback, void*) {}
void WKE_CALL_TYPE ready_second(wkeWebView, wkeDocumentReady2Callback, void*) {}
const wkeCursorBrokerApi* WKE_CALL_TYPE broker_api(unsigned int) { return nullptr; }
void WINAPI unused_export() {}

FARPROC WINAPI lookup_export(HMODULE module, LPCSTR name) {
  exports.wrong_module = exports.wrong_module || module != exports.module;
  if (exports.missing && std::strcmp(name, exports.missing) == 0) return nullptr;
  if (std::strcmp(name, "wkeInitializeEx") == 0)
    return reinterpret_cast<FARPROC>(&initialize);
  if (std::strcmp(name, "wkeIsInitialize") == 0)
    return reinterpret_cast<FARPROC>(&is_initialized);
  if (std::strcmp(name, "wkeOnDocumentReady2") == 0) {
    ++exports.readiness_queries;
    return exports.ready ? reinterpret_cast<FARPROC>(
        exports.second_ready ? &ready_second : &ready_first) : nullptr;
  }
  if (std::strcmp(name, "wkeGetCursorBrokerApi") == 0) {
    ++exports.broker_queries;
    return exports.broker ? reinterpret_cast<FARPROC>(&broker_api) : nullptr;
  }
  // Required exports are resolved but never invoked by this loader test.
  return reinterpret_cast<FARPROC>(&unused_export);
}
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace

// Compile the actual loader body. Only its operating-system lookup is replaced;
// no DLL, rendering engine, document callback or controlled mount runs here.
#define GetProcAddress lookup_export
#include "../../sdk/src/host/source_kernel_api.h"
#undef GetProcAddress

namespace {
void cold_loader_binds_frame_readiness() {
  exports = {};
  wkeOnDocumentReady2 = nullptr;
  wkeGetCursorBrokerApi = nullptr;
  require(initialize_source_kernel(exports.module), "cold loader initialization");
  require(wkeOnDocumentReady2 == &ready_first, "actual frame-readiness export must bind");
  require(wkeGetCursorBrokerApi == &broker_api, "versioned broker export must bind");
  require(exports.initialize_calls == 1 && !exports.nonnull_settings && !exports.wrong_module,
          "initialize exactly once using the supplied module");
  require(exports.readiness_queries == 1 && exports.broker_queries == 1,
          "both optional exports must be queried");
}
void legacy_loader_clears_absent_optional_exports() {
  exports = {};
  exports.ready = false;
  exports.broker = false;
  wkeOnDocumentReady2 = &ready_first;
  wkeGetCursorBrokerApi = &broker_api;
  require(initialize_source_kernel(exports.module), "legacy required exports remain sufficient");
  require(!wkeOnDocumentReady2 && !wkeGetCursorBrokerApi,
          "a new module cannot inherit stale optional pointers");
  require(exports.initialize_calls == 1 && exports.readiness_queries == 1 &&
              exports.broker_queries == 1 && !exports.wrong_module,
          "legacy absence is explicitly resolved before initialization");
}
void new_module_replaces_optional_pointer() {
  exports = {};
  exports.module = reinterpret_cast<HMODULE>(2);
  exports.second_ready = true;
  wkeOnDocumentReady2 = &ready_first;
  require(initialize_source_kernel(exports.module), "second module initialization");
  require(wkeOnDocumentReady2 == &ready_second && !exports.wrong_module,
          "readiness pointer must belong to the supplied module");
}
void missing_required_export_prevents_initialization() {
  exports = {};
  exports.missing = "jsEval";
  require(!initialize_source_kernel(exports.module), "missing required export must fail");
  require(exports.initialize_calls == 0, "required binding failure must precede initialization");
}
void missing_initializer_prevents_initialization() {
  exports = {};
  exports.missing = "wkeInitializeEx";
  require(!initialize_source_kernel(exports.module), "missing initializer must fail");
  require(exports.initialize_calls == 0 && exports.readiness_queries == 1,
          "initializer absence must not be called");
}
void unsuccessful_initialization_is_reported() {
  exports = {};
  exports.initialized = false;
  require(!initialize_source_kernel(exports.module), "engine initialization status must be checked");
  require(exports.initialize_calls == 1, "initialization status is checked after the real call site");
}
}  // namespace

int main() {
  const struct test_case { const char* name; void (*run)(); } cases[] = {
      {"cold-frame-readiness", cold_loader_binds_frame_readiness},
      {"legacy-optional-absence", legacy_loader_clears_absent_optional_exports},
      {"module-pointer-replacement", new_module_replaces_optional_pointer},
      {"required-export-failure", missing_required_export_prevents_initialization},
      {"initializer-export-failure", missing_initializer_prevents_initialization},
      {"initialization-status-failure", unsuccessful_initialization_is_reported},
  };
  try {
    for (const auto& test : cases) {
      test.run();
      std::cout << "PASS " << test.name << '\n';
    }
    std::cout << "source-kernel loader: 6 passed; actual DLL and GUI not exercised\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
