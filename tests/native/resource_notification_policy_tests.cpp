#include "cc/trees/ResourceNotificationPolicy.h"
#include <cstdio>

static int failures = 0;
static void require(bool value) { if (!value) { ++failures; std::fputs("resource notification requirement failed\n", stderr); } }

int main()
{
    cc::ResourceNotificationPolicy policy;
    // Completion before registration is covered by the owner's condition
    // recheck; it creates no unnecessary task by itself.
    require(!policy.arm(1).sequence);
    policy.wait();
    policy.notify(false);
    const auto during = policy.arm(1);
    require(during.sequence != 0);
    policy.notify(false);
    require(!policy.arm(1).sequence); // Any number of producers coalesce.
    const auto first = policy.consume(during, 1);
    require(first.waiting && !first.dirty && !first.stale);
    require(!policy.consume(during, 1).waiting); // Exactly once.
    policy.notify(true);
    const auto rejected = policy.arm(1);
    require(policy.dispose(rejected.sequence));
    const auto retry = policy.arm(1);
    require(retry.sequence && retry.sequence != rejected.sequence);
    require(!policy.dispose(rejected.sequence));
    const auto stale = policy.consume(retry, 2);
    require(stale.stale);
    const auto current = policy.arm(2);
    require(current.sequence && policy.consume(current, 2).dirty);
    policy.wait();
    require(policy.arm(2).sequence != 0);
    return failures ? 1 : 0;
}
