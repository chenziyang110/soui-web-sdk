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

void test_rate_policy_and_override() {
  using namespace soui::sdk;
  for (unsigned rate : {30U, 59U, 60U, 75U, 90U, 100U, 120U, 144U, 165U, 240U, 360U, 500U, 1000U}) {
    require(is_supported_frame_rate(rate), "Supported integer rate");
    require(clamp_frame_rate_to_display(rate, rate) == rate, "Exact display rate is retained");
  }
  for (unsigned rate : {0U, 1U, 29U, 1001U, 0xffffffffU}) {
    require(!is_supported_frame_rate(rate), "Reject out-of-range rate");
    require(clamp_frame_rate_to_display(rate, 100) == 0, "Invalid request does not become a valid target");
  }
  require(clamp_frame_rate_to_display(120, 100) == 100, "100 Hz display is not rounded to 60");
  require(clamp_frame_rate_to_display(60, 100) == 60, "Requested ceiling is preserved");
  require(clamp_frame_rate_to_display(120, 59) == 59, "Lower integer display rate is respected");
  for (unsigned display : {0U, 29U, 1001U, 0xffffffffU}) {
    require(clamp_frame_rate_to_display(240, display) == 60, "Unknown display uses conservative ceiling");
    require(clamp_frame_rate_to_display(30, display) == 30, "Fallback does not raise the request");
  }
  // These environment writes affect only this test process.
  for (const char* value : {"30", "75", "100", "165", "240", "1000"}) {
    require(SetEnvironmentVariableA("SOUI_DISPLAY_REFRESH_OVERRIDE", value) != FALSE, "Set local diagnostic override");
    unsigned expected = 0; for (const char* p=value; *p; ++p) expected=expected*10+static_cast<unsigned>(*p-'0');
    require(frame_pacer::query_display_refresh(nullptr) == expected, "Entire valid diagnostic rate is accepted");
  }
  for (const char* value : {"", "0", "29", "1001", "-100", "+100", " 100", "100 ", "100Hz", "100.0", "9999999", "10000000"}) {
    require(SetEnvironmentVariableA("SOUI_DISPLAY_REFRESH_OVERRIDE", value) != FALSE, "Set invalid diagnostic override");
    require(frame_pacer::query_display_refresh(nullptr) == 0, "Invalid override cannot impersonate a display rate");
  }
  require(SetEnvironmentVariableA("SOUI_DISPLAY_REFRESH_OVERRIDE", nullptr) != FALSE, "Clear local diagnostic override");
}
void test_high_rate_timers(HWND window) {
  soui::sdk::frame_pacer pacer;
  for (bool high_resolution : {false, true}) {
    for (unsigned rate : {30U, 59U, 60U, 75U, 90U, 100U, 120U, 144U, 165U, 240U, 360U, 500U, 1000U}) {
      require(pacer.start(window, rate, high_resolution), "Each supported target starts a real timer");
      require(pacer.target() == rate && !pacer.failed(), "Started rate is reported exactly");
      require(high_resolution ? pacer.backend() == 2 || pacer.backend() == 3 : pacer.backend() == 3, "Backend fallback remains valid");
      unsigned pulses=0; run_for(window,pacer,200,pulses);
      require(pulses > 0 && !pacer.failed(), "Supported target delivers real window messages");
      pacer.stop();
      MSG message{};
      require(!PeekMessageW(&message,window,soui::sdk::frame_pulse_message,soui::sdk::frame_pulse_message,PM_REMOVE), "Rate switch drains old pulses");
    }
    require(pacer.start(window,100,high_resolution), "100 Hz starts before coalescing test");
    Sleep(120);
    MSG message{}; unsigned queued=0;
    while(PeekMessageW(&message,window,soui::sdk::frame_pulse_message,soui::sdk::frame_pulse_message,PM_REMOVE)) ++queued;
    require(queued==1,"100 Hz blocked UI queues one pulse");
    pacer.stop();
  }
  for(unsigned invalid : {0U,29U,1001U,0xffffffffU}) require(!pacer.start(window,invalid),"Reject invalid timer target");
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
    test_rate_policy_and_override();
    test_high_rate_timers(window);
    DestroyWindow(window);
    std::cout << "frame pacer passed; legacy wakes=" << counters.wakes << ", delivered=" << counters.due << '\n';
    return 0;
  } catch (const std::exception& e) { if (window) DestroyWindow(window); std::cerr << e.what() << '\n'; return 1; }
}
