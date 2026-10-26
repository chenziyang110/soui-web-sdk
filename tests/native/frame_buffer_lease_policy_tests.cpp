#include "cc/trees/FrameBufferLeasePolicy.h"
#include <cstdio>
#include <memory>

static int failures = 0;
static void require(bool value) { if (!value) { ++failures; std::fputs("framebuffer lease requirement failed\n", stderr); } }
int main()
{
    using Policy = cc::FrameBufferLeasePolicy;
    uint64_t bytes = 0;
    require(Policy::bytesForSize(3840, 2160, &bytes) && bytes == 33177600);
    require(!Policy::bytesForSize(0, 2160, &bytes));
    require(!Policy::bytesForSize(100000, 100000, &bytes));
    auto front = std::make_shared<int>(17);
    auto lease = front;
    auto back = std::make_shared<int>(23);
    require(Policy::writableSlot(0, front.use_count(), back.use_count()) == 1);
    auto secondLease = back;
    require(Policy::writableSlot(0, front.use_count(), back.use_count()) == -1);
    back.reset(); // Pool/host close cannot invalidate an outstanding lease.
    require(*lease == 17 && *secondLease == 23);
    secondLease.reset();
    lease.reset();
    require(Policy::writableSlot(0, front.use_count(), 0) == 0);
    return failures ? 1 : 0;
}
