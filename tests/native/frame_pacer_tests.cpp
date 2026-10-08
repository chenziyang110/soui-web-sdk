#define SOUI_BRIDGE_BENCHMARK
#include <soui/host/frame_pacer.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void run_for(HWND window, soui::sdk::frame_pacer& pacer, unsigned milliseconds, unsigned& pulses) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < end) {
    (void)MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    MSG message{};
    while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) {
      if (message.message == soui::sdk::frame_pulse_message) { ++pulses; pacer.acknowledge(); }
      else { TranslateMessage(&message); DispatchMessageW(&message); }
    }
  }
}
int main() {
  HWND window = CreateWindowExW(0, L"STATIC", L"Pacer test", 0, 0, 0, 1, 1,
                                HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
  try {
    require(window != nullptr, "Create hidden message window");
    soui::sdk::frame_pacer pacer;
    require(!pacer.start(nullptr, 60), "Reject missing owner");
    require(!pacer.start(window, 0), "Reject invalid rate");
    require(pacer.start(window, 60, false), "Legacy one-shot timer starts");
    require(pacer.backend() == 3, "Legacy must use deadline timer");
    unsigned pulses = 0;
    run_for(window, pacer, 350, pulses);
    const auto counters = pacer.diagnostic_counters();
    require(pulses >= 3, "Legacy path delivers pulses");
    // Allow coarse clocks and loaded hosts; the old 2 ms poll creates ~175
    // wakes in this interval. Timing precision is measured separately.
    require(counters.wakes < 65, "No 2 ms polling on the legacy path");
    Sleep(100); // Deliberately block the UI: only one pulse may be queued.
    MSG message{}; unsigned queued = 0;
    while (PeekMessageW(&message, window, soui::sdk::frame_pulse_message,
                        soui::sdk::frame_pulse_message, PM_REMOVE)) ++queued;
    require(queued == 1, "Blocked UI coalesces frames");
    pacer.stop();
    require(!PeekMessageW(&message, window, soui::sdk::frame_pulse_message,
                          soui::sdk::frame_pulse_message, PM_REMOVE), "Stop drains the generation");
    pulses = 0; run_for(window, pacer, 80, pulses);
    require(pulses == 0 && pacer.target() == 0, "Stopped timer has no late pulses");
    require(pacer.start(window, 144), "Restart after pause");
    run_for(window, pacer, 120, pulses);
    require(pulses > 0, "Restart delivers new frames");
    pacer.stop(); pacer.stop();
    DestroyWindow(window);
    std::cout << "frame pacer passed; legacy wakes=" << counters.wakes << ", delivered=" << counters.due << '\n';
    return 0;
  } catch (const std::exception& e) { if (window) DestroyWindow(window); std::cerr << e.what() << '\n'; return 1; }
}
