#include "../../sdk/src/desktop/deferred_failure.h"

#include <iostream>
#include <stdexcept>

namespace {
struct owner : std::enable_shared_from_this<owner> {
  soui::desktop::detail::deferred_failure<owner> pending;
  int calls = 0;
};
using pending = soui::desktop::detail::deferred_failure<owner>;
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
  ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; } } while (false)
}

int main() {
  {
    auto value = std::make_shared<owner>();
    value->pending.schedule(value);
    value->pending.schedule(value);
    CHECK(value->calls == 0); // Scheduling never calls application code inline.
    pending::drain([](const auto& live) { ++live->calls; });
    CHECK(value->calls == 1);
    pending::drain([](const auto& live) { ++live->calls; });
    CHECK(value->calls == 1);
    value->pending.schedule(value);
    value->pending.cancel();
    pending::drain([](const auto& live) { ++live->calls; });
    CHECK(value->calls == 1);
  }
  {
    int calls = 0;
    auto value = std::make_shared<owner>();
    std::weak_ptr<owner> weak = value;
    value->pending.schedule(value);
    value.reset(); // Destruction unlinks the embedded node; no idle strong cycle.
    CHECK(weak.expired());
    pending::drain([&](const auto&) { ++calls; });
    CHECK(calls == 0);
  }
  {
    auto value = std::make_shared<owner>();
    std::weak_ptr<owner> weak = value;
    value->pending.schedule(value);
    pending::drain([&](const auto& live) {
      value.reset();
      CHECK(!weak.expired()); // The consumer owns a strong reference on its stack.
      ++live->calls;
    });
    CHECK(weak.expired());
  }
  {
    auto first = std::make_shared<owner>();
    auto second = std::make_shared<owner>();
    first->pending.schedule(first);
    second->pending.schedule(second);
    pending::drain([&](const auto& live) {
      ++live->calls;
      second.reset(); // Cancellation during another owner's callback is safe.
    });
    CHECK(first->calls == 1);
  }
  {
    auto first = std::make_shared<owner>();
    auto middle = std::make_shared<owner>();
    auto tail = std::make_shared<owner>();
    auto added = std::make_shared<owner>();
    first->pending.schedule(first);
    middle->pending.schedule(middle);
    tail->pending.schedule(tail);
    middle->pending.cancel();
    middle->pending.cancel();
    tail->pending.cancel();
    added->pending.schedule(added);
    middle->pending.schedule(middle);
    int sequence = 0;
    pending::drain([&](const auto& live) {
      ++sequence;
      CHECK(live == (sequence == 1 ? first : sequence == 2 ? added : middle));
      ++live->calls;
    });
    CHECK(sequence == 3 && first->calls == 1 && added->calls == 1 && middle->calls == 1);
    CHECK(tail->calls == 0 && !pending::pending());
  }
  {
    auto first = std::make_shared<owner>();
    auto second = std::make_shared<owner>();
    auto added = std::make_shared<owner>();
    first->pending.schedule(first);
    second->pending.schedule(second);
    int sequence = 0;
    pending::drain([&](const auto& live) {
      if (live == first) {
        CHECK(++sequence == 1);
        added->pending.schedule(added);
      } else {
        CHECK(live == second && ++sequence == 2);
      }
      ++live->calls;
    });
    CHECK(first->calls == 1 && second->calls == 1 && added->calls == 0);
    CHECK(pending::pending()); // The UI pump must not block with this work idle.
    pending::drain([](const auto& live) { ++live->calls; });
    CHECK(added->calls == 1 && !pending::pending());
  }
  {
    auto value = std::make_shared<owner>();
    value->pending.schedule(value);
    pending::drain([](const auto& live) {
      ++live->calls;
      live->pending.schedule(live);
      pending::drain([](const auto&) { CHECK(false); });
    });
    CHECK(value->calls == 1); // New work and nested drains do not extend this batch.
    CHECK(pending::pending());
    pending::drain([](const auto& live) { ++live->calls; });
    CHECK(value->calls == 2);
    CHECK(!pending::pending());
  }
  {
    auto value = std::make_shared<owner>();
    value->pending.schedule(value);
    try {
      pending::drain([](const auto&) { throw std::runtime_error("consumer failure"); });
      CHECK(false);
    } catch (const std::runtime_error&) {}
    CHECK(!pending::pending());
    value->pending.schedule(value);
    pending::drain([](const auto& live) { ++live->calls; });
    CHECK(value->calls == 1); // A throwing consumer releases the drain guard.
  }
  return failures ? 1 : 0;
}
