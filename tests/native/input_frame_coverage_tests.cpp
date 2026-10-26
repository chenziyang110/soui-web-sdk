#include "cc/trees/InputFrameCoverage.h"
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
    return failures ? 1 : 0;
}
