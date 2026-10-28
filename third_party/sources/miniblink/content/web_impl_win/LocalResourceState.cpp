#include "LocalResourceState.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace content {
namespace {
const unsigned int maxOwners = 32, maxRejectedOwners = 64, maxOperations = SOUI_LOCAL_RESOURCE_MAX_OPERATIONS;
static_assert(SOUI_LOCAL_RESOURCE_MAX_QUEUED_OPERATIONS == maxOperations,
    "Each admitted operation reserves one first-continuation queue credit");
const uint64_t maxPayload = 512ULL * 1024 * 1024, maxBorrowed = SOUI_LOCAL_RESOURCE_MAX_BORROWED_BYTES;
const UINT resourceWake = WM_APP + 0x31A;
std::mutex registryMutex;
std::array<std::weak_ptr<LocalResourceState>, maxOwners + maxRejectedOwners> owners;
unsigned int payloadOwners = 0, rejectedOwners = 0;
uint64_t payloadBytes = 0;
std::atomic<unsigned int> pauseDepth{0};
std::atomic<DWORD> engineOwnerThread{0};
#if defined(SOUI_LOCAL_RESOURCE_TESTING)
std::atomic<unsigned int> injectedWakeFailures{0};
#endif
bool postResourceWake(HWND window) {
#if defined(SOUI_LOCAL_RESOURCE_TESTING)
    if (injectedWakeFailures.exchange(0)) return false;
#endif
    return PostMessageW(window, resourceWake, 0, 0) != FALSE;
}
bool lowerHex(const char* text, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return false;
    return text[n] == 0;
}
template<size_t N> bool boundedString(const char (&text)[N], std::string& out) {
    const void* end = std::memchr(text, 0, N);
    if (!end) return false;
    out.assign(text, static_cast<const char*>(end) - text); return true;
}
}

struct LocalResourceState::Dispatcher : public std::enable_shared_from_this<Dispatcher> {
    DWORD owner = GetCurrentThreadId();
    std::atomic<HWND> window{nullptr};
    std::weak_ptr<LocalResourceState> state;
    std::shared_ptr<Dispatcher> windowOwner;
    std::array<std::shared_ptr<LocalResourceTask>, maxOperations> queue;
    unsigned int head = 0, count = 0, dispatchDepth = 0;
    bool wakePending = false, closing = false;
    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* raw = reinterpret_cast<Dispatcher*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            raw = static_cast<Dispatcher*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(raw));
        }
        if (!raw) return DefWindowProcW(window, message, w, l);
        const auto retained = raw->windowOwner;
        if (message == WM_CLOSE) return 0;
        if (message == resourceWake) {
            raw->wakePending = false;
            // A consumed wake while paused stays dormant. Only outermost
            // resume posts another wake; there is no polling timer.
            if (LocalResourceState::paused() || raw->closing || !raw->count) return 0;
            auto state = raw->state.lock();
            if (!state) return 0;
            if (state->m_dispatchFailed) return 0;
            auto task = std::move(raw->queue[raw->head]);
            raw->head = (raw->head + 1) % maxOperations; --raw->count;
            ++raw->dispatchDepth;
            { LocalResourceCallbackBorrow borrow(state); if (task) task->run(); }
            --raw->dispatchDepth;
            // The strong state survives the task and all its reentrant
            // cancellation. It may die only after this complete callback.
            if (raw->count && !state->closed() && !raw->wake()) {
                state->m_failed = state->m_dispatchFailed = true;
            }
            return 0;
        }
        if (message == WM_NCDESTROY) {
            raw->window = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            raw->windowOwner.reset();
        }
        return DefWindowProcW(window, message, w, l);
    }
    bool wake() {
        if (closing || !window || wakePending || !count || LocalResourceState::paused()) return true;
        if (auto ownerState = state.lock()) if (ownerState->m_dispatchFailed) return false;
        if (!postResourceWake(window.load())) return false;
        wakePending = true; return true;
    }
    static std::shared_ptr<Dispatcher> create(const std::shared_ptr<LocalResourceState>& state) {
        auto out = std::make_shared<Dispatcher>(); out->state = state;
        WNDCLASSW type = {}; type.lpfnWndProc = proc; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"SOUI.MiniBlink.LocalResourceControl.1";
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
        WNDCLASSW registered = {};
        if (!GetClassInfoW(type.hInstance, type.lpszClassName, &registered)
            || registered.lpfnWndProc != proc) return nullptr;
        out->window = CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, type.hInstance, out.get());
        if (!out->window) return nullptr;
        out->windowOwner = out; return out;
    }
    bool post(const std::shared_ptr<LocalResourceTask>& task) {
        if (owner != GetCurrentThreadId() || closing || !window || count >= maxOperations) return false;
        for (unsigned int i = 0; i < count; ++i) if (queue[(head + i) % maxOperations] == task) return false;
        queue[(head + count) % maxOperations] = task; ++count;
        if (wake()) return true;
        // Failed OS scheduling retains this exact bounded pending task. Native
        // FAILED is observable; only explicit owner close cancels its lease.
        return false;
    }
    void remove(LocalResourceTask* task) {
        std::array<std::shared_ptr<LocalResourceTask>, maxOperations> retained;
        unsigned int n = 0;
        for (unsigned int i = 0; i < count; ++i) {
            auto& entry = queue[(head + i) % maxOperations];
            if (entry.get() != task) retained[n++] = std::move(entry);
            else entry.reset();
        }
        head = 0; count = n; queue = std::move(retained);
    }
    void cancelPending() {
        closing = true;
        std::array<std::shared_ptr<LocalResourceTask>, maxOperations> dropped;
        const unsigned int n = count;
        for (unsigned int i = 0; i < n; ++i) {
            dropped[i] = std::move(queue[head]); head = (head + 1) % maxOperations;
        }
        count = 0;
        // Captured operation destruction can reenter; never hold a registry
        // lock or retain cancelled tasks in another pending queue.
        for (unsigned int i = 0; i < n; ++i) if (dropped[i]) dropped[i]->cancel();
    }
    bool destroy() {
        if (!window) return true;
        if (owner != GetCurrentThreadId() || count || dispatchDepth) return false;
        closing = true;
        return DestroyWindow(window.load()) != FALSE;
    }
};

LocalResourceState::LocalResourceState() : m_ownerThread(GetCurrentThreadId()) { }
bool LocalResourceState::ownerThread() const { return m_ownerThread == GetCurrentThreadId(); }
bool LocalResourceState::initializeDispatcher() {
    m_dispatcher = Dispatcher::create(shared_from_this()); return !!m_dispatcher;
}
std::shared_ptr<LocalResourceState> LocalResourceState::create(const SouiLocalResourceConfig& config,
    const char* nonce, uint64_t viewBirth, uint64_t brokerDocument, uint64_t receipt, unsigned int* failure) {
    if (failure) *failure = SouiLocalInvalid;
    std::string entry;
    if (config.structSize != sizeof(config) || config.abiVersion != SOUI_LOCAL_RESOURCE_ABI
        || !config.fileCount || config.fileCount > 4096 || config.totalBytes > 128U * 1024 * 1024
        || !lowerHex(config.hostNonce, 32) || !lowerHex(config.payloadId, 64)
        || !nonce || !lowerHex(nonce, 32) || !viewBirth || !brokerDocument || !receipt
        || !boundedString(config.entryRelative, entry) || !validRelativePath(entry)
        || config.callbacks.structSize != sizeof(config.callbacks) || config.callbacks.abiVersion != SOUI_LOCAL_RESOURCE_ABI
        || !config.callbacks.owner || !config.callbacks.retain || !config.callbacks.release || !config.callbacks.lookup) return nullptr;
    DWORD expectedOwner = 0;
    engineOwnerThread.compare_exchange_strong(expectedOwner, GetCurrentThreadId());
    if (engineOwnerThread.load() != GetCurrentThreadId()) {
        if (failure) *failure = SouiLocalWrongThread; return nullptr;
    }
    auto out = std::shared_ptr<LocalResourceState>(new LocalResourceState());
    out->m_base = "https://" + std::string(config.hostNonce) + ".soui-local.invalid/" + nonce + "/";
    out->m_entry = out->m_base + entry;
    out->m_viewBirth = viewBirth; out->m_brokerDocument = brokerDocument; out->m_receipt = receipt;
    out->m_payloadBytes = config.totalBytes;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        if (payloadOwners >= maxOwners || payloadBytes > maxPayload - config.totalBytes) {
            if (failure) *failure = SouiLocalLimit; return nullptr;
        }
        auto slot = std::find_if(owners.begin(), owners.end(), [](const std::weak_ptr<LocalResourceState>& p) { return p.expired(); });
        if (slot == owners.end()) { if (failure) *failure = SouiLocalLimit; return nullptr; }
        *slot = out; ++payloadOwners; payloadBytes += config.totalBytes; out->m_budgetHeld = true;
    }
    if (!out->initializeDispatcher()) {
        out->m_phase = SouiLocalPayloadRetired; out->releaseBudget();
        if (failure) *failure = SouiLocalFailed; return nullptr;
    }
    // Retain is a genuine native callout. The broker wraps create in its own
    // counted lease; the local borrow also protects reentrant close.
    out->m_callbacks = config.callbacks;
    out->m_payloadHeld = true;
    { LocalResourceCallbackBorrow borrow(out);
      // The ABI requires nonthrowing owner callbacks. A violation has no
      // trustworthy release witness and must not escape as a leaked success.
      try { out->m_callbacks.retain(out->m_callbacks.owner); } catch (...) { std::terminate(); } }
    if (failure) *failure = SouiLocalOk;
    return out;
}
std::shared_ptr<LocalResourceState> LocalResourceState::rejection() {
    // Workers never acquire a rejection HWND/owner. Their JavaScript entry
    // points reject the namespace before creating any URLLoader.
    if (!engineOwnerThread.load() || engineOwnerThread.load() != GetCurrentThreadId()) return nullptr;
    auto out = std::shared_ptr<LocalResourceState>(new LocalResourceState());
    { std::lock_guard<std::mutex> lock(registryMutex);
      if (rejectedOwners >= maxRejectedOwners) return nullptr;
      auto slot = std::find_if(owners.begin(), owners.end(), [](const std::weak_ptr<LocalResourceState>& p) { return p.expired(); });
      if (slot == owners.end()) return nullptr;
      *slot = out; ++rejectedOwners; out->m_budgetHeld = out->m_rejectionBudget = true; }
    if (!out->initializeDispatcher()) { out->m_phase = SouiLocalPayloadRetired; out->releaseBudget(); return nullptr; }
    return out;
}
void LocalResourceState::releaseBudget() {
    if (!m_budgetHeld) return; // Retired cache identities may die on another thread.
    std::lock_guard<std::mutex> lock(registryMutex);
    for (auto& owner : owners) if (owner.lock().get() == this) owner.reset();
    if (m_rejectionBudget) { if (!rejectedOwners) std::terminate(); --rejectedOwners; }
    else { if (!payloadOwners || payloadBytes < m_payloadBytes) std::terminate(); --payloadOwners; payloadBytes -= m_payloadBytes; }
    m_budgetHeld = false;
}
LocalResourceState::~LocalResourceState() {
    // Real payloads are released only by the explicit counted native drain.
    // Request/cache identity destruction must not invoke an SDK callback.
    if (m_payloadHeld || m_active || m_callbacksActive) std::terminate();
    if (m_dispatcher && !m_dispatcher->destroy()) std::terminate();
    releaseBudget();
}
bool LocalResourceState::validRelativePath(const std::string& path) {
    if (path.empty() || path.size() > 1024 || path.front() == '/' || path.back() == '/') return false;
    size_t begin = 0; unsigned int depth = 0;
    while (begin < path.size()) {
        const auto end = path.find('/', begin); const auto segment = path.substr(begin, end == std::string::npos ? end : end - begin);
        if (++depth > 32 || segment.empty() || segment.size() > 128 || segment == "." || segment == ".."
            || segment.back() == '.' || segment.back() == ' ') return false;
        std::string upper;
        for (unsigned char c : segment) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '_' || c == '-' || c == '.')) return false;
            upper += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
        }
        const auto stem = upper.substr(0, upper.find('.'));
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
            || (stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT") && stem[3] >= '1' && stem[3] <= '9')) return false;
        if (end == std::string::npos) break; begin = end + 1;
    }
    return true;
}
bool LocalResourceState::reservedURL(const std::string& url) {
    const auto scheme = url.find(':');
    if (scheme == std::string::npos || !scheme || url.compare(scheme, 3, "://") != 0) return false;
    std::string protocol;
    for (size_t i = 0; i < scheme; ++i) {
        const unsigned char c = static_cast<unsigned char>(url[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (i && ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')))) return false;
        protocol += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
    }
    if (protocol == "data" || protocol == "blob" || protocol == "javascript" || protocol == "about"
        || protocol == "mailto" || protocol == "tel" || protocol == "urn") return false;
    auto authority = url.substr(scheme + 3, url.find_first_of("/?#", scheme + 3) - (scheme + 3));
    const auto user = authority.rfind('@'); if (user != std::string::npos) authority.erase(0, user + 1);
    authority = authority.substr(0, authority.find(':'));
    for (char& c : authority) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    while (!authority.empty() && authority.back() == '.') authority.pop_back();
    const std::string suffix = ".soui-local.invalid";
    return authority == "soui-local.invalid" || (authority.size() > suffix.size()
        && authority.compare(authority.size() - suffix.size(), suffix.size(), suffix) == 0);
}
std::shared_ptr<LocalResourceState> LocalResourceState::forPage(const void* page) {
    if (!page || engineOwnerThread.load() != GetCurrentThreadId()) return nullptr;
    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto& weak : owners) { auto state = weak.lock(); if (state && state->m_page == page) return state; }
    return nullptr;
}
bool LocalResourceState::install(const void* page) {
    if (!page || !ownerThread() || closed()) return false;
    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto& weak : owners) { auto state = weak.lock(); if (state && state.get() != this && state->m_page == page && !state->retired()) return false; }
    m_page = page; return true;
}
bool LocalResourceState::permitsDocument(const std::string& url, bool initialMainRequest) const {
    if (!ownerThread() || closed()) return false;
    if (initialMainRequest) return url == m_entry;
    return url == m_entry || (url.size() > m_entry.size() && url.compare(0, m_entry.size(), m_entry) == 0 && url[m_entry.size()] == '#');
}
bool LocalResourceState::allowMainNavigation(const std::string& url, bool redirect) {
    if (!ownerThread() || closed() || redirect) { ++m_denied; return false; }
    if (!m_navigationStarted && url == m_entry) { m_navigationStarted = true; return true; }
    if (m_navigationStarted && url.size() > m_entry.size()
        && url.compare(0, m_entry.size(), m_entry) == 0 && url[m_entry.size()] == '#') return true;
    ++m_denied; return false;
}
bool LocalResourceState::permitsURL(const std::string& url) const {
    return ownerThread() && !closed() && !m_base.empty() && url.compare(0, m_base.size(), m_base) == 0
        && validRelativePath(url.substr(m_base.size()));
}
bool LocalResourceState::lookup(const std::string& url, LocalResourceBytes& out) {
    out = {};
    if (!permitsURL(url) || !m_payloadHeld || !m_callbacks.owner) { ++m_denied; return false; }
    const auto path = url.substr(m_base.size());
    uint32_t status;
    try { LocalResourceCallbackBorrow borrow(shared_from_this());
        status = m_callbacks.lookup(m_callbacks.owner, path.data(), static_cast<uint32_t>(path.size()), &out.data, &out.size, &out.mime); }
    catch (...) { fault(); return false; }
    if (closed()) return false;
    if (!status) { out = {}; return true; }
    if (status != 1 || out.size > 16U * 1024 * 1024 || (out.size && !out.data) || out.mime > SouiLocalMp3) { fault(); return false; }
    out.status = 200; return true;
}
bool LocalResourceState::beginOperation(const std::shared_ptr<LocalResourceTask>& task, unsigned int& slot) {
    if (!ownerThread() || closed() || m_dispatchFailed || !task) { ++m_denied; return false; }
    if (m_active >= maxOperations) { ++m_denied; return false; }
    for (unsigned int i = 0; i < maxOperations; ++i) if (m_operations[i].expired()) {
        m_operations[i] = task; slot = i; ++m_active; ++m_started; return true;
    }
    fault(); return false;
}
bool LocalResourceState::borrowBytes(unsigned int bytes) {
    if (!ownerThread() || closed()) { ++m_denied; return false; }
    if (bytes > maxBorrowed || m_borrowedBytes > maxBorrowed - bytes) { ++m_denied; return false; }
    m_borrowedBytes.fetch_add(bytes); return true;
}
bool LocalResourceState::transferOperation(unsigned int slot, LocalResourceTask* reserved,
    const std::shared_ptr<LocalResourceTask>& replacement) {
    if (!ownerThread() || closed() || slot >= maxOperations || !replacement
        || m_operations[slot].lock().get() != reserved) return false;
    m_operations[slot] = replacement; return true;
}
void LocalResourceState::endOperation(unsigned int slot, unsigned int bytes, bool cancelled) {
    if (!ownerThread() || slot >= maxOperations || !m_active || m_borrowedBytes < bytes) std::terminate();
    auto operation = m_operations[slot].lock();
    if (m_dispatcher && operation) m_dispatcher->remove(operation.get());
    m_operations[slot].reset(); m_borrowedBytes.fetch_sub(bytes); --m_active;
    if (cancelled) ++m_cancelled; else ++m_completed;
}
bool LocalResourceState::post(const std::shared_ptr<LocalResourceTask>& task) {
    if (!ownerThread() || closed()) return false;
    const bool posted = m_dispatcher && m_dispatcher->post(task);
    if (!posted) m_failed = m_dispatchFailed = true;
    return posted;
}
void LocalResourceState::enterCallback() {
    if (!ownerThread() || m_callbacksActive.fetch_add(1) >= SOUI_LOCAL_RESOURCE_MAX_CALLBACK_BORROWS) std::terminate();
}
void LocalResourceState::leaveCallback() {
    if (!ownerThread() || !m_callbacksActive.fetch_sub(1)) std::terminate();
}
void LocalResourceState::close() {
    if (!ownerThread()) { m_failed = true; return; }
    unsigned int expected = SouiLocalReady;
    if (m_phase.compare_exchange_strong(expected, SouiLocalClosing)) ++m_closeSequence;
    if (retired()) return;
    if (m_dispatcher) m_dispatcher->cancelPending();
    const auto operations = m_operations;
    for (const auto& weak : operations) if (auto op = weak.lock()) op->cancel();
}
void LocalResourceState::fault() { m_failed = true; close(); }
bool LocalResourceState::finishClose() {
    if (!ownerThread() || !closed() || m_retiring || m_releaseFailed || m_active || m_callbacksActive
        || (m_dispatcher && (m_dispatcher->count || m_dispatcher->dispatchDepth))) return false;
    if (retired()) return true;
    m_retiring = true;
    if (m_dispatcher && !m_dispatcher->destroy()) { m_failed = true; m_retiring = false; return false; }
    // Exchange before external release, retain the callout borrow until it
    // really returns. Recursive queries cannot publish a retired receipt.
    void* owner = std::exchange(m_callbacks.owner, nullptr);
    if (owner) { LocalResourceCallbackBorrow borrow(shared_from_this());
        try { m_callbacks.release(owner); } catch (...) {
            m_failed = m_releaseFailed = true; m_retiring = false; return false;
        } }
    m_payloadHeld = false; m_callbacks = {};
    { std::lock_guard<std::mutex> lock(registryMutex); m_page = nullptr; }
    releaseBudget(); m_phase = SouiLocalPayloadRetired; m_retiring = false;
    return true;
}
void LocalResourceState::snapshot(SouiLocalResourceDrain& out) const {
    out.abiVersion = SOUI_LOCAL_RESOURCE_ABI; out.phase = m_phase; out.failed = m_failed ? 1 : 0;
    out.viewGeneration = m_viewBirth; out.documentReceipt = m_receipt; out.closeSequence = m_closeSequence;
    out.activeOperations = m_active; out.callbackBorrows = m_callbacksActive;
    out.queuedOperations = m_dispatcher ? m_dispatcher->count : 0; out.payloadHeld = m_payloadHeld ? 1 : 0;
    out.borrowedBytes = m_borrowedBytes; out.started = m_started; out.completed = m_completed;
    out.cancelled = m_cancelled; out.denied = m_denied; out.deliveredBytes = m_delivered;
}
void LocalResourceState::receipt(SouiLocalResourceReceipt& out) const {
    out.abiVersion = SOUI_LOCAL_RESOURCE_ABI; out.viewGeneration = m_viewBirth;
    out.boundBrokerDocumentGeneration = m_brokerDocument; out.documentReceipt = m_receipt;
    std::memcpy(out.virtualBase, m_base.c_str(), m_base.size() + 1);
    std::memcpy(out.entryUrl, m_entry.c_str(), m_entry.size() + 1);
}
bool LocalResourceState::paused() { return pauseDepth.load() != 0; }
#if defined(SOUI_LOCAL_RESOURCE_TESTING)
void LocalResourceState::failNextWakeForTesting() { injectedWakeFailures = 1; }
#endif
void LocalResourceState::enterPause() {
    if (pauseDepth.fetch_add(1) == (std::numeric_limits<unsigned int>::max)()) std::terminate();
}
void LocalResourceState::leavePause() {
    const auto previous = pauseDepth.fetch_sub(1); if (!previous) std::terminate();
    if (previous != 1) return;
    std::array<std::shared_ptr<LocalResourceState>, maxOwners + maxRejectedOwners> retained;
    { std::lock_guard<std::mutex> lock(registryMutex); for (unsigned int i = 0; i < retained.size(); ++i) retained[i] = owners[i].lock(); }
    for (const auto& state : retained) if (state && state->m_dispatcher && !state->closed() && !state->m_dispatchFailed) {
        // Every unretired owner is Engine-bound. No worker HWND or foreign
        // last-owner release is admitted into this registry.
        auto& dispatcher = *state->m_dispatcher;
        if (!state->ownerThread()) std::terminate();
        if (!dispatcher.wake()) state->m_failed = state->m_dispatchFailed = true;
    }
}
}
