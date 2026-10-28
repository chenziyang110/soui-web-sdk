#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "content/web_impl_win/LocalResourceState.h"
#include "content/web_impl_win/LocalResourceAdmission.h"
#include <windows.h>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#if !defined(SOUI_LOCAL_RESOURCE_TESTING)
#error This unit target must compile the production State TU with SOUI_LOCAL_RESOURCE_TESTING.
#endif

// Direct behavioral tests of the production native State/Win32 dispatcher.
// The callback provider is a unit boundary, not a browser/view/runtime receipt.
namespace {
using content::LocalResourceState;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Provider {
    unsigned int retains = 0, releases = 0, lookups = 0;
    bool failLookup = false;
    const std::string body = "fixed";
    std::function<void()> onRelease;
    static void __cdecl retain(void* value) noexcept { ++static_cast<Provider*>(value)->retains; }
    static void __cdecl release(void* value) noexcept {
        auto& self = *static_cast<Provider*>(value); ++self.releases;
        if (self.onRelease) self.onRelease();
    }
    static uint32_t __cdecl lookup(void* value, const char* key, uint32_t length,
        const unsigned char** bytes, uint32_t* size, uint32_t* mime) noexcept {
        auto& self = *static_cast<Provider*>(value); ++self.lookups;
        if (self.failLookup) return 2;
        const std::string path(key, length);
        if (path == "empty.txt") { *bytes = nullptr; *size = 0; *mime = SouiLocalPlain; return 1; }
        if (path != "index.html") return 0;
        *bytes = reinterpret_cast<const unsigned char*>(self.body.data());
        *size = static_cast<uint32_t>(self.body.size()); *mime = SouiLocalHtml; return 1;
    }
};
SouiLocalResourceConfig config(Provider& provider) {
    SouiLocalResourceConfig out{}; out.structSize = sizeof(out); out.abiVersion = SOUI_LOCAL_RESOURCE_ABI;
    out.fileCount = 2; out.totalBytes = static_cast<uint32_t>(provider.body.size());
    std::memset(out.hostNonce, 'b', 32); std::memset(out.payloadId, 'a', 64);
    std::memcpy(out.entryRelative, "index.html", sizeof("index.html"));
    out.callbacks = { sizeof(out.callbacks), SOUI_LOCAL_RESOURCE_ABI, &provider,
        Provider::retain, Provider::release, Provider::lookup };
    return out;
}
const char documentNonce[] = "0123456789abcdef0123456789abcdef";
struct Session {
    Provider provider;
    std::shared_ptr<LocalResourceState> state;
    Session() : state(LocalResourceState::create(config(provider), documentNonce, 5, 7, 9)) {
        require(!!state, "create native state");
    }
    ~Session() {
        if (state) { state->close(); if (!state->finishClose()) std::terminate(); }
    }
};
SouiLocalResourceDrain observe(const std::shared_ptr<LocalResourceState>& state) {
    SouiLocalResourceDrain out{}; out.structSize = sizeof(out); state->snapshot(out); return out;
}
struct Task final : content::LocalResourceTask {
    std::weak_ptr<LocalResourceState> state;
    unsigned int slot = 0, bytes = 0, runs = 0, cancellations = 0;
    bool active = false;
    std::function<void()> body;
    ~Task() override { cancel(); }
    void finish(bool cancelled) noexcept {
        if (!active) return;
        active = false;
        if (auto retained = state.lock()) retained->endOperation(slot, bytes, cancelled);
        else std::terminate();
    }
    void run() noexcept override { ++runs; if (body) body(); finish(false); }
    void cancel() noexcept override { if (active) ++cancellations; finish(true); }
};
std::shared_ptr<Task> admitted(const std::shared_ptr<LocalResourceState>& state) {
    auto task = std::make_shared<Task>(); task->state = state;
    task->active = state->beginOperation(task, task->slot);
    require(task->active, "operation admitted"); return task;
}
bool removeMessage(MSG& message) {
    return PeekMessageW(&message, nullptr, WM_APP, 0xBFFF, PM_REMOVE) != FALSE;
}
void pumpBounded() {
    MSG message{};
    for (unsigned int i = 0; i < 512; ++i) {
        if (!removeMessage(message)) return;
        DispatchMessageW(&message);
    }
    throw std::runtime_error("dispatcher exceeded bounded message drain");
}
void namespaceAndClosedLookup() {
    require(LocalResourceState::reservedURL("https://0123.soui-local.invalid/x"), "reserved HTTPS");
    require(LocalResourceState::reservedURL("http://u@0123.SOUI-LOCAL.INVALID.:99/x"), "aliases remain reserved");
    require(!LocalResourceState::reservedURL("https://0123.soui-local.invalid.example/x"), "external suffix distinct");
    require(!LocalResourceState::reservedURL("data:text/plain,https://0123.soui-local.invalid/x"), "data payload is not an origin");
    require(!LocalResourceState::reservedURL("blob:https://0123.soui-local.invalid/guid"), "blob inner origin is not an HTTP resource request");
    require(!LocalResourceState::reservedURL("javascript:'https://0123.soui-local.invalid/x'"), "opaque script payload not interpreted as authority");
    require(!LocalResourceState::reservedURL("file:///C:/notes/https://0123.soui-local.invalid/x"), "file path payload not interpreted as authority");
    require(!LocalResourceState::validRelativePath("x/%2e%2e/secret"), "encoded path denied");
    require(!LocalResourceState::validRelativePath("x/../secret"), "traversal denied");
    require(!LocalResourceState::validRelativePath("NUL.txt"), "device path denied");
    require(!LocalResourceState::validRelativePath("x.json?token"), "query denied");
    require(LocalResourceState::validRelativePath(".soui-web-profile.json"), "closed metadata key supported");
    Session session;
    content::LocalResourceBytes bytes;
    require(session.state->lookup(session.state->base() + "empty.txt", bytes)
        && bytes.status == 200 && !bytes.size && !bytes.data, "zero-byte found body exact");
    require(session.state->lookup(session.state->base() + "absent.json", bytes)
        && bytes.status == 404 && !bytes.size && !bytes.data, "missing has real empty 404");
    require(!session.state->lookup("https://other.soui-local.invalid/" + std::string(documentNonce) + "/index.html", bytes), "cross Host denied");
    require(!session.state->lookup(session.state->base() + "index.html?x", bytes), "URL query denied");
    const unsigned int before = session.provider.lookups;
    session.state->close(); require(session.state->finishClose(), "empty owner retired");
    require(!session.state->lookup(session.state->entry(), bytes) && session.provider.lookups == before, "old identity cannot lookup retired callback");
    require(session.provider.retains == 1 && session.provider.releases == 1, "snapshot retained and released once");
}
void pauseQueueCancellationAndCredits() {
    Session session;
    std::vector<std::shared_ptr<Task>> tasks;
    LocalResourceState::enterPause();
    for (unsigned int i = 0; i < 64; ++i) {
        auto task = admitted(session.state); require(session.state->post(task), "bounded pending accepted"); tasks.push_back(task);
    }
    auto overflow = std::make_shared<Task>(); unsigned int ignored = 0;
    require(!session.state->beginOperation(overflow, ignored), "65th active credit rejected");
    tasks[3]->cancel();
    require(observe(session.state).activeOperations == 63 && observe(session.state).queuedOperations == 63, "cancel returns active and pending credit while paused");
    auto replacement = admitted(session.state); require(session.state->post(replacement), "returned credit reusable"); tasks.push_back(replacement);
    MSG message{}; require(!removeMessage(message), "no wake or callback posted during pause");
    LocalResourceState::enterPause(); LocalResourceState::leavePause();
    require(!removeMessage(message), "inner resume does not wake business lane");
    LocalResourceState::leavePause();
    require(removeMessage(message), "outer resume posts a wake");
    MSG second{}; require(!removeMessage(second), "outer resume coalesces one wake");
    DispatchMessageW(&message); pumpBounded();
    auto end = observe(session.state);
    require(!end.activeOperations && !end.queuedOperations && !end.callbackBorrows && !end.borrowedBytes, "all actual dispatcher operations retired");
    require(end.completed == 64 && end.cancelled == 1 && end.started == 65, "native terminal accounting exact");
    require(end.failed == 0 && end.denied == 1, "request quota rejection remains counted without faulting its owner");
    // Further real state operations prove that terminal credits are not a
    // lifetime request counter. This is not the DevTools 1,000-cycle test.
    for (unsigned int i = 0; i < 128; ++i) {
        auto task = admitted(session.state); require(session.state->post(task), "terminal credit recycled"); pumpBounded();
        require(task->runs == 1, "actual continuation dispatched once");
    }
    session.state->close(); require(session.state->finishClose(), "close after recycled requests");
    require(observe(session.state).failed == 0 && observe(session.state).denied == 1
        && session.provider.releases == 1, "retirement preserves the actual request quota denial");
}
void closeInsideCallbackAndReleaseReentry() {
    Session session;
    bool nestedPending = false, releasePending = false;
    session.provider.onRelease = [&] {
        const auto sample = observe(session.state);
        releasePending = sample.phase == SouiLocalClosing && sample.callbackBorrows != 0
            && sample.payloadHeld == 1 && !session.state->finishClose();
        session.state->close();
    };
    auto task = admitted(session.state);
    require(session.state->borrowBytes(5), "response bytes borrowed"); task->bytes = 5;
    task->body = [&] {
        session.state->close();
        const auto sample = observe(session.state);
        nestedPending = sample.callbackBorrows != 0 && !session.state->finishClose();
    };
    require(session.state->post(task), "callback close task posted"); pumpBounded();
    require(nestedPending, "real callback stack prevents payload retirement");
    const auto pending = observe(session.state);
    require(!pending.activeOperations && !pending.borrowedBytes && pending.phase == SouiLocalClosing, "callback exit returns operation and body borrow");
    require(session.state->finishClose() && releasePending, "owner release reentry remains pending until real return");
    const auto done = observe(session.state);
    require(done.phase == SouiLocalPayloadRetired && !done.payloadHeld && done.closeSequence == 1
        && !done.callbackBorrows && !done.borrowedBytes, "explicit stable retired receipt");
    session.state->close(); require(session.state->finishClose() && session.provider.releases == 1, "repeated close never double releases");
}
void ownerFailureAndOldPageBinding() {
    Session old;
    int pageIdentity = 0;
    require(old.state->install(&pageIdentity), "unit Page identity installed");
    require(LocalResourceState::forPage(&pageIdentity) == old.state, "bound lookup names exact owner");
    Session successor;
    require(!successor.state->install(&pageIdentity), "cannot overwrite live owner");
    old.provider.failLookup = true; content::LocalResourceBytes bytes;
    require(!old.state->lookup(old.state->entry(), bytes), "provider failure rejected");
    require(old.state->closed() && old.state->failed() && old.state->finishClose(), "failed owner still physically retires");
    require(observe(old.state).failed == 1 && old.provider.releases == 1, "failed receipt stays sticky");
    require(!LocalResourceState::forPage(&pageIdentity), "retirement removes old Page binding");
    require(successor.state->install(&pageIdentity), "retired Page can bind a distinct owner");
    require(LocalResourceState::forPage(&pageIdentity) == successor.state, "successor mapping independent from old shared identity");
    const unsigned int before = old.provider.lookups;
    require(!old.state->lookup(old.state->entry(), bytes) && old.provider.lookups == before, "cached old identity never adopts successor");
}
void admissionTransferAndDeferredCancellation() {
    Session session;
    auto cell = std::make_shared<blink::LocalResourceAdmission>(session.state, session.state->entry(), "GET");
    auto copy = cell;
    require(cell->reserve() && copy->reserve() && observe(session.state).started == 1, "same-thread cell copies share one reservation");
    cell->claimLoader(); cell->abandonUnclaimed();
    require(observe(session.state).activeOperations == 1, "deferred real ResourceLoader keeps its reserved credit");
    auto task = std::make_shared<Task>(); std::shared_ptr<LocalResourceState> owner; unsigned int slot = 0;
    require(cell->transfer(task, owner, slot) && owner == session.state, "actual task consumes original slot");
    task->state = owner; task->slot = slot; task->active = true;
    require(!copy->reserve(), "transferred cell cannot preflight a second load");
    auto duplicate = std::make_shared<Task>();
    require(!copy->transfer(duplicate, owner, slot), "shared request copy cannot consume twice");
    task->body = [cell] { cell->terminal(); };
    require(session.state->post(task), "consumed task has its first bounded queue slot"); pumpBounded();
    require(observe(session.state).started == 1 && observe(session.state).completed == 1
        && !observe(session.state).activeOperations, "transfer did not duplicate native accounting");
    session.state->close(); require(session.state->finishClose() && session.provider.releases == 1, "cached terminal cells do not retain payload");
    require(!copy->matches(session.state, session.state->entry(), "GET"), "old terminal cell stays unusable");

    Session deferred;
    auto pending = std::make_shared<blink::LocalResourceAdmission>(deferred.state, deferred.state->entry(), "GET");
    require(pending->reserve(), "deferred admission reserved"); pending->claimLoader();
    deferred.state->close();
    require(!observe(deferred.state).activeOperations && deferred.state->finishClose(), "close cancels claimed but untransferred reservation");
    require(!pending->reserve() && deferred.provider.releases == 1, "retained deferred cell cannot reopen retired owner");
}
void schedulingFailureRetainsExactPendingOwner() {
    Session session;
    auto task = admitted(session.state);
    LocalResourceState::failNextWakeForTesting();
    require(!session.state->post(task), "actual dispatcher reports failed native wake");
    const auto failed = observe(session.state);
    require(failed.failed == 1 && failed.phase == SouiLocalReady && failed.payloadHeld == 1
        && failed.activeOperations == 1 && failed.queuedOperations == 1
        && !failed.completed && !failed.cancelled, "scheduling failure preserves exact accepted task and lease");
    auto rejected = std::make_shared<Task>(); unsigned int ignored = 0;
    require(!session.state->beginOperation(rejected, ignored), "failed dispatcher rejects new admission");
    pumpBounded(); require(task->runs == 0, "failed scheduling is not inline success/failure callout");
    LocalResourceState::enterPause(); LocalResourceState::leavePause();
    pumpBounded(); require(task->runs == 0, "resume does not reopen failed dispatcher");
    session.state->close();
    const auto cancelled = observe(session.state);
    require(!cancelled.activeOperations && !cancelled.queuedOperations && cancelled.cancelled == 1
        && !cancelled.completed, "explicit native close cancels tracked task once");
    require(session.state->finishClose() && session.provider.releases == 1
        && observe(session.state).failed == 1, "real payload retirement retains failure evidence");
}
void foreignThreadCannotAcquireNativeOwner() {
    Session session;
    int pageIdentity = 0; require(session.state->install(&pageIdentity), "owner Page installed");
    bool rejectedOwner = false, rejectedDispatcher = false, rejectedMapping = false;
    unsigned int status = SouiLocalOk;
    const auto settings = config(session.provider);
    std::thread worker([&] {
        rejectedOwner = !LocalResourceState::create(settings, documentNonce, 6, 8, 10, &status);
        rejectedDispatcher = !LocalResourceState::rejection();
        rejectedMapping = !LocalResourceState::forPage(&pageIdentity);
    });
    worker.join();
    require(rejectedOwner && status == SouiLocalWrongThread && rejectedDispatcher && rejectedMapping,
        "foreign thread cannot mint owner, rejection HWND or Page grant");
    require(session.provider.retains == 1 && !session.provider.releases, "foreign rejection never calls snapshot owner");
}
void byteAndProcessCreditBoundaries() {
    {
        Session session; auto task = admitted(session.state);
        const unsigned int limit = static_cast<unsigned int>(SOUI_LOCAL_RESOURCE_MAX_BORROWED_BYTES);
        require(session.state->borrowBytes(limit), "exact response-byte ledger limit accepted"); task->bytes = limit;
        require(!session.state->borrowBytes(1), "byte limit plus one rejected");
        require(observe(session.state).borrowedBytes == limit && !observe(session.state).failed
            && observe(session.state).denied == 1, "byte capacity rejects request without faulting owner");
        task->cancel(); require(!observe(session.state).borrowedBytes, "cancel returns exact byte credit");
    }
    // These are the actual production admission ledgers with declared metadata
    // from a unit provider, not a 512 MiB payload allocation or browser test.
    struct Batch {
        std::vector<std::unique_ptr<Provider>> providers;
        std::vector<std::shared_ptr<LocalResourceState>> states;
        ~Batch() { for (auto& state : states) { state->close(); if (!state->finishClose()) std::terminate(); } }
        std::shared_ptr<LocalResourceState> add(unsigned int declaredBytes, uint64_t receipt, unsigned int& status) {
            providers.emplace_back(new Provider()); auto settings = config(*providers.back()); settings.totalBytes = declaredBytes;
            auto state = LocalResourceState::create(settings, documentNonce, 5, 7, receipt, &status);
            if (state) states.push_back(state); return state;
        }
    };
    {
        Batch owners; unsigned int status = SouiLocalFailed;
        for (unsigned int i = 0; i < 32; ++i) require(!!owners.add(1, 100 + i, status), "32 native owners accepted");
        require(!owners.add(1, 200, status) && status == SouiLocalLimit, "33rd unreleased owner rejected");
        auto cachedIdentity = owners.states.front(); cachedIdentity->close(); require(cachedIdentity->finishClose(), "first owner really retired");
        require(!!owners.add(1, 201, status) && status == SouiLocalOk, "owner credit reusable while old identity still cached");
    }
    {
        Batch bytes; unsigned int status = SouiLocalFailed;
        for (unsigned int i = 0; i < 4; ++i) require(!!bytes.add(128U * 1024 * 1024, 300 + i, status), "512 MiB aggregate ledger exact");
        require(!bytes.add(1, 400, status) && status == SouiLocalLimit, "aggregate ledger plus one rejected");
        bytes.states.front()->close(); require(bytes.states.front()->finishClose(), "retire one payload ledger owner");
        require(!!bytes.add(1, 401, status) && status == SouiLocalOk, "payload credit returned before identity destruction");
    }
}
}
int main() {
    try {
        namespaceAndClosedLookup(); pauseQueueCancellationAndCredits();
        closeInsideCallbackAndReleaseReentry(); ownerFailureAndOldPageBinding();
        admissionTransferAndDeferredCancellation();
        schedulingFailureRetainsExactPendingOwner();
        foreignThreadCannotAcquireNativeOwner();
        byteAndProcessCreditBoundaries();
        std::cout << "native local resource state behavior passed (not browser/runtime qualification)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
