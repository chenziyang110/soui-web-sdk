#include "cc/trees/InputFrameCoverage.h"
#include "cc/trees/ViewTraceIdentity.h"
#include <cstdio>

static int failures = 0;
static void require(bool value) { if (!value) { ++failures; std::fputs("input frame coverage requirement failed\n", stderr); } }
static cc::InputFrameCoverage::Event range(uint64_t first, uint64_t last, uint64_t view = 1)
{
    cc::InputFrameCoverage::Event value;
    value.view_id = view; value.view_generation = 1;
    value.document_generation = 1; value.geometry_generation = 1;
    value.input_first = first; value.input_last = last;
    return value;
}
int main()
{
    cc::InputFrameCoverage coverage;
    require(coverage.append(range(1, 2)));
    require(coverage.append(range(4, 5)) && coverage.size() == 2);
    require(coverage.append(range(3, 3)) && coverage.size() == 1);
    require(coverage.at(0).input_first == 1 && coverage.at(0).input_last == 5);
    require(coverage.append(range(6, 6, 2)) && coverage.size() == 2);
    auto nextGeometry = range(6, 6); nextGeometry.geometry_generation = 2;
    require(coverage.append(nextGeometry) && coverage.size() == 3);
    coverage.clear();
    for (size_t i = 0; i < cc::InputFrameCoverage::maximumSegments; ++i)
        require(coverage.append(range(i * 2 + 1, i * 2 + 1)));
    require(!coverage.append(range(1000, 1000)));
    require(coverage.size() == cc::InputFrameCoverage::maximumSegments);
    cc::InputDispatchTraceStack stack;
    auto clear = range(1, 1); clear.input_first = clear.input_last = 0;
    cc::InputFrameCoverage::Event processed;
    require(stack.enter(range(1, 1)));
    require(stack.enter(range(2, 2)));
    require(stack.leave(clear, &processed) && processed.input_first == 2);
    require(stack.leave(clear, &processed) && processed.input_first == 1);
    require(!stack.leave(clear, &processed));
    const auto otherView = range(11, 11, 2);
    const auto outer = range(10, 10, 1);
    auto otherClear = otherView; otherClear.input_first = otherClear.input_last = 0;
    cc::InputDispatchTraceStack nestedOtherView;
    require(nestedOtherView.enter(otherView, &outer));
    cc::InputFrameCoverage::Event restore;
    bool hasRestore = false;
    require(nestedOtherView.leave(otherClear, &processed, &restore, &hasRestore));
    require(hasRestore && restore.view_id == 1 && restore.input_first == 10);
    for (size_t i = 0; i < cc::InputDispatchTraceStack::maximumDepth; ++i)
        require(stack.enter(range(i + 1, i + 1)));
    require(!stack.enter(range(100, 100)));
    require(!stack.leave(clear, &processed)); // Overflow clear does not pop parent.
    require(stack.leave(clear, &processed) && processed.input_first == 16);

    cc::ViewTraceIdentity identity;
    auto base = range(0, 0, 37);
    base.frame_id = 99; base.action_first = base.action_last = 42;
    base.task_id = 7; base.qpc = 123; base.flags = content::input_frame_trace::coverage_valid;
    require(identity.set(base));
    const auto firstAppFrame = identity.frameContext({});
    require(firstAppFrame.view_id == 37 && firstAppFrame.document_generation == 1);
    require(!firstAppFrame.input_first && !firstAppFrame.input_last
        && !firstAppFrame.frame_id && !firstAppFrame.action_first && !firstAppFrame.task_id
        && !firstAppFrame.flags && !firstAppFrame.qpc);
    const auto leasedOldFrame = firstAppFrame; // A captured descriptor owns its metadata.
    require(!identity.set(range(1, 1, 37)));
    auto invalid = base; invalid.geometry_generation = 0;
    require(!identity.set(invalid));
    auto foreign = base; foreign.view_id = 38;
    require(!identity.set(foreign));
    auto currentInput = range(101, 101, 37); currentInput.task_id = 9;
    auto causalFrame = identity.frameContext(currentInput);
    require(causalFrame.input_first == 101 && causalFrame.task_id == 9);
    require(!identity.frameContext(range(1, 1, 38)).input_first);
    cc::InputFrameCoverage pending, unpresented, retired;
    require(pending.append(currentInput) && unpresented.append(currentInput));
    auto nextDocument = base; nextDocument.document_generation = 2;
    require(identity.set(nextDocument));
    require(!identity.set(base)); // A late document notification cannot roll back identity.
    size_t lost = 0;
    identity.retainCurrent(pending, retired, lost);
    identity.retainCurrent(unpresented, retired, lost);
    require(!pending.size() && !unpresented.size() && retired.size() == 1 && !lost);
    require(retired.at(0).input_first == 101 && !identity.accepts(currentInput));
    require(leasedOldFrame.document_generation == 1 && identity.value().document_generation == 2);
    require(!identity.frameContext(currentInput).input_first);
    auto nextInput = currentInput; nextInput.document_generation = 2;
    require(pending.append(nextInput));
    identity.retainCurrent(pending, retired, lost);
    require(pending.size() == 1); // Idempotent confirmed metadata preserves current coverage.
    auto resized = nextDocument; resized.geometry_generation = 2;
    require(identity.set(resized) && !identity.set(nextDocument));
    identity.retainCurrent(pending, retired, lost);
    require(!pending.size() && retired.size() == 2);
    return failures ? 1 : 0;
}
