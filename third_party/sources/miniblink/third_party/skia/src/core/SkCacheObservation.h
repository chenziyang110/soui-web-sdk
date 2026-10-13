/* Optional scalar observation; no creation, traversal, purge or message consumption. */
#ifndef SkCacheObservation_DEFINED
#define SkCacheObservation_DEFINED
#include <stdint.h>

enum SkCacheObservationStatus {
    kNotStarted_SkCacheObservation = 0,
    kReady_SkCacheObservation = 1,
    kBusy_SkCacheObservation = 2,
    kUnavailable_SkCacheObservation = 3
};
struct SkGlyphCacheObservation {
    uint32_t status;
    uint64_t bytes, count, byteLimit, countLimit;
};
struct SkResourceCacheObservation {
    uint32_t status, discardable;
    uint64_t bytes, count, byteLimit, countLimit, rawSingleLimit, effectiveSingleLimit;
};
SkGlyphCacheObservation SkPeekAttachedGlyphCache();
#endif
