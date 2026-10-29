#ifndef CONTENT_COMMON_PAINT_LIFETIME_H_
#define CONTENT_COMMON_PAINT_LIFETIME_H_

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace blink { class WebThread; }
namespace content {

enum class PaintCloseKind { none, view, window, direct };
class PaintLifetimePolicy {
public:
    bool enter()
    {
        if (sealed || dead || active == (std::numeric_limits<uint64_t>::max)())
            return false;
        ++active;
        return true;
    }
    bool leave()
    {
        if (!active)
            return false;
        --active;
        return !active && pending != PaintCloseKind::none && !queued;
    }
    bool request(PaintCloseKind kind)
    {
        if (dead || kind == PaintCloseKind::none || (callouts & mask(kind))
            || (nativeFailed && !nativeRetryable))
            return false;
        sealed = true;
        if (active) {
            if (pending == PaintCloseKind::none || kind == PaintCloseKind::view
                || (pending == PaintCloseKind::window && kind == PaintCloseKind::direct))
                pending = kind;
            return false;
        }
        pending = PaintCloseKind::none;
        callouts |= mask(kind);
        return true;
    }
    uint64_t reserveTask()
    {
        if (dead || active || nativeFailed || pending == PaintCloseKind::none || queued
            || serial == (std::numeric_limits<uint64_t>::max)())
            return 0;
        return queued = ++serial;
    }
    void disposeTask(uint64_t ticket) { if (ticket == queued) queued = 0; }
    bool ready() const { return !dead && !active && pending != PaintCloseKind::none; }
    void finish(PaintCloseKind kind) { callouts &= ~mask(kind); }
    void invalidate() { dead = sealed = true; pending = PaintCloseKind::none; }
    bool beginNativeWindow(uintptr_t window)
    {
        if (nativeFailed && nativeWindow != window)
            return false;
        nativeWindow = window;
        nativeDestroyed = false;
        return true;
    }
    bool noteNativeDestroyed(uintptr_t window)
    {
        if (!window || nativeWindow != window)
            return false;
        nativeDestroyed = true;
        nativeFailed = nativeRetryable = false;
        if (pending == PaintCloseKind::window)
            pending = PaintCloseKind::none;
        return true;
    }
    void noteNativeFailure(bool retryable)
    {
        nativeFailed = true;
        nativeDestroyed = false;
        nativeRetryable = retryable;
        sealed = true;
        if (!dead)
            pending = PaintCloseKind::window;
    }
    bool takeDestroyCallback()
    {
        if (dead || destroyCallbackDelivered)
            return false;
        destroyCallbackDelivered = true;
        return true;
    }
    static unsigned mask(PaintCloseKind kind) { return kind == PaintCloseKind::window ? 2U : 1U; }
    uint64_t active = 0, serial = 0, queued = 0;
    PaintCloseKind pending = PaintCloseKind::none;
    unsigned callouts = 0;
    bool sealed = false, dead = false;
    uintptr_t nativeWindow = 0;
    bool nativeDestroyed = false, nativeFailed = false, nativeRetryable = false;
    bool destroyCallbackDelivered = false;
};

struct PaintLifetimeState {
    PaintLifetimePolicy policy;
    void* target = nullptr;
    blink::WebThread* owner = nullptr;
    void (*onIdle)(const std::shared_ptr<PaintLifetimeState>&) = nullptr;
};
struct PaintLifetimeRuntime {
    std::mutex mutex;
    uint64_t active = 0, pending = 0, queued = 0, callouts = 0, nativeFailures = 0;
    bool closing = false;
    bool drained() const { return !active && !pending && !queued && !callouts && !nativeFailures; }
};
template<class Key> class PaintCloseServiceCursor {
public:
    bool enterService() { if (m_servicing) return false; m_servicing = true; return true; }
    void leaveService() { m_servicing = false; }
    template<class Set> Key next(const Set& views)
    {
        if (views.empty())
            return Key{};
        auto position = views.upper_bound(m_previous);
        if (position == views.end())
            position = views.begin();
        return m_previous = *position;
    }
private:
    Key m_previous{}; // Reconstructed lookup key only; never a retained iterator.
    bool m_servicing = false;
};
inline PaintLifetimeRuntime& paintLifetimeRuntime()
{
    static PaintLifetimeRuntime value;
    return value;
}
class PaintActivityScope {
public:
    PaintActivityScope() = default;
    explicit PaintActivityScope(const std::shared_ptr<PaintLifetimeState>& state) : m_state(state)
    {
        auto& runtime = paintLifetimeRuntime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        if (state && !runtime.closing
            && runtime.active != (std::numeric_limits<uint64_t>::max)() && state->policy.enter()) {
            ++runtime.active;
            m_entered = true;
        }
    }
    ~PaintActivityScope()
    {
        if (!m_entered)
            return;
        bool notify;
        {
            auto& runtime = paintLifetimeRuntime();
            std::lock_guard<std::mutex> lock(runtime.mutex);
            notify = m_state->policy.leave();
            --runtime.active;
        }
        if (notify && m_state->onIdle)
            m_state->onIdle(m_state);
    }
    explicit operator bool() const { return m_entered; }
    PaintActivityScope(PaintActivityScope&& other) noexcept
        : m_state(std::move(other.m_state)), m_entered(other.m_entered)
    { other.m_entered = false; }
    PaintActivityScope& operator=(PaintActivityScope&& other) noexcept
    {
        if (this != &other) {
            PaintActivityScope previous(std::move(*this));
            m_state = std::move(other.m_state);
            m_entered = other.m_entered;
            other.m_entered = false;
        }
        return *this;
    }
    PaintActivityScope(const PaintActivityScope&) = delete;
    PaintActivityScope& operator=(const PaintActivityScope&) = delete;
private:
    std::shared_ptr<PaintLifetimeState> m_state;
    bool m_entered = false;
};

class PaintClosePermit {
public:
    PaintClosePermit(const std::shared_ptr<PaintLifetimeState>& state, PaintCloseKind kind)
        : m_state(state), m_kind(kind)
    {
        auto& runtime = paintLifetimeRuntime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        const bool wasPending = state->policy.pending != PaintCloseKind::none;
        m_entered = state->policy.request(kind);
        const bool pending = state->policy.pending != PaintCloseKind::none;
        if (pending != wasPending) {
            if (pending) ++runtime.pending;
            else --runtime.pending;
        }
        if (m_entered)
            ++runtime.callouts;
    }
    ~PaintClosePermit()
    {
        if (!m_entered)
            return;
        auto& runtime = paintLifetimeRuntime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        m_state->policy.finish(m_kind);
        --runtime.callouts;
    }
    explicit operator bool() const { return m_entered; }
    PaintClosePermit(PaintClosePermit&& other) noexcept
        : m_state(std::move(other.m_state)), m_kind(other.m_kind), m_entered(other.m_entered)
    { other.m_entered = false; }
    PaintClosePermit(const PaintClosePermit&) = delete;
    PaintClosePermit& operator=(const PaintClosePermit&) = delete;
private:
    std::shared_ptr<PaintLifetimeState> m_state;
    PaintCloseKind m_kind;
    bool m_entered = false;
};
} // namespace content
#endif
