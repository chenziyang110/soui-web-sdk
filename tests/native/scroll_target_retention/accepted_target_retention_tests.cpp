#include "cc/trees/ScrollTreeSnapshot.h"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace cc;
constexpr double kEpsilon = 0.00001;
constexpr double kExpectedEndpoint = 350.0;
constexpr int kBeforeExpansionSamples = 120;
constexpr int kFinalSamples = 240;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void quoted(const std::string& text)
{
    std::cout << '"';
    for (const char c : text) {
        if (c == '"' || c == '\\')
            std::cout << '\\' << c;
        else if (c == '\n')
            std::cout << "\\n";
        else if (c == '\r')
            std::cout << "\\r";
        else
            std::cout << c;
    }
    std::cout << '"';
}

ScrollNodeSnapshot readyNode(uint64_t scene, const ScrollBarrier* acknowledged)
{
    ScrollNodeSnapshot node;
    node.key = ScrollNodeKey(1, 2, 31, 4);
    node.sceneVersion = scene;
    node.scrollEpoch = 5;
    node.baseOffset = acknowledged ? acknowledged->visualOffset : ScrollVector(0, 0);
    node.acknowledgedSample = acknowledged ? acknowledged->sampleSequence : 0;
    node.acknowledgedTotal = acknowledged ? acknowledged->consumedTotal : ScrollVector(0, 0);
    node.minimumOffset = ScrollVector(0, 0);
    node.maximumOffset = ScrollVector(0, 1000);
    node.coverageMinimum = node.baseOffset;
    node.coverageMaximum = node.baseOffset;
    node.inputAxes = ScrollInputAxisVertical;
    node.viewportClip = ScrollRect(20, 70, 560, 300);
    node.mainThreadReasons = ScrollMainThreadReasonIncompleteCoverage;
    node.setInputPrediction(node.baseOffset);

    ScrollTreeSnapshot tree;
    tree.setSceneVersion(scene);
    tree.addNode(node);
    tree.addHitTestRegion(ScrollHitTestRegion(node.viewportClip, node.key));
    const ScrollCoverageWindow coverage = computeScrollCoverageWindow(
        node.baseOffset, node.minimumOffset, node.maximumOffset,
        ScrollVector(560, 300), ScrollVector(0, 1), node.inputAxes);
    require(tree.refreshCoverage(node.key, node.scrollEpoch, coverage, true, true),
        "prepared coverage could not be published");
    const ScrollNodeSnapshot* prepared = tree.findNode(node.key);
    require(prepared && prepared->compositorEligible(), "fixture node is not eligible");
    require(tree.scrollTargetAt(200, 200) != nullptr, "fixture hit region is not eligible");
    return *prepared;
}

struct Scenario {
    const char* name;
    ScrollController controller;
    ScrollNodeSnapshot inputNode;
    int acceptedCount = 0;
    bool allSamplesCovered = true;

    explicit Scenario(const char* value) : name(value), inputNode(readyNode(1, nullptr))
    {
        require(controller.activateScene(inputNode), "initial scene activation failed");
    }

    void record(const char* step, double now, int inputAccepted = -1,
        int controllerAccepted = -1, int sampleValid = -1, double delta = 0)
    {
        const ScrollNodeSnapshot& node = controller.activeNode();
        const ScrollBarrier barrier = controller.captureBarrier();
        const ScrollVector visual = controller.visualOffset();
        const bool covered = std::isfinite(visual.y)
            && visual.y >= node.coverageMinimum.y - kEpsilon
            && visual.y <= node.coverageMaximum.y + kEpsilon
            && visual.y >= node.minimumOffset.y - kEpsilon
            && visual.y <= node.maximumOffset.y + kEpsilon;
        allSamplesCovered = allSamplesCovered && covered;
        std::cout << "{\"kind\":\"ScrollTargetRetention\",\"event\":\"state\",\"scenario\":";
        quoted(name);
        std::cout << ",\"step\":";
        quoted(step);
        std::cout << ",\"time\":" << now << ",\"delta_y\":" << delta
            << ",\"input_accepted\":";
        if (inputAccepted < 0) std::cout << "null";
        else std::cout << (inputAccepted ? "true" : "false");
        std::cout << ",\"controller_accepted\":";
        if (controllerAccepted < 0) std::cout << "null";
        else std::cout << (controllerAccepted ? "true" : "false");
        std::cout << ",\"sample_valid\":";
        if (sampleValid < 0) std::cout << "null";
        else std::cout << (sampleValid ? "true" : "false");
        std::cout << ",\"accepted_count\":" << acceptedCount
            << ",\"offset_y\":" << visual.y
            << ",\"input_prediction_y\":" << inputNode.inputPrediction.y
            << ",\"coverage_min_y\":" << node.coverageMinimum.y
            << ",\"coverage_max_y\":" << node.coverageMaximum.y
            << ",\"minimum_y\":" << node.minimumOffset.y
            << ",\"maximum_y\":" << node.maximumOffset.y
            << ",\"base_y\":" << node.baseOffset.y
            << ",\"ack_total_y\":" << node.acknowledgedTotal.y
            << ",\"ack_sample\":" << node.acknowledgedSample
            << ",\"consumed_total_y\":" << barrier.consumedTotal.y
            << ",\"sample_sequence\":" << barrier.sampleSequence
            << ",\"through_input_sequence\":" << barrier.throughInputSequence
            << ",\"scene\":" << node.sceneVersion
            << ",\"epoch\":" << node.scrollEpoch
            << ",\"active\":" << (controller.needsAnimationFrame() ? "true" : "false")
            << ",\"covered\":" << (covered ? "true" : "false") << "}\n";
        require(covered, "visual offset escaped prepared coverage or actual content bounds");
    }

    void submit(uint64_t sequence, double delta, double now, bool smooth)
    {
        // These are the production admission and controller APIs. No copied
        // clamp, damping, coverage computation or fallback implementation.
        const bool admitted = inputNode.advanceInputPrediction(ScrollVector(0, delta));
        const bool enqueued = admitted
            && controller.enqueueDelta(sequence, ScrollVector(0, delta), now, smooth);
        if (admitted && enqueued)
            ++acceptedCount;
        record(smooth ? "submit-smooth" : "submit-snap", now, admitted, enqueued, -1, delta);
        require(admitted && enqueued, "baseline accepted-input fixture precondition changed");
    }

    void sample(double now, const char* step)
    {
        const ScrollSample sampled = controller.sample(now);
        record(step, now, -1, -1, sampled.valid);
    }

    void publishNextScene(double now)
    {
        const ScrollBarrier acknowledged = controller.captureBarrier();
        require(acknowledged.valid, "cannot acknowledge an invalid controller state");
        const ScrollVector previousPrediction = inputNode.inputPrediction;
        const ScrollNodeSnapshot next = readyNode(inputNode.sceneVersion + 1, &acknowledged);
        require(next.scrollEpoch == inputNode.scrollEpoch, "fixture changed the scroll epoch");
        require(controller.activateScene(next), "same-epoch scene activation failed");
        inputNode = next;
        // Match current LayerTreeHost's retained input prediction on scene swap.
        inputNode.setInputPrediction(previousPrediction);
        record("publish-expanded-coverage", now);
    }
};

struct Result {
    bool passed = false;
    bool completed = false;
    bool coverageSafe = false;
    int accepted = 0;
    double endpoint = 0;
};

Result runScenario(bool expandBeforeSecondInput)
{
    const char* name = expandBeforeSecondInput ? "expand-before-second-input" : "expand-after-motion";
    Result result;
    try {
        Scenario scenario(name);
        scenario.record("initial", 1.0);
        require(scenario.inputNode.coverageMinimum.y == 0
                && scenario.inputNode.coverageMaximum.y == 300,
            "initial production coverage differs from the defined fixture");
        scenario.submit(1, 250, 1.0, false);
        scenario.sample(1.0, "first-snap-sample");
        require(std::fabs(scenario.controller.visualOffset().y - 250) < kEpsilon,
            "first snap did not reach 250");
        if (expandBeforeSecondInput)
            scenario.publishNextScene(1.005);
        scenario.submit(2, 100, 1.010, true);

        // Fixed virtual-time schedules, no wall clock or sleeping. We do not
        // require motion to become inactive before publication: a future
        // implementation may park a retained target at the coverage boundary.
        if (!expandBeforeSecondInput) {
            for (int i = 1; i <= kBeforeExpansionSamples; ++i)
                scenario.sample(1.010 + i * 0.016, "sample-before-expansion");
            scenario.publishNextScene(1.010 + kBeforeExpansionSamples * 0.016);
        }
        const double finalStart = expandBeforeSecondInput
            ? 1.010 : 1.010 + kBeforeExpansionSamples * 0.016;
        for (int i = 1; i <= kFinalSamples; ++i)
            scenario.sample(finalStart + i * 0.016, "sample-after-expansion");

        result.completed = true;
        result.coverageSafe = scenario.allSamplesCovered;
        result.accepted = scenario.acceptedCount;
        result.endpoint = scenario.controller.visualOffset().y;
        require(result.accepted == 2, "fixture did not accept both inputs");
        require(result.coverageSafe, "a sampled offset escaped coverage");
        require(!scenario.controller.needsAnimationFrame(), "motion did not settle in virtual-time bound");
        require(std::fabs(result.endpoint - kExpectedEndpoint) < kEpsilon,
            "accepted wheel displacement was not retained after coverage became available");
        result.passed = true;
    } catch (const std::exception& error) {
        std::cout << "{\"kind\":\"ScrollTargetRetention\",\"event\":\"assertion-failed\",\"scenario\":";
        quoted(name);
        std::cout << ",\"message\":";
        quoted(error.what());
        std::cout << "}\n";
    }
    std::cout << "{\"kind\":\"ScrollTargetRetention\",\"event\":\"scenario-result\",\"scenario\":";
    quoted(name);
    std::cout << ",\"completed\":" << (result.completed ? "true" : "false")
        << ",\"passed\":" << (result.passed ? "true" : "false")
        << ",\"accepted\":" << result.accepted
        << ",\"coverage_safe\":" << (result.coverageSafe ? "true" : "false")
        << ",\"endpoint_y\":" << result.endpoint
        << ",\"expected_endpoint_y\":" << kExpectedEndpoint << "}\n";
    return result;
}
} // namespace

int main()
{
    std::cout << std::setprecision(17);
    std::cout << "{\"kind\":\"ScrollTargetRetention\",\"event\":\"start\",\"architecture\":\""
        << (sizeof(void*) == 8 ? "x64" : "x86")
        << "\",\"scope\":\"production-controller-and-snapshot-only\","
        << "\"current_source_red_expected\":true,\"red_does_not_invert_exit_code\":true}\n";
    const Result early = runScenario(true);
    const Result late = runScenario(false);
    const bool sameEndpoint = early.completed && late.completed
        && std::fabs(early.endpoint - late.endpoint) < kEpsilon;
    const bool passed = early.passed && late.passed && sameEndpoint;
    std::cout << "{\"kind\":\"ScrollTargetRetention\",\"event\":\"summary\","
        << "\"scenarios\":2,\"passed_scenarios\":" << (int(early.passed) + int(late.passed))
        << ",\"same_endpoint\":" << (sameEndpoint ? "true" : "false")
        << ",\"passed\":" << (passed ? "true" : "false")
        << ",\"exit_code\":" << (passed ? 0 : 1) << "}\n";
    return passed ? 0 : 1;
}

