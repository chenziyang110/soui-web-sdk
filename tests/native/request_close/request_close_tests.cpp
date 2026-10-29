// Actual byte-extracted broker algorithms; real Win32 timers, HWND and HMENU.
// This leaf uses opaque view identity and qualifies no Blink/SDK/COM object.
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
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
using wkeWebView = void*;
#include "wke_cursor_broker.h"
#include "content/web_impl_win/LocalResourceState.h"
// This real resource implementation is intentionally independent of Blink.
// Its private retirement command must be present before the broker fragments.
#include "../../../third_party/sources/miniblink/third_party/WebKit/Source/platform/network/InspectorResourceSnapshot.cpp"
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
struct Environment {
    std::shared_ptr<CursorBrokerControl> control;
    HMENU menu = nullptr;
    unsigned actions = 0, bodies = 0, completions = 0, cancelled = 0;
    unsigned menuHolds = 0, menuDestroys = 0, menuReleases = 0, comAdds = 0, comReleases = 0;
    bool oomObservedWithRealHeldMenu = false, actionLeaseObservedWithRealHeldMenu = false;
    bool nativeAdmissionFaultObserved = false, comLedgerObserved = false;
    bool inspectorOwnerFenceObserved = false;
    unsigned inspectorOwnerReleases = 0;
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
            << ",\"inspectorOwnerFenceVerified\":" << (inspectorOwnerFenceObserved ? "true" : "false")
            << ",\"inspectorOwnerReleases\":" << inspectorOwnerReleases
            << ",\"realWin32Timer\":" << (completions ? "true" : "false")
            << ",\"realWin32ApisUnreplaced\":true,\"realHmenuUsed\":true,\"fullQualification\":false}" << std::endl;
    }
};
struct ResourceOwnerWitness {
    unsigned retains = 0, releases = 0, destroys = 0, lookups = 0, terminalBodies = 0;
    bool soleOwnerReleased = false, countedRelease = false, reentrantDrainFenced = false;
};
struct ResourceOwner {
    ResourceOwnerWitness& witness;
    unsigned references = 1;
    std::function<void()> onRelease;
    explicit ResourceOwner(ResourceOwnerWitness& value) : witness(value) { }
    void dropInitialReference() {
        if (!--references) { ++witness.destroys; delete this; }
    }
    static void __cdecl retain(void* opaque) {
        auto& owner = *static_cast<ResourceOwner*>(opaque);
        ++owner.references; ++owner.witness.retains;
    }
    static void __cdecl release(void* opaque) {
        auto& owner = *static_cast<ResourceOwner*>(opaque);
        ++owner.witness.releases;
        owner.witness.soleOwnerReleased = owner.references == 1;
        if (owner.onRelease) owner.onRelease();
        if (!--owner.references) { ++owner.witness.destroys; delete &owner; }
    }
    static unsigned __cdecl lookup(void* opaque, const char* relative, unsigned length,
        const unsigned char** bytes, unsigned* size, unsigned* mime) {
        auto& owner = *static_cast<ResourceOwner*>(opaque); ++owner.witness.lookups;
        static const char entry[] = "front_end/inspector.html";
        static const unsigned char body[] = {'<', 'h', 't', 'm', 'l', '>'};
        if (!relative || length != sizeof(entry) - 1 || std::memcmp(relative, entry, length)
            || !bytes || !size || !mime) return 0;
        *bytes = body; *size = sizeof(body); *mime = 1; return 1;
    }
};
struct InspectorResourceFence {
    Environment& environment;
    ResourceOwnerWitness witness;
    std::shared_ptr<blink::InspectorResourceState> state;
    explicit InspectorResourceFence(Environment& value) : environment(value) {
        auto* owner = new ResourceOwner(witness);
        blink::InspectorSnapshotConfig config{};
        config.structSize = sizeof(config); config.abiVersion = 2;
        config.fileCount = 1; config.totalBytes = 6;
        std::fill_n(config.payloadId, 64, 'a');
        config.callbacks = {sizeof(config.callbacks), 2, owner, &ResourceOwner::retain,
            &ResourceOwner::release, &ResourceOwner::lookup};
        state = blink::InspectorResourceState::create(config, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        owner->dropInitialReference();
        expect(state && witness.retains == 1 && !witness.releases && !witness.destroys,
            "real resource callback owner must retain its sole reference");
        environment.control->inspectorResources = state;
        owner->onRelease = [this] {
            auto& control = *environment.control;
            witness.countedRelease = control.engineCallbacks == 1 && state->closed()
                && !state->active() && !state->queued() && !state->dispatcherAlive()
                && !state->beginOperation();
            wkeCursorBrokerDrainReceipt nested{}; nested.structSize = sizeof(nested);
            const auto status = inspectDrain(control.token, &nested);
            witness.reentrantDrainFenced = status == WKE_CURSOR_BROKER_OK && !nested.drained
                && nested.engineCallbacks >= 1 && control.engineCallbacks == 1;
        };
        blink::InspectorResourceBytes bytes;
        expect(state->arm() && state->lookup(state->entry(), bytes) && bytes.size == 6
            && bytes.data && witness.lookups == 1, "actual resource admission and original callback owner");
        expect(state->beginOperation(), "actual resource operation admission");
        state->enterCallback();
        wkeCursorBrokerDrainReceipt open{}; open.structSize = sizeof(open);
        expect(inspectDrain(environment.control->token, &open) == WKE_CURSOR_BROKER_OK
            && !open.closing && !open.drained && open.engineCallbacks == 2,
            "actual open receipt counts both resource operation and callback");
    }
    ~InspectorResourceFence() { if (state) std::terminate(); }
    void pending(unsigned active, unsigned queued) {
        wkeCursorBrokerDrainReceipt out{}; out.structSize = sizeof(out); out.drained = 77;
        std::array<unsigned char, sizeof(out)> sentinel{};
        std::memcpy(sentinel.data(), &out, sizeof(out));
        expect(state->closed() && state->active() == active && state->queued() == queued
            && state->dispatcherAlive() && !witness.releases && !witness.destroys
            && !drained(*environment.control), "actual pending resources retain the owner and forbid drain");
        expect(inspectDrain(environment.control->token, &out) == WKE_CURSOR_BROKER_BUSY
            && !std::memcmp(&out, sentinel.data(), sizeof(out)) && !environment.control->engineCallbacks,
            "pending retirement must not publish a receipt or leak its counted callout");
    }
    void finishAfterNativeDrain() {
        blink::InspectorResourceBytes bytes;
        expect(!state->beginOperation() && !state->lookup(state->entry(), bytes) && witness.lookups == 1,
            "actual close denies new resource admission before owner retirement");
        pending(2, 0);
        state->leaveCallback(); pending(1, 0);
        expect(state->post([this] { ++witness.terminalBodies; }, true),
            "actual closed dispatcher admits its counted terminal continuation");
        state->endOperation(true); pending(0, 1);
        const ULONGLONG deadline = GetTickCount64() + 2000;
        while (state->queued()) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline) throw std::runtime_error("real resource dispatcher exceeded fixture bound");
            const DWORD ready = MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(deadline - now),
                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (ready != WAIT_OBJECT_0) throw std::runtime_error("real resource dispatcher readiness wait failed or expired");
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (GetTickCount64() >= deadline) throw std::runtime_error("real resource message pumping exceeded fixture bound");
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
        expect(witness.terminalBodies == 1 && !state->active() && state->dispatcherAlive()
            && !witness.releases && !witness.destroys && !drained(*environment.control),
            "a live resource dispatcher alone still forbids an actual broker drain");
        wkeCursorBrokerDrainReceipt out{}; out.structSize = sizeof(out);
        expect(inspectDrain(environment.control->token, &out) == WKE_CURSOR_BROKER_OK
            && out.drained && !out.engineCallbacks && !state->dispatcherAlive()
            && witness.releases == 1 && witness.destroys == 1 && witness.soleOwnerReleased
            && witness.countedRelease && witness.reentrantDrainFenced,
            "actual retirement releases the original owner once behind the reentrant broker fence");
        const auto actions = environment.actions;
        const auto sequence = environment.control->taskSequence.load();
        expect(environment.close() == WKE_CURSOR_BROKER_OK && environment.actions == actions
            && environment.control->taskSequence == sequence
            && inspectDrain(environment.control->token, &out) == WKE_CURSOR_BROKER_OK && out.drained
            && witness.releases == 1 && witness.destroys == 1,
            "retired resources cannot readmit cleanup or release their callback owner twice");
        auto cacheIdentity = state; std::weak_ptr<blink::InspectorResourceState> weak = state;
        environment.control->inspectorResources.reset(); state.reset();
        expect(!weak.expired() && witness.destroys == 1,
            "a cache-held identity does not keep the already retired callback owner alive");
        cacheIdentity.reset();
        expect(weak.expired() && witness.releases == 1 && witness.destroys == 1,
            "last real resource identity destruction cannot repeat callback release");
        environment.inspectorOwnerFenceObserved = true;
        environment.inspectorOwnerReleases = witness.releases;
    }
};
void ordinaryFirst() {
    Environment e; InspectorResourceFence resources(e); e.holdMenu(); e.addPostingAction();
    expect(brokerPostUiTask(e.control->view, WKE_CURSOR_BROKER_NATIVE_TASK,
        [&e](bool cancelled) { expect(cancelled, "ordinary task cancels after close"); ++e.bodies; }, false),
        "actual ordinary admission");
    const bool first = e.close() == WKE_CURSOR_BROKER_OK && e.actions == 1
        && e.control->queued == 2 && e.control->closeSequence == 41;
    e.pump(); resources.finishAfterNativeDrain(); e.print("ordinary-first-menu");
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
void boundaryExhaustion() {
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
// This provider owns real body storage; the production State controls its sole
// retained reference. It is an SDK-callback boundary, not a renderer substitute.
struct LocalOwnerWitness {
    unsigned retains = 0, releases = 0, destroys = 0, lookups = 0;
    bool soleOwnerReleased = false, countedRelease = false, reentrantDrainFenced = false;
};
struct LocalOwner {
    LocalOwnerWitness& witness;
    unsigned references = 1;
    std::vector<unsigned char> body{'l', 'o', 'c', 'a', 'l', '!'};
    std::function<void()> onRelease;
    explicit LocalOwner(LocalOwnerWitness& value) : witness(value) { }
    void dropInitialReference() {
        if (!--references) { ++witness.destroys; delete this; }
    }
    static void __cdecl retain(void* opaque) noexcept {
        auto& owner = *static_cast<LocalOwner*>(opaque);
        ++owner.references; ++owner.witness.retains;
    }
    static void __cdecl release(void* opaque) noexcept {
        auto& owner = *static_cast<LocalOwner*>(opaque); ++owner.witness.releases;
        owner.witness.soleOwnerReleased = owner.references == 1;
        if (owner.onRelease) owner.onRelease();
        if (!--owner.references) { ++owner.witness.destroys; delete &owner; }
    }
    static uint32_t __cdecl lookup(void* opaque, const char* key, uint32_t length,
        const unsigned char** bytes, uint32_t* size, uint32_t* mime) noexcept {
        auto& owner = *static_cast<LocalOwner*>(opaque); ++owner.witness.lookups;
        static const char entry[] = "index.html";
        if (!key || length != sizeof(entry) - 1 || std::memcmp(key, entry, length)
            || !bytes || !size || !mime) return 0;
        *bytes = owner.body.data(); *size = static_cast<uint32_t>(owner.body.size());
        *mime = SouiLocalHtml; return 1;
    }
};
struct LocalInFlight final : content::LocalResourceTask {
    std::shared_ptr<content::LocalResourceState> state;
    content::LocalResourceBytes bytes;
    unsigned int slot = 0, cancellations = 0;
    bool inCall = true, completed = false;
    void run() noexcept override { cancel(); }
    void cancel() noexcept override {
        ++cancellations;
        // Like an actual loader callout, cancellation cannot invalidate bytes
        // while its current stack still uses them. The real State sees both
        // the operation and the byte credit until complete() ends that lease.
        if (!inCall && !completed) complete();
    }
    void complete() noexcept {
        if (completed) return;
        inCall = false; completed = true;
        auto retained = std::move(state);
        const unsigned int count = bytes.size; bytes = {};
        retained->endOperation(slot, count, true);
    }
};
void nativeLocalOwnerFence() {
    Environment e;
    LocalOwnerWitness witness;
    auto* owner = new LocalOwner(witness);
    SouiLocalResourceConfig config{};
    config.structSize = sizeof(config); config.abiVersion = SOUI_LOCAL_RESOURCE_ABI;
    config.fileCount = 1; config.totalBytes = 6;
    std::fill_n(config.hostNonce, 32, 'a'); std::fill_n(config.payloadId, 64, 'b');
    std::memcpy(config.entryRelative, "index.html", sizeof("index.html"));
    config.callbacks = {sizeof(config.callbacks), SOUI_LOCAL_RESOURCE_ABI, owner,
        &LocalOwner::retain, &LocalOwner::release, &LocalOwner::lookup};
    unsigned int status = SouiLocalInvalid;
    auto state = content::LocalResourceState::create(config,
        "cccccccccccccccccccccccccccccccc", 3, 4, 99, &status);
    owner->dropInitialReference();
    expect(state && status == SouiLocalOk && witness.retains == 1 && !witness.releases
        && !witness.destroys, "actual local State retains the sole callback body owner");
    e.control->localResources = state;
    owner->onRelease = [&] {
        SouiLocalResourceDrain local{}; local.structSize = sizeof(local); state->snapshot(local);
        witness.countedRelease = e.control->engineCallbacks == 1 && local.callbackBorrows == 1
            && !local.activeOperations && !local.borrowedBytes && local.payloadHeld == 1;
        wkeCursorBrokerDrainReceipt nested{}; nested.structSize = sizeof(nested); nested.drained = 77;
        std::array<unsigned char, sizeof(nested)> before{};
        std::memcpy(before.data(), &nested, sizeof(nested));
        witness.reentrantDrainFenced = inspectDrain(e.control->token, &nested) == WKE_CURSOR_BROKER_BUSY
            && !std::memcmp(before.data(), &nested, sizeof(nested)) && e.control->engineCallbacks == 1;
    };
    auto operation = std::make_shared<LocalInFlight>(); operation->state = state;
    expect(state->lookup(state->entry(), operation->bytes) && operation->bytes.status == 200
        && operation->bytes.size == 6 && operation->bytes.data && witness.lookups == 1,
        "actual State lookup returns the original callback-owned body");
    expect(state->beginOperation(operation, operation->slot) && state->borrowBytes(operation->bytes.size),
        "actual State must account the running operation and borrowed body bytes");
    const auto pending = [&] {
        SouiLocalResourceDrain local{}; local.structSize = sizeof(local); state->snapshot(local);
        expect(local.phase == SouiLocalClosing && local.closeSequence == 1 && local.activeOperations == 1
            && local.callbackBorrows == 1 && local.borrowedBytes == 6 && local.payloadHeld == 1
            && !witness.releases && !witness.destroys && !drained(*e.control)
            && operation->bytes.data[0] == 'l', "real in-flight callback/body keeps the SDK owner alive");
        wkeCursorBrokerDrainReceipt out{}; out.structSize = sizeof(out); out.drained = 77;
        std::array<unsigned char, sizeof(out)> before{}; std::memcpy(before.data(), &out, sizeof(out));
        expect(inspectDrain(e.control->token, &out) == WKE_CURSOR_BROKER_BUSY
            && !std::memcmp(before.data(), &out, sizeof(out)) && !e.control->engineCallbacks,
            "actual pending local retirement may not publish a broker drain receipt");
    };
    {
        content::LocalResourceCallbackBorrow callback(state);
        expect(e.close() == WKE_CURSOR_BROKER_OK && e.control->closing
            && e.control->closeSequence == 41 && operation->cancellations == 1,
            "actual requestClose accepts close and cancels without retiring the active callout");
        pending();
        state->fault();
        expect(e.close() == WKE_CURSOR_BROKER_FAILED && state->failed() && e.control->failed,
            "actual local fault becomes a sticky broker failure even while the owner is pending");
        pending();
    }
    expect(!witness.releases && !witness.destroys && !state->retired(),
        "leaving the callback alone cannot release still-borrowed operation bytes");
    wkeCursorBrokerDrainReceipt bodyHeld{}; bodyHeld.structSize = sizeof(bodyHeld); bodyHeld.drained = 77;
    std::array<unsigned char, sizeof(bodyHeld)> bodySentinel{};
    std::memcpy(bodySentinel.data(), &bodyHeld, sizeof(bodyHeld));
    expect(inspectDrain(e.control->token, &bodyHeld) == WKE_CURSOR_BROKER_BUSY
        && !std::memcmp(bodySentinel.data(), &bodyHeld, sizeof(bodyHeld)) && !witness.releases,
        "actual broker must still reject drain after callback exit while body credit remains");
    operation->complete();
    SouiLocalResourceDrain local{}; local.structSize = sizeof(local); state->snapshot(local);
    expect(!local.activeOperations && !local.callbackBorrows && !local.borrowedBytes
        && local.payloadHeld == 1 && !state->retired() && !witness.releases,
        "only actual operation completion returns the final body credit");
    wkeCursorBrokerDrainReceipt out{}; out.structSize = sizeof(out);
    expect(inspectDrain(e.control->token, &out) == WKE_CURSOR_BROKER_FAILED && out.drained
        && out.closeSequence == 41 && !out.engineCallbacks && state->retired() && state->failed()
        && witness.releases == 1 && witness.destroys == 1 && witness.soleOwnerReleased
        && witness.countedRelease && witness.reentrantDrainFenced,
        "actual broker retirement releases the sole body owner once behind its reentrant fence");
    expect(e.close() == WKE_CURSOR_BROKER_FAILED && inspectDrain(e.control->token, &out) == WKE_CURSOR_BROKER_FAILED
        && out.drained && witness.releases == 1 && witness.destroys == 1,
        "physical retirement must not erase failure or release the callback owner twice");
    auto cacheIdentity = state; std::weak_ptr<content::LocalResourceState> weak = state;
    e.control->localResources.reset(); state.reset(); operation.reset();
    expect(!weak.expired() && witness.destroys == 1, "retired cache identity does not retain the callback body owner");
    cacheIdentity.reset();
    expect(weak.expired() && witness.releases == 1 && witness.destroys == 1,
        "final cache identity destruction cannot repeat local callback release");
    std::cout << "{\"case\":\"native-local-owner-fence\",\"localOwnerRetains\":" << witness.retains
        << ",\"localOwnerReleases\":" << witness.releases << ",\"localOwnerDestroys\":" << witness.destroys
        << ",\"reentrantDrainFenced\":true,\"stickyFailurePreserved\":true,\"realStateImplementation\":true,"
        << "\"realHmenuUsed\":false,\"rendererExecuted\":false,\"fullQualification\":false}" << std::endl;
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
        else if (name == "native-local-owner-fence") fixture::nativeLocalOwnerFence();
        else throw std::runtime_error("unknown exact case");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ACTUAL-LEAF-FAILURE " << e.what() << std::endl; return 1;
    }
}
