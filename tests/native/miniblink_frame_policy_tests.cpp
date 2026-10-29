#include "../../third_party/sources/miniblink/cc/trees/FrameSchedulingPolicy.h"
#include "../../third_party/sources/miniblink/cc/trees/DirtyRegionPolicy.h"
#include "../../third_party/sources/miniblink/cc/raster/CompletionWaiters.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
struct Rect { double x, y, width, height; };
template<typename T> struct RejectAllocator {
    using value_type = T;
    RejectAllocator() = default;
    template<typename U> RejectAllocator(const RejectAllocator<U>&) noexcept { }
    T* allocate(size_t) { throw std::bad_alloc(); }
    void deallocate(T*, size_t) noexcept { }
};
template<typename T, typename U> bool operator==(const RejectAllocator<T>&, const RejectAllocator<U>&) { return true; }
template<typename T, typename U> bool operator!=(const RejectAllocator<T>&, const RejectAllocator<U>&) { return false; }
double area(const Rect& value) { return value.width * value.height; }
double unionArea(const Rect& a, const Rect& b)
{
    return (std::max(a.x + a.width, b.x + b.width) - std::min(a.x, b.x))
        * (std::max(a.y + a.height, b.y + b.height) - std::min(a.y, b.y));
}
void cadence()
{
    for (const unsigned rate : { 60U, 120U, 144U, 165U, 240U, 360U, 1000U }) {
        const double interval = 1.0 / rate;
        double deadline = 100.0 + interval;
        for (unsigned frame = 0; frame < 10000; ++frame) {
            const double lateWake = deadline + interval * 0.1;
            deadline = cc::frame_scheduling::advanceDeadline(deadline, lateWake, interval);
            require(deadline > lateWake, "next target must be future, not a catch-up frame");
        }
        require(std::fabs(deadline - (100.0 + 10001 * interval)) < 0.000001,
            "fractional cadence must not accumulate integer-ms drift");
        require(cc::frame_scheduling::advanceDeadline(200, 199, interval) == 200,
            "early wake must preserve the future deadline");
        const double resumed = cc::frame_scheduling::advanceDeadline(100, 200, interval);
        require(resumed > 200 && resumed <= 200 + interval * 1.001,
            "idle recovery must skip the missed backlog");
    }
    require(cc::frame_scheduling::advanceDeadline(10, 10, 0) == 0,
        "disabled pacing must clear the deadline");
    require(cc::frame_scheduling::remainingDelayMilliseconds(1.0001, 1) == 1,
        "sub-ms remaining delay must not become immediate retry");
    require(cc::frame_scheduling::remainingDelayMilliseconds(1, 2) == 0,
        "past deadline must not produce a negative delay");
}
void dirtySelection()
{
    std::array<Rect, 8> regions{};
    for (size_t i = 0; i < regions.size(); ++i)
        regions[i] = { static_cast<double>(i * 100), 0, 10, 10 };
    auto result = cc::dirty_region::leastCostMerge(regions.data(), regions.size(),
        Rect{ 11, 0, 10, 10 }, area, unionArea);
    require(result.first == 0 && result.second == regions.size(),
        "incoming region must participate before old distant regions are merged");
    result = cc::dirty_region::leastCostMerge(regions.data(), regions.size(),
        regions[0], area, unionArea);
    require(result.first == 0 && result.second == regions.size(),
        "contained incoming region must not enlarge unrelated coverage");
    regions[1] = { 11, 0, 10, 10 };
    result = cc::dirty_region::leastCostMerge(regions.data(), regions.size(),
        Rect{ 10000, 0, 10, 10 }, area, unionArea);
    require(result.first == 0 && result.second == 1,
        "old pair remains eligible when it has the least additional area");
}

void completionRegistration()
{
    struct Host {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false;
        bool release = false;
        unsigned calls = 0;
        void notifyRecordResourcesChanged()
        {
            std::unique_lock<std::mutex> lock(mutex);
            ++calls;
            entered = true;
            changed.notify_all();
            changed.wait(lock, [this] { return release; });
        }
    } host;
    cc::CompletionWaiters<Host, RejectAllocator<Host*> > exhausted;
    require(!exhausted.add(&host), "allocation rejection must be reported without registering a dangling waiter");
    exhausted.notify();
    cc::CompletionWaiters<Host> waiters;
    waiters.add(&host);
    waiters.add(&host);
    auto notification = std::async(std::launch::async, [&] { waiters.notify(); });
    bool entered = false;
    {
        std::unique_lock<std::mutex> lock(host.mutex);
        entered = host.changed.wait_for(lock, std::chrono::seconds(2), [&] { return host.entered; });
    }
    std::promise<void> retirementStarted;
    auto retirement = std::async(std::launch::async, [&] {
        retirementStarted.set_value();
        waiters.remove(&host);
    });
    retirementStarted.get_future().wait();
    const bool waitedForBorrow = retirement.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
    {
        std::lock_guard<std::mutex> lock(host.mutex);
        host.release = true;
        host.changed.notify_all();
    }
    notification.get();
    retirement.get();
    require(entered && waitedForBorrow, "retirement must join an in-progress borrowed host notification");
    waiters.notify();
    require(host.calls == 1, "duplicate registration is coalesced and a retired host is never notified");
}
}

int main()
{
    try {
        cadence();
        dirtySelection();
        completionRegistration();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
