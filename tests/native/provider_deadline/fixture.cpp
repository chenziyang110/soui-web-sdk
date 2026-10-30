#include <windows.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "input_ui_api.h"

namespace fixture {
struct Controls {
    bool owner = true, paintOwner = true, platform = true, mainThread = true;
    bool qpf = true, qpc = true, schedulerMore = false, schedulerThrows = false;
    int reentry = 0;
    LONGLONG now = 1000, frequency = 1000000;
    unsigned schedulerCalls = 0, legacyCalls = 0, visits = 0, closes = 0;
    unsigned long long receivedDeadline = 0, receivedFrequency = 0;
    std::function<void()> onScheduler;
} controls;
unsigned assertions = 0;
void require(bool value, const char* description)
{
    ++assertions;
    if (!value) throw std::runtime_error(description);
}
BOOL queryFrequency(LARGE_INTEGER* value)
{
    value->QuadPart = controls.frequency;
    return controls.qpf ? TRUE : FALSE;
}
BOOL queryCounter(LARGE_INTEGER* value)
{
    value->QuadPart = controls.now;
    return controls.qpc ? TRUE : FALSE;
}
}

#define QueryPerformanceFrequency fixture::queryFrequency
#define QueryPerformanceCounter fixture::queryCounter
#define WKE_CALL_TYPE __cdecl

namespace content {
enum class PaintCloseKind { none, view, direct, window };
struct Policy {
    PaintCloseKind pending = PaintCloseKind::none;
    bool eligible = false;
    bool ready() const { return eligible; }
};
struct PaintLifetimeState { Policy policy; };
struct PaintLifetimeRuntime {
    std::mutex mutex;
    bool closing = false;
    bool isDrained = false;
    bool drained() const { return isDrained; }
};
#include "cursor.inc"
PaintLifetimeRuntime& paintLifetimeRuntime()
{
    static PaintLifetimeRuntime runtime;
    return runtime;
}
struct CheckReEnter { static int getEnterCount() { return fixture::controls.reentry; } };
class WebThreadImpl {
public:
    bool serviceOwnerOpportunity(unsigned long long deadlineQpc = 0, unsigned long long qpcFrequency = 0)
    {
        auto& c = fixture::controls;
        ++c.schedulerCalls;
        if (!qpcFrequency) ++c.legacyCalls;
        c.receivedDeadline = deadlineQpc;
        c.receivedFrequency = qpcFrequency;
        if (c.onScheduler) c.onScheduler();
        if (c.schedulerThrows) throw std::runtime_error("explicit scheduler fake failure");
        return c.schedulerMore;
    }
};
}
namespace blink {
class Platform {
public:
    static Platform* current()
    {
        static Platform value;
        return fixture::controls.platform ? &value : nullptr;
    }
    content::WebThreadImpl* mainThread()
    {
        static content::WebThreadImpl value;
        return fixture::controls.mainThread ? &value : nullptr;
    }
};
}

namespace wke {
class CWebView;
using wkeWebView = CWebView*;
std::set<wkeWebView> g_liveWebViews;
using PaintKind = content::PaintCloseKind;
bool inputUiOwnerAvailable() { return fixture::controls.owner; }
bool paintOwner() { return fixture::controls.paintOwner; }
struct Page {
    CWebView* owner;
    unsigned retries = 0;
    std::function<void()> onRetry;
    void retryRejectedDrawWake()
    {
        ++fixture::controls.visits;
        ++retries;
        if (onRetry) onRetry();
    }
};
class CWebView {
public:
    CWebView() : page{this, 0, {}} { g_liveWebViews.insert(this); }
    ~CWebView() { g_liveWebViews.erase(this); }
    CWebView(const CWebView&) = delete;
    CWebView& operator=(const CWebView&) = delete;
    static bool serviceDeferredPaintCloses(unsigned long long deadlineQpc = 0,
        unsigned long long qpcFrequency = 0);
    static bool preparePaintShutdown();
    Page* webPage() { return &page; }
    std::shared_ptr<content::PaintLifetimeState> m_paintLifetime = std::make_shared<content::PaintLifetimeState>();
    Page page;
    std::function<void()> onClose;
};
void invokePaintClose(CWebView* target, PaintKind)
{
    ++fixture::controls.visits;
    ++fixture::controls.closes;
    const auto callback = target->onClose;
    if (callback) callback();
}
#include "sweep.inc"
}

namespace {
#include "provider.inc"
int WKE_INPUT_UI_CALL configureTrace(const wkeInputUiTraceConfig*) { return 0; }
void WKE_INPUT_UI_CALL stopTrace() { }
int WKE_INPUT_UI_CALL snapshotTrace(wkeInputUiTraceSnapshot*) { return 0; }
int WKE_INPUT_UI_CALL drainTrace(void*, unsigned, unsigned, unsigned*) { return 0; }
int WKE_INPUT_UI_CALL setMirror(void*, unsigned) { return 0; }
int WKE_INPUT_UI_CALL queryMirror(void*, unsigned*) { return 0; }
int WKE_INPUT_UI_CALL setInputContext(void*, const void*, unsigned) { return 0; }
int WKE_INPUT_UI_CALL cancelInput(void*) { return 0; }
int WKE_INPUT_UI_CALL setViewContext(void*, const void*, unsigned) { return 0; }
int WKE_INPUT_UI_CALL setVisibility(void*, unsigned) { return 0; }
int WKE_INPUT_UI_CALL retainTraceProducer() { return 0; }
void WKE_INPUT_UI_CALL releaseTraceProducer() { }
}
#include "api.inc"

namespace fixture {
using Views = std::vector<std::unique_ptr<wke::CWebView>>;
Views makeViews(unsigned count)
{
    Views views;
    for (unsigned n = 0; n < count; ++n) views.emplace_back(std::make_unique<wke::CWebView>());
    return views;
}
unsigned call(unsigned long long deadline = 0)
{
    unsigned more = 99;
    require(serviceOwnerUntil(deadline, 1000000, &more) == WKE_INPUT_UI_OK, "bounded provider status");
    require(more <= 1, "remaining is a Boolean independent of status");
    return more;
}
void validation()
{
    auto views = makeViews(1);
    unsigned more = 99;
    require(serviceOwnerUntil(0, 1000000, nullptr) == WKE_INPUT_UI_INVALID, "null remaining rejected");
    require(serviceOwnerUntil(0, 0, &more) == WKE_INPUT_UI_INVALID && more == 0, "zero frequency rejected");
    require(serviceOwnerUntil(0, 2, &more) == WKE_INPUT_UI_INVALID && more == 0, "foreign frequency rejected");
    controls.owner = false;
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_UNAVAILABLE, "non-owner unavailable");
    controls.owner = true; controls.reentry = 1;
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_BUSY, "entered Blink refused");
    controls.reentry = 0; controls.platform = false;
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_UNAVAILABLE, "missing platform unavailable");
    controls.platform = true; controls.mainThread = false;
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_UNAVAILABLE, "missing owner unavailable");
    controls.mainThread = true; controls.qpf = false;
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_UNAVAILABLE, "failed frequency pre-work");
    controls.qpf = true; controls.frequency = -1;
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_UNAVAILABLE, "negative frequency pre-work");
    controls.frequency = 1000000; controls.qpc = false;
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_UNAVAILABLE, "failed counter pre-work");
    controls.qpc = true; controls.now = -1;
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_UNAVAILABLE, "negative counter pre-work");
    require(controls.schedulerCalls == 0 && controls.visits == 0, "invalid entry does no provider work");
    controls.now = 1000;
    controls.onScheduler = [&] {
        unsigned nested = 99;
        require(serviceOwnerUntil(0, 1000000, &nested) == WKE_INPUT_UI_BUSY && nested == 0,
            "recursive service refused without legacy replay");
        controls.qpf = false;
        require(serviceOwnerUntil(0, 1000000, &nested) == WKE_INPUT_UI_BUSY && nested == 0,
            "recursive service cannot become unavailable and trigger fallback");
        controls.qpf = true;
    };
    require(call() == 0, "single-view complete");
    require(controls.schedulerCalls == 1 && controls.visits == 1, "recursive entry did not duplicate work");
}
void bothLanes()
{
    auto views = makeViews(4);
    controls.onScheduler = [] { controls.now = 9000; }; // Models an indivisible callback beyond D.
    controls.schedulerMore = true;
    require(call(1000) == 1, "scheduler and interrupted close report remaining");
    require(controls.schedulerCalls == 1 && controls.visits == 1, "close minimum survives scheduler overrun");
    require(controls.receivedDeadline == 1000 && controls.receivedFrequency == 1000000,
        "same original cutoff reaches scheduler");
    controls.schedulerMore = false;
    require(call(1000) == 1 && call(1000) == 1 && call(1000) == 0, "finite close continuation ends");
    require(controls.visits == 4, "one visit at a time at expired deadline");
    for (const auto& view : views) require(view->page.retries == 1, "cursor visits each live view once");
}
void finiteSweep()
{
    auto views = makeViews(40);
    unsigned calls = 0, more = 1;
    while (more && calls < 50) { more = call(); ++calls; }
    require(calls == 32 && controls.visits == 32 && more == 0, "one finite32 sweep cannot self-renew");
    require(controls.legacyCalls == 0, "bounded continuation never replays legacy");
    require(call() == 1 && controls.visits == 33, "new external opportunity may start another finite sweep");
}
void waitingReader()
{
    auto views = makeViews(5);
    for (auto& view : views) view->m_paintLifetime->policy.pending = content::PaintCloseKind::view;
    unsigned calls = 0, more = 1;
    while (more && calls < 8) { more = call(); ++calls; }
    require(calls == 5 && controls.closes == 0 && more == 0, "reader wait does not become a wake reason");
    for (auto& view : views) view->m_paintLifetime->policy.eligible = true;
    require(call() == 1 && controls.closes == 1, "external completion can resume ready close");
}
void deleteReentry()
{
    auto views = makeViews(3);
    auto* first = *wke::g_liveWebViews.begin();
    first->m_paintLifetime->policy.pending = content::PaintCloseKind::view;
    first->m_paintLifetime->policy.eligible = true;
    first->onClose = [&] {
        require(!wke::CWebView::serviceDeferredPaintCloses(0, 1000000), "nested sweep is fenced");
        for (auto& view : views) if (view.get() == first) { view.reset(); break; }
    };
    require(call() == 1 && controls.closes == 1, "delete first view under lock-external close");
    require(call() == 1 && call() == 0, "fresh lookup resumes after deleting cursor key");
    require(controls.visits == 3 && wke::g_liveWebViews.size() == 2, "finite visits survive deletion and reentry");
    for (const auto& view : views) if (view) require(view->page.retries == 1, "remaining live view once");
}
void legacyShutdown()
{
    auto views = makeViews(4);
    require(call() == 1 && controls.visits == 1, "bounded sweep interrupted first");
    require(!wke::CWebView::serviceDeferredPaintCloses(), "legacy scan has no bounded continuation");
    require(controls.visits == 5, "legacy still performs its full original scan");
    controls.qpc = false;
    require(!wke::CWebView::preparePaintShutdown(), "shutdown returns actual runtime drain state");
    require(controls.visits == 9 && content::paintLifetimeRuntime().closing, "shutdown uses unrestricted scan without QPC");
    content::paintLifetimeRuntime().isDrained = true;
    require(wke::CWebView::preparePaintShutdown(), "drained shutdown result preserved");
    require(controls.visits == 13, "shutdown full scan remains available after prior cutoff");
    controls.qpc = true;
    serviceOwner();
    require(controls.legacyCalls == 1 && controls.visits == 17, "legacy provider keeps old callback and scan");
}
void partialFailure()
{
    auto views = makeViews(2);
    unsigned more = 99;
    controls.schedulerThrows = true;
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_REJECTED && more == 0,
        "partial scheduler failure is not unavailable or remaining");
    require(controls.schedulerCalls == 1 && controls.visits == 0 && controls.legacyCalls == 0,
        "partial scheduler failure not replayed");
    controls.schedulerThrows = false;
    auto* first = *wke::g_liveWebViews.begin();
    first->page.onRetry = [] { throw std::runtime_error("explicit retry fake failure"); };
    require(serviceOwnerUntil(0, 1000000, &more) == WKE_INPUT_UI_REJECTED && more == 0,
        "partial close failure is rejected");
    require(controls.visits == 1 && controls.legacyCalls == 0, "close failure not replayed");
    first->page.onRetry = {};
    require(call() == 0 && controls.visits == 2, "service guard and remaining sweep survive unwinding");
}
void clockLoss()
{
    auto views = makeViews(3);
    controls.onScheduler = [] { controls.qpc = false; };
    require(call(9000) == 1 && controls.visits == 1, "lost clock after work conservatively cuts sweep");
    controls.qpc = true;
    require(call(9000) == 1 && controls.visits == 2, "lost clock continuation retains original finite sweep");
    controls.qpc = true;
    require(call(9000) == 0 && controls.visits == 3, "clock loss cannot self-renew idle sweep");
    require(controls.legacyCalls == 0, "clock loss after scheduler never replays legacy");
}
void abi()
{
    const auto* api = wkeGetInputUiApi(WKE_INPUT_UI_ABI_VERSION);
    require(api && api->structSize == sizeof(wkeInputUiApi), "actual table returns complete size");
    require(wkeGetInputUiApi(WKE_INPUT_UI_ABI_VERSION + 1) == nullptr, "unsupported ABI rejected");
    require((api->capabilities & WKE_INPUT_UI_CAP_OWNER_DEADLINE) != 0, "actual table advertises bounded capability");
    require(api->serviceOwnerOpportunity == &serviceOwner && api->serviceOwnerOpportunityUntil == &serviceOwnerUntil,
        "legacy and tail pointers are actual source entries");
    require(offsetof(wkeInputUiApi, traceConfigure) == 16, "legacy prefix fixed");
    require(offsetof(wkeInputUiApi, serviceOwnerOpportunity) == 16 + 4 * sizeof(void*), "legacy owner offset fixed");
    require(offsetof(wkeInputUiApi, serviceOwnerOpportunityUntil) == 16 + 13 * sizeof(void*), "tail after all original fields");
    require(sizeof(wkeInputUiApi) == 16 + 14 * sizeof(void*), "tail only grows by one pointer");
    unsigned more = 99;
    require(api->serviceOwnerOpportunityUntil(0, 1000000, &more) == WKE_INPUT_UI_OK && more == 0,
        "actual table tail uses correct calling convention");
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try {
        const std::string name = argv[1];
        if (name == "validation") fixture::validation();
        else if (name == "both-lanes") fixture::bothLanes();
        else if (name == "finite-sweep") fixture::finiteSweep();
        else if (name == "waiting-reader") fixture::waitingReader();
        else if (name == "delete-reentry") fixture::deleteReentry();
        else if (name == "legacy-shutdown") fixture::legacyShutdown();
        else if (name == "partial-failure") fixture::partialFailure();
        else if (name == "clock-loss") fixture::clockLoss();
        else if (name == "abi") fixture::abi();
        else return 2;
        std::cout << "PASS " << name << " assertions=" << fixture::assertions << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL assertions=" << fixture::assertions << " " << error.what() << '\n';
        return 1;
    }
}
