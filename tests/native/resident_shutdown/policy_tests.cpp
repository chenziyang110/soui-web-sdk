#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "../../../demos/workbench/app/resident_shutdown_policy.h"

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void inactive() {
  resident_shutdown_policy policy;
  require(!policy.started(), "an unrequested shutdown must not be started");
  require(!policy.expired((std::numeric_limits<resident_shutdown_policy::tick>::max)()),
      "an unrequested shutdown must not expire");
  require(policy.remaining(123) == 10000 && policy.wait_ms(123) == 50,
      "the initial policy budget must remain fixed");
}
void zero_tick() {
  resident_shutdown_policy policy;
  policy.begin(0);
  require(policy.started(), "tick zero is a real first request");
  policy.begin(9999);
  require(policy.elapsed(10000) == 10000 && policy.expired(10000),
      "zero must not be treated as an unset first-request sentinel");
}
void repeated_exit() {
  resident_shutdown_policy policy;
  policy.begin(17);
  for (resident_shutdown_policy::tick offset = 50; offset < 10000; offset += 50) {
    policy.begin(17 + offset);
    require(policy.elapsed(17 + offset) == offset,
        "pending retries must not renew the first exit clock");
  }
  policy.begin(10017);
  require(policy.expired(10017) && policy.remaining(10017) == 0,
      "another exit request at expiry must not restore any budget");
}
void pending_boundary() {
  resident_shutdown_policy policy;
  policy.begin(100);
  for (resident_shutdown_policy::tick now : {100ULL, 150ULL, 10050ULL, 10099ULL}) {
    require(!policy.expired(now), "a pending owner may retry before the deadline");
    require(policy.wait_ms(now) <= 50 && policy.wait_ms(now) <= policy.remaining(now),
        "the message wait must not exceed the actual remaining deadline");
  }
  require(policy.remaining(10099) == 1 && policy.wait_ms(10099) == 1,
      "only one millisecond remains immediately before expiry");
  require(policy.expired(10100) && policy.wait_ms(10100) == 0,
      "the exact deadline must fail closed rather than start another retry interval");
  require(policy.expired(10101), "expiry must remain sticky on a later clock reading");
}
void tick_wrap() {
  resident_shutdown_policy policy;
  const auto first = (std::numeric_limits<resident_shutdown_policy::tick>::max)() - 4999;
  policy.begin(first);
  require(policy.elapsed(first + 9999) == 9999 && !policy.expired(first + 9999),
      "unsigned clock wrap must preserve the real elapsed interval");
  require(policy.expired(first + 10000) && policy.remaining(first + 10000) == 0,
      "unsigned wrap must not renew the shutdown budget");
}
void clock_regression() {
  resident_shutdown_policy policy;
  policy.begin(100);
  require(policy.expired(99) && policy.wait_ms(99) == 0,
      "an unexpected clock regression must not extend owner lifetime");
}
void exact_pending() {
  for (const std::string code : {"E_BUSY", "E_CURSOR_CLOSE_PENDING"})
    require(resident_classify_host_close(false, code) == resident_host_close::retry,
        "only the two actual retryable Host states can preserve owners for a retry");
}
void unknown_close() {
  for (const std::string code : {"", "E_HOST", "E_BUSY_MORE", "E_CURSOR_CLOSE_PENDING_OTHER",
                               "e_busy", "E_THREAD_AFFINITY", "E_MENU_CLEANUP_PENDING"})
    require(resident_classify_host_close(false, code) == resident_host_close::fail_stop,
        "unknown Host errors must never become pending or complete");
  require(resident_classify_host_close(false, std::string("E_BUSY\0suffix", 13)) ==
      resident_host_close::fail_stop, "an embedded NUL suffix must not be accepted as E_BUSY");
}
void completed_close() {
  require(resident_classify_host_close(true, std::string()) == resident_host_close::completed,
      "actual Host success permits the subsequent real HWND cleanup");
}
struct test_case { const char* name; void (*run)(); };
const test_case cases[] = {
    {"inactive", inactive}, {"zero-tick", zero_tick}, {"repeated-exit", repeated_exit},
    {"pending-boundary", pending_boundary}, {"tick-wrap", tick_wrap},
    {"clock-regression", clock_regression}, {"exact-pending", exact_pending},
    {"unknown-close", unknown_close}, {"completed-close", completed_close}};
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "Select exactly one resident shutdown policy case.\n"; return 2; }
  for (const auto& test : cases) {
    if (std::string(argv[1]) != test.name) continue;
    try {
      test.run();
      std::cout << "resident-shutdown-policy " << test.name << " passed\n";
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "resident-shutdown-policy " << test.name << ": " << error.what() << '\n';
      return 1;
    }
  }
  std::cerr << "Unknown resident shutdown policy case.\n";
  return 2;
}
