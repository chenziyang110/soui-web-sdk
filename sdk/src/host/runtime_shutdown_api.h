#pragma once

// A kernel which can initialize but cannot perform the retained shutdown
// handshake is not a usable SDK runtime. Resolve before any initialization.
inline bool has_runtime_shutdown_api(HMODULE module) {
  return GetProcAddress(module, "wkePrepareWorkerShutdown") != nullptr &&
      GetProcAddress(module, "wkeFinalizePrepared") != nullptr;
}
