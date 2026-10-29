#ifndef CC_TREES_SCROLL_TREE_SNAPSHOT_H_
#define CC_TREES_SCROLL_TREE_SNAPSHOT_H_

#include "cc/input/ScrollController.h"
#include "cc/trees/IndependentInputTrace.h"

#include <cstdint>
#include <array>
#include <vector>

namespace cc {

// Regions are stored in paint order. An invalid target deliberately denotes
// an opaque/main-thread-owned blocker, so hit testing cannot scroll through a
// popup or overlay into content behind it.
struct ScrollHitTestRegion {
    ScrollHitTestRegion();
    ScrollHitTestRegion(const ScrollRect& regionBounds,
        const ScrollNodeKey& regionTarget);

    bool isValid() const;

    ScrollRect bounds;
    ScrollNodeKey target;
};

class ScrollTreeSnapshot {
public:
    ScrollTreeSnapshot();

    void setSceneVersion(uint64_t sceneVersion);
    uint64_t sceneVersion() const { return m_sceneVersion; }
    void setInputIdentity(const content::input_frame_trace::event& value) { m_inputIdentity.set(value); }
    bool matchesInputIdentity(const content::input_frame_trace::event& value) const { return m_inputIdentity.matches(value); }
    void addNode(const ScrollNodeSnapshot& node);
    void addHitTestRegion(const ScrollHitTestRegion& region);
    ScrollNodeSnapshot* findMutableNode(const ScrollNodeKey& key);
    const ScrollNodeSnapshot* findNode(const ScrollNodeKey& key) const;
    // Refresh only the raster coverage of an already-built scene. Newly
    // prepared tiles are usable only after this scene's resource fence.
    bool refreshCoverage(const ScrollNodeKey& key, uint64_t scrollEpoch,
        const ScrollCoverageWindow& coverage, bool coveragePrepared,
        bool hasResourceFence);
    const ScrollNodeSnapshot* scrollTargetAt(double x, double y) const;
    const std::vector<ScrollNodeSnapshot>& nodes() const { return m_nodes; }
    const std::vector<ScrollHitTestRegion>& hitTestRegions() const
        { return m_hitTestRegions; }
    bool isValid() const;

private:
    SceneInputIdentity m_inputIdentity;
    uint64_t m_sceneVersion;
    std::vector<ScrollNodeSnapshot> m_nodes;
    std::vector<ScrollHitTestRegion> m_hitTestRegions;
};

struct SceneTransaction {
    SceneTransaction();

    bool isValidForBase(uint64_t expectedBaseVersion) const;

    uint64_t baseVersion;
    uint64_t newVersion;
    int64_t firstActionId;
    int64_t lastActionId;
    bool resourcesReady;
    ScrollTreeSnapshot scrollTree;
};

// Owns at most one scene in each stage. submit() never mutates active state;
// activateReady() is the explicit compositor frame-boundary switch.
class SceneCoordinator {
public:
    SceneCoordinator();

    bool submit(const SceneTransaction& transaction);
    bool submit(SceneTransaction&& transaction);
    bool markResourcesReady(uint64_t sceneVersion);
    bool markResourcesReadyThroughAction(int64_t actionId);
    bool cancelPendingForAction(int64_t actionId);
    bool cancellationReceiptsOverflowed() const { return m_cancelledOverflow; }
    bool activateReady();

    uint64_t activeVersion() const { return m_active.newVersion; }
    bool hasPending() const { return m_hasPending; }
    bool hasReady() const { return m_hasReady; }
    const SceneTransaction& activeScene() const { return m_active; }
    const SceneTransaction& pendingScene() const { return m_pending; }

private:
    SceneTransaction m_active;
    SceneTransaction m_pending;
    SceneTransaction m_ready;
    bool m_hasPending;
    bool m_hasReady;
    static const size_t maximumCancellationReceipts = 64;
    std::array<int64_t, maximumCancellationReceipts> m_cancelledActions{};
    size_t m_cancelledCount = 0;
    int64_t m_cancelledHighWatermark = 0;
    bool m_cancelledOverflow = false;
    bool containsCancelledAction(int64_t first, int64_t last) const;
};

} // namespace cc

#endif // CC_TREES_SCROLL_TREE_SNAPSHOT_H_
