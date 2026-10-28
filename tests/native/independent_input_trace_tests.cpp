#include "cc/trees/IndependentInputTrace.h"
#include "cc/trees/ScrollTreeSnapshot.h"
#include <cstdio>
#include <utility>

static int failures = 0;
static void require(bool value) { if (!value) { ++failures; std::fputs("independent trace requirement failed\n", stderr); } }
static cc::IndependentInputTrace::Event input(uint64_t sequence)
{
    cc::IndependentInputTrace::Event result;
    result.view_id = result.view_generation = result.document_generation = result.geometry_generation = 1;
    result.input_first = result.input_last = sequence;
    return result;
}
static cc::ScrollNodeSnapshot node()
{
    cc::ScrollNodeSnapshot result;
    result.key = cc::ScrollNodeKey(1, 1, 7, 1); result.sceneVersion = result.scrollEpoch = 1;
    result.baseOffset = result.minimumOffset = cc::ScrollVector(0, 0);
    result.maximumOffset = result.coverageMaximum = cc::ScrollVector(0, 1000);
    result.coverageMinimum = cc::ScrollVector(0, 0); result.viewportClip = cc::ScrollRect(0, 0, 100, 100);
    result.inputAxes = cc::ScrollInputAxisVertical; result.coverageComplete = true;
    result.mainThreadReasons = cc::ScrollMainThreadReasonNone; result.setInputPrediction(result.baseOffset);
    return result;
}
int main()
{
    const auto target = node();
    cc::ScrollController controller;
    require(controller.activateScene(target));
    cc::IndependentInputTrace coverage;
    auto packet = input(1000);
    require(controller.enqueueDelta(77, cc::ScrollVector(0, 10), 100, false));
    require(coverage.append(packet, target.key, target.scrollEpoch, 77, 100, false));
    auto frame = input(0); frame.frame_id = 9; frame.action_first = 4; frame.action_last = 6; frame.related_id = 1; frame.task_id = 444;
    cc::IndependentInputTrace::Event emitted;
    require(!coverage.takeForPaint(frame, &emitted)); // Receipt acceptance alone is insufficient.
    const auto sample = controller.sample(100);
    require(sample.valid && sample.throughInputSequence == 77);
    coverage.adopt(sample.key, sample.scrollEpoch, sample.throughInputSequence, 100, false);
    require(!coverage.takeForPaint(frame, &emitted)); // No actual layer visual application.
    coverage.adopt(sample.key, sample.scrollEpoch, sample.throughInputSequence, 100, true);
    auto unrelated = frame; ++unrelated.geometry_generation;
    require(!coverage.takeForPaint(unrelated, &emitted));
    require(coverage.takeForPaint(frame, &emitted));
    require(emitted.input_first == 1000 && emitted.input_last == 1000 && emitted.value == 77);
    require(emitted.task_id == 0); // Receipt/old main task are never invented as compositor task IDs.
    require(emitted.frame_id == 9 && emitted.action_first == 4 && emitted.action_last == 6 && emitted.related_id == 1);
    require(emitted.flags == content::input_frame_trace::coverage_valid
        && emitted.outcome == content::input_frame_trace::terminal::none);

    cc::ScrollController reversed;
    require(reversed.activateScene(target));
    require(reversed.enqueueDelta(1, cc::ScrollVector(0, 10), 100, false));
    require(reversed.enqueueDelta(2, cc::ScrollVector(0, -10), 100, false));
    require(coverage.append(input(1001), target.key, 1, 1, 100, false));
    require(coverage.append(input(1002), target.key, 1, 2, 100, false));
    const auto zeroVisual = reversed.sample(100);
    require(!zeroVisual.valid);
    coverage.adopt(target.key, 1, 2, 100, zeroVisual.valid);
    require(!coverage.takeForPaint(frame, &emitted)); // Net reversal never becomes a visible claim.
    require(coverage.takeNode(target.key, 1, &emitted) && coverage.takeNode(target.key, 1, &emitted));
    require(!coverage.size());

    require(coverage.append(input(1003), target.key, 1, 3, 100, true));
    coverage.adopt(target.key, 1, 3, 100, true);
    require(!coverage.takeForPaint(frame, &emitted)); // Smooth target not integrated at equal timestamp.
    coverage.adopt(target.key, 2, 3, 100.01, true);
    require(!coverage.takeForPaint(frame, &emitted)); // Different scroll epoch is not authority.
    coverage.adopt(target.key, 1, 3, 100.01, true);
    require(coverage.takeForPaint(frame, &emitted));
    for (size_t index = 0; index < cc::IndependentInputTrace::maximumEntries; ++index)
        require(coverage.append(input(2000 + index), target.key, 1, 100 + index, 100, false));
    require(!coverage.append(input(9999), target.key, 1, 999, 100, false));
    require(controller.enqueueDelta(999, cc::ScrollVector(0, 1), 100.01, false)); // Diagnostic quota cannot reject product input.
    auto nextDocument = frame; ++nextDocument.document_generation;
    for (size_t index = 0; index < cc::IndependentInputTrace::maximumEntries; ++index)
        require(coverage.takeStale(nextDocument, &emitted));
    require(!coverage.size());

    cc::SceneCoordinator scenes;
    cc::SceneTransaction first;
    first.newVersion = 1; first.resourcesReady = true; first.scrollTree.setSceneVersion(1);
    first.scrollTree.setInputIdentity(frame);
    require(scenes.submit(std::move(first)) && scenes.activateReady());
    require(scenes.activeScene().scrollTree.matchesInputIdentity(frame));
    auto changed = frame; ++changed.geometry_generation;
    require(!scenes.activeScene().scrollTree.matchesInputIdentity(changed));
    cc::SceneTransaction pending;
    pending.baseVersion = 1; pending.newVersion = 2; pending.firstActionId = 10; pending.lastActionId = 11;
    pending.scrollTree.setSceneVersion(2); pending.scrollTree.setInputIdentity(changed);
    require(scenes.submit(std::move(pending)) && !scenes.activateReady());
    require(!scenes.activeScene().scrollTree.matchesInputIdentity(changed)); // Applied resize alone cannot authorize an old scene.
    require(scenes.markResourcesReadyThroughAction(11) && scenes.activateReady());
    require(scenes.activeScene().scrollTree.matchesInputIdentity(changed));
    auto noDetail = changed; noDetail.qpc = 0; noDetail.flags = 0;
    require(scenes.activeScene().scrollTree.matchesInputIdentity(noDetail)); // No recorder configuration/availability dependency.
    cc::ScrollTreeSnapshot unknown;
    require(!unknown.matchesInputIdentity(changed));
    return failures ? 1 : 0;
}
