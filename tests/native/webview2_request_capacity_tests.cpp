#include "request_capacity.h"
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
namespace capacity = soui::sdk::detail::webview2_capacity;
using json = nlohmann::json;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
json packet(const char* method, json params) {
  return {{"method", method}, {"params", std::move(params)}, {"sessionId", "session:A"},
      {"origin", "app://test"}, {"token", "opaque-bearer"}};
}
void classification_is_independent_of_claims() {
  auto business = packet("app.work", json::object());
  business["control"] = true; business["priority"] = "renewal";
  require(capacity::classify(business) == capacity::lane::ordinary, "Page claims changed native priority");
  auto renewal = packet("bridge.session.renew", {{"operationId", "rotation:1"}});
  require(capacity::classify(renewal) == capacity::lane::renewal, "Valid renewal was not classified");
  renewal["params"]["extra"] = true;
  require(capacity::classify(renewal) == capacity::lane::ordinary, "Unbounded renewal claimed reserve");
  for (const auto& id : {std::string{}, std::string("bad space"), std::string(129, 'a')}) {
    renewal["params"] = {{"operationId", id}};
    require(capacity::classify(renewal) == capacity::lane::ordinary, "Invalid operationId claimed reserve");
  }
  renewal["params"] = {{"operationId", "valid"}}; renewal["token"] = "";
  require(capacity::classify(renewal) == capacity::lane::ordinary, "Missing credentials claimed reserve");
  require(capacity::classify(packet("task.cancel", {{"taskId", "task:A"}})) == capacity::lane::cleanup,
      "Cancel lost cleanup classification");
  require(capacity::classify(packet("task.unsubscribe", {{"subscriptionId", "sub:A"}})) == capacity::lane::cleanup,
      "Unsubscribe lost cleanup classification");
  require(capacity::classify(packet("task.cancel", {{"subscriptionId", "sub:A"}})) == capacity::lane::ordinary,
      "Wrong resource selector claimed cleanup reserve");
  require(capacity::classify(packet("task.unsubscribe", {{"subscriptionId", std::string(257, 'x')}})) == capacity::lane::ordinary,
      "Oversized selector claimed cleanup reserve");
}
void independent_budgets_and_release() {
  capacity::request_budget budget;
  capacity::request_budget::rejection rejected;
  std::vector<std::shared_ptr<capacity::request_budget::lease>> ordinary, cleanup;
  for (int index = 0; index < 128; ++index) {
    ordinary.push_back(budget.acquire("work:" + std::to_string(index), 1, capacity::lane::ordinary, rejected));
    require(ordinary.back() != nullptr, "Ordinary limit shrank");
  }
  require(!budget.acquire("overflow", 1, capacity::lane::ordinary, rejected) &&
      rejected == capacity::request_budget::rejection::full, "Ordinary overflow admitted");
  auto renewal = budget.acquire("renew", 1, capacity::lane::renewal, rejected);
  require(renewal != nullptr, "Business saturation consumed renewal reserve");
  for (int index = 0; index < 128; ++index) {
    cleanup.push_back(budget.acquire("cleanup:" + std::to_string(index), 1, capacity::lane::cleanup, rejected));
    require(cleanup.back() != nullptr, "Business saturation consumed cleanup reserve");
  }
  require(!budget.acquire("renew:overflow", 1, capacity::lane::renewal, rejected), "Renewal grew beyond one slot");
  require(!budget.acquire("cleanup:overflow", 1, capacity::lane::cleanup, rejected), "Cleanup became unbounded");
  require(!budget.acquire("work:0", 1, capacity::lane::renewal, rejected) &&
      rejected == capacity::request_budget::rejection::duplicate, "Cross-lane duplicate admitted");
  renewal->release(); renewal->release();
  auto replacement = budget.acquire("renew", 1, capacity::lane::renewal, rejected);
  require(replacement != nullptr, "Completion/timeout failed to return renewal capacity");
  renewal.reset();
  require(!budget.acquire("renew:extra", 1, capacity::lane::renewal, rejected), "Old destructor returned replacement slot");
  cleanup.pop_back();
  require(budget.acquire("cleanup:replacement", 1, capacity::lane::cleanup, rejected) != nullptr,
      "Dropped reply/queue failed to return cleanup capacity");
  require(!budget.acquire("work:still-full", 1, capacity::lane::ordinary, rejected), "Control release freed business capacity");
}
void old_document_release_is_isolated() {
  auto budget = std::make_shared<capacity::request_budget>();
  capacity::request_budget::rejection rejected;
  auto old = budget->acquire("same-id", 1, capacity::lane::renewal, rejected);
  budget = std::make_shared<capacity::request_budget>();
  auto current = budget->acquire("same-id", 2, capacity::lane::renewal, rejected);
  old.reset();
  require(current && !budget->acquire("new-id", 2, capacity::lane::renewal, rejected),
      "Old document release changed current capacity");
  current.reset();
  require(budget->acquire("new-id", 2, capacity::lane::renewal, rejected) != nullptr, "Current lease was not released");
}
void priority_is_bounded_and_fair() {
  struct queued { capacity::lane category; unsigned id; };
  std::deque<queued> queue;
  capacity::priority_policy policy;
  for (unsigned index = 0; index < 128; ++index) queue.push_back({capacity::lane::ordinary, index});
  for (unsigned index = 0; index < 128; ++index) queue.push_back({capacity::lane::cleanup, index});
  queue.push_back({capacity::lane::renewal, 0});
  const auto take = [&] {
    auto item = capacity::next(queue, policy, [](const queued& value) { return value.category; });
    require(item != queue.end(), "Nonempty queue was not scheduled");
    const auto chosen = item->category; queue.erase(item); return chosen;
  };
  require(take() == capacity::lane::renewal, "Renewal waited behind business/cleanup backlog");
  for (int index = 0; index < 4; ++index) require(take() == capacity::lane::cleanup, "Cleanup did not receive bounded priority");
  require(take() == capacity::lane::ordinary, "Cleanup starvation denied business progress");
  require(take() == capacity::lane::cleanup, "Cleanup priority did not resume");
}
void navigation_barrier_precedes_new_control_work() {
  struct queued { capacity::lane category; bool revoke; };
  std::deque<queued> queue{{capacity::lane::ordinary, true}, {capacity::lane::ordinary, false},
      {capacity::lane::cleanup, false}, {capacity::lane::renewal, false}};
  capacity::priority_policy policy;
  const auto take = [&] {
    auto item = capacity::next_with_barrier(queue, policy,
        [](const queued& value) { return value.category; }, [](const queued& value) { return value.revoke; });
    require(item != queue.end(), "Navigation queue was not scheduled");
    const auto chosen = *item; queue.erase(item); return chosen;
  };
  require(take().revoke, "New document renewal bypassed session revocation");
  require(take().category == capacity::lane::renewal, "Renewal priority did not resume after the barrier");
}
void reply_reserves_survive_business_events_and_rejections() {
  capacity::output_budget output;
  for (int index = 0; index < 128; ++index)
    require(output.reserve(capacity::lane::ordinary, capacity::message_limit), "Ordinary reply budget shrank");
  require(!output.reserve(capacity::lane::ordinary, 1), "Ordinary output overflow admitted");
  require(output.reserve(capacity::lane::renewal, capacity::message_limit), "Business output consumed renewal reply reserve");
  for (int index = 0; index < 128; ++index)
    require(output.reserve(capacity::lane::cleanup, capacity::message_limit), "Business output consumed cleanup reply reserve");
  require(!output.reserve(capacity::lane::renewal, 1), "Renewal replies exceeded one slot");
  output.release(capacity::lane::renewal, capacity::message_limit);
  require(!output.reserve(capacity::lane::renewal, capacity::message_limit + 1), "Oversized reply admitted");
  require(output.reserve(capacity::lane::renewal, 1), "Reply delivery did not release its byte/count budget");
  require(!output.reserve(capacity::lane::ordinary, 0), "Reply release affected another lane");
}
}  // namespace

int main() {
  try {
    classification_is_independent_of_claims(); independent_budgets_and_release();
    old_document_release_is_isolated(); priority_is_bounded_and_fair(); navigation_barrier_precedes_new_control_work();
    reply_reserves_survive_business_events_and_rejections();
    std::cout << "WebView2 capacity policy: 6 cases passed\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
