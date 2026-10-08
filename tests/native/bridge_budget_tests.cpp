#include "../../sdk/src/host/bridge_request_budget.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using budget = soui::sdk::detail::bridge_request_budget;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main() {
  try {
    budget queue;
    budget::rejection error;
    std::vector<std::shared_ptr<budget::lease>> ordinary, control;
    for (std::size_t i = 0; i < budget::ordinary_limit; ++i) {
      auto lease = queue.acquire("request:" + std::to_string(i), 1, false, error);
      require(lease && error == budget::rejection::none, "ordinary capacity available");
      ordinary.push_back(std::move(lease));
    }
    require(!queue.acquire("overflow", 1, false, error) && error == budget::rejection::full, "reject ordinary overload");
    require(!queue.acquire("after-reload", 2, false, error) && error == budget::rejection::full, "reload must not reset occupied capacity");
    require(!queue.acquire("request:0", 1, true, error) && error == budget::rejection::duplicate, "identity unique across both capacities");
    for (std::size_t i = 0; i < budget::control_limit; ++i) {
      auto lease = queue.acquire("release:" + std::to_string(i), 1, true, error);
      require(lease != nullptr, "reserve cleanup capacity while ordinary requests are full");
      control.push_back(std::move(lease));
    }
    require(!queue.acquire("release-overflow", 1, true, error) && error == budget::rejection::full, "cleanup capacity is also bounded");
    auto renewal = queue.acquire("renew", 1, budget::lane::renewal, error);
    require(renewal != nullptr, "renewal has capacity while ordinary and cleanup lanes are full");
    require(!queue.acquire("renew-overflow", 1, budget::lane::renewal, error) && error == budget::rejection::full,
        "only one native renewal can wait or execute per host");
    require(!queue.acquire("renew", 1, true, error) && error == budget::rejection::duplicate,
        "renewal identity cannot be reused in another lane");
    require(!queue.acquire("renew-next-document", 2, budget::lane::renewal, error),
        "reload must not reset an occupied renewal slot");
    renewal->release();
    auto next_renewal = queue.acquire("renew", 1, budget::lane::renewal, error);
    renewal.reset();
    require(next_renewal != nullptr && !queue.acquire("renew-late", 1, budget::lane::renewal, error),
        "idempotent old release cannot free replacement renewal");
    next_renewal.reset();
    auto response_owner = ordinary.back();
    ordinary.pop_back();
    require(!queue.acquire("response-wait", 1, false, error), "response queue retains lease");
    response_owner->release();
    auto replacement = queue.acquire("request:255", 1, false, error);
    require(replacement != nullptr, "response callback can reuse completed capacity before old callback destruction");
    response_owner->release();
    response_owner.reset();
    require(!queue.acquire("request:255", 1, false, error) && error == budget::rejection::duplicate,
        "old response destruction must not release the replacement identity");
    replacement.reset();
    auto new_document = queue.acquire("request:0", 2, false, error);
    require(new_document != nullptr, "new document can reuse identity when capacity is available");
    ordinary.clear(); control.clear(); new_document.reset();
    for (unsigned round = 0; round < 30; ++round) {
      std::atomic<bool> start{false}, release{false};
      std::atomic<unsigned> attempted{0}, accepted{0};
      std::vector<std::thread> workers;
      for (unsigned i = 0; i < 8; ++i) workers.emplace_back([&] {
        while (!start.load()) std::this_thread::yield();
        budget::rejection reason;
        auto lease = queue.acquire("concurrent-identity", 3, false, reason);
        if (lease) ++accepted;
        ++attempted;
        while (!release.load()) std::this_thread::yield();
      });
      start = true;
      while (attempted.load() != 8) std::this_thread::yield();
      release = true;
      for (auto& worker : workers) worker.join();
      require(accepted == 1, "only one concurrent owner can reserve an identity");
    }
    std::shared_ptr<budget::lease> outstanding;
    { budget temporary; outstanding = temporary.acquire("late-response", 1, false, error); }
    outstanding.reset(); // Lifetime ends safely after the budget facade is destroyed.
    std::cout << "Native bridge budget: limits, generations, concurrency and lease cleanup passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
