// Actual byte-extracted broker algorithms; real Win32 timers, HWND and HMENU.
// This leaf uses opaque view identity and qualifies no Blink/SDK/COM object.
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static std::atomic<bool> failNextAllocation{false};
void* operator new(std::size_t n) {
    if (failNextAllocation.exchange(false)) throw std::bad_alloc();
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#define WKE_CALL_TYPE __cdecl
// Explicit paint-lifetime dependency fake; no Blink view or native paint runs.
struct TestPaintView {
    bool paint_sealed = false, paint_drained = true;
    void quiescePaintCallbacks() { paint_sealed = true; }
    bool paintCallbacksDrained() const { return paint_drained; }
};
using wkeWebView = TestPaintView*;
#include "wke_cursor_broker.h"
static bool wkeIsInit = true;
namespace wke {
DWORD wkeThreadId = 0;
#include "actual-control-and-action-support.inc"
#include "actual-lookup-and-drain.inc"
#include "actual-timer-proc.inc"
#include "current-request-close.inc"
#include "actual-inspect-drain.inc"
#include "actual-boundary.inc"
#include "actual-safe-close.inc"
} // production anonymous namespace
#include "actual-closing-and-failure.inc"
#include "actual-post-ui.inc"
#include "actual-native-resource-kind.inc"
#include "actual-native-resources.inc"
#include "actual-com-ledger.inc"
#include "actual-close-registration.inc"
} // namespace wke

namespace fixture {
using namespace wke;
void expect(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Environment : TestPaintView {
    std::shared_ptr<CursorBrokerControl> control;
    HMENU menu = nullptr;
    unsigned actions = 0, bodies = 0, completions = 0, cancelled = 0;
    unsigned menuHolds = 0, menuDestroys = 0, menuReleases = 0, comAdds = 0, comReleases = 0;
    bool oomObservedWithRealHeldMenu = false, actionLeaseObservedWithRealHeldMenu = false;
    bool nativeAdmissionFaultObserved = false, comLedgerObserved = false;
    bool ownComLedger = false;
    Environment() : control(std::make_shared<CursorBrokerControl>()) {
        expect(s_endpoints.empty() && s_uiTasks.empty() && s_closeContinuations.empty()
            && s_cancelContinuations.empty(), "actual queues must initially be empty");
        wkeThreadId = GetCurrentThreadId();
        s_maxTasks = WKE_CURSOR_BROKER_MAX_NATIVE_TASKS;
        s_maxEndpoints = WKE_CURSOR_BROKER_MAX_ENDPOINTS;
        control->token = {1, 2};
        control->identity = {sizeof(wkeCursorBrokerIdentity), 0, 3, 4, 5};
        control->view = this; control->publishSequence = 40;
        control->ownerThread = GetCurrentThreadId();
        control->window = CreateWindowExW(0, L"STATIC", L"Actual owned-menu close leaf", 0,
            0, 0, 1, 1, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        expect(control->window && actualOwner(*control), "real owner HWND required");
        control->hooks.structSize = sizeof(control->hooks);
        control->hooks.userData = this;
        control->hooks.onNativeComplete = [](void* opaque, const wkeCursorBrokerNativeReceipt* r, unsigned cancel) {
            auto& e = *static_cast<Environment*>(opaque);
            expect(GetCurrentThreadId() == e.control->ownerThread && r && r->taskSequence
                && equalToken(r->endpoint, e.control->token), "real completion owner and receipt");
            ++e.completions; e.cancelled += cancel;
        };
        s_endpoints.push_back(control);
    }
    ~Environment() {
        failNextAllocation = false;
        if (menu || ownComLedger || control->nativeResources || control->comReferences
            || control->queued || control->running || control->engineCallbacks
            || !s_uiTasks.empty() || !s_closeContinuations.empty() || !s_cancelContinuations.empty())
            std::terminate();
        if (!DestroyWindow(control->window)) std::terminate();
        control->window = nullptr; s_endpoints.clear();
    }
    void holdMenu() {
        menu = CreateMenu();
        expect(menu && IsMenu(menu) && brokerHoldNativeResource(control->view, BrokerNativeMenu, menu),
            "real CreateMenu and actual typed resource hold");
        ++menuHolds;
        expect(control->nativeResources == 1 && !drained(*control), "actual hidden menu ledger prevents drain");
    }
    void disposeMenu() {
        if (!menu) return; // Old repeated tasks still reach their distinct bodies.
        const HMENU held = menu;
        expect(DestroyMenu(held) != FALSE, "actual DestroyMenu must succeed before release"); ++menuDestroys;
        expect(brokerReleaseNativeResource(control->view, BrokerNativeMenu, held), "actual typed resource release");
        ++menuReleases; menu = nullptr;
    }
    void holdComLedger() {
        brokerAddComReference(control->view); ++comAdds; ownComLedger = true;
        expect(control->comReferences == 1 && !drained(*control), "actual COM ledger API prevents drain");
        comLedgerObserved = true; // No real COM object or AddRef is claimed.
    }
    void disposeComLedger() {
        if (ownComLedger) { brokerReleaseComReference(control->view); ++comReleases; ownComLedger = false; }
    }
    void pump() {
        const ULONGLONG deadline = GetTickCount64() + 2000;
        while (control->queued || control->running) {
            if (GetTickCount64() >= deadline) throw std::runtime_error("real Win32 timer exceeded fixture bound");
            MSG msg{};
            while (PeekMessageW(&msg, control->window, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            Sleep(1);
        }
        expect(s_uiTasks.empty() && s_closeContinuations.empty() && s_cancelContinuations.empty(),
            "actual queued storage must drain before window destruction");
    }
    unsigned close() { return safeRequestClose(control->token); }
    void addPostingAction(bool faultFirst = false) {
        brokerAddCloseAction(control->view, [this, faultFirst] {
            ++actions;
            std::function<void(bool)> body = [this](bool wasCancelled) {
                expect(!wasCancelled, "terminal cleanup must run despite sticky failure");
                ++bodies; disposeMenu(); disposeComLedger();
            };
            if (faultFirst && actions == 1) failNextAllocation = true;
            if (!brokerPostUiTask(control->view, WKE_CURSOR_BROKER_NATIVE_MENU, std::move(body), true))
                brokerRecordFailure(control->view);
            failNextAllocation = false;
        });
    }
    void noteRealOom() {
        oomObservedWithRealHeldMenu = control->failed && !control->queued && !control->running
            && menu && IsMenu(menu) && control->nativeResources == 1 && !drained(*control);
        expect(oomObservedWithRealHeldMenu, "actual allocation failure retains real menu and false drain");
    }
    void print(const char* name) const {
        std::cout << "{\"case\":\"" << name << "\",\"actions\":" << actions
            << ",\"actualBodies\":" << bodies << ",\"actualCompletions\":" << completions
            << ",\"queued\":" << control->queued.load() << ",\"running\":" << control->running.load()
            << ",\"engineCallbacks\":" << control->engineCallbacks.load()
            << ",\"nativeResources\":" << control->nativeResources.load()
            << ",\"comReferences\":" << control->comReferences.load()
            << ",\"nativeAdmissionFault\":" << (control->nativeResourceAdmissionFault ? "true" : "false")
            << ",\"drained\":" << (control->closing && drained(*control) ? "true" : "false")
            << ",\"closeSequence\":" << control->closeSequence << ",\"taskSequence\":" << control->taskSequence.load()
            << ",\"stickyFailed\":" << (control->failed ? "true" : "false")
            << ",\"menuHolds\":" << menuHolds << ",\"menuDestroys\":" << menuDestroys
            << ",\"menuReleases\":" << menuReleases << ",\"comAdds\":" << comAdds << ",\"comReleases\":" << comReleases
            << ",\"realOomHeldMenu\":" << (oomObservedWithRealHeldMenu ? "true" : "false")
            << ",\"realLeaseHeldMenu\":" << (actionLeaseObservedWithRealHeldMenu ? "true" : "false")
            << ",\"actualComLedgerApiOnly\":" << (comLedgerObserved ? "true" : "false")
            << ",\"realWin32Timer\":" << (completions ? "true" : "false")
            << ",\"realWin32ApisUnreplaced\":true,\"realHmenuUsed\":true,\"fullQualification\":false}" << std::endl;
    }
};
void ordinaryFirst() {
    Environment e; e.holdMenu(); e.addPostingAction();
    expect(brokerPostUiTask(e.control->view, WKE_CURSOR_BROKER_NATIVE_TASK,
        [&e](bool cancelled) { expect(cancelled, "ordinary task cancels after close"); ++e.bodies; }, false),
        "actual ordinary admission");
    const bool first = e.close() == WKE_CURSOR_BROKER_OK && e.actions == 1
        && e.control->queued == 2 && e.control->closeSequence == 41;
    e.pump(); e.print("ordinary-first-menu");
    expect(first && e.bodies == 2 && e.completions == 2 && e.cancelled == 1 && e.menuReleases == 1
        && drained(*e.control), "first close must invoke cleanup even with an already queued ordinary task");
}
void queued() {
    Environment e; e.holdMenu(); e.addPostingAction(); const auto first = e.close(), second = e.close();
    const bool correct = first == WKE_CURSOR_BROKER_OK && second == WKE_CURSOR_BROKER_OK
        && e.actions == 1 && e.control->queued == 1 && e.control->taskSequence == 1 && e.control->closeSequence == 41;
    e.pump(); e.print("queued-no-readmission");
    expect(correct && e.bodies == 1 && e.completions == 1 && e.menuReleases == 1,
        "already closing queued cleanup must not call actions or readmit a timer payload");
}
void running() {
    Environment e; e.holdMenu(); bool checked = false, correct = false; unsigned inner = WKE_CURSOR_BROKER_INVALID;
    brokerAddCloseAction(e.control->view, [&] {
        ++e.actions;
        if (!brokerPostUiTask(e.control->view, WKE_CURSOR_BROKER_NATIVE_MENU, [&](bool cancelled) {
            expect(!cancelled, "terminal running task must execute"); ++e.bodies;
            if (!checked) {
                checked = true; const bool actuallyRunning = e.control->running == 1 && e.control->queued == 0
                    && e.control->nativeResources == 1 && IsMenu(e.menu);
                inner = e.close(); correct = actuallyRunning && e.actions == 1
                    && e.control->queued == 0 && e.control->taskSequence == 1;
            }
            e.disposeMenu();
        }, true)) brokerRecordFailure(e.control->view);
    });
    expect(e.close() == WKE_CURSOR_BROKER_OK, "first running close"); e.pump(); e.print("running-no-readmission");
    expect(checked && correct && inner == WKE_CURSOR_BROKER_OK && e.completions == 1 && e.menuReleases == 1,
        "already closing running cleanup must not call actions or readmit a timer payload");
}
void stickyPending() {
    Environment e; e.holdMenu(); e.addPostingAction(true);
    expect(e.close() == WKE_CURSOR_BROKER_FAILED, "first actual new failure"); e.noteRealOom();
    expect(e.close() == WKE_CURSOR_BROKER_FAILED && e.actions == 2 && e.control->queued == 1,
        "hidden real menu requires an empty-queue retry");
    const bool correct = e.close() == WKE_CURSOR_BROKER_FAILED && e.actions == 2
        && e.control->queued == 1 && e.control->taskSequence == 1 && e.control->closeSequence == 41;
    e.pump();
    if (!correct) { e.print("sticky-failed-pending-menu");
        expect(false, "pending retry must keep failed status without invoking cleanup again"); }
    expect(e.close() == WKE_CURSOR_BROKER_FAILED && e.actions == 2 && e.control->queued == 0
        && e.control->taskSequence == 1 && drained(*e.control), "fully drained failed endpoint must not readmit");
    e.print("sticky-failed-pending-menu");
    expect(e.bodies == 1 && e.completions == 1 && e.menuDestroys == 1 && e.menuReleases == 1,
        "terminal retry disposes real menu once and preserves sticky failure");
}
void emptyMenuRetry() {
    Environment e; e.holdMenu(); e.addPostingAction(true);
    expect(e.close() == WKE_CURSOR_BROKER_FAILED, "first actual allocation failure"); e.noteRealOom();
    expect(e.close() == WKE_CURSOR_BROKER_FAILED && e.actions == 2 && e.control->queued == 1,
        "empty queue with actual held resource must permit retry"); e.pump(); e.print("genuine-menu-empty-retry");
    expect(e.bodies == 1 && e.completions == 1 && e.menuDestroys == 1 && e.menuReleases == 1
        && e.control->failed && drained(*e.control) && e.control->closeSequence == 41,
        "genuine retry drains real menu without clearing failure or repeating sequence");
}
void reentrant() {
    Environment e; e.holdMenu(); bool nested = false; unsigned inner = WKE_CURSOR_BROKER_INVALID;
    brokerAddCloseAction(e.control->view, [&] {
        ++e.actions;
        if (!nested) {
            nested = true; e.actionLeaseObservedWithRealHeldMenu = e.control->engineCallbacks == 1
                && !e.control->queued && !e.control->running && e.control->nativeResources == 1 && IsMenu(e.menu);
            expect(e.actionLeaseObservedWithRealHeldMenu && !drained(*e.control), "real active lease and menu prevent drain");
            inner = e.close();
        }
        e.disposeMenu();
    });
    const auto outer = e.close(); e.print("menu-callout-reentrant");
    expect(outer == WKE_CURSOR_BROKER_OK && inner == WKE_CURSOR_BROKER_OK && e.actions == 1
        && !e.control->engineCallbacks && e.menuReleases == 1 && drained(*e.control)
        && e.control->closeSequence == 41, "active action lease with held menu must not recursively invoke cleanup");
}
void postDrain(bool fault) {
    Environment e; e.holdMenu(); e.addPostingAction(fault);
    if (fault) { expect(e.close() == WKE_CURSOR_BROKER_FAILED, "actual fault first close"); e.noteRealOom(); }
    expect(e.close() == (fault ? WKE_CURSOR_BROKER_FAILED : WKE_CURSOR_BROKER_OK), "actual cleanup admission");
    e.pump(); expect(drained(*e.control) && e.menuReleases == 1 && e.menuDestroys == 1, "actual complete menu drain");
    const unsigned priorActions = e.actions; const auto priorSequence = e.control->taskSequence.load();
    const bool noNew = e.close() == (fault ? WKE_CURSOR_BROKER_FAILED : WKE_CURSOR_BROKER_OK)
        && e.actions == priorActions && !e.control->queued && e.control->taskSequence == priorSequence;
    e.pump(); e.print(fault ? "failed-menu-drained-no-readmission" : "post-menu-drain-no-readmission");
    expect(noNew && e.menuReleases == 1 && e.menuDestroys == 1 && drained(*e.control),
        "actual fully drained state must not invoke actions or readmit timer payloads");
}
void admissionFault() {
    Environment e; e.holdMenu();
    expect(!brokerHoldNativeResource(e.control->view, BrokerNativeMenu, nullptr)
        && e.control->nativeResourceAdmissionFault && e.control->failed, "actual invalid native hold establishes sticky admission fault");
    e.nativeAdmissionFaultObserved = true; e.addPostingAction();
    expect(e.close() == WKE_CURSOR_BROKER_FAILED, "admission fault retains failed status"); e.pump();
    expect(!e.control->nativeResources && !e.control->queued && !e.control->running && !drained(*e.control),
        "hidden actual admission fault forbids a false drain receipt");
    expect(e.close() == WKE_CURSOR_BROKER_FAILED && e.actions == 2 && e.control->queued == 1,
        "admission-fault empty queue remains retryable without inventing drain"); e.pump(); e.print("native-admission-fault-empty-retry");
    expect(e.actions == 2 && e.bodies == 2 && e.completions == 2 && e.menuReleases == 1
        && !drained(*e.control) && e.control->nativeResourceAdmissionFault && e.control->closeSequence == 41,
        "retry keeps the actual admission fault and original sequence");
}
void comLedger() {
    Environment e; e.holdMenu(); e.holdComLedger(); e.addPostingAction(true);
    expect(e.close() == WKE_CURSOR_BROKER_FAILED, "actual COM-ledger case allocation fault"); e.noteRealOom();
    expect(e.control->comReferences == 1 && !drained(*e.control), "actual COM ledger still held");
    expect(e.close() == WKE_CURSOR_BROKER_FAILED && e.actions == 2 && e.control->queued == 1,
        "actual COM/native ledgers permit genuine empty retry"); e.pump(); e.print("com-ledger-empty-retry");
    expect(e.bodies == 1 && e.completions == 1 && e.comAdds == 1 && e.comReleases == 1
        && !e.control->comReferences && e.menuReleases == 1 && drained(*e.control),
        "actual ledger API balances without claiming a real COM object");
}
void paintCloseDependency() {
    Environment e;
    e.paint_drained = false;
    expect(e.close() == WKE_CURSOR_BROKER_OK && e.paint_sealed && !drained(*e.control),
        "held paint dependency must prevent actual broker drain");
    e.paint_drained = true;
    expect(drained(*e.control), "actual paint completion dependency admits drain");
}
void boundaryExhaustion() {
    paintCloseDependency();
    Environment e; e.holdMenu(); e.addPostingAction(); unsigned wrong = WKE_CURSOR_BROKER_INVALID;
    std::thread other([&] { wrong = e.close(); }); other.join();
    expect(wrong == WKE_CURSOR_BROKER_WRONG_THREAD && !e.control->closing && e.actions == 0,
        "actual other thread must fail before actions and closing");
    expect(safeRequestClose({9, 10}) == WKE_CURSOR_BROKER_STALE, "unknown endpoint remains stale");
    e.control->destroying = true;
    expect(e.close() == WKE_CURSOR_BROKER_CLOSING && !e.control->closing && e.actions == 0, "destroying boundary stays unchanged");
    e.control->destroying = false; e.control->publishSequence = (std::numeric_limits<unsigned __int64>::max)();
    expect(e.close() == WKE_CURSOR_BROKER_FAILED && e.control->closing && e.control->closeSequence == 0,
        "exhausted publish sequence must not fabricate close sequence"); e.pump();
    const bool noNew = e.close() == WKE_CURSOR_BROKER_FAILED && e.actions == 1 && !e.control->queued
        && e.control->closeSequence == 0 && e.control->taskSequence == 1;
    e.pump(); e.print("boundary-exhausted-drain");
    expect(noNew && e.control->failed && e.menuReleases == 1 && drained(*e.control),
        "exhausted and fully drained endpoint must keep failure without repeat admission");
}
} // namespace fixture
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("one exact case argument is required");
        const std::string name = argv[1];
        if (name == "ordinary-first-menu") fixture::ordinaryFirst();
        else if (name == "queued-no-readmission") fixture::queued();
        else if (name == "running-no-readmission") fixture::running();
        else if (name == "sticky-failed-pending-menu") fixture::stickyPending();
        else if (name == "genuine-menu-empty-retry") fixture::emptyMenuRetry();
        else if (name == "menu-callout-reentrant") fixture::reentrant();
        else if (name == "post-menu-drain-no-readmission") fixture::postDrain(false);
        else if (name == "failed-menu-drained-no-readmission") fixture::postDrain(true);
        else if (name == "native-admission-fault-empty-retry") fixture::admissionFault();
        else if (name == "com-ledger-empty-retry") fixture::comLedger();
        else if (name == "boundary-exhausted-drain") fixture::boundaryExhaustion();
        else throw std::runtime_error("unknown exact case");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ACTUAL-LEAF-FAILURE " << e.what() << std::endl; return 1;
    }
}
