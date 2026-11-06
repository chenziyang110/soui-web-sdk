#ifndef DirtyRegionPolicy_h
#define DirtyRegionPolicy_h

#include <cstddef>
#include <limits>

namespace cc {
namespace dirty_region {

struct MergeChoice {
    size_t first;
    size_t second; // count denotes the incoming rectangle.
};

// At the region cap, include the incoming rectangle in the least-cost choice.
// Merging two old distant regions before considering a nearby incoming region
// can inflate raster/paint work by almost a viewport.
template<typename Rect, typename Area, typename UnionArea>
inline MergeChoice leastCostMerge(const Rect* regions, size_t count,
    const Rect& incoming, Area area, UnionArea unionArea)
{
    MergeChoice result = { 0, count };
    double best = std::numeric_limits<double>::max();
    for (size_t i = 0; i < count; ++i) {
        const double incomingCost = unionArea(regions[i], incoming) - area(regions[i]) - area(incoming);
        if (incomingCost < best) {
            best = incomingCost;
            result.first = i;
            result.second = count;
        }
        for (size_t j = i + 1; j < count; ++j) {
            const double cost = unionArea(regions[i], regions[j]) - area(regions[i]) - area(regions[j]);
            if (cost < best) {
                best = cost;
                result.first = i;
                result.second = j;
            }
        }
    }
    return result;
}

} // namespace dirty_region
} // namespace cc

#endif
