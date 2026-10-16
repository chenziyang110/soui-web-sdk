// Internal shutdown ownership record. No Worker/Document/proxy pointer is stored here.
#ifndef WorkerShutdownRecord_h
#define WorkerShutdownRecord_h
#include "core/CoreExport.h"
#include "core/dom/ExecutionContextTask.h"
#include "wtf/RefPtr.h"
#include "wtf/ThreadSafeRefCounted.h"
#include "wtf/ThreadingPrimitives.h"
#include "wtf/Vector.h"
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#endif


namespace blink {
enum class WorkerRetirementResult { Ready, Pending, Failed, Unsupported };
class WorkerMainCallbackSlot;
CORE_EXPORT void scheduleWorkerRetirementPoll();
class WorkerNativeCallbackCapsule : public ThreadSafeRefCounted<WorkerNativeCallbackCapsule> {
public:
    virtual ~WorkerNativeCallbackCapsule() { }
    virtual bool rawTargetRetired() = 0;
};

#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
// Test-only message cohort. The containing record owns no raw native target.
class WorkerBacklogDiagnostic final {
    typedef std::chrono::steady_clock Clock;
public:
    WorkerBacklogDiagnostic()
        : m_armed(false), m_gateCommitted(false), m_entered(false), m_held(false),
          m_released(false), m_timedOut(false), m_failed(false), m_gateDestroyed(false),
          m_generation(0), m_nativeTid(0), m_gateTid(0), m_nextOrdinal(0),
          m_terminateTransitions(0), m_committed(0), m_pending(0), m_begun(0),
          m_destroyed(0), m_rejected(0), m_committedBits{0, 0},
          m_pendingBits{0, 0}, m_begunBits{0, 0}, m_destroyedBits{0, 0} { }
    bool arm(unsigned generation, unsigned nativeTid, unsigned timeoutMs)
    {
        if (!generation || !nativeTid || !timeoutMs || timeoutMs > 5000) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_armed) return false; // One generation only; repeated arm never resets evidence.
        m_generation = generation; m_nativeTid = nativeTid;
        m_deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        m_armed = true;
        return true;
    }
    bool reserve(unsigned& generation, unsigned& ordinal)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        expireLocked();
        if (!m_armed || m_failed || m_released) return false;
        if (m_nextOrdinal > 128) { failLocked(); return false; }
        generation = m_generation; ordinal = m_nextOrdinal++;
        return true;
    }
    void committed(unsigned generation, unsigned ordinal) noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!validLocked(generation, ordinal)) { failLocked(); return; }
        if (!ordinal) {
            if (m_gateCommitted || m_gateDestroyed) { failLocked(); return; }
            m_gateCommitted = true; return;
        }
        if (has(m_committedBits, ordinal) || has(m_destroyedBits, ordinal)) { failLocked(); return; }
        put(m_committedBits, ordinal); put(m_pendingBits, ordinal);
        ++m_committed; ++m_pending;
    }
    void begun(unsigned generation, unsigned ordinal)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!validLocked(generation, ordinal)) { failLocked(); return; }
        if (!ordinal) {
            if (!m_gateCommitted || m_gateDestroyed) failLocked();
            return;
        }
        if (!has(m_pendingBits, ordinal) || has(m_begunBits, ordinal)
            || has(m_destroyedBits, ordinal)) { failLocked(); return; }
        erase(m_pendingBits, ordinal); --m_pending;
        put(m_begunBits, ordinal); ++m_begun;
    }
    void destroyed(unsigned generation, unsigned ordinal) noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!validLocked(generation, ordinal)) { failLocked(); return; }
        if (!ordinal) {
            if (m_gateDestroyed) { failLocked(); return; }
            m_gateDestroyed = true;
            if (!m_gateCommitted || m_held || !m_entered || !m_released || m_terminateTransitions != 1)
                failLocked();
            return;
        }
        if (has(m_destroyedBits, ordinal)) { failLocked(); return; }
        put(m_destroyedBits, ordinal); ++m_destroyed;
        if (has(m_pendingBits, ordinal)) { erase(m_pendingBits, ordinal); --m_pending; }
        if (!has(m_committedBits, ordinal)) { ++m_rejected; failLocked(); }
    }
    bool enterCallback(unsigned generation, unsigned ordinal, unsigned nativeTid)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        expireLocked();
        if (!validLocked(generation, ordinal) || m_failed || nativeTid != m_nativeTid) {
            failLocked(); return false;
        }
        if (ordinal) return has(m_begunBits, ordinal);
        if (!m_gateCommitted || m_entered || m_gateDestroyed || m_released) { failLocked(); return false; }
        m_gateTid = nativeTid; m_entered = true; m_held = true;
        m_condition.notify_all();
        try {
            if (!m_condition.wait_until(lock, m_deadline, [this] { return m_released || m_failed; })) {
                m_timedOut = true; failLocked();
            }
        } catch (...) { failLocked(); }
        expireLocked();
        return m_released && !m_failed;
    }
    bool waitEntered(unsigned generation, unsigned timeoutMs)
    {
        if (!timeoutMs || timeoutMs > 5000) return false;
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_armed || generation != m_generation) return false;
        expireLocked();
        if (m_failed || m_released) return false;
        const Clock::time_point requested = Clock::now() + std::chrono::milliseconds(timeoutMs);
        const Clock::time_point due = requested < m_deadline ? requested : m_deadline;
        try {
            if (!m_condition.wait_until(lock, due, [this] { return m_entered || m_failed || m_released; }))
                failLocked(); // An entered-wait failure also releases a late worker.
        } catch (...) { failLocked(); }
        expireLocked();
        return m_entered && m_held && !m_failed;
    }
    void terminatedWhileHeld()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        expireLocked();
        if (!m_armed || !m_held) return;
        if (m_failed || !m_entered || m_terminateTransitions || m_nextOrdinal != 129
            || m_committed != 128 || m_pending != 128 || m_begun || m_destroyed || m_rejected) {
            failLocked(); return;
        }
        m_terminateTransitions = 1; // Called only after first actual terminateInternal transition.
    }
    bool release(unsigned generation)
    {
        bool result = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_armed || generation != m_generation) return false;
            expireLocked();
            if (m_released) return !m_failed; // Idempotent release does not erase failure.
            if (!m_failed && m_held && m_entered && m_terminateTransitions == 1
                && m_committed == 128 && m_pending == 128 && !m_begun && !m_destroyed && !m_rejected) {
                m_released = true; m_held = false; result = true;
            } else failLocked(); // Early release frees the worker but cannot pass.
        }
        m_condition.notify_all();
        return result;
    }
    void abort()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_armed && !m_gateDestroyed && !m_released) failLocked();
        }
        m_condition.notify_all();
    }
    void read(unsigned* values)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        expireLocked();
        values[0] = 1; values[1] = m_generation; values[2] = m_gateCommitted;
        values[3] = m_entered; values[4] = m_held; values[5] = m_gateTid;
        values[6] = m_terminateTransitions; values[7] = m_released; values[8] = m_timedOut;
        values[9] = m_nextOrdinal ? m_nextOrdinal - 1 : 0; values[10] = m_committed;
        values[11] = m_pending; values[12] = m_begun; values[13] = m_destroyed;
        values[14] = m_rejected; values[15] = m_gateDestroyed; values[16] = m_failed;
    }
private:
    bool validLocked(unsigned generation, unsigned ordinal) const
    { return m_armed && generation == m_generation && ordinal < m_nextOrdinal && ordinal <= 128; }
    static bool has(const uint64_t* bits, unsigned ordinal)
    { return (bits[(ordinal - 1) / 64] & (uint64_t(1) << ((ordinal - 1) % 64))) != 0; }
    static void put(uint64_t* bits, unsigned ordinal)
    { bits[(ordinal - 1) / 64] |= uint64_t(1) << ((ordinal - 1) % 64); }
    static void erase(uint64_t* bits, unsigned ordinal)
    { bits[(ordinal - 1) / 64] &= ~(uint64_t(1) << ((ordinal - 1) % 64)); }
    void failLocked() { m_failed = true; m_held = false; m_condition.notify_all(); }
    void expireLocked()
    {
        if (m_armed && !m_released && !m_gateDestroyed && !m_failed && Clock::now() >= m_deadline) {
            m_timedOut = true; failLocked();
        }
    }
    std::mutex m_mutex;
    std::condition_variable m_condition;
    Clock::time_point m_deadline;
    bool m_armed, m_gateCommitted, m_entered, m_held, m_released, m_timedOut, m_failed, m_gateDestroyed;
    unsigned m_generation, m_nativeTid, m_gateTid, m_nextOrdinal, m_terminateTransitions;
    unsigned m_committed, m_pending, m_begun, m_destroyed, m_rejected;
    uint64_t m_committedBits[2], m_pendingBits[2], m_begunBits[2], m_destroyedBits[2];
};
#endif

class WorkerShutdownRecord final : public ThreadSafeRefCounted<WorkerShutdownRecord> {
public:
    enum TaskKind { Ordinary, Initialize, Shutdown, Idle, Debugger };
    static PassRefPtr<WorkerShutdownRecord> create() { return adoptRef(new WorkerShutdownRecord); }
    bool admit(TaskKind kind)
    {
        MutexLocker lock(m_mutex);
        if (m_failed || m_scopePrepared || (m_closing && kind != Initialize && kind != Shutdown))
            return false;
        ++m_payloads;
        return true;
    }
    bool begin(TaskKind kind)
    {
        MutexLocker lock(m_mutex);
        if (kind == Shutdown) m_shutdownQueued = false;
        if (m_scopePrepared || (m_closing && kind != Initialize && kind != Shutdown))
            return false; // This check precedes every raw Worker access.
        ++m_active;
        return true;
    }
    void finish(TaskKind kind, bool ran)
    {
        {
            MutexLocker lock(m_mutex);
            ASSERT(m_payloads);
            --m_payloads; // Caller has already destroyed the actual payload.
            if (ran) { ASSERT(m_active); --m_active; }
            if ((kind == Initialize || kind == Shutdown) && !ran && !m_scopePrepared) m_failed = true;
        }
        scheduleWorkerRetirementPoll();
    }
    bool beginObserver()
    {
        MutexLocker lock(m_mutex);
        if (m_scopePrepared) return false;
        ++m_active;
        return true;
    }
    void finishObserver() { MutexLocker lock(m_mutex); ASSERT(m_active); --m_active; }
    void closeAdmission() { MutexLocker lock(m_mutex); m_closing = true; }
    bool closing() { MutexLocker lock(m_mutex); return m_closing; }
    bool acceptsOrdinary() { MutexLocker lock(m_mutex); return !m_closing && !m_scopePrepared && !m_failed; }
    bool soleRawAccess() { MutexLocker lock(m_mutex); return m_active == 1; }
    bool reserveShutdown()
    {
        MutexLocker lock(m_mutex);
        if (!m_closing || !m_initialized || m_scopePrepared || m_shutdownQueued || m_active > 1)
            return false;
        m_shutdownQueued = true;
        return true;
    }
    void initialized() { MutexLocker lock(m_mutex); m_initialized = true; }
    void scopePrepared() { MutexLocker lock(m_mutex); m_scopePrepared = true; m_closing = true; }
    bool rawAccessQuiescent() { MutexLocker lock(m_mutex); return m_scopePrepared && !m_active; }
    bool payloadsRetired() { MutexLocker lock(m_mutex); return !m_payloads; }
    bool failed() { MutexLocker lock(m_mutex); return m_failed; }
    void fail() { MutexLocker lock(m_mutex); m_failed = true; }
    void nativeJoined() { MutexLocker lock(m_mutex); m_joined = true; }
    bool joined() { MutexLocker lock(m_mutex); return m_joined; }
    void retainNativeCapsule(PassRefPtr<WorkerNativeCallbackCapsule> capsule)
    { MutexLocker lock(m_mutex); m_nativeCapsules.append(capsule); }
    bool nativeTargetsRetired()
    {
        Vector<RefPtr<WorkerNativeCallbackCapsule>> capsules;
        { MutexLocker lock(m_mutex); capsules = m_nativeCapsules; }
        for (const auto& capsule : capsules) if (!capsule->rawTargetRetired()) return false;
        return true; // Empty tombstones remain held until native join/record destruction.
    }
    struct Snapshot { unsigned payloads, rawActive, callbackActive, callbacks; bool prepared, joined, failed; };
    Snapshot snapshot() { MutexLocker lock(m_mutex); return { m_payloads, m_active, m_callbackActive,
        static_cast<unsigned>(m_callbacks.size()), m_scopePrepared, m_joined, m_failed }; }

#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    bool canArmBacklogDiagnostic()
    {
        MutexLocker lock(m_mutex);
        return m_initialized && !m_closing && !m_scopePrepared && !m_joined && !m_failed;
    }
    WorkerBacklogDiagnostic& backlogDiagnostic() { return m_backlogDiagnostic; }
#endif

    // Main-thread callback payloads use independent slots. An empty cancelled
    // capsule may survive a suspended Document, but contains no raw target/payload.
    PassRefPtr<WorkerMainCallbackSlot> addCallback(PassOwnPtr<ExecutionContextTask>);
    void removeCallback(WorkerMainCallbackSlot*);
    void cancelCallbacks();
    bool beginCallback() { MutexLocker lock(m_mutex); if (m_callbacksClosed) return false; ++m_callbackActive; return true; }
    void finishCallback() { MutexLocker lock(m_mutex); ASSERT(m_callbackActive); --m_callbackActive; }
    bool callbacksRetired() { MutexLocker lock(m_mutex); return m_callbacks.isEmpty() && !m_callbackActive; }

private:
    WorkerShutdownRecord() : m_payloads(0), m_active(0), m_callbackActive(0),
        m_closing(false), m_initialized(false), m_scopePrepared(false),
        m_shutdownQueued(false), m_joined(false), m_failed(false), m_callbacksClosed(false) { }
    Mutex m_mutex;
    unsigned m_payloads, m_active, m_callbackActive;
    bool m_closing, m_initialized, m_scopePrepared, m_shutdownQueued, m_joined, m_failed, m_callbacksClosed;
    Vector<RefPtr<WorkerMainCallbackSlot>> m_callbacks;
    Vector<RefPtr<WorkerNativeCallbackCapsule>> m_nativeCapsules;
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    WorkerBacklogDiagnostic m_backlogDiagnostic;
#endif
};

class WorkerMainCallbackSlot final : public ThreadSafeRefCounted<WorkerMainCallbackSlot> {
public:
    WorkerMainCallbackSlot(PassRefPtr<WorkerShutdownRecord> record, PassOwnPtr<ExecutionContextTask> payload)
        : m_record(record), m_payload(payload), m_running(false), m_retired(false) { }
    PassOwnPtr<ExecutionContextTask> begin()
    {
        MutexLocker lock(m_mutex);
        if (!m_payload || !m_record->beginCallback()) return nullptr;
        m_running = true;
        return m_payload.release();
    }
    void finish()
    {
        { MutexLocker lock(m_mutex); ASSERT(m_running); m_running = false; }
        m_record->finishCallback();
        retire();
    }
    void retire()
    {
        OwnPtr<ExecutionContextTask> retired;
        {
            MutexLocker lock(m_mutex);
            if (m_running || m_retired) return;
            m_retired = true;
            retired = m_payload.release();
        }
        retired.clear(); // No record/slot/provider lock around payload destruction.
        m_record->removeCallback(this);
        scheduleWorkerRetirementPoll();
    }
private:
    RefPtr<WorkerShutdownRecord> m_record;
    Mutex m_mutex;
    OwnPtr<ExecutionContextTask> m_payload;
    bool m_running, m_retired;
};

inline PassRefPtr<WorkerMainCallbackSlot> WorkerShutdownRecord::addCallback(PassOwnPtr<ExecutionContextTask> task)
{
    RefPtr<WorkerMainCallbackSlot> slot = adoptRef(new WorkerMainCallbackSlot(this, task));
    { MutexLocker lock(m_mutex); m_callbacks.append(slot); }
    // Late posts remain visible to the main-thread cancellation poll. Do not
    // destroy their arbitrary payload under an external provider's mutex.
    if (closing()) scheduleWorkerRetirementPoll();
    return slot.release();
}
inline void WorkerShutdownRecord::removeCallback(WorkerMainCallbackSlot* slot)
{
    MutexLocker lock(m_mutex);
    for (size_t i = 0; i < m_callbacks.size(); ++i) {
        if (m_callbacks[i].get() == slot) { m_callbacks.remove(i); break; }
    }
}
inline void WorkerShutdownRecord::cancelCallbacks()
{
    Vector<RefPtr<WorkerMainCallbackSlot>> slots;
    { MutexLocker lock(m_mutex); m_callbacksClosed = true; slots = m_callbacks; }
    for (const auto& slot : slots) slot->retire();
}

class WorkerCallbackTask final : public ExecutionContextTask {
public:
    explicit WorkerCallbackTask(PassRefPtr<WorkerMainCallbackSlot> slot) : m_slot(slot) { }
    ~WorkerCallbackTask() override { m_slot->retire(); } // Drop never touches a raw owner.
    void performTask(ExecutionContext* context) override
    {
        OwnPtr<ExecutionContextTask> payload = m_slot->begin();
        if (!payload) { m_slot->retire(); return; }
        struct Completion {
            Completion(WorkerMainCallbackSlot* slot, OwnPtr<ExecutionContextTask>& payload) : slot(slot), payload(payload) { }
            ~Completion() { payload.clear(); slot->finish(); }
            WorkerMainCallbackSlot* slot;
            OwnPtr<ExecutionContextTask>& payload;
        } completion(m_slot.get(), payload);
        payload->performTask(context);
    }
private:
    RefPtr<WorkerMainCallbackSlot> m_slot;
};
inline PassOwnPtr<ExecutionContextTask> wrapWorkerCallback(WorkerShutdownRecord* record, PassOwnPtr<ExecutionContextTask> task)
{
    return adoptPtr(new WorkerCallbackTask(record->addCallback(task)));
}
} // namespace blink
#endif