#include "cc/trees/ScrollTreeSnapshot.h"
#include "cc/raster/RasterSchedulingPolicy.h"
#include <cstdio>
#include <utility>

static int failures = 0;
static void require(bool value) { if (!value) { ++failures; std::fputs("scene/cancellation requirement failed\n", stderr); } }
static cc::SceneTransaction transaction(uint64_t base, uint64_t version,
    int64_t first, int64_t last, bool ready)
{
    cc::SceneTransaction result;
    result.baseVersion = base; result.newVersion = version;
    result.firstActionId = first; result.lastActionId = last;
    result.resourcesReady = ready; result.scrollTree.setSceneVersion(version);
    return result;
}
int main()
{
    cc::SceneCoordinator scene;
    require(scene.submit(transaction(0, 1, 0, 0, true)) && scene.activateReady());
    require(scene.submit(transaction(1, 2, 10, 12, false)));
    require(scene.cancelPendingForAction(11));
    require(!scene.hasPending() && !scene.hasReady() && scene.activeVersion() == 1);
    require(!scene.markResourcesReadyThroughAction(12) && !scene.activateReady());
    require(!scene.submit(transaction(1, 2, 10, 12, false)));
    require(scene.submit(transaction(1, 3, 13, 14, false)));
    require(!scene.markResourcesReadyThroughAction(13));
    require(scene.markResourcesReadyThroughAction(14) && scene.activateReady());
    // A rejection receipt may beat scene registration; multiple/duplicate
    // receipts still reject that exact interval and preserve the visible scene.
    require(!scene.cancelPendingForAction(20));
    require(!scene.cancelPendingForAction(21));
    require(!scene.cancelPendingForAction(20));
    require(!scene.submit(transaction(3, 4, 20, 22, false)));
    require(scene.activeVersion() == 3);
    require(scene.submit(transaction(3, 4, 23, 25, false)));
    require(!scene.markResourcesReadyThroughAction(24));
    require(scene.markResourcesReadyThroughAction(25) && scene.activateReady());
    for (int64_t action = 100; action != 165; ++action)
        scene.cancelPendingForAction(action);
    require(scene.cancellationReceiptsOverflowed());
    require(!scene.submit(transaction(4, 5, 100, 164, false)));
    require(scene.submit(transaction(4, 5, 200, 201, false)));
    require(scene.markResourcesReadyThroughAction(201) && scene.activateReady());
    require(scene.submit(transaction(5, 6, 300, 301, true)));
    require(scene.cancelPendingForAction(300) && !scene.activateReady());
    require(scene.activeVersion() == 5);
    require(scene.submit(transaction(5, 6, 0, 0, true)) && scene.activateReady());
    require(!scene.cancelPendingForAction(300) && scene.activeVersion() == 6);
    cc::SceneCoordinator ownership;
    auto owned = transaction(0, 1, 10, 11, false);
    owned.scrollTree.addHitTestRegion(cc::ScrollHitTestRegion(
        cc::ScrollRect(0, 0, 10, 10), cc::ScrollNodeKey()));
    const auto* regionStorage = owned.scrollTree.hitTestRegions().data();
    require(ownership.submit(std::move(owned)));
    require(ownership.pendingScene().scrollTree.hitTestRegions().data() == regionStorage);
    require(ownership.markResourcesReadyThroughAction(11) && ownership.activateReady());
    require(ownership.activeScene().scrollTree.hitTestRegions().data() == regionStorage);
    cc::RasterTaskReceipt cancelled;
    require(cancelled.cancelUnstarted() && !cancelled.cancelUnstarted());
    require(!cancelled.start() && !cancelled.finish());
    cc::RasterTaskReceipt running;
    require(running.start() && !running.cancelUnstarted());
    require(running.finish() && !running.finish() && !running.failRunning());
    cc::RasterTaskReceipt failed;
    require(failed.start() && failed.failRunning() && !failed.failRunning());
    return failures ? 1 : 0;
}
