#include "cc/trees/ScrollTreeSnapshot.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace cc;
constexpr double epsilon = 0.00001;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool near(double a, double b) { return std::fabs(a - b) < epsilon; }
void quote(const char* text) {
    std::cout << '"';
    for (; *text; ++text) {
        if (*text == '"' || *text == '\\') std::cout << '\\';
        std::cout << *text;
    }
    std::cout << '"';
}
ScrollNodeSnapshot node(ScrollVector base = ScrollVector(0, 0),
    ScrollVector maximum = ScrollVector(0, 1000),
    uint32_t axes = ScrollInputAxisVertical)
{
    ScrollNodeSnapshot n;
    n.key = ScrollNodeKey(1, 2, 31, 4);
    n.sceneVersion = 1;
    n.scrollEpoch = 5;
    n.baseOffset = base;
    n.minimumOffset = ScrollVector(0, 0);
    n.maximumOffset = maximum;
    n.viewportClip = ScrollRect(20, 70, 300, 300);
    n.inputAxes = axes;
    n.mainThreadReasons = ScrollMainThreadReasonNone;
    n.coverageComplete = true;
    const ScrollCoverageWindow c = computeScrollCoverageWindow(base,
        n.minimumOffset, maximum, ScrollVector(300, 300),
        ScrollVector(1, 1), axes);
    n.coverageMinimum = c.minimumOffset;
    n.coverageMaximum = c.maximumOffset;
    n.setInputPrediction(base);
    return n;
}

struct Fixture {
    ScrollController controller;
    ScrollNodeSnapshot input;
    uint64_t sequence = 0;
    double time = 1.0;
    explicit Fixture(const ScrollNodeSnapshot& n = node()) : input(n) {
        require(controller.activateScene(n), "fixture scene activation failed");
        covered();
    }
    void covered() const {
        const ScrollNodeSnapshot& n = controller.activeNode();
        const ScrollVector p = controller.visualOffset();
        require(std::isfinite(p.x) && std::isfinite(p.y), "nonfinite visual position");
        require(p.x >= n.coverageMinimum.x - epsilon && p.x <= n.coverageMaximum.x + epsilon
            && p.y >= n.coverageMinimum.y - epsilon && p.y <= n.coverageMaximum.y + epsilon,
            "visual escaped prepared coverage");
        require(p.x >= n.minimumOffset.x - epsilon && p.x <= n.maximumOffset.x + epsilon
            && p.y >= n.minimumOffset.y - epsilon && p.y <= n.maximumOffset.y + epsilon,
            "visual escaped actual content bounds");
    }
    void push(ScrollVector delta, bool smooth) {
        require(input.advanceInputPrediction(delta), "input admission unexpectedly refused");
        require(controller.enqueueDelta(++sequence, delta, time, smooth), "controller rejected admitted input");
        covered();
    }
    void frame(double step = 0.016) {
        time += step;
        controller.sample(time);
        covered();
    }
    void sampleNow() { controller.sample(time); covered(); }
    void settle(int frames = 240) { for (int i = 0; i < frames; ++i) frame(); }
    void publish() {
        const ScrollBarrier b = controller.captureBarrier();
        ScrollNodeSnapshot next = controller.activeNode();
        ++next.sceneVersion;
        next.baseOffset = b.visualOffset;
        next.acknowledgedSample = b.sampleSequence;
        next.acknowledgedTotal = b.consumedTotal;
        const ScrollCoverageWindow c = computeScrollCoverageWindow(next.baseOffset,
            next.minimumOffset, next.maximumOffset,
            ScrollVector(next.viewportClip.width, next.viewportClip.height),
            b.consumedTotal, next.inputAxes);
        next.coverageMinimum = c.minimumOffset;
        next.coverageMaximum = c.maximumOffset;
        next.setInputPrediction(next.baseOffset);
        const ScrollVector oldPrediction = input.inputPrediction;
        require(controller.activateScene(next), "same-epoch ready scene refused");
        input = next;
        input.setInputPrediction(oldPrediction);
        covered();
    }
    void endpoint(double y, double x = 0) {
        const ScrollVector p = controller.visualOffset();
        require(near(p.x, x) && near(p.y, y), "retained target endpoint differs");
        require(!controller.needsAnimationFrame(), "settled or parked controller still requests animation");
    }
    void trace(const char* step) const {
        const ScrollNodeSnapshot& n = controller.activeNode();
        const ScrollBarrier b = controller.captureBarrier();
        std::cout << "{\"event\":\"state\",\"step\":"; quote(step);
        std::cout << ",\"time\":" << time
            << ",\"offset_x\":" << b.visualOffset.x << ",\"offset_y\":" << b.visualOffset.y
            << ",\"prediction_x\":" << input.inputPrediction.x << ",\"prediction_y\":" << input.inputPrediction.y
            << ",\"coverage_min_x\":" << n.coverageMinimum.x << ",\"coverage_max_x\":" << n.coverageMaximum.x
            << ",\"coverage_min_y\":" << n.coverageMinimum.y << ",\"coverage_max_y\":" << n.coverageMaximum.y
            << ",\"base_y\":" << n.baseOffset.y << ",\"ack_total_y\":" << n.acknowledgedTotal.y
            << ",\"through_input\":" << b.throughInputSequence
            << ",\"active\":" << (controller.needsAnimationFrame() ? "true" : "false") << "}\n";
    }
};

void prepareParked(Fixture& f) {
    f.push(ScrollVector(0, 250), false);
    f.sampleNow();
    f.time += 0.010;
    f.push(ScrollVector(0, 100), true);
    require(near(f.input.inputPrediction.y, 350), "prediction lost target beyond coverage");
    f.settle();
    f.endpoint(300);
    f.trace("parked-target-350-at-300");
}

void parkedReadyResume() {
    Fixture f;
    prepareParked(f);
    f.publish();
    require(f.controller.needsAnimationFrame(), "ready coverage did not resume retained target");
    const double before = f.controller.visualOffset().y;
    f.frame(100.0);
    require(near(f.controller.visualOffset().y, before), "parked idle time was integrated on resume");
    require(f.controller.needsAnimationFrame(), "reanchoring discarded retained motion");
    f.frame();
    require(f.controller.visualOffset().y > before, "second resumed sample did not move");
    f.settle();
    f.endpoint(350);
    f.trace("resumed-target-350");
}

void reverseParkedTarget() {
    Fixture f;
    prepareParked(f);
    f.push(ScrollVector(0, -100), true);
    require(near(f.input.inputPrediction.y, 250), "reverse delta used clamped prediction");
    f.settle();
    f.endpoint(250);
}

void negativeTargetFromNonzeroBase() {
    ScrollNodeSnapshot n = node(ScrollVector(0,700));
    const ScrollCoverageWindow coverage = computeScrollCoverageWindow(n.baseOffset,
        n.minimumOffset,n.maximumOffset,ScrollVector(300,300),
        ScrollVector(0,-1),n.inputAxes);
    n.coverageMinimum = coverage.minimumOffset;
    n.coverageMaximum = coverage.maximumOffset;
    Fixture f(n);
    f.push(ScrollVector(0,-200),false);
    f.sampleNow();
    f.endpoint(500);
    f.push(ScrollVector(0,-150),true);
    f.settle();
    f.endpoint(475);
    require(near(f.input.inputPrediction.y,350), "negative logical target clipped to coverage");
    require(near(f.controller.captureBarrier().consumedTotal.y,-225),
        "negative cumulative displacement differs from absolute visual offset");
    f.trace("negative-target-parked-at-475");
    f.publish();
    f.settle();
    f.endpoint(350);
    require(near(f.controller.captureBarrier().consumedTotal.y,-350),
        "negative cumulative target was not retained");
}

void snapAddsToParkedIntent() {
    Fixture f;
    prepareParked(f);
    f.push(ScrollVector(0, 50), false);
    f.sampleNow();
    require(near(f.input.inputPrediction.y, 400), "snap delta lost previous logical target");
    f.endpoint(300);
    f.publish();
    f.settle();
    f.endpoint(400);
}

void snapAddsToInFlightIntent() {
    Fixture f;
    f.push(ScrollVector(0, 200), true);
    f.frame();
    require(f.controller.visualOffset().y > 0 && f.controller.visualOffset().y < 200,
        "smooth fixture did not remain in flight");
    f.push(ScrollVector(0, 50), false);
    f.sampleNow();
    f.endpoint(250);
}

void dualAxisRetention() {
    Fixture f(node(ScrollVector(0,0), ScrollVector(1000,1000),
        ScrollInputAxisHorizontal | ScrollInputAxisVertical));
    f.push(ScrollVector(100,100), false);
    f.sampleNow();
    f.time += 0.010;
    f.push(ScrollVector(100,80), true);
    f.settle();
    f.endpoint(120,120);
    require(near(f.input.inputPrediction.x,200) && near(f.input.inputPrediction.y,180),
        "dual-axis prediction lost accepted displacement");
    f.publish();
    require(f.controller.needsAnimationFrame(), "dual-axis coverage did not resume motion");
    f.settle();
    f.endpoint(180,200);
}

void snapOnOtherAxisPreservesInFlightMotion() {
    Fixture f(node(ScrollVector(0,0), ScrollVector(1000,1000),
        ScrollInputAxisHorizontal | ScrollInputAxisVertical));
    f.push(ScrollVector(100,0),true);
    f.frame();
    const double before = f.controller.visualOffset().x;
    require(before > 0 && before < 100, "horizontal fixture did not remain in flight");
    f.push(ScrollVector(0,40),false);
    f.sampleNow();
    require(near(f.controller.visualOffset().x,before),
        "vertical-only snap moved the unrelated horizontal axis");
    require(near(f.controller.visualOffset().y,40), "vertical snap did not realize its input");
    require(f.controller.needsAnimationFrame(), "vertical snap cancelled horizontal motion");
    f.settle();
    f.endpoint(40,100);
}

void trueBoundsClipOnlyActualContent() {
    Fixture f(node(ScrollVector(0,0), ScrollVector(0,400)));
    f.push(ScrollVector(0,600), true);
    require(near(f.input.inputPrediction.y,400), "logical target not clipped to content bound");
    f.settle();
    f.endpoint(300);
    f.publish();
    f.settle();
    f.endpoint(400);
    f.publish();
    require(!f.input.advanceInputPrediction(ScrollVector(0,100)),
        "outward input accepted when committed and logical positions are both at true maximum");
}

void newEpochUsesNewBoundsBeforeReset() {
    Fixture f;
    prepareParked(f);
    f.publish(); // Old cumulative bounds are now above zero.
    ScrollNodeSnapshot next = node(ScrollVector(0,700));
    next.sceneVersion = f.controller.activeNode().sceneVersion + 1;
    next.scrollEpoch = f.controller.activeNode().scrollEpoch + 1;
    next.acknowledgedSample = 0;
    next.acknowledgedTotal = ScrollVector(0,0);
    require(f.controller.activateScene(next), "new epoch refused");
    f.input = next;
    f.covered();
    f.settle();
    f.endpoint(700);
}

void barrierCancelsPendingIntent() {
    Fixture f;
    prepareParked(f);
    const ScrollBarrier barrier = f.controller.captureBarrier();
    ScrollSample synchronized;
    require(f.controller.synchronizeBarrier(barrier,&synchronized) && synchronized.valid,
        "current barrier synchronization failed");
    f.input.setInputPrediction(barrier.visualOffset);
    require(!f.controller.needsAnimationFrame(), "barrier retained old pending intent");
    f.publish();
    f.settle();
    f.endpoint(300);
}

void revokeCancelsPendingIntent() {
    Fixture f;
    prepareParked(f);
    f.controller.revokeEligibility(ScrollMainThreadReasonWheelHandler);
    require(!f.controller.compositorEligible() && !f.controller.needsAnimationFrame(),
        "revoked controller remains active");
    ScrollNodeSnapshot next = f.controller.activeNode();
    const ScrollBarrier b = f.controller.captureBarrier();
    ++next.sceneVersion;
    next.mainThreadReasons = ScrollMainThreadReasonNone;
    next.baseOffset = b.visualOffset;
    next.acknowledgedTotal = b.consumedTotal;
    next.acknowledgedSample = b.sampleSequence;
    const ScrollCoverageWindow c = computeScrollCoverageWindow(next.baseOffset,
        next.minimumOffset,next.maximumOffset,ScrollVector(300,300),
        ScrollVector(0,1),next.inputAxes);
    next.coverageMinimum = c.minimumOffset;
    next.coverageMaximum = c.maximumOffset;
    next.setInputPrediction(next.baseOffset);
    require(f.controller.activateScene(next), "eligible same-epoch scene refused");
    f.input = next;
    f.settle();
    f.endpoint(300);
}

void zeroViewportRefusesInput() {
    for (int dimension = 0; dimension < 2; ++dimension) {
        ScrollNodeSnapshot n = node();
        if (dimension == 0) n.viewportClip.height = 0;
        else n.viewportClip.width = 0;
        require(!n.canConsumeDelta(ScrollVector(0,100))
                && !n.advanceInputPrediction(ScrollVector(0,100)),
            "zero viewport admitted wheel motion");
        Fixture f;
        f.push(ScrollVector(0,100),true);
        n.sceneVersion = 2;
        require(f.controller.activateScene(n), "zero viewport snapshot was invalid");
        require(!f.controller.compositorEligible()
                && !f.controller.enqueueDelta(2,ScrollVector(0,100),f.time,true),
            "previously active controller admitted zero-viewport input");
    }
}

void noDirectionalCoverageRefusesInput() {
    ScrollNodeSnapshot n = node(ScrollVector(0,100));
    n.coverageMinimum.y = 100;
    n.coverageMaximum.y = 100;
    require(!n.advanceInputPrediction(ScrollVector(0,100))
        && !n.advanceInputPrediction(ScrollVector(0,-100)), "zero-margin coverage admitted motion");
    n.coverageMinimum.y = 0;
    require(!n.advanceInputPrediction(ScrollVector(0,100)), "backward-only coverage admitted forward motion");
    n.coverageMinimum.y = 100;
    n.coverageMaximum.y = 200;
    require(!n.advanceInputPrediction(ScrollVector(0,-100)), "forward-only coverage admitted backward motion");
}

void reversalAdmissionUsesCandidateNotRawSign() {
    ScrollNodeSnapshot n = node();
    n.setInputPrediction(ScrollVector(0,350));
    require(near(n.inputPrediction.y,350), "logical prediction was clipped to coverage");
    require(n.advanceInputPrediction(ScrollVector(0,-100))
            && near(n.inputPrediction.y,250),
        "reverse input toward a drawable candidate was refused because of raw delta sign");
}

void saturatedTargetNoOpKeepsAcceptedTail() {
    Fixture f;
    f.push(ScrollVector(0,1000),false);
    f.sampleNow();
    f.endpoint(300);
    f.publish();
    require(near(f.input.inputPrediction.y,1000), "scene swap lost saturated target");
    const uint64_t before = f.controller.captureBarrier().throughInputSequence;
    f.push(ScrollVector(0,100),true);
    require(near(f.input.inputPrediction.y,1000), "saturated target changed");
    require(f.controller.captureBarrier().throughInputSequence == before + 1,
        "saturated no-op did not advance accepted sequence");
    for (int i=0; i<5; ++i) { f.settle(); f.publish(); }
    f.settle();
    f.endpoint(1000);
    require(!f.input.advanceInputPrediction(ScrollVector(0,100)),
        "fully committed true-boundary input did not retain ordinary rejection");
}

void cancelToBaseWithoutDirectionalMargin() {
    ScrollNodeSnapshot n = node(ScrollVector(0,100));
    n.coverageMinimum = n.baseOffset;
    n.coverageMaximum = n.baseOffset;
    n.setInputPrediction(ScrollVector(0,350));
    require(n.advanceInputPrediction(ScrollVector(0,-250))
            && near(n.inputPrediction.y,100),
        "cancellation back to base was refused without directional raster margin");
}

void sameEpochRealBoundsShrink() {
    Fixture f;
    f.push(ScrollVector(0,250),false);
    f.sampleNow();
    f.push(ScrollVector(0,100),true);
    ScrollNodeSnapshot next = f.controller.activeNode();
    const ScrollBarrier b = f.controller.captureBarrier();
    ++next.sceneVersion;
    next.baseOffset = b.visualOffset;
    next.acknowledgedTotal = b.consumedTotal;
    next.acknowledgedSample = b.sampleSequence;
    next.maximumOffset.y = 280;
    next.coverageMaximum.y = 280;
    next.setInputPrediction(ScrollVector(0,350));
    require(f.controller.activateScene(next), "smaller real content range refused");
    f.input = next;
    f.settle();
    f.endpoint(280);
}

void readyRefreshRetainsLogicalPrediction() {
    ScrollNodeSnapshot n = node(ScrollVector(0,100));
    n.setInputPrediction(ScrollVector(0,350));
    n.coverageComplete = false;
    n.mainThreadReasons = ScrollMainThreadReasonIncompleteCoverage;
    n.coverageMinimum = n.baseOffset;
    n.coverageMaximum = n.baseOffset;
    ScrollTreeSnapshot tree;
    tree.setSceneVersion(n.sceneVersion);
    tree.addNode(n);
    const ScrollCoverageWindow coverage = computeScrollCoverageWindow(n.baseOffset,
        n.minimumOffset,n.maximumOffset,ScrollVector(300,300),ScrollVector(0,1),n.inputAxes);
    require(tree.refreshCoverage(n.key,n.scrollEpoch,coverage,true,true), "ready refresh failed");
    const ScrollNodeSnapshot* refreshed = tree.findNode(n.key);
    require(refreshed && near(refreshed->inputPrediction.y,350),
        "ready coverage refresh clipped logical prediction");
}

void uninterruptedAcksDoNotReanchorAnimation() {
    Fixture f;
    f.push(ScrollVector(0,100),true);
    f.frame();
    for (int i=0; i<20; ++i) {
        const double before = f.controller.visualOffset().y;
        f.publish();
        f.frame();
        require(f.controller.visualOffset().y > before,
            "same-epoch acknowledgement stalled unparked animation");
    }
    f.settle();
    f.endpoint(100);
}

void shrinkingCoverageDoesNotReplayConsumedOffset() {
    Fixture f;
    f.push(ScrollVector(0,250),false);
    f.sampleNow();
    f.push(ScrollVector(0,100),true);
    ScrollNodeSnapshot next = f.controller.activeNode();
    ++next.sceneVersion;
    next.coverageMaximum.y = 200;
    next.setInputPrediction(f.input.inputPrediction);
    require(f.controller.activateScene(next), "shrinking prepared coverage refused");
    f.input = next;
    f.covered();
    require(near(f.controller.visualOffset().y,200), "shrinking coverage was painted outside its edge");
    f.trace("coverage-shrank-to-200");
    ++next.sceneVersion;
    next.coverageMaximum.y = 400;
    require(f.controller.activateScene(next), "expanded prepared coverage refused");
    f.input = next;
    f.covered();
    f.trace("coverage-expanded-to-400");
    require(near(f.controller.visualOffset().y,200),
        "coverage expansion replayed consumed offset before an animation sample");
    double previous = f.controller.visualOffset().y;
    for (int i=0; i<240; ++i) {
        f.frame();
        const double current = f.controller.visualOffset().y;
        require(current >= previous - epsilon, "coverage expansion made retained motion reverse");
        previous = current;
    }
    f.endpoint(350);
}
} // namespace

int main() {
    std::cout << std::setprecision(17);
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {
#if defined(SOUI_SCROLL_CONTRACTION_DIAGNOSTIC)
        {"coverage-shrink-expand-no-replay",shrinkingCoverageDoesNotReplayConsumedOffset}
#else
        {"park-ready-resume",parkedReadyResume},
        {"reverse-parked-target",reverseParkedTarget},
        {"negative-target-nonzero-base",negativeTargetFromNonzeroBase},
        {"snap-adds-to-parked-intent",snapAddsToParkedIntent},
        {"snap-adds-to-inflight-intent",snapAddsToInFlightIntent},
        {"dual-axis-retention",dualAxisRetention},
        {"other-axis-snap-keeps-motion",snapOnOtherAxisPreservesInFlightMotion},
        {"real-content-bounds",trueBoundsClipOnlyActualContent},
        {"new-epoch-new-bounds",newEpochUsesNewBoundsBeforeReset},
        {"barrier-cancels-intent",barrierCancelsPendingIntent},
        {"revoke-cancels-intent",revokeCancelsPendingIntent},
        {"zero-viewport-refusal",zeroViewportRefusesInput},
        {"directional-coverage-refusal",noDirectionalCoverageRefusesInput},
        {"reversal-candidate-admission",reversalAdmissionUsesCandidateNotRawSign},
        {"saturated-target-noop",saturatedTargetNoOpKeepsAcceptedTail},
        {"cancel-to-base-no-margin",cancelToBaseWithoutDirectionalMargin},
        {"same-epoch-real-bounds-shrink",sameEpochRealBoundsShrink},
        {"ready-refresh-prediction",readyRefreshRetainsLogicalPrediction},
        {"uninterrupted-ack-motion",uninterruptedAcksDoNotReanchorAnimation}
#endif
    };
    int passed=0, failed=0;
    std::cout << "{\"event\":\"start\",\"suite\":\""
#if defined(SOUI_SCROLL_CONTRACTION_DIAGNOSTIC)
        << "scroll-coverage-contraction-diagnostic"
#else
        << "scroll-target-boundaries"
#endif
        << "\",\"architecture\":\""
        << (sizeof(void*) == 8 ? "x64" : "x86") << "\"}\n";
    for (const Case& test : cases) {
        try {
            test.run();
            ++passed;
            std::cout << "{\"event\":\"case-result\",\"name\":"; quote(test.name);
            std::cout << ",\"passed\":true}\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "{\"event\":\"case-result\",\"name\":"; quote(test.name);
            std::cout << ",\"passed\":false,\"message\":"; quote(e.what()); std::cout << "}\n";
        }
    }
    std::cout << "{\"event\":\"summary\",\"passed\":" << passed << ",\"failed\":" << failed
        << ",\"exit_code\":" << (failed ? 1 : 0) << "}\n";
    return failed ? 1 : 0;
}

