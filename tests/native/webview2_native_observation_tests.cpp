#include "native_observation.h"
#include "preparation_lifetime.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using soui::sdk::detail::preparation_lifetime;
using soui::sdk::detail::webview2_native_counters;
using operation = preparation_lifetime::operation;
using snapshot = webview2_native_counters::snapshot;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void expect(const snapshot& value, std::uint64_t acquired, std::uint64_t released,
            std::uint64_t closed, std::uint64_t policy, bool owns, bool current_closed,
            bool fault, const char* message) {
  require(value.acquired == acquired && value.released == released && value.closed == closed &&
      value.policy_active == policy && value.owns_controller == owns &&
      value.current_closed == current_closed && value.fault == fault, message);
}
bool same_scalars(const snapshot& before, const snapshot& after) {
  return before.acquired == after.acquired && before.released == after.released &&
      before.closed == after.closed && before.policy_active == after.policy_active &&
      before.owns_controller == after.owns_controller && before.current_closed == after.current_closed;
}
void expect_preparation(const preparation_lifetime& value, std::size_t pending,
                        std::size_t active, bool settled, const char* message) {
  const auto counts = value.observe();
  require(counts.pending == pending && counts.active == active && value.settled() == settled, message);
  const auto second = value.observe();
  require(second.pending == counts.pending && second.active == counts.active,
      "The preparation getter must not consume pending or active ownership");
}

void controller_reference_lifetime() {
  webview2_native_counters value{true};
  require(value.enabled(), "Explicit observation enables the real counter helper");
  expect(value.observe(), 0, 0, 0, 0, false, false, false, "Fresh observer has no acquired controller");
  value.acquire();
  expect(value.observe(), 1, 0, 0, 0, true, false, false, "An acquired reference is live before Close");
  value.close_result(true);
  expect(value.observe(), 1, 0, 1, 0, true, true, false,
      "Successful Close is recorded while the controller reference is still held");
  value.release();
  expect(value.observe(), 1, 1, 1, 0, false, true, false,
      "Reference release after the integration Reset is a separate lifetime event");
  value.acquire();
  expect(value.observe(), 2, 1, 1, 0, true, false, false,
      "A later reference retains history but clears the current Close flag");
  value.close_result(true); value.release();
  expect(value.observe(), 2, 2, 2, 0, false, true, false, "Two owned controller lifetimes balance");
  webview2_native_counters next_instance{true};
  expect(next_instance.observe(), 0, 0, 0, 0, false, false, false,
      "A new recording instance starts independent history; it does not inherit old totals");
  next_instance.acquire();
  expect(value.observe(), 2, 2, 2, 0, false, true, false, "A separate instance cannot mutate old history");
  next_instance.close_result(true); next_instance.release();
}

void repeated_close_and_release() {
  webview2_native_counters value{true}; value.acquire();
  value.close_result(true); value.close_result(true); value.close_result(true);
  expect(value.observe(), 1, 0, 1, 0, true, true, false, "Repeated successful Close does not double-count");
  value.release(); value.release();
  expect(value.observe(), 1, 1, 1, 0, false, true, true,
      "A second release is invalid, cannot increment twice, and latches a diagnostic fault");
  value.close_result(true);
  expect(value.observe(), 1, 1, 1, 0, false, true, true,
      "Close without a held reference cannot manufacture a further closed controller");
}

void failed_close_is_sticky() {
  webview2_native_counters value{true}; value.acquire(); value.close_result(false);
  expect(value.observe(), 1, 0, 0, 0, true, false, true,
      "Failed Close preserves the held reference and records no successful Close");
  value.release();
  expect(value.observe(), 1, 0, 0, 0, true, false, true,
      "A failed Close cannot be converted to a valid released reference");
  value.close_result(true); value.release();
  expect(value.observe(), 1, 1, 1, 0, false, true, true,
      "Later cleanup may balance counts but cannot heal the earlier observation fault");
}

void invalid_controller_ownership() {
  webview2_native_counters empty{true}; empty.release();
  expect(empty.observe(), 0, 0, 0, 0, false, false, true, "Release without acquisition cannot underflow");
  webview2_native_counters before_close{true}; before_close.acquire(); before_close.release();
  expect(before_close.observe(), 1, 0, 0, 0, true, false, true, "Release before successful Close retains ownership");
  webview2_native_counters doubled{true}; doubled.acquire(); doubled.close_result(true); doubled.acquire();
  expect(doubled.observe(), 1, 0, 1, 0, true, true, true,
      "Double acquire cannot overwrite a still-held closed reference or inflate acquisition totals");
  doubled.release();
  expect(doubled.observe(), 1, 1, 1, 0, false, true, true, "The retained original reference remains releasable");
}

void policy_borrow_and_underflow() {
  webview2_native_counters value{true};
  require(value.enter_policy() && value.enter_policy(), "Nested real policy callbacks each borrow ownership");
  expect(value.observe(), 0, 0, 0, 2, false, false, false, "Two active policy callback frames are visible");
  value.leave_policy();
  expect(value.observe(), 0, 0, 0, 1, false, false, false, "One callback returning leaves the outer callback active");
  value.leave_policy();
  expect(value.observe(), 0, 0, 0, 0, false, false, false, "Both returned callbacks drain the policy domain");
  value.leave_policy();
  expect(value.observe(), 0, 0, 0, 0, false, false, true, "An unmatched callback return latches fault without wrapping");
  require(value.enter_policy(), "A later real callback is still counted after a fault"); value.leave_policy();
  require(value.observe().fault, "A later balanced callback cannot clear a prior underflow fault");
}

void disabled_is_not_recording() {
  webview2_native_counters value;
  require(!value.enabled(), "Recording is disabled by default");
  value.acquire(); value.acquire(false); value.close_result(false); value.close_result(true);
  value.close_result(true, false); value.release(); value.release(false); value.leave_policy(); value.mark_fault();
  require(!value.enter_policy(), "A disabled policy observer does not borrow or record a callback");
  expect(value.observe(), 0, 0, 0, 0, false, false, false,
      "Disabled observation never fabricates history or fault recovery from lifecycle calls");
}

void overflow_never_wraps() {
  auto maximum = (std::numeric_limits<std::uint64_t>::max)();
  require(!webview2_native_counters::increment(maximum) && maximum == (std::numeric_limits<std::uint64_t>::max)(),
      "UINT64_MAX is rejected and preserved, not wrapped to zero");
  auto near_maximum = maximum - 1;
  require(webview2_native_counters::increment(near_maximum) && near_maximum == maximum,
      "The last representable increment succeeds exactly once");
  require(!webview2_native_counters::increment(near_maximum) && near_maximum == maximum,
      "Repeating the boundary increment cannot wrap an accumulated history");
  std::uint64_t zero{};
  require(webview2_native_counters::increment(zero) && zero == 1, "The same production guard admits an initial counter");
}

void foreign_owner_only_faults() {
  webview2_native_counters acquire{true}; const auto empty = acquire.observe(); acquire.acquire(false);
  require(same_scalars(empty, acquire.observe()) && acquire.observe().fault,
      "Foreign acquire writes only the atomic fault latch");
  webview2_native_counters close{true}; close.acquire(); const auto held = close.observe(); close.close_result(true, false);
  require(same_scalars(held, close.observe()) && close.observe().fault,
      "Foreign Close result cannot mutate owner-STA history or reference state");
  webview2_native_counters release{true}; release.acquire(); release.close_result(true);
  const auto closed = release.observe(); release.release(false);
  require(same_scalars(closed, release.observe()) && release.observe().fault,
      "Foreign release cannot make a still-held controller look drained");
}

void preparation_pending_and_callback_borrow() {
  preparation_lifetime value;
  expect_preparation(value, 0, 0, true, "Fresh preparation has no submitted or running callbacks");
  require(value.begin(operation::environment) && value.begin(operation::controller) && value.begin(operation::script),
      "Independent native preparation operations may be submitted");
  require(!value.begin(operation::environment), "Duplicate submission does not add another pending callback");
  expect_preparation(value, 3, 0, false, "Every actual pending preparation operation is observed");
  {
    auto environment = value.receive(operation::environment);
    require(bool(environment), "Environment callback receives its real outstanding submission");
    expect_preparation(value, 2, 1, false, "Receive transfers a pending operation to an active borrowed callback");
    {
      auto script = value.receive(operation::script);
      require(bool(script), "A nested script callback is independently active");
      auto duplicate = value.receive(operation::environment);
      require(!duplicate, "Duplicate callback arrival does not acquire a second active borrow");
      expect_preparation(value, 1, 2, false, "Nested real callbacks count twice; duplicate counts zero");
      require(value.settle(operation::controller), "Synchronous submission failure settles only its pending operation");
      require(!value.settle(operation::controller), "Repeated synchronous settlement cannot decrement again");
      expect_preparation(value, 0, 2, false, "Settled pending ownership cannot erase running callback frames");
    }
    expect_preparation(value, 0, 1, false, "Returning the nested callback retains the outer borrowed frame");
  }
  expect_preparation(value, 0, 0, true, "Only the final callback return makes preparation settled");
}

void cancellation_preserves_real_observation() {
  preparation_lifetime value;
  require(value.begin(operation::environment) && value.begin(operation::controller), "Submit before cancellation");
  value.cancel();
  require(value.cancelled() && !value.begin(operation::script), "Cancellation prevents new submissions");
  expect_preparation(value, 2, 0, false, "Cancel does not claim callbacks arrived or drained");
  bool caught = false;
  try {
    auto environment = value.receive(operation::environment);
    require(bool(environment), "A late callback still receives its outstanding operation after cancellation");
    expect_preparation(value, 1, 1, false, "Cancelled preparation still observes the live late callback");
    require(value.settle(operation::controller), "Synchronous controller failure settles the other operation");
    expect_preparation(value, 0, 1, false, "Cancellation and settlement cannot destroy a borrowed callback owner");
    throw 17;
  } catch (int failure) { caught = failure == 17; }
  require(caught && value.cancelled(), "Only the controlled callback exception is consumed; cancellation remains latched");
  expect_preparation(value, 0, 0, true, "Exception unwinding returns the actual callback borrow");
  { auto duplicate = value.receive(operation::environment); require(!duplicate, "Late duplicate is not a new completion"); }
  expect_preparation(value, 0, 0, true, "Duplicate cleanup cannot underflow active callbacks");
}
}  // namespace

int main() {
  try {
    controller_reference_lifetime(); repeated_close_and_release(); failed_close_is_sticky();
    invalid_controller_ownership(); policy_borrow_and_underflow(); disabled_is_not_recording();
    overflow_never_wraps(); foreign_owner_only_faults(); preparation_pending_and_callback_borrow();
    cancellation_preserves_real_observation();
    std::cout << "10 WebView2 native observation helper groups passed; no COM, product, GUI or Runtime executed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
