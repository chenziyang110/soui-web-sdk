#include "cc/trees/LegacyPaintRetryPolicy.h"

#include <cassert>

// Source-only handoff. The implementation agent does not execute this test.
int main()
{
    cc::LegacyPaintRetryPolicy policy;
    const auto first = policy.arm(1.001, 1.0, 1.0 / 60.0);
    assert(first.sequence && policy.accepts(first));
    assert(!policy.arm(1.002, 1.0, 1.0 / 60.0).sequence);
    assert(cc::LegacyPaintRetryPolicy::delayMilliseconds(1.002, first.deadline) > 0);
    // An early scheduler callback must return to the real deadline, never
    // consume dirty or immediately repost a ready task.
    policy.dispose(first);
    const auto early = policy.arm(1.003, 1.0, 1.0 / 60.0);
    assert(early.deadline == first.deadline);
    assert(cc::LegacyPaintRetryPolicy::delayMilliseconds(1.003, early.deadline) > 0);
    policy.invalidate();
    assert(!policy.accepts(early));
    assert(!policy.arm(1.004, 1.0, 1.0 / 60.0).sequence);
    policy.dispose(early);
    const auto resized = policy.arm(1.02, 1.0, 1.0 / 60.0);
    assert(policy.accepts(resized));
    // Disposal of an old task cannot clear a replacement ticket.
    policy.dispose(first);
    assert(policy.accepts(resized));
    policy.dispose(resized); // Includes rejected-post/cancelled-task disposal.
    assert(!policy.queued());
    const auto closing = policy.arm(2.0, 0, 1.0 / 60.0);
    policy.close();
    assert(!policy.accepts(closing));
    assert(!policy.arm(2.0, 0, 1.0 / 60.0).sequence);
}
