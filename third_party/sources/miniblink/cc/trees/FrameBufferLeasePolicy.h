#ifndef CC_TREES_FRAME_BUFFER_LEASE_POLICY_H_
#define CC_TREES_FRAME_BUFFER_LEASE_POLICY_H_

#include <cstddef>
#include <cstdint>

namespace cc {
class FrameBufferLeasePolicy {
public:
    static uint64_t completedPaintFrame(uint64_t previous, uint64_t painted, bool complete)
    {
        return complete && painted > previous ? painted : previous;
    }
    static bool retiresInputCoverage(uint64_t coverageFrame, uint64_t painted, bool complete)
    {
        return complete && painted && coverageFrame == painted;
    }
    static const size_t maximumSlots = 2;
    static const uint64_t maximumBytes = 256ULL * 1024 * 1024;
    static bool bytesForSize(int width, int height, uint64_t* bytes)
    {
        if (!bytes || width <= 0 || height <= 0)
            return false;
        const uint64_t pixels = static_cast<uint64_t>(width) * height;
        if (pixels > maximumBytes / 4)
            return false;
        *bytes = pixels * 4;
        return true;
    }
    static int writableSlot(size_t current, long firstOwners, long secondOwners)
    {
        const long owners[maximumSlots] = {firstOwners, secondOwners};
        if (current < maximumSlots && owners[current] <= 1)
            return static_cast<int>(current);
        for (size_t index = 0; index < maximumSlots; ++index) {
            if (owners[index] <= 1)
                return static_cast<int>(index);
        }
        return -1;
    }
};
} // namespace cc
#endif
