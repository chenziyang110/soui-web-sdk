#include "cc/raster/RasterSchedulingPolicy.h"
#include "cc/raster/RasterCandidateBudget.h"
#include <cstdio>
#include <limits>

static int failures = 0;
static void require(bool value) { if (!value) { ++failures; std::fputs("raster budget requirement failed\n", stderr); } }
int main()
{
    using Policy = cc::RasterSchedulingPolicy;
    require(Policy::workers(0) == 1 && Policy::workers(5) == 1);
    require(Policy::workers(2) == 2 && Policy::workers(4) == 4);
    Policy budget;
    for (unsigned i = 0; i < Policy::maximumQueuedTasks; ++i)
        require(budget.reserve(1024));
    require(!budget.reserve(1));
    for (unsigned i = 0; i < Policy::maximumQueuedTasks; ++i)
        budget.release(1024);
    require(budget.tasks() == 0 && budget.bytes() == 0);
    require(budget.reserve(Policy::maximumQueuedBytes));
    require(!budget.reserve(1));
    budget.release(Policy::maximumQueuedBytes);
    require(!budget.reserve(Policy::estimatedBytes(100000, 100000)));
    require(!budget.reserve(Policy::estimatedBytes((std::numeric_limits<double>::infinity)(), 10)));
    require(budget.reserve(Policy::estimatedBytes(128, 128)));
    cc::RasterCandidateBudget scene;
    for (size_t index = 0; index < cc::RasterCandidateBudget::maximumLayers; ++index)
        require(scene.layer());
    require(!scene.layer() && scene.failure() == cc::RasterCandidateBudget::Failure::LayerLimit);
    cc::RasterCandidateBudget memory;
    require(memory.addBytes(cc::RasterCandidateBudget::maximumBytes));
    require(!memory.addBytes(1));
    cc::RasterCandidateBudget unknown;
    require(!unknown.reject(cc::RasterCandidateBudget::Failure::UnknownPictureOwner));
    require(!unknown.eligible());
    return failures ? 1 : 0;
}
