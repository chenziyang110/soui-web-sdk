// Execute the bound production namespace bodies; only observation boundaries are faked.
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <sys/timeb.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fixture {
enum class Event { utc, mktime, frequency, qpc, tick, begin, end };
struct Probe {
    bool available = true;
    LONGLONG frequency = 1000000;
    LONGLONG qpc = 1000000;
    DWORD tick = 1000;
    std::int64_t utcMs = 1000000;
    unsigned precisionDepth = 0;
    std::vector<Event> events;
};
Probe* active = nullptr;
void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}
Probe& record(Event event)
{
    require(active != nullptr, "observation shim called outside a sample");
    active->events.push_back(event);
    return *active;
}
} // namespace fixture

BOOL WINAPI fixtureQueryPerformanceFrequency(LARGE_INTEGER* frequency)
{
    auto& probe = fixture::record(fixture::Event::frequency);
    frequency->QuadPart = probe.available ? probe.frequency : 0;
    return probe.available ? TRUE : FALSE;
}
BOOL WINAPI fixtureQueryPerformanceCounter(LARGE_INTEGER* counter)
{
    counter->QuadPart = fixture::record(fixture::Event::qpc).qpc;
    return TRUE;
}
DWORD WINAPI fixtureGetTickCount()
{
    return fixture::record(fixture::Event::tick).tick;
}
VOID WINAPI fixtureGetSystemTime(LPSYSTEMTIME systemTime)
{
    auto& probe = fixture::record(fixture::Event::utc);
    *systemTime = {};
    systemTime->wYear = 2023;
    systemTime->wMonth = 1;
    systemTime->wDay = 2;
    systemTime->wDayOfWeek = 1;
    systemTime->wMilliseconds = static_cast<WORD>(probe.utcMs % 1000);
}
time_t __cdecl fixtureMktime(struct tm* value)
{
    auto& probe = fixture::record(fixture::Event::mktime);
    // Inspect only initialized fields. Existing tm_isdst/timezone behavior is out of scope.
    fixture::require(value->tm_year == 123 && value->tm_mon == 0 && value->tm_mday == 2 &&
        value->tm_wday == 1 && value->tm_hour == 0 && value->tm_min == 0 && value->tm_sec == 0,
        "lowResUTCTime did not populate the observed SYSTEMTIME fields");
    return static_cast<time_t>(probe.utcMs / 1000);
}
MMRESULT WINAPI fixtureTimeBeginPeriod(UINT period)
{
    auto& probe = fixture::record(fixture::Event::begin);
    fixture::require(period == 1 && probe.precisionDepth == 0, "unbalanced timeBeginPeriod(1)");
    ++probe.precisionDepth;
    return TIMERR_NOERROR;
}
MMRESULT WINAPI fixtureTimeEndPeriod(UINT period)
{
    auto& probe = fixture::record(fixture::Event::end);
    fixture::require(period == 1 && probe.precisionDepth == 1, "unbalanced timeEndPeriod(1)");
    --probe.precisionDepth;
    return TIMERR_NOERROR;
}

#define COMPILER(name) 0
#define QueryPerformanceFrequency fixtureQueryPerformanceFrequency
#define QueryPerformanceCounter fixtureQueryPerformanceCounter
#define GetTickCount fixtureGetTickCount
#define GetSystemTime fixtureGetSystemTime
#ifdef mktime
#undef mktime
#endif
#define mktime fixtureMktime
#define timeBeginPeriod fixtureTimeBeginPeriod
#define timeEndPeriod fixtureTimeEndPeriod
#if defined(__clang__)
// Keep the byte-bound legacy LARGE_INTEGER = { 0 } initializers intact.
// Restrict the two observed aggregate-style diagnostics to these imports.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
namespace original {
#include "original.utf8.inc"
}
namespace current {
#include "current.utf8.inc"
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#undef timeEndPeriod
#undef timeBeginPeriod
#undef mktime
#undef GetSystemTime
#undef GetTickCount
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency
#undef COMPILER

namespace fixture {
struct Sample {
    double value;
    std::vector<Event> events;
};
Sample call(Probe& probe, double (*function)())
{
    require(active == nullptr && probe.precisionDepth == 0, "sample began with unfinished observation");
    probe.events.clear();
    active = &probe;
    const double value = function();
    active = nullptr;
    require(probe.precisionDepth == 0, "sample left timer precision raised");
    require(std::isfinite(value), "production clock returned a non-finite result");
    return { value, probe.events };
}
unsigned count(const Sample& sample, Event event)
{
    unsigned result = 0;
    for (const auto observed : sample.events)
        result += observed == event ? 1u : 0u;
    return result;
}
void events(const Sample& sample, std::initializer_list<Event> expected, const char* context)
{
    require(sample.events == std::vector<Event>(expected), std::string(context) + ": observable call order differs");
}
void same(const Sample& before, const Sample& after, const char* context)
{
    require(before.value == after.value, std::string(context) + ": current return differs from pinned production baseline");
}
void input(Probe& before, Probe& after, LONGLONG qpc, DWORD tick, std::int64_t utcMs)
{
    before.qpc = after.qpc = qpc;
    before.tick = after.tick = tick;
    before.utcMs = after.utcMs = utcMs;
}
void steady()
{
    Probe before, after;
    input(before, after, 50000000, 50000, 1700000000123);
    const auto firstBefore = call(before, original::currentTimeImpl);
    const auto firstAfter = call(after, current::currentTimeImpl);
    same(firstBefore, firstAfter, "first sync");
    require(firstBefore.value == 1700000000.123, "first sync did not use the bounded UTC anchor in seconds");
    events(firstBefore, { Event::utc, Event::mktime, Event::frequency, Event::qpc, Event::tick,
        Event::begin, Event::utc, Event::mktime, Event::end }, "original first sync");
    unsigned originalSteadyReads = 0;
    unsigned currentSteadyReads = 0;
    unsigned currentSteadyMktime = 0;
    std::vector<Sample> currentSteadySamples;
    const LONGLONG qpcValues[] = { 50010000, 50020000, 50090000, 50250000 };
    const double expected[] = { 1700000000.133, 1700000000.143, 1700000000.213, 1700000000.373 };
    for (unsigned index = 0; index < 4; ++index) {
        // Wildly different UTC input proves steady returns continue from the QPC anchor.
        input(before, after, qpcValues[index], static_cast<DWORD>(qpcValues[index] / 1000),
            1800000000000 + static_cast<std::int64_t>(index) * 777);
        const auto oldSample = call(before, original::currentTimeImpl);
        const auto newSample = call(after, current::currentTimeImpl);
        same(oldSample, newSample, "steady QPC");
        require(oldSample.value == expected[index], "steady QPC returned an unexpected anchor offset");
        events(oldSample, { Event::utc, Event::mktime, Event::qpc, Event::tick }, "original steady QPC");
        require(count(newSample, Event::begin) == 0 && count(newSample, Event::end) == 0,
            "steady QPC repeated the precision window");
        originalSteadyReads += count(oldSample, Event::utc);
        currentSteadyReads += count(newSample, Event::utc);
        currentSteadyMktime += count(newSample, Event::mktime);
        currentSteadySamples.push_back(newSample);
    }
    require(originalSteadyReads == 4, "pinned baseline did not expose its redundant steady UTC reads");
    // Keep this first candidate work-count assertion: unchanged production must fail here in RED.
    require(currentSteadyReads == 0, "candidate stable-QPC UTC calls: expected 0, got " + std::to_string(currentSteadyReads));
    require(currentSteadyMktime == 0, "candidate steady QPC must not call mktime");
    for (const auto& sample : currentSteadySamples)
        events(sample, { Event::qpc, Event::tick }, "candidate steady QPC");
    require(count(firstAfter, Event::utc) == 1 && count(firstAfter, Event::mktime) == 1,
        "candidate first sync must read UTC once (pinned baseline reads twice)");
    events(firstAfter, { Event::frequency, Event::qpc, Event::tick,
        Event::begin, Event::utc, Event::mktime, Event::end }, "candidate first sync");
    require(count(firstBefore, Event::utc) == 2, "pinned first sync must retain both original UTC reads");
}
void fallback()
{
    Probe before, after;
    before.available = after.available = false;
    for (unsigned index = 0; index < 3; ++index) {
        input(before, after, 0, 0, 1700000000123 + static_cast<std::int64_t>(index) * 345);
        const auto oldSample = call(before, original::currentTimeImpl);
        const auto newSample = call(after, current::currentTimeImpl);
        same(oldSample, newSample, "QPC unavailable");
        require(newSample.value == static_cast<double>(before.utcMs) / 1000.0,
            "QPC fallback did not return one UTC sample in seconds");
        if (index == 0) {
            events(oldSample, { Event::utc, Event::mktime, Event::frequency }, "original first fallback");
            events(newSample, { Event::frequency, Event::utc, Event::mktime }, "candidate first fallback");
        } else {
            events(oldSample, { Event::utc, Event::mktime }, "original cached fallback");
            events(newSample, { Event::utc, Event::mktime }, "candidate cached fallback");
        }
    }
}
void backwards()
{
    Probe before, after;
    input(before, after, 10000000, 10000, 1000000);
    auto previous = call(before, original::currentTimeImpl);
    same(previous, call(after, current::currentTimeImpl), "backwards first sync");
    const LONGLONG qpcValues[] = { 5000000, 4999000 };
    for (unsigned index = 0; index < 2; ++index) {
        input(before, after, qpcValues[index], 10001 + index, 2000000);
        const auto oldSample = call(before, original::currentTimeImpl);
        const auto newSample = call(after, current::currentTimeImpl);
        same(oldSample, newSample, "backwards guard");
        require(newSample.value > previous.value &&
            std::fabs((newSample.value - previous.value) - 0.0000001) < 0.000000000001,
            "active backwards guard did not preserve its 0.0001ms increment beyond two seconds");
        require(count(newSample, Event::begin) == 0 && count(newSample, Event::end) == 0,
            "active backwards guard unexpectedly resynchronized UTC");
        previous = newSample;
    }
}
void warmLegacy(Probe& before, Probe& after)
{
    same(call(before, original::currentTimeImpl_unuse), call(after, current::currentTimeImpl_unuse), "legacy warmup");
    require(original::s_syncedTime && current::s_syncedTime, "legacy warmup did not set the legacy sync flag");
    same(call(before, original::currentTimeImpl), call(after, current::currentTimeImpl), "active warmup after legacy");
}
void tickWrap()
{
    Probe before, after;
    input(before, after, 1000000000, 0xfffffff0u, 1000000);
    warmLegacy(before, after);
    input(before, after, 1000032000, 0x10u, 2000000);
    const auto oldSample = call(before, original::currentTimeImpl);
    const auto newSample = call(after, current::currentTimeImpl);
    same(oldSample, newSample, "GetTickCount wrap");
    require(newSample.value == 1000.032, "tick wrap changed the QPC return interval");
    require(original::s_syncedTime && current::s_syncedTime, "32ms tick wrap incorrectly cleared the legacy sync flag");
    require(count(newSample, Event::begin) == 0 && count(newSample, Event::end) == 0,
        "tick wrap unexpectedly repeated active UTC sync");
}
void drift()
{
    Probe before, after;
    warmLegacy(before, after);
    input(before, after, 1001000, 1601, 2000000);
    const auto oldSample = call(before, original::currentTimeImpl);
    const auto newSample = call(after, current::currentTimeImpl);
    same(oldSample, newSample, "600ms QPC/tick drift");
    require(newSample.value == 1000.001, "active drift changed the original QPC anchor");
    require(!original::s_syncedTime && !current::s_syncedTime,
        "more than 500ms drift did not clear the legacy sync flag");
    require(count(newSample, Event::begin) == 0 && count(newSample, Event::end) == 0,
        "legacy drift flag incorrectly resynchronized the active function");
    input(before, after, 1002000, 1602, 2000000);
    const auto oldLegacy = call(before, original::currentTimeImpl_unuse);
    const auto newLegacy = call(after, current::currentTimeImpl_unuse);
    same(oldLegacy, newLegacy, "legacy resync after drift");
    events(newLegacy, { Event::utc, Event::mktime, Event::qpc, Event::tick,
        Event::begin, Event::utc, Event::mktime, Event::end }, "unchanged legacy drift resync");
    require(newLegacy.value == 2000.0 && original::s_syncedTime && current::s_syncedTime,
        "legacy function did not consume its own cleared sync flag");
}
} // namespace fixture

int main(int argc, char** argv)
{
    try {
        fixture::require(argc == 2, "usage: current_time_hot_path_contract --steady|--fallback|--backwards|--tick-wrap|--drift");
        const std::string scenario = argv[1];
        if (scenario == "--steady") fixture::steady();
        else if (scenario == "--fallback") fixture::fallback();
        else if (scenario == "--backwards") fixture::backwards();
        else if (scenario == "--tick-wrap") fixture::tickWrap();
        else if (scenario == "--drift") fixture::drift();
        else throw std::runtime_error("unknown scenario: " + scenario);
        std::cout << "PASS " << scenario << ": exact production bodies; deterministic observation shims\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
