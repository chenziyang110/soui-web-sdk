#include <soui/host/frame_rate_policy.h>
#include "cc/trees/FrameDeadlinePolicy.h"

#include <cstdio>
#include <initializer_list>

static int failures = 0;
static void require(bool value) { if (!value) { ++failures; std::fputs("frame phase requirement failed\n", stderr); } }
#include <cmath>
#include <cstdint>

// Source-only: deterministic clock oracles for the validation handoff.
int main()
{
    for (const unsigned rate : {60U, 120U, 144U, 165U, 240U}) {
        soui::sdk::fractional_frame_phase phase;
        require(phase.reset(100, 10000000, rate));
        for (std::int64_t ordinal = 1; ordinal <= 10000; ++ordinal) {
            const auto expected = 100 + (ordinal * 10000000 + rate - 1) / rate;
            require(phase.deadline() == expected);
            require(!phase.advance_past(expected - 1));
            phase.advance_past(expected);
        }
        phase.advance_past(1000000000);
        require(phase.deadline() > 1000000000);
        const double interval = 1.0 / rate;
        const double deadline = cc::nextFrameDeadline(10.0, 20.0, interval);
        require(deadline > 20.0 && deadline <= 20.0 + interval * 1.001);
        require(cc::nextFrameDeadline(0, 20.0, interval) == 20.0 + interval);
    }
    soui::sdk::fractional_frame_phase invalid;
    require(!invalid.reset(0, 1, 60));
    require(!invalid.reset(-1, 10000000, 60));
    return failures ? 1 : 0;
}
