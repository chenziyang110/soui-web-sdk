#include "../../sdk/src/desktop/macos_close_request.h"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
struct state {
  std::atomic<bool> accepting_work{true};
  struct { std::function<void()> close_requested; } events;
  bool* destroyed{};
  ~state() { if (destroyed) *destroyed = true; }
};
int failures = 0;
#define CHECK(value) do { if (!(value)) { ++failures; std::cerr << "Failed at " << __LINE__ << '\n'; } } while (false)
}
int main() {
  using soui::desktop::detail::request_macos_native_close;
  auto window = std::make_shared<state>();
  int requested = 0, closed = 0;
  window->events.close_requested = [&] { ++requested; }; // Host vetoes: no native close.
  request_macos_native_close(window, true);
  CHECK(requested == 1 && closed == 0 && window->accepting_work.load());
  request_macos_native_close(window, false); // Stale native sender.
  CHECK(requested == 1);
  request_macos_native_close(std::shared_ptr<state>{}, true);
  CHECK(requested == 1);
  window->events.close_requested = [&] {
    ++requested;
    window->accepting_work = false;
    window->events = {}; // Approval may revoke the stored callback itself.
    ++closed;
  };
  request_macos_native_close(window, true);
  request_macos_native_close(window, true); // Late duplicate after teardown.
  CHECK(requested == 2 && closed == 1 && !window->accepting_work.load());

  window = std::make_shared<state>();
  request_macos_native_close(window, true); // No handler is not force-close permission.
  CHECK(window->accepting_work.load() && closed == 1);
  window->events.close_requested = [] { throw std::runtime_error("veto"); };
  request_macos_native_close(window, true);
  CHECK(window->accepting_work.load() && closed == 1);

  bool destroyed = false, survived_callback = false;
  window->destroyed = &destroyed;
  window->events.close_requested = [&] {
    window->events = {};
    window.reset();
    survived_callback = !destroyed;
  };
  request_macos_native_close(window, true);
  CHECK(survived_callback && destroyed && !window);
  std::cout << "macOS native close-request policy: " << failures << " failures\n";
  return failures ? 1 : 0;
}
