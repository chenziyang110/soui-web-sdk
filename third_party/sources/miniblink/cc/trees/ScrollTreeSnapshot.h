#ifndef CC_TREES_SCROLL_TREE_SNAPSHOT_H_
#define CC_TREES_SCROLL_TREE_SNAPSHOT_H_

#include "cc/input/ScrollController.h"

#include <cstdint>
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
    bool markResourcesReady(uint64_t sceneVersion);
    bool markResourcesReadyThroughAction(int64_t actionId);
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
};

} // namespace cc

#endif // CC_TREES_SCROLL_TREE_SNAPSHOT_H_
