#include "cc/trees/ScrollTreeSnapshot.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

using namespace cc;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool sameVector(const ScrollVector& a, const ScrollVector& b)
{
    return a.x == b.x && a.y == b.y;
}

bool sameRect(const ScrollRect& a, const ScrollRect& b)
{
    return a.x == b.x && a.y == b.y
        && a.width == b.width && a.height == b.height;
}

bool sameScrollbar(const CompositorScrollbarSnapshot& a,
    const CompositorScrollbarSnapshot& b)
{
    return a.enabled == b.enabled && a.vertical == b.vertical
        && a.overlay == b.overlay && a.customStyle == b.customStyle
        && a.layerId == b.layerId && sameRect(a.trackRect, b.trackRect)
        && sameRect(a.localTrackRect, b.localTrackRect)
        && a.minimumThumbLength == b.minimumThumbLength;
}

ScrollNodeSnapshot makeNode(uint64_t version = 1)
{
    ScrollNodeSnapshot node;
    node.key = ScrollNodeKey(1, 2, 31, 4);
    node.sceneVersion = version;
    node.scrollEpoch = 5;
    node.baseOffset = ScrollVector(0, 300);
    node.minimumOffset = ScrollVector(0, 0);
    node.maximumOffset = ScrollVector(0, 1800);
    node.coverageMinimum = node.baseOffset;
    node.coverageMaximum = node.baseOffset;
    node.inputAxes = ScrollInputAxisVertical;
    node.viewportClip = ScrollRect(20, 70, 560, 300);
    node.mainThreadReasons = ScrollMainThreadReasonIncompleteCoverage;
    node.setInputPrediction(node.baseOffset);
    return node;
}

ScrollCoverageWindow coverageFor(const ScrollNodeSnapshot& node)
{
    return computeScrollCoverageWindow(node.baseOffset, node.minimumOffset,
        node.maximumOffset, ScrollVector(560, 300), ScrollVector(0, 1),
        node.inputAxes);
}

ScrollTreeSnapshot makeTree(const ScrollNodeSnapshot& node)
{
    ScrollTreeSnapshot tree;
    tree.setSceneVersion(node.sceneVersion);
    tree.addNode(node);
    tree.addHitTestRegion(ScrollHitTestRegion(node.viewportClip, node.key));
    return tree;
}

SceneTransaction transactionFor(const ScrollTreeSnapshot& tree,
    int64_t firstAction, int64_t lastAction, bool resourcesReady)
{
    SceneTransaction transaction;
    transaction.baseVersion = 0;
    transaction.newVersion = tree.sceneVersion();
    transaction.firstActionId = firstAction;
    transaction.lastActionId = lastAction;
    transaction.resourcesReady = resourcesReady;
    transaction.scrollTree = tree;
    return transaction;
}

void preparedCoverageWaitsForLastRasterAction()
{
    const ScrollNodeSnapshot node = makeNode();
    ScrollTreeSnapshot tree = makeTree(node);
    const ScrollCoverageWindow coverage = coverageFor(node);
    require(!tree.scrollTargetAt(200, 200), "unprepared node accepted input");
    require(tree.refreshCoverage(node.key, node.scrollEpoch, coverage, true, true),
        "prepared coverage was not refreshed");
    require(tree.findNode(node.key)->compositorEligible(), "prepared node remained ineligible");
    require(sameVector(tree.findNode(node.key)->coverageMinimum, coverage.minimumOffset)
            && sameVector(tree.findNode(node.key)->coverageMaximum, coverage.maximumOffset),
        "refresh did not retain the requested coverage window");

    SceneCoordinator coordinator;
    require(coordinator.submit(transactionFor(tree, 101, 104, false)), "scene submit failed");
    require(coordinator.hasPending() && !coordinator.hasReady(), "fence did not hold scene pending");
    require(!coordinator.activateReady(), "scene activated before raster applied");
    require(!coordinator.markResourcesReadyThroughAction(103), "partial raster fence accepted");
    require(!coordinator.activateReady(), "partial raster completion activated scene");
    require(coordinator.markResourcesReadyThroughAction(104), "final raster action did not ready scene");
    require(coordinator.activateReady(), "ready scene did not activate");
    const ScrollNodeSnapshot* active = coordinator.activeScene().scrollTree.scrollTargetAt(200, 200);
    require(active && active->key == node.key, "prepared node did not become input target");
    ScrollController controller;
    require(controller.activateScene(*active), "controller rejected prepared scene");
    require(controller.enqueueDelta(1, ScrollVector(0, 50), 1.0, false), "scroll input rejected");
    require(controller.sample(1.016).valid, "scroll did not advance after resource fence");
}

void noFenceCannotPromoteNewCoverage()
{
    const ScrollNodeSnapshot node = makeNode();
    ScrollTreeSnapshot tree = makeTree(node);
    require(!tree.refreshCoverage(node.key, node.scrollEpoch, coverageFor(node), true, false),
        "new coverage promoted without a resource fence");
    const ScrollNodeSnapshot& result = *tree.findNode(node.key);
    require(!result.coverageComplete && !result.compositorEligible(), "unfenced node became eligible");
    require(result.mainThreadReasons == node.mainThreadReasons
            && sameVector(result.coverageMinimum, node.baseOffset)
            && sameVector(result.coverageMaximum, node.baseOffset),
        "unfenced refresh changed coverage or reasons");
}

void noRasterFrameKeepsExistingCoverage()
{
    const ScrollNodeSnapshot node = makeNode();
    ScrollTreeSnapshot tree = makeTree(node);
    const ScrollCoverageWindow coverage = coverageFor(node);
    require(tree.refreshCoverage(node.key, node.scrollEpoch, coverage, true, true), "preparation failed");
    require(tree.refreshCoverage(node.key, node.scrollEpoch, coverage, true, false),
        "already ready coverage required an unnecessary raster fence");
    SceneCoordinator coordinator;
    require(coordinator.submit(transactionFor(tree, 0, 0, true)), "no-raster scene rejected");
    require(coordinator.activateReady(), "no-raster scene did not activate");
    require(coordinator.activeScene().scrollTree.scrollTargetAt(200, 200),
        "no-raster scene lost its input target");
}

void missingOrDirtyCoverageStaysIneligible()
{
    const ScrollNodeSnapshot node = makeNode();
    ScrollTreeSnapshot tree = makeTree(node);
    const ScrollCoverageWindow coverage = coverageFor(node);
    require(tree.refreshCoverage(node.key, node.scrollEpoch, coverage, false, true), "missing refresh failed");
    SceneCoordinator coordinator;
    require(coordinator.submit(transactionFor(tree, 10, 11, false)), "incomplete scene rejected");
    require(coordinator.markResourcesReadyThroughAction(11) && coordinator.activateReady(),
        "incomplete scene did not finish its actions");
    require(!coordinator.activeScene().scrollTree.scrollTargetAt(200, 200),
        "action completion fabricated missing coverage");

    require(tree.refreshCoverage(node.key, node.scrollEpoch, coverage, true, true), "preparation failed");
    require(tree.refreshCoverage(node.key, node.scrollEpoch, coverage, false, false), "dirty refresh failed");
    const ScrollNodeSnapshot& dirty = *tree.findNode(node.key);
    require(!dirty.coverageComplete && !dirty.compositorEligible()
            && (dirty.mainThreadReasons & ScrollMainThreadReasonIncompleteCoverage),
        "newly dirty coverage remained eligible");
    require(sameVector(dirty.coverageMinimum, node.baseOffset)
            && sameVector(dirty.coverageMaximum, node.baseOffset),
        "dirty node retained unsafe coverage bounds");
}

void refreshPreservesOtherReasonsAndMetadata()
{
    ScrollNodeSnapshot node = makeNode();
    node.parent = ScrollNodeKey(1, 2, 30, 4);
    node.acknowledgedSample = 17;
    node.acknowledgedTotal = ScrollVector(0, 37);
    node.mainThreadReasons |= ScrollMainThreadReasonWheelHandler
        | ScrollMainThreadReasonUnsupportedScrollbar | ScrollMainThreadReasonComplexTransform;
    node.verticalScrollbar.enabled = true;
    node.verticalScrollbar.vertical = true;
    node.verticalScrollbar.customStyle = true;
    node.verticalScrollbar.layerId = 44;
    node.verticalScrollbar.trackRect = ScrollRect(570, 70, 10, 300);
    node.verticalScrollbar.localTrackRect = ScrollRect(0, 0, 10, 300);
    ScrollTreeSnapshot tree = makeTree(node);
    require(tree.refreshCoverage(node.key, node.scrollEpoch, coverageFor(node), true, true), "refresh failed");
    const ScrollNodeSnapshot& result = *tree.findNode(node.key);
    require(result.mainThreadReasons == (node.mainThreadReasons & ~ScrollMainThreadReasonIncompleteCoverage)
            && !result.compositorEligible(), "coverage refresh cleared an unrelated fallback reason");
    require(result.key == node.key && result.parent == node.parent
            && result.sceneVersion == node.sceneVersion && result.scrollEpoch == node.scrollEpoch
            && result.acknowledgedSample == node.acknowledgedSample
            && sameVector(result.acknowledgedTotal, node.acknowledgedTotal)
            && sameVector(result.baseOffset, node.baseOffset)
            && sameVector(result.inputPrediction, node.inputPrediction)
            && sameVector(result.minimumOffset, node.minimumOffset)
            && sameVector(result.maximumOffset, node.maximumOffset)
            && result.inputAxes == node.inputAxes && sameRect(result.viewportClip, node.viewportClip)
            && sameScrollbar(result.verticalScrollbar, node.verticalScrollbar)
            && sameScrollbar(result.horizontalScrollbar, node.horizontalScrollbar),
        "coverage refresh changed geometry, ownership, acknowledgement, epoch or scrollbar metadata");
    require(tree.sceneVersion() == node.sceneVersion && tree.hitTestRegions().size() == 1
            && sameRect(tree.hitTestRegions()[0].bounds, node.viewportClip)
            && tree.hitTestRegions()[0].target == node.key, "coverage refresh rebuilt the scene or hit order");
}

void staleIdentityAndEpochCannotRefresh()
{
    const ScrollNodeSnapshot node = makeNode();
    ScrollTreeSnapshot tree = makeTree(node);
    ScrollNodeKey retiredKey = node.key;
    ++retiredKey.nodeGeneration;
    require(!tree.refreshCoverage(retiredKey, node.scrollEpoch, coverageFor(node), true, true),
        "retired node generation refreshed a reused layer");
    require(!tree.refreshCoverage(node.key, node.scrollEpoch + 1, coverageFor(node), true, true),
        "different epoch refreshed coverage");
    require(tree.nodes().size() == 1 && !tree.findNode(node.key)->coverageComplete,
        "identity mismatch mutated the scene");
}

void emptyFrameCanStillActivate()
{
    ScrollTreeSnapshot tree;
    tree.setSceneVersion(1);
    const ScrollNodeSnapshot node = makeNode();
    require(!tree.refreshCoverage(node.key, node.scrollEpoch, coverageFor(node), true, true),
        "coverage refresh fabricated a node");
    SceneCoordinator coordinator;
    require(coordinator.submit(transactionFor(tree, 0, 0, true)) && coordinator.activateReady(),
        "empty frame acquired a spurious raster dependency");
    require(coordinator.activeScene().scrollTree.nodes().empty(), "empty frame gained a node");
}

ScrollNodeSnapshot readyMotionNode(bool horizontal, double direction)
{
    ScrollNodeSnapshot node = makeNode();
    if (horizontal) {
        node.baseOffset = ScrollVector(300, 0);
        node.maximumOffset = ScrollVector(1800, 0);
        node.inputAxes = ScrollInputAxisHorizontal;
    }
    const ScrollCoverageWindow coverage = computeScrollCoverageWindow(
        node.baseOffset, node.minimumOffset, node.maximumOffset,
        ScrollVector(560, 300),
        horizontal ? ScrollVector(direction, 0) : ScrollVector(0, direction),
        node.inputAxes);
    node.coverageComplete = true;
    node.coverageMinimum = coverage.minimumOffset;
    node.coverageMaximum = coverage.maximumOffset;
    node.mainThreadReasons = ScrollMainThreadReasonNone;
    node.setInputPrediction(node.baseOffset);
    return node;
}

void fractionalSamplesKeepMotion(bool horizontal, double direction)
{
    // No coverage refresh occurs here: this also detects the independent
    // controller error on the preserved pre-fix production source.
    const ScrollNodeSnapshot node = readyMotionNode(horizontal, direction);
    const ScrollVector delta = horizontal
        ? ScrollVector(direction * 90, 0) : ScrollVector(0, direction * 90);
    ScrollController controller;
    require(controller.activateScene(node), "motion fixture activation failed");
    require(controller.enqueueDelta(1, delta, 1.0, true), "motion input rejected");
    require(controller.needsAnimationFrame(), "input did not start animation");
    const ScrollSample first = controller.sample(1.016);
    const double firstOffset = horizontal ? controller.visualOffset().x : controller.visualOffset().y;
    const double target = 300 + direction * 90;
    require(first.valid && (firstOffset - 300) * direction > 0
            && (target - firstOffset) * direction > 0,
        "first fractional sample did not move toward the target");
    require(controller.needsAnimationFrame(), "roundoff discarded the in-flight target");
    const ScrollSample second = controller.sample(1.032);
    const double secondOffset = horizontal ? controller.visualOffset().x : controller.visualOffset().y;
    require(second.valid && (secondOffset - firstOffset) * direction > 0
            && second.throughInputSequence == 1 && second.scrollEpoch == node.scrollEpoch,
        "second fractional sample did not continue the same input");
}

void verticalFractionalMotion()
{
    fractionalSamplesKeepMotion(false, 1);
    fractionalSamplesKeepMotion(false, -1);
}

void horizontalFractionalMotion()
{
    fractionalSamplesKeepMotion(true, 1);
    fractionalSamplesKeepMotion(true, -1);
}

void reversalKeepsTheNewSmoothTarget()
{
    const ScrollNodeSnapshot node = readyMotionNode(false, 1);
    ScrollController controller;
    require(controller.activateScene(node), "reversal fixture activation failed");
    require(controller.enqueueDelta(1, ScrollVector(0, 90), 1.0, true), "forward input rejected");
    require(controller.sample(1.016).valid && controller.needsAnimationFrame(),
        "forward motion stopped before reversal");
    require(controller.enqueueDelta(2, ScrollVector(0, -120), 1.020, true), "reverse input rejected");
    double peak = controller.visualOffset().y;
    bool reversed = false;
    for (int i = 1; i <= 6; ++i) {
        const ScrollSample sample = controller.sample(1.020 + i * 0.016);
        const double position = controller.visualOffset().y;
        require(controller.needsAnimationFrame(), "reverse target disappeared before convergence");
        if (position > peak)
            peak = position;
        if (sample.valid && position < peak) {
            reversed = true;
            require(sample.throughInputSequence == 2, "reverse sample lost its input sequence");
        }
    }
    require(reversed && controller.visualOffset().y > 270,
        "motion did not approach the new reverse target");
}

void trueCoverageBoundsStillStopMotion()
{
    for (const double direction : { -1.0, 1.0 }) {
        const ScrollNodeSnapshot node = readyMotionNode(false, direction);
        const double boundary = direction < 0
            ? node.coverageMinimum.y : node.coverageMaximum.y;
        ScrollController controller;
        require(controller.activateScene(node), "boundary fixture activation failed");
        require(controller.enqueueDelta(1, ScrollVector(0, direction * 1000), 1.0, true),
            "boundary input rejected");
        for (int i = 1; i <= 500 && controller.needsAnimationFrame(); ++i) {
            controller.sample(1.0 + i * 0.016);
            require(controller.visualOffset().y >= node.coverageMinimum.y
                    && controller.visualOffset().y <= node.coverageMaximum.y,
                "smooth motion escaped raster coverage");
        }
        require(!controller.needsAnimationFrame()
                && std::fabs(controller.visualOffset().y - boundary) < 0.000001,
            "real coverage clamp did not stop at the boundary");
        require(controller.enqueueDelta(2, ScrollVector(0, direction * 1000), 10.0, true),
            "second boundary input rejected");
        require(!controller.needsAnimationFrame() && !controller.sample(10.016).valid,
            "outward input restarted motion beyond raster coverage");
    }
}

void narrowedSceneBoundsStillClampMotion()
{
    ScrollNodeSnapshot node = readyMotionNode(false, 1);
    ScrollController controller;
    require(controller.activateScene(node), "narrowed scene fixture activation failed");
    require(controller.enqueueDelta(1, ScrollVector(0, 90), 1.0, true), "initial input rejected");
    const ScrollSample first = controller.sample(1.016);
    require(first.valid && controller.needsAnimationFrame(), "initial motion did not survive first sample");
    node.sceneVersion = 2;
    node.baseOffset = controller.visualOffset();
    node.acknowledgedSample = first.sampleSequence;
    node.acknowledgedTotal = first.consumedTotal;
    node.maximumOffset.y = 310;
    node.coverageMaximum.y = 310;
    node.setInputPrediction(node.baseOffset);
    require(controller.activateScene(node), "narrowed same-epoch scene rejected");
    for (int i = 2; i <= 500 && controller.needsAnimationFrame(); ++i) {
        controller.sample(1.0 + i * 0.016);
        require(controller.visualOffset().y <= 310, "motion escaped the new scene boundary");
    }
    require(!controller.needsAnimationFrame() && std::fabs(controller.visualOffset().y - 310) < 0.000001,
        "narrower geometry failed to stop the previous target");
}

void preparedRefreshPreservesSmoothMotionAcrossScene()
{
    ScrollNodeSnapshot node = makeNode();
    ScrollTreeSnapshot initial = makeTree(node);
    require(initial.refreshCoverage(node.key, node.scrollEpoch, coverageFor(node), true, true), "initial refresh failed");
    ScrollController controller;
    require(controller.activateScene(*initial.findNode(node.key)), "initial activation failed");
    require(controller.enqueueDelta(9, ScrollVector(0, 90), 1.0, true), "smooth input rejected");
    require(controller.needsAnimationFrame(), "smooth input did not schedule animation");
    const ScrollSample sample = controller.sample(1.016);
    std::cout << std::setprecision(17)
        << "DIAGNOSTIC: before any coverage refresh, base=" << node.baseOffset.y
        << " delta=90 sample_valid=" << sample.valid
        << " visual=" << controller.visualOffset().y
        << " consumed=" << sample.consumedTotal.y
        << " animation_active=" << controller.needsAnimationFrame() << '\n';
    require(sample.valid, "smooth input produced no first sample");
    require(controller.visualOffset().y > node.baseOffset.y
            && controller.visualOffset().y < node.baseOffset.y + 90,
        "first smooth sample did not lie between start and target");
    require(controller.needsAnimationFrame(),
        "first sample stopped in-flight motion before any coverage refresh");
    const ScrollVector before = controller.visualOffset();

    node.sceneVersion = 2;
    node.baseOffset = before;
    node.coverageMinimum = before;
    node.coverageMaximum = before;
    node.setInputPrediction(before);
    node.acknowledgedSample = sample.sampleSequence;
    node.acknowledgedTotal = sample.consumedTotal;
    ScrollTreeSnapshot next = makeTree(node);
    require(next.refreshCoverage(node.key, node.scrollEpoch, coverageFor(node), true, true), "next refresh failed");
    require(controller.activateScene(*next.findNode(node.key)), "same-epoch scene rejected");
    require(sameVector(controller.visualOffset(), before), "refresh jumped the visual offset");
    require(controller.needsAnimationFrame(), "refresh stopped the in-flight smooth motion");
    const ScrollSample continued = controller.sample(1.032);
    require(continued.valid && controller.visualOffset().y > before.y
            && continued.scrollEpoch == sample.scrollEpoch
            && continued.throughInputSequence == 9, "same-epoch motion did not continue");
}

} // namespace

int main()
{
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {
        {"prepared coverage waits for raster fence", preparedCoverageWaitsForLastRasterAction},
        {"no fence cannot promote coverage", noFenceCannotPromoteNewCoverage},
        {"no-raster frame keeps existing coverage", noRasterFrameKeepsExistingCoverage},
        {"missing or dirty coverage stays ineligible", missingOrDirtyCoverageStaysIneligible},
        {"other reasons and scene metadata survive", refreshPreservesOtherReasonsAndMetadata},
        {"stale identity and epoch are rejected", staleIdentityAndEpochCannotRefresh},
        {"empty frame activates", emptyFrameCanStillActivate},
        {"smooth motion survives same-epoch refresh", preparedRefreshPreservesSmoothMotionAcrossScene},
        {"vertical fractional samples keep both directions", verticalFractionalMotion},
        {"horizontal fractional samples keep both directions", horizontalFractionalMotion},
        {"reversal keeps the new smooth target", reversalKeepsTheNewSmoothTarget},
        {"true coverage bounds still stop motion", trueCoverageBoundsStillStopMotion},
        {"narrowed scene bounds still clamp motion", narrowedSceneBoundsStillClampMotion},
    };
    unsigned passed = 0;
    unsigned failed = 0;
    for (const Case& test : cases) {
        try {
            test.run();
            ++passed;
            std::cout << "PASS: " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << passed << " scroll coverage regressions passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
