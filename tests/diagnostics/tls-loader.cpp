#include <windows.h>

#include <curl/curl.h>
#include <wke.h>

#include <cstdio>
#include <cstring>
#include <atomic>
#include <string>
#include <thread>

namespace {
std::string ca_path;
bool try_disable_verification = false;

using curl_setopt = void(WKE_CALL_TYPE*)(void*, __int32, ...);

void WKE_CALL_TYPE configure_curl(void* handle, curl_setopt setopt) {
  if (!ca_path.empty()) setopt(handle, CURLOPT_CAINFO, ca_path.c_str());
  if (try_disable_verification) {
    setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
  }
}

struct load_state {
  bool done{false};
  wkeLoadingResult result{WKE_LOADING_CANCELED};
  std::string reason;
};

void WKE_CALL_TYPE loading_finished(wkeWebView, void* user, const wkeString,
                                    wkeLoadingResult result, const wkeString failed_reason) {
  auto* state = static_cast<load_state*>(user);
  state->result = result;
  const char* reason = failed_reason ? wkeGetString(failed_reason) : nullptr;
  if (reason) state->reason = reason;
  state->done = true;
}
}  // namespace

int run_probe(int argc, char** argv) {
  const std::wstring dll_path(argv[1], argv[1] + std::strlen(argv[1]));
  ca_path = argc >= 4 && std::strcmp(argv[3], "-") != 0 ? argv[3] : "";
  try_disable_verification = argc == 5 && std::string(argv[4]) == "try-disable";
  HMODULE module = LoadLibraryW(dll_path.c_str());
  if (!module) return 65;
  std::fprintf(stderr, "stage=loaded\n");
  const auto initialize = reinterpret_cast<FN_wkeInitializeEx>(
      GetProcAddress(module, "wkeInitializeEx"));
  if (!initialize) return 66;
  std::fprintf(stderr, "stage=initialize\n");
  initialize(nullptr);
  std::fprintf(stderr, "stage=initialized\n");
#define BIND(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(module, #name)); if (!name) return 67
  BIND(wkeCreateWebView);
  BIND(wkeDestroyWebView);
  BIND(wkeSetDebugConfig);
  BIND(wkeOnLoadingFinish);
  BIND(wkeLoadURL);
  BIND(wkeGetString);
  BIND(wkeUpdate);
  BIND(wkeShutdown);
#undef BIND
  std::fprintf(stderr, "stage=create\n");
  wkeWebView view = wkeCreateWebView();
  if (!view) {
    wkeShutdown();
    return 69;
  }
  wkeSetDebugConfig(view, "setCurlSetopt",
                    reinterpret_cast<const char*>(&configure_curl));
  std::fprintf(stderr, "stage=load\n");
  load_state state;
  wkeOnLoadingFinish(view, loading_finished, &state);
  wkeLoadURL(view, argv[2]);
  const ULONGLONG deadline = GetTickCount64() + 15000;
  while (!state.done && GetTickCount64() < deadline) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    wkeUpdate();
    Sleep(10);
  }
  const bool done = state.done;
  std::fprintf(stderr, "stage=finish done=%d\n", done ? 1 : 0);
  const int result = static_cast<int>(state.result);
  std::printf("done=%d result=%d reason=%s\n", done ? 1 : 0, result,
              state.reason.c_str());
  wkeDestroyWebView(view);
  std::fprintf(stderr, "stage=shutdown\n");
  wkeShutdown();
  return done ? 0 : 67;
}

int main(int argc, char** argv) {
  if (argc < 3 || argc > 5) {
    std::fprintf(stderr, "usage: tls-loader.exe DLL URL [CA_PEM] [try-disable]\n");
    return 64;
  }
  std::atomic<bool> complete{false};
  int exit_code = 68;
  std::thread engine([&] {
    exit_code = run_probe(argc, argv);
    complete.store(true, std::memory_order_release);
  });
  const ULONGLONG deadline = GetTickCount64() + 30000;
  while (!complete.load(std::memory_order_acquire) && GetTickCount64() < deadline) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(10);
  }
  if (!complete.load(std::memory_order_acquire)) {
    std::fprintf(stderr, "stage=process-timeout\n");
    ExitProcess(68);
  }
  engine.join();
  return exit_code;
}
