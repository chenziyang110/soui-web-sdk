// This translation unit intentionally has no Blink/C++17 dependency. The host
// can compile its boundary checks separately without building the kernel.
#include "InspectorResourceSnapshot.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>

namespace blink {
namespace {
const unsigned int maxOperations = 512;
const UINT resourceWake = WM_APP + 0x319;
bool lowerHex(const char* text, unsigned int length) {
    for (unsigned int i = 0; i < length; ++i)
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return false;
    return text[length] == 0;
}
// This private command permit never changes Page policy or the public ABI.
struct OwnerRetirementPermit;
thread_local OwnerRetirementPermit* ownerRetirementPermit = nullptr;
struct OwnerRetirementPermit {
    InspectorResourceState* const state;
    OwnerRetirementPermit* const previous;
    bool entered = false;
    explicit OwnerRetirementPermit(InspectorResourceState* value)
        : state(value), previous(ownerRetirementPermit) { ownerRetirementPermit = this; }
    ~OwnerRetirementPermit() { ownerRetirementPermit = previous; }
    OwnerRetirementPermit(const OwnerRetirementPermit&) = delete;
    OwnerRetirementPermit& operator=(const OwnerRetirementPermit&) = delete;
};
// Track every dispatcher destroy, including legacy finish-only calls. A nested
// Broker command cannot hide an ancestor frame by installing a fresh permit.
struct DispatcherClosingFrame;
thread_local DispatcherClosingFrame* dispatcherClosingFrame = nullptr;
struct DispatcherClosingFrame {
    const void* const dispatcher; // Comparison only; never a callable or exported capability.
    DispatcherClosingFrame* const previous;
    explicit DispatcherClosingFrame(const void* value)
        : dispatcher(value), previous(dispatcherClosingFrame) { dispatcherClosingFrame = this; }
    ~DispatcherClosingFrame() { dispatcherClosingFrame = previous; }
    DispatcherClosingFrame(const DispatcherClosingFrame&) = delete;
    DispatcherClosingFrame& operator=(const DispatcherClosingFrame&) = delete;
};
bool closingSameDispatcher(const void* dispatcher) {
    for (auto* frame = dispatcherClosingFrame; frame; frame = frame->previous)
        if (frame->dispatcher == dispatcher) return true;
    return false;
}
}

struct InspectorResourceState::Dispatcher : public std::enable_shared_from_this<Dispatcher> {
    DWORD owner = GetCurrentThreadId();
    std::atomic<HWND> window{nullptr};
    std::mutex mutex;
    std::deque<std::function<void()>> queue;
    // Reserved fixed terminal storage: one failure continuation per live
    // operation cannot be crowded out by cancelled ordinary continuations.
    std::array<std::function<void()>, maxOperations> terminalQueue;
    unsigned int terminalHead = 0, terminalCount = 0;
    // A live native window retains its own callback storage. It is released
    // only in WM_NCDESTROY; no queued Win32 callback can use a destroyed owner.
    std::shared_ptr<Dispatcher> windowOwner;
    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
        Dispatcher* self = reinterpret_cast<Dispatcher*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Dispatcher*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, w, l);
        // Only broker drain may destroy this private control window.
        if (message == WM_CLOSE) return 0;
        if (message == resourceWake || (message == WM_TIMER && w == 1)) {
            std::function<void()> task;
            { std::lock_guard<std::mutex> lock(self->mutex);
              if (self->terminalCount) {
                  task = std::move(self->terminalQueue[self->terminalHead]);
                  self->terminalHead = (self->terminalHead + 1) % maxOperations; --self->terminalCount;
              } else if (!self->queue.empty()) { task = std::move(self->queue.front()); self->queue.pop_front(); } }
            // Individual resource continuations own their terminal/cancel guard.
            if (task) { try { task(); } catch (...) { std::terminate(); } }
            return 0;
        }
        if (message == WM_NCDESTROY) {
            auto retained = self->windowOwner;
            self->window = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            self->windowOwner.reset();
            return DefWindowProcW(window, message, w, l);
        }
        return DefWindowProcW(window, message, w, l);
    }
    static std::shared_ptr<Dispatcher> create() {
        auto result = std::make_shared<Dispatcher>();
        WNDCLASSW type = {};
        type.lpfnWndProc = proc; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"SOUI.MiniBlink.Stage0.ResourceControl";
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
        result->window = CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, type.hInstance, result.get());
        if (!result->window) return nullptr;
        result->windowOwner = result;
        // A timer is a reserved wake fallback if the thread's posted-message
        // quota is exhausted. It never runs the business FIFO.
        if (!SetTimer(result->window.load(), 1, 10, nullptr)) { result->destroy(); return nullptr; }
        return result;
    }
    bool post(std::function<void()>&& task, bool terminal) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!window.load()) return false;
        if (terminal) {
            if (terminalCount >= maxOperations) return false;
            terminalQueue[(terminalHead + terminalCount) % maxOperations] = std::move(task); ++terminalCount;
        } else {
            if (queue.size() >= maxOperations) return false;
            queue.push_back(std::move(task));
        }
        PostMessageW(window, resourceWake, 0, 0); // The already installed timer also drains this owned task.
        return true;
    }
    unsigned int pending() {
        std::lock_guard<std::mutex> lock(mutex);
        return static_cast<unsigned int>(queue.size()) + terminalCount;
    }
    bool destroy() {
        // Cover all entries, including destructor and create-failure cleanup.
        // Reject before the window-null fast path or acquiring this mutex.
        if (closingSameDispatcher(this)) return false;
        DispatcherClosingFrame closing(this);
        // Cache-only identities can die on another thread after owner drain.
        if (!window.load()) return true;
        if (owner != GetCurrentThreadId()) return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (!queue.empty() || terminalCount) return false;
        KillTimer(window.load(), 1);
        return DestroyWindow(window.load()) != FALSE;
    }
};

InspectorResourceState::InspectorResourceState(const InspectorSnapshotConfig& config, const char* nonce)
    : m_callbacks(config.callbacks), m_base("https://soui-inspector.invalid/") {
    m_base += config.payloadId; m_base += '/'; m_base += nonce; m_base += '/';
    m_callbacks.retain(m_callbacks.owner);
}
std::shared_ptr<InspectorResourceState> InspectorResourceState::create(
    const InspectorSnapshotConfig& config, const char* nonce) {
    // Do not retain a fresh callback owner from a synchronous dispatcher-close
    // callout: an unsuccessful construction could not safely release it there.
    if (dispatcherClosingFrame) return nullptr;
    if (config.structSize != sizeof(config) || config.abiVersion != 2
        || !config.fileCount || config.fileCount > 1024 || config.totalBytes > 16 * 1024 * 1024
        || !lowerHex(config.payloadId, 64) || !nonce || !lowerHex(nonce, 32)
        || config.callbacks.structSize != sizeof(config.callbacks) || config.callbacks.abiVersion != 2
        || !config.callbacks.owner || !config.callbacks.retain || !config.callbacks.release || !config.callbacks.lookup)
        return nullptr;
    auto result = std::shared_ptr<InspectorResourceState>(new InspectorResourceState(config, nonce));
    result->m_dispatcher = Dispatcher::create();
    if (!result->m_dispatcher) return nullptr;
    return result;
}
InspectorResourceState::~InspectorResourceState() {
    close();
    // Actual operations retain this state. A cache-only identity can be freed
    // after the Page, but the broker has already destroyed its dispatcher.
    if (m_dispatcher && !m_dispatcher->destroy()) std::terminate();
    // No queue or deferred owner exists for a last-owner fallback. If another
    // dispatcher is still closing, fail closed rather than call under its lock.
    if (m_callbacks.owner && dispatcherClosingFrame) std::terminate();
    const auto release = m_callbacks.release;
    void* const owner = std::exchange(m_callbacks.owner, nullptr);
    try { if (owner) release(owner); } catch (...) { std::terminate(); }
}
bool InspectorResourceState::validRelativePath(const std::string& path) {
    if (path.empty() || path.size() > 256 || path.front() == '/' || path.back() == '/') return false;
    unsigned int depth = 0; size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto segment = path.substr(start, end == std::string::npos ? end : end - start);
        if (++depth > 8 || segment.empty() || segment.size() > 128 || segment == "." || segment == ".."
            || segment.back() == '.' || segment.back() == ' ') return false;
        std::string upper;
        for (const unsigned char c : segment) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '_' || c == '-' || c == '.')) return false;
            upper += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
        }
        const auto dot = upper.find('.'); const auto stem = upper.substr(0, dot);
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
            || (stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT")
                && stem[3] >= '1' && stem[3] <= '9')) return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}
bool InspectorResourceState::reservedURL(const std::string& url) {
    return url.find("https://soui-inspector.invalid/") == 0;
}
bool InspectorResourceState::lookup(const std::string& url, InspectorResourceBytes& out) {
    out = {};
    // A last-State destructor callback may reenter a closed raw identity. Deny
    // before shared_from_this, which is not available once final destruction began.
    if (closed()) { ++denied; return false; }
    // Keep both the State and its callback owner live until all lookup output
    // validation is over. Returned bytes still require the caller's outer lease.
    const auto retained = shared_from_this();
    if (!beginOperation()) { ++denied; return false; }
    struct LookupOperation {
        std::shared_ptr<InspectorResourceState> state;
        ~LookupOperation() { state->endOperation(false); }
    } operation{retained};
    if (!m_armed || closed() || url.compare(0, m_base.size(), m_base) != 0) { ++denied; return false; }
    const auto path = url.substr(m_base.size());
    if (!validRelativePath(path)) { ++denied; return false; }
    InspectorResourceBytes found;
    unsigned int status = 0;
    try { status = m_callbacks.lookup(m_callbacks.owner, path.data(), static_cast<unsigned int>(path.size()),
        &found.data, &found.size, &found.mime); } catch (...) { close(); }
    if (status != 1 || found.size > 1024 * 1024 || (found.size && !found.data)
        || found.mime > 12 || closed()) { ++denied; return false; }
    // SVGImage creates a separate Page without the creator policy in this
    // engine. Until that trusted inheritance exists, never create that context.
    if (found.mime == 6) { ++unsupported; ++denied; return false; }
    out = found; ++hits; return true;
}
bool InspectorResourceState::permits(const std::string& url) { InspectorResourceBytes bytes; return lookup(url, bytes); }
bool InspectorResourceState::allowMainNavigation(const std::string& url) {
    if (m_committed || url != entry()) { close(); ++denied; return false; }
    return permits(url);
}
void InspectorResourceState::documentCommitted() { if (m_committed.exchange(true)) close(); }
bool InspectorResourceState::beginOperation() {
    if (closed()) return false;
    auto active = m_active.load();
    do { if (active >= maxOperations) { ++denied; return false; } }
    while (!m_active.compare_exchange_weak(active, active + 1));
    if (closed()) { --m_active; return false; }
    ++started; return true;
}
void InspectorResourceState::endOperation(bool wasCancelled) {
    if (!m_active.fetch_sub(1)) std::terminate();
    if (wasCancelled) ++cancelled; else ++completed;
}
void InspectorResourceState::enterCallback() {
    if (m_callbackActive.fetch_add(1) >= maxOperations) std::terminate();
}
void InspectorResourceState::leaveCallback() {
    if (!m_callbackActive.fetch_sub(1)) std::terminate();
}
bool InspectorResourceState::post(std::function<void()>&& task, bool terminal) {
    try { return m_dispatcher && m_dispatcher->post(std::move(task), terminal); } catch (...) { return false; }
}
bool InspectorResourceState::finishCloseOnOwner() {
    auto* const permit = ownerRetirementPermit && ownerRetirementPermit->state == this
        && !ownerRetirementPermit->entered ? ownerRetirementPermit : nullptr;
    if (permit) permit->entered = true;
    if (!closed() || active() || !m_dispatcher) return false;
    const bool destroyed = m_dispatcher->destroy();
    if (!destroyed) return false;
    // Ordinary Page-policy finish remains dispatcher-only. A one-use permit is
    // issued solely by the counted Broker command below, never by this method.
    if (!permit) return true;
    if (dispatcherClosingFrame || m_dispatcher->owner != GetCurrentThreadId()
        || !closed() || active() || m_dispatcher->window.load()) return false;
    const auto release = m_callbacks.release;
    void* const owner = std::exchange(m_callbacks.owner, nullptr);
    try { if (owner) release(owner); } catch (...) { std::terminate(); }
    return true;
}
// Private, nonexported command. The bool result is admission, not drain status;
// preserve callers that previously ignored a pending finish while cancelling.
bool finishAndRetireInspectorResourceOwner(const std::shared_ptr<InspectorResourceState>& state,
    bool& finished) {
    finished = false;
    const auto retained = state;
    if (!retained || dispatcherClosingFrame) return false;
    OwnerRetirementPermit permit(retained.get());
    finished = retained->finishCloseOnOwner();
    return true;
}
unsigned int InspectorResourceState::queued() const { return m_dispatcher ? m_dispatcher->pending() : 0; }
bool InspectorResourceState::dispatcherAlive() const { return m_dispatcher && m_dispatcher->window; }
} // namespace blink
