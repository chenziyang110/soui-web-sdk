#include "cc/trees/ScrollTreeSnapshot.h"

#include <cmath>

namespace cc {

ScrollHitTestRegion::ScrollHitTestRegion()
{
}

ScrollHitTestRegion::ScrollHitTestRegion(const ScrollRect& regionBounds,
    const ScrollNodeKey& regionTarget)
    : bounds(regionBounds)
    , target(regionTarget)
{
}

bool ScrollHitTestRegion::isValid() const
{
    return std::isfinite(bounds.x) && std::isfinite(bounds.y)
        && std::isfinite(bounds.width) && std::isfinite(bounds.height)
        && bounds.width > 0.0 && bounds.height > 0.0;
}

ScrollTreeSnapshot::ScrollTreeSnapshot()
    : m_sceneVersion(0)
{
}

void ScrollTreeSnapshot::setSceneVersion(uint64_t sceneVersion)
{
    m_sceneVersion = sceneVersion;
}

void ScrollTreeSnapshot::addNode(const ScrollNodeSnapshot& node)
{
    m_nodes.push_back(node);
}

void ScrollTreeSnapshot::addHitTestRegion(
    const ScrollHitTestRegion& region)
{
    m_hitTestRegions.push_back(region);
}

const ScrollNodeSnapshot* ScrollTreeSnapshot::findNode(
    const ScrollNodeKey& key) const
{
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].key == key)
            return &m_nodes[i];
    }
    return nullptr;
}

ScrollNodeSnapshot* ScrollTreeSnapshot::findMutableNode(
    const ScrollNodeKey& key)
{
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].key == key)
            return &m_nodes[i];
    }
    return nullptr;
}

const ScrollNodeSnapshot* ScrollTreeSnapshot::scrollTargetAt(
    double x, double y) const
{
    for (size_t i = m_hitTestRegions.size(); i > 0; --i) {
        const ScrollHitTestRegion& region = m_hitTestRegions[i - 1];
        if (!region.bounds.contains(x, y))
            continue;

        if (!region.target.isValid())
            return nullptr;
        const ScrollNodeSnapshot* target = findNode(region.target);
        if (!target || !target->compositorEligible()
            || !target->viewportClip.contains(x, y)) {
            return nullptr;
        }
        return target;
    }
    return nullptr;
}

bool ScrollTreeSnapshot::isValid() const
{
    if (!m_sceneVersion)
        return false;

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const ScrollNodeSnapshot& node = m_nodes[i];
        if (!node.isValid() || node.sceneVersion != m_sceneVersion)
            return false;
        for (size_t j = i + 1; j < m_nodes.size(); ++j) {
            if (node.key == m_nodes[j].key)
                return false;
        }

        if (node.parent.isValid()) {
            if (node.parent == node.key || !findNode(node.parent))
                return false;

            const ScrollNodeSnapshot* ancestor = findNode(node.parent);
            size_t remaining = m_nodes.size();
            while (ancestor && ancestor->parent.isValid() && remaining--) {
                if (ancestor->parent == node.key)
                    return false;
                ancestor = findNode(ancestor->parent);
            }
            if (ancestor && ancestor->parent.isValid())
                return false;
        }
    }

    for (size_t i = 0; i < m_hitTestRegions.size(); ++i) {
        const ScrollHitTestRegion& region = m_hitTestRegions[i];
        if (!region.isValid())
            return false;
        if (region.target.isValid() && !findNode(region.target))
            return false;
    }
    return true;
}

SceneTransaction::SceneTransaction()
    : baseVersion(0)
    , newVersion(0)
    , firstActionId(0)
    , lastActionId(0)
    , resourcesReady(false)
{
}

bool SceneTransaction::isValidForBase(uint64_t expectedBaseVersion) const
{
    if (baseVersion != expectedBaseVersion || newVersion <= baseVersion)
        return false;
    if ((firstActionId == 0) != (lastActionId == 0))
        return false;
    if (firstActionId && (firstActionId < 0 || lastActionId < firstActionId))
        return false;
    return scrollTree.sceneVersion() == newVersion && scrollTree.isValid();
}

SceneCoordinator::SceneCoordinator()
    : m_hasPending(false)
    , m_hasReady(false)
{
}

bool SceneCoordinator::submit(const SceneTransaction& transaction)
{
    if (m_hasPending || m_hasReady
        || !transaction.isValidForBase(activeVersion())) {
        return false;
    }

    if (transaction.resourcesReady) {
        m_ready = transaction;
        m_hasReady = true;
    } else {
        m_pending = transaction;
        m_hasPending = true;
    }
    return true;
}

bool SceneCoordinator::markResourcesReady(uint64_t sceneVersion)
{
    if (!m_hasPending || m_hasReady
        || m_pending.newVersion != sceneVersion) {
        return false;
    }

    m_pending.resourcesReady = true;
    m_ready = m_pending;
    m_pending = SceneTransaction();
    m_hasPending = false;
    m_hasReady = true;
    return true;
}

bool SceneCoordinator::markResourcesReadyThroughAction(int64_t actionId)
{
    if (!m_hasPending || m_pending.lastActionId <= 0
        || actionId < m_pending.lastActionId) {
        return false;
    }
    return markResourcesReady(m_pending.newVersion);
}

bool SceneCoordinator::activateReady()
{
    if (!m_hasReady)
        return false;

    m_active = m_ready;
    m_ready = SceneTransaction();
    m_hasReady = false;
    return true;
}

} // namespace cc
