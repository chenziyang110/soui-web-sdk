#include "cc/trees/LegacyPaintRetryPolicy.h"

#include <cstdio>
#include <limits>

static int failures = 0;
static void require(bool value) { if (!value) { ++failures; std::fputs("render deadline requirement failed\n", stderr); } }

// Source-only handoff. The implementation agent does not execute this test.
int main()
{
    cc::LegacyPaintRetryPolicy policy;
    const auto first = policy.arm(1.001, 1.0, 1.0 / 60.0);
    require(first.sequence && policy.accepts(first));
    require(!policy.arm(1.002, 1.0, 1.0 / 60.0).sequence);
    require(cc::LegacyPaintRetryPolicy::delayMilliseconds(1.002, first.deadline) > 0);
    // An early scheduler callback must return to the real deadline, never
    // consume dirty or immediately repost a ready task.
    policy.dispose(first);
    const auto early = policy.arm(1.003, 1.0, 1.0 / 60.0);
    require(early.deadline == first.deadline);
    require(cc::LegacyPaintRetryPolicy::delayMilliseconds(1.003, early.deadline) > 0);
    policy.invalidate();
    require(!policy.accepts(early));
    require(!policy.arm(1.004, 1.0, 1.0 / 60.0).sequence);
    policy.dispose(early);
    const auto resized = policy.arm(1.02, 1.0, 1.0 / 60.0);
    require(policy.accepts(resized));
    // Disposal of an old task cannot clear a replacement ticket.
    policy.dispose(first);
    require(policy.accepts(resized));
    policy.dispose(resized); // Includes rejected-post/cancelled-task disposal.
    require(!policy.queued());
    const auto closing = policy.arm(2.0, 0, 1.0 / 60.0);
    policy.close();
    require(!policy.accepts(closing));
    require(!policy.arm(2.0, 0, 1.0 / 60.0).sequence);
    cc::LegacyPaintRetryPolicy exhausted((std::numeric_limits<uint64_t>::max)());
    require(!exhausted.arm(1, 0, 0.01).sequence);
    cc::LegacyPaintRetryPolicy generationEnd(0, (std::numeric_limits<uint64_t>::max)());
    const auto old = generationEnd.arm(1, 0, 0.01);
    generationEnd.invalidate();
    require(!generationEnd.accepts(old));
    generationEnd.dispose(old);
    require(!generationEnd.arm(1, 0, 0.01).sequence);
    cc::LegacyPaintRetryPolicy invalid;
    require(!invalid.arm(1, 0, 0).sequence);
    require(!invalid.arm((std::numeric_limits<double>::infinity)(), 0, 0.01).sequence);
    require(cc::LegacyPaintRetryPolicy::delayMilliseconds(0,
        (std::numeric_limits<double>::infinity)()) == 1000);
    require(cc::LegacyPaintRetryPolicy::delayMilliseconds(-1e300, 1e300) == 1000);
    return failures ? 1 : 0;
}
