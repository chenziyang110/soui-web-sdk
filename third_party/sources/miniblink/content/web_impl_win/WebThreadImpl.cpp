
#include "config.h"

#include "content/web_impl_win/WebThreadImpl.h"
#include "content/web_impl_win/TaskSchedulingPolicy.h"
#include "content/web_impl_win/BlinkPlatformImpl.h"
#include "content/web_impl_win/WebTimerBase.h"
#include "content/web_impl_win/WebSchedulerImpl.h"
#include "content/web_impl_win/ActivatingTimerCheck.h"
#include "content/browser/SharedTimerWin.h"
#include "third_party/WebKit/public/platform/WebTraceLocation.h"
#include "third_party/WebKit/Source/wtf/ThreadingPrimitives.h"

#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
#include "wke/wkeUtil.h"
#endif

#include "base/compiler_specific.h"
#include "base/thread.h"

#include <windows.h>
#include <process.h>
#include <memory>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <mmsystem.h>

namespace content {

// Cooperative normal turns return after 2ms or32 callbacks; a running JS/GC
// callback cannot be preempted. Explicit platform shutdown keeps its drain path.
static const double maxDurationOfFiringTimers = 0.002;
static const double mainThreadTimerBudget = 0.002;
static const unsigned mainThreadTaskBudget = 32;

namespace {

class WebThreadCriticalSectionOwner {
public:
    explicit WebThreadCriticalSectionOwner(CRITICAL_SECTION* section) : m_section(section)
    {
        if (!::InitializeCriticalSectionAndSpinCount(section, 0))
            throw std::runtime_error("WebThread critical section initialization failed");
    }
    ~WebThreadCriticalSectionOwner() { if (m_section) ::DeleteCriticalSection(m_section); }
    void release() { m_section = nullptr; }
private:
    WebThreadCriticalSectionOwner(const WebThreadCriticalSectionOwner&) = delete;
    WebThreadCriticalSectionOwner& operator=(const WebThreadCriticalSectionOwner&) = delete;
    CRITICAL_SECTION* m_section;
};

static void closeWebThreadHandle(HANDLE handle)
{
    if (handle && !::CloseHandle(handle))
        std::terminate(); // Never certify release after a failed native close.
}

class WebThreadHandleOwner {
public:
    explicit WebThreadHandleOwner(HANDLE handle) : m_handle(handle) { }
    ~WebThreadHandleOwner() { closeWebThreadHandle(m_handle); }
    HANDLE get() const { return m_handle; }
    void release() { m_handle = nullptr; }
private:
    WebThreadHandleOwner(const WebThreadHandleOwner&) = delete;
    WebThreadHandleOwner& operator=(const WebThreadHandleOwner&) = delete;
    HANDLE m_handle;
};

} // namespace

unsigned WebThreadImpl::m_currentHeapInsertionOrder = 0;

unsigned __stdcall WebThreadImpl::WebThreadImplThreadEntryPoint(void* param)
{
    WebThreadImpl* impl = static_cast<WebThreadImpl*>(param);
    try {
        impl->threadEntryPoint();
        return 0;
    } catch (...) {
        // Only the unready, unpublished startup prefix can roll back locally.
        // Arbitrary callback/GC failures after ready have no proven drain path.
        if (impl->m_hadThreadInit.load(std::memory_order_acquire))
            std::terminate();
        return 1;
    }
}

unsigned WebThreadImpl::getNewCurrentHeapInsertionOrder()
{
    return atomicIncrement((volatile int *)&m_currentHeapInsertionOrder);
}

#ifdef _DEBUG
ActivatingTimerCheck* gActivatingTimerCheck = nullptr;
#endif

WebThreadImpl::WebThreadImpl(const char* name)
    : m_hEvent(nullptr)
    , m_threadId(-1)
    , m_willExit(false)
    , m_threadClosed(false)
    , m_firingTimers(0)
    , m_webSchedulerImpl(nullptr)
    , m_isObserversDirty(false)
    , m_name(name)
    , m_suspendTimerQueue(false)
    , m_hadThreadInit(false)
    , m_threadHandle(nullptr)
    , m_hasPlatformRegistration(false)
    , m_isMainThread(0 == strcmp("MainThread", name))
{
    std::unique_ptr<WebSchedulerImpl> scheduler(new WebSchedulerImpl(this));
    WebThreadCriticalSectionOwner taskPairs(&m_taskPairsMutex);
    WebThreadCriticalSectionOwner observers(&m_observersMutex);
    m_webSchedulerImpl = scheduler.get();

    if (m_isMainThread) {
#ifdef _DEBUG
        if (!gActivatingTimerCheck)
            gActivatingTimerCheck = new ActivatingTimerCheck();
#endif
        m_threadId = WTF::currentThread();
        m_hadThreadInit.store(true, std::memory_order_release);
        updateSharedTimer(); // Existing native timer/window transaction remains separate.
    } else {
        WebThreadHandleOwner wakeEvent(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!wakeEvent.get())
            throw std::runtime_error("WebThread event creation failed");
        m_hEvent = wakeEvent.get();
        unsigned threadIdentifier = 0;
        WebThreadHandleOwner worker(reinterpret_cast<HANDLE>(
            _beginthreadex(nullptr, 0, WebThreadImplThreadEntryPoint, this, 0, &threadIdentifier)));
        if (!worker.get())
            throw std::runtime_error("WebThread native launch failed");
        m_threadHandle = worker.get();
        while (!m_hadThreadInit.load(std::memory_order_acquire)) {
            const DWORD observed = ::WaitForSingleObject(worker.get(), 20);
            if (observed == WAIT_OBJECT_0) {
                DWORD exitCode = STILL_ACTIVE;
                if (!::GetExitCodeThread(worker.get(), &exitCode) || exitCode == STILL_ACTIVE)
                    std::terminate();
                // The real native thread has exited. Local owners can now unwind.
                throw std::runtime_error("WebThread exited before initialization");
            }
            if (observed != WAIT_TIMEOUT)
                std::terminate(); // Live worker ownership cannot be discarded on wait failure.
        }
        worker.release();
        wakeEvent.release();
    }
    observers.release();
    taskPairs.release();
    scheduler.release();
}

WebThreadImpl::~WebThreadImpl()
{
    if (m_threadHandle && m_threadId == WTF::currentThread())
        std::terminate(); // Deleting the currently executing worker is never a drain receipt.
    shutdown(); // Worker release follows a real native join, not m_threadClosed polling.

    if (m_hasPlatformRegistration) {
        BlinkPlatformImpl* platform = static_cast<BlinkPlatformImpl*>(blink::Platform::current());
        platform->onThreadExit(this);
        m_hasPlatformRegistration = false;
    }
    closeWebThreadHandle(m_threadHandle);
    m_threadHandle = nullptr;
    closeWebThreadHandle(m_hEvent);
    m_hEvent = nullptr;
    delete m_webSchedulerImpl;
    ::DeleteCriticalSection(&m_taskPairsMutex);
    ::DeleteCriticalSection(&m_observersMutex);
}

void WebThreadImpl::shutdown()
{
    if (m_isMainThread)
        stopSharedTimer();
    willExit();
    waitForExit();
}

bool WebThreadImpl::duplicateShutdownWitness(ShutdownWitness& output)
{
    output = { nullptr, 0, 0 };
    if (m_isMainThread || !m_threadHandle) return false;
    FILETIME created, exited, kernel, user;
    if (!::GetThreadTimes(m_threadHandle, &created, &exited, &kernel, &user)) return false;
    const DWORD id = ::GetThreadId(m_threadHandle);
    HANDLE duplicate = nullptr;
    if (!id || !::DuplicateHandle(::GetCurrentProcess(), m_threadHandle, ::GetCurrentProcess(),
        &duplicate, SYNCHRONIZE | THREAD_QUERY_INFORMATION, FALSE, 0)) return false;
    output.handle = duplicate;
    output.threadId = id;
    output.creationFileTime = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return true; // Original held handle identity, never OpenThread(TID).
}

blink::WebThread::Retirement WebThreadImpl::tryShutdownAndJoin()
{
    if (m_isMainThread || !m_threadHandle || m_threadId == WTF::currentThread())
        return Retirement::Unsupported;
    // No caller registry/state/provider lock is held. The owner and this
    // original HANDLE remain alive for every Pending/Failed result.
    willExit();
    const DWORD observed = ::WaitForSingleObject(m_threadHandle, 0);
    if (observed == WAIT_TIMEOUT) return Retirement::Pending;
    DWORD exitCode = STILL_ACTIVE;
    if (observed != WAIT_OBJECT_0 || !::GetExitCodeThread(m_threadHandle, &exitCode) || exitCode != 0)
        return Retirement::Failed;
    m_threadClosed.store(true, std::memory_order_release);
    return Retirement::Joined;
}

void WebThreadImpl::waitForExit()
{
    if (m_threadId == WTF::currentThread())
        return; // Current-thread shutdown does not claim native worker termination.
    if (!m_threadHandle)
        return; // MainThread owns no spawned native worker.
    const DWORD observed = ::WaitForSingleObject(m_threadHandle, INFINITE);
    DWORD exitCode = STILL_ACTIVE;
    if (observed != WAIT_OBJECT_0 || !::GetExitCodeThread(m_threadHandle, &exitCode)
        || exitCode != 0)
        std::terminate(); // No resource release or success receipt without actual clean exit.
    m_threadClosed.store(true, std::memory_order_release);
}

void WebThreadImpl::willExit()
{
    ::EnterCriticalSection(&m_taskPairsMutex);
    m_willExit = true; // Serialized with final cross-thread admission.
    ::LeaveCriticalSection(&m_taskPairsMutex);
    if (m_hEvent)
        ::SetEvent(m_hEvent);

//     if (0 == strcmp("MainThread", m_name))
//         return;

    if (m_threadId == WTF::currentThread())
        fireOnExit();
}

void WebThreadImpl::threadEntryPoint()
{
    base::SetThreadName(m_name);

    m_threadId = WTF::currentThread();
    BlinkPlatformImpl::onCurrentThreadWhenWebThreadImplCreated(this);
    if (blink::Platform::current()->currentThread() != this)
        throw std::runtime_error("WebThread TLS initialization failed");
    (void)input_frame_trace::register_current_thread();
    struct TraceDetach { ~TraceDetach() { input_frame_trace::detach_current_thread(); } } traceDetach;
    m_hadThreadInit.store(true, std::memory_order_release);

    while (!m_willExit) {
        schedulerTasks();
        if (m_willExit) break;
        DWORD waitMs = INFINITE;
        if (hasPendingTaskAdmission() || !m_unusedTimersToDelete.empty()) waitMs = 0;
        else if (!m_timerHeap.empty()) {
            const double remainingMs = (m_timerHeap[0]->m_nextFireTime - monotonicallyIncreasingTime()) * 1000.0;
            waitMs = remainingMs <= 0 ? 0 : remainingMs >= 1000.0 ? 1000
                : static_cast<DWORD>(ceil(remainingMs));
        }
        ::WaitForSingleObject(m_hEvent, waitMs);
    }

    fireOnExit();
    // waitForExit publishes closure only after the native thread handle is signaled.
}

void WebThreadImpl::postTask(const blink::WebTraceLocation& location, blink::WebThread::Task* task)
{
    postDelayedTask(location, task, 0);
}

void WebThreadImpl::postDelayedTaskImpl(
    const blink::WebTraceLocation& location, blink::WebThread::Task* task, 
    long long delayMs, double* createTimeOnOtherThread, int priority, unsigned* heapInsertionOrder, const input_frame_trace::event* trace, const double* absoluteDeadline)
{
    input_frame_trace::event localTrace;
    if (!trace) {
        (void)input_frame_trace::current_context(localTrace);
        localTrace.thread_id = localTrace.producer_slot = 0;
        localTrace.flags = 0; localTrace.outcome = input_frame_trace::terminal::none;
        localTrace.frame_id = localTrace.action_first = localTrace.action_last = localTrace.value = 0;
        localTrace.related_id = localTrace.task_id;
        localTrace.task_id = input_frame_trace::next_task_id();
    }
    const auto* effectiveTrace = trace ? trace : &localTrace;
    // Consume the posted task exactly once, including failed timer creation.
    std::unique_ptr<blink::WebThread::Task> payload(task);
    std::unique_ptr<WebTimerBase> timer;
    try {
        // Allocate before timer state changes. Its first heap insertion is
        // non-allocating; failed reserve leaves the posted payload owned here.
        const size_t required = m_timerHeap.size();
        const size_t maximum = m_timerHeap.max_size();
        if (required == maximum) throw std::bad_alloc();
        if (m_timerHeap.capacity() < required + 1) {
            const size_t capacity = m_timerHeap.capacity();
            size_t grown = capacity == 0 ? (maximum < 16 ? maximum : 16)
                : (capacity > maximum / 2 ? maximum : capacity * 2);
            if (grown < required + 1) grown = required + 1;
            m_timerHeap.reserve(grown);
        }
        timer.reset(WebTimerBase::create(this, location, payload.get(), priority, effectiveTrace));
        payload.release(); // Timer now owns the task, including shutdown drain.
        if (absoluteDeadline)
            timer->startAt(*absoluteDeadline, heapInsertionOrder);
        else
            timer->startFromOtherThread((double)delayMs / 1000.0, createTimeOnOtherThread, heapInsertionOrder);
        if (!trace) {
            timer->m_trace.point = input_frame_trace::stage::task_posted;
            timer->m_trace.qpc = input_frame_trace::now_qpc();
            (void)input_frame_trace::record(timer->m_trace);
        }
        timer.release(); // Timer heap now owns this self-deleting timer.
    } catch (...) {
        if (!timer) {
            auto cancelled = *effectiveTrace; cancelled.point = input_frame_trace::stage::task_cancelled;
            cancelled.why = input_frame_trace::reason::cancelled; cancelled.qpc = 0;
            (void)input_frame_trace::record(cancelled);
        }
        // RAII destroys whichever owner actually received the payload.
    }
}

void WebThreadImpl::postDelayedTaskWithPriorityCrossThread(
    const blink::WebTraceLocation& location, 
    blink::WebThread::Task* task,
    long long delayMs,
    int priority)
{
    postTaskWithPriorityCrossThread(location, task, delayMs, priority, nullptr);
}

void WebThreadImpl::postTaskAtWithPriorityCrossThread(const blink::WebTraceLocation& location,
    blink::WebThread::Task* task, double deadline, int priority)
{
    if (!(deadline > 0)) {
        postDelayedTaskWithPriorityCrossThread(location, task, 0, priority);
        return;
    }
    if (!std::isfinite(deadline)) {
        std::unique_ptr<blink::WebThread::Task> cancelled(task);
        return; // Positive infinity is consumed outside every producer lock.
    }
    postTaskWithPriorityCrossThread(location, task, 0, priority, &deadline);
}

void WebThreadImpl::postTaskWithPriorityCrossThread(const blink::WebTraceLocation& location,
    blink::WebThread::Task* task, long long delayMs, int priority, const double* absoluteDeadline)
{
    // WebThread's public contract transfers ownership at post, not at run.
    std::unique_ptr<blink::WebThread::Task> payload(task);
    if (m_willExit) {
        if (m_hEvent)
            ::SetEvent(m_hEvent);
        return;
    }

    if (isCurrentThread()) {
        // Relative start still samples its own clock; absolute start retains D.
        postDelayedTaskImpl(location, payload.release(), delayMs, nullptr, priority, nullptr, nullptr, absoluteDeadline);
        if (m_isMainThread && !m_firingTimers && !m_willExit
            && !m_timerHeap.empty() && m_timerHeap[0]->m_nextFireTime <= monotonicallyIncreasingTime())
            requestSharedTimerContinuation();
        return;
    }

    std::unique_ptr<TaskPair> pair;
    try {
        pair.reset(new TaskPair(location, payload.get(), delayMs, priority, absoluteDeadline));
        payload.release();
    } catch (...) { return; }
    ::EnterCriticalSection(&m_taskPairsMutex);
    if (m_willExit) {
        ::LeaveCriticalSection(&m_taskPairsMutex);
        return; // Pair owns the rejected payload outside the critical section.
    }
    try {
        m_pendingIndex.prepare(priority);
        pair->pendingPosition = m_taskPairsToPost.size();
        m_taskPairsToPost.push_back(pair.get());
        m_pendingIndex.insert(pair->scheduling);
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
        pair->task->diagnosticQueueCommitted(); // Actual insertion, still serialized with willExit.
#endif
        pair->trace.point = input_frame_trace::stage::task_posted;
        pair->trace.qpc = input_frame_trace::now_qpc();
        (void)input_frame_trace::record(pair->trace);
        pair.release();
    } catch (...) {
        m_pendingIndex.discardEmpty(priority);
        ::LeaveCriticalSection(&m_taskPairsMutex);
        return;
    }
    if (m_hEvent)
        ::SetEvent(m_hEvent);
    else if (m_isMainThread)
        requestSharedTimerContinuation();
    ::LeaveCriticalSection(&m_taskPairsMutex);
}

void WebThreadImpl::postDelayedTask(const blink::WebTraceLocation& location, blink::WebThread::Task* task, long long delayMs)
{
    postDelayedTaskWithPriorityCrossThread(location, task, delayMs, kDefaultPriority);
}

bool WebThreadImpl::hasReadyTaskAtOrAbove(int minimumPriority)
{
    const double now = monotonicallyIncreasingTime();
    ::EnterCriticalSection(&m_taskPairsMutex);
    const bool pending = m_pendingIndex.hasReadyAtOrAbove(minimumPriority, now);
    ::LeaveCriticalSection(&m_taskPairsMutex);
    return pending || (isCurrentThread() && m_timerIndex.hasReadyAtOrAbove(minimumPriority, now));
}
WebThreadImpl::TaskPair::TaskPair(const blink::WebTraceLocation& location, blink::WebThread::Task* task, long long delayMs, int priority, const double* absoluteDeadline)
{
    this->location = location;
    this->task = task;
    this->delayMs = delayMs;
    this->hasAbsoluteDeadline = absoluteDeadline != nullptr;
    this->priority = priority;
    this->createTime = WTF::monotonicallyIncreasingTime();
    this->heapInsertionOrder = WebThreadImpl::getNewCurrentHeapInsertionOrder();
    (void)input_frame_trace::current_context(this->trace);
    this->trace.thread_id = this->trace.producer_slot = 0;
    this->trace.flags = 0; this->trace.outcome = input_frame_trace::terminal::none;
    this->trace.frame_id = this->trace.action_first = this->trace.action_last = this->trace.value = 0;
    this->trace.related_id = this->trace.task_id;
    this->trace.task_id = input_frame_trace::next_task_id();
    this->pendingPosition = 0;
    this->scheduling.value = this;
    this->scheduling.deadline = absoluteDeadline ? *absoluteDeadline
        : this->createTime + static_cast<double>(delayMs) / 1000.0;
    this->scheduling.priority = priority;
}

WebThreadImpl::TaskPair::~TaskPair()
{
    if (task) {
        trace.point = input_frame_trace::stage::task_cancelled;
        trace.why = input_frame_trace::reason::cancelled;
        trace.qpc = 0;
        (void)input_frame_trace::record(trace);
    }
    delete task; // Only untransferred work remains here (cancel-before-run).
}

void WebThreadImpl::startTriggerTasks()
{
    unsigned remaining = mainThreadTaskBudget;
    startTriggerTasks(m_willExit ? nullptr : &remaining, monotonicallyIncreasingTime() + maxDurationOfFiringTimers);
}

bool WebThreadImpl::hasPendingTaskAdmission()
{
    ::EnterCriticalSection(&m_taskPairsMutex);
    const bool pending = !m_taskPairsToPost.empty();
    ::LeaveCriticalSection(&m_taskPairsMutex);
    return pending;
}

void WebThreadImpl::startTriggerTasks(unsigned* remainingAdmissions, double admissionDeadline)
{
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    wke::freeV8TempObejctOnOneFrameBefore();
#endif
    unsigned workerAdmissions = mainThreadTaskBudget;
    if (!remainingAdmissions && !m_willExit) {
        remainingAdmissions = &workerAdmissions;
        admissionDeadline = monotonicallyIncreasingTime() + maxDurationOfFiringTimers;
    }
    while (!remainingAdmissions || (*remainingAdmissions && monotonicallyIncreasingTime() < admissionDeadline)) {
        TaskPair* selected = nullptr;
        ::EnterCriticalSection(&m_taskPairsMutex);
        // Three eligible priority admissions and one oldest-deadline share.
        // A future flood cannot hide new interaction behind an owner batch;
        // the fourth share still advances background/future registration.
        auto* entry = (++m_admissionShare % 4) ? m_pendingIndex.select(monotonicallyIncreasingTime(), false) : nullptr;
        if (!entry) entry = m_pendingIndex.oldest();
        if (entry) {
            selected = static_cast<TaskPair*>(entry->value);
            m_pendingIndex.erase(*entry);
            const size_t position = selected->pendingPosition;
            if (position + 1 != m_taskPairsToPost.size()) {
                m_taskPairsToPost[position] = m_taskPairsToPost.back();
                m_taskPairsToPost[position]->pendingPosition = position;
            }
            m_taskPairsToPost.pop_back();
        }
        ::LeaveCriticalSection(&m_taskPairsMutex);
        if (!selected) break;
        std::unique_ptr<TaskPair> pair(selected);
        blink::WebThread::Task* payload = pair->task;
        pair->task = nullptr;
        postDelayedTaskImpl(pair->location, payload, pair->delayMs, &pair->createTime, pair->priority, &pair->heapInsertionOrder, &pair->trace,
            pair->hasAbsoluteDeadline ? &pair->scheduling.deadline : nullptr);
        if (remainingAdmissions) --*remainingAdmissions;
    }
}
bool WebThreadImpl::isCurrentThread() const 
{
    return threadId() == WTF::currentThread();
}

blink::PlatformThreadId WebThreadImpl::threadId() const
{
    return m_threadId;
}

static std::vector<WebThreadImpl::TaskObserver*>::iterator findObserver(std::vector<WebThreadImpl::TaskObserver*>& observers, WebThreadImpl::TaskObserver* observer)
{
    for (std::vector<WebThreadImpl::TaskObserver*>::iterator it = observers.begin(); it != observers.end(); ++it) {
        if (*it == observer)
            return it;
    }

    return observers.end();
}

class EmptyTask : public blink::WebThread::Task {
public:
    virtual ~EmptyTask() override {}
    virtual void run() override {};
};

void WebThreadImpl::addTaskObserver(TaskObserver* observer)
{
    ::EnterCriticalSection(&m_observersMutex);
    if (m_observers.end() != findObserver(m_observers, observer)) {
        if (m_hEvent)
            ::SetEvent(m_hEvent);
        ::LeaveCriticalSection(&m_observersMutex);
        return;
    }

    m_observers.push_back(observer);
    ::LeaveCriticalSection(&m_observersMutex);

    double fireTime = monotonicallyIncreasingTime();
    if (!m_timerHeap.empty() && (m_timerHeap[0]->m_nextFireTime <= fireTime)) {
        if (m_hEvent)
            ::SetEvent(m_hEvent);
    } else
        postTask(FROM_HERE, new EmptyTask());

    m_isObserversDirty = true;
}

void WebThreadImpl::removeTaskObserver(TaskObserver* observer)
{
    ::EnterCriticalSection(&m_observersMutex);
    for (size_t i = 0; i < m_observers.size(); ++i) {
        if (observer == m_observers[i])
            m_observers[i] = nullptr;
    }
    ::LeaveCriticalSection(&m_observersMutex);
    m_isObserversDirty = true;
}

void WebThreadImpl::willProcessTasks()
{
    // 有些回调，比如Microtask::enqueueMicrotask，会在退出的时候append进来，需要在最后执行，否则一些ImageLoad没法释放
    for (size_t i = 0; ; ++i) {
        ::EnterCriticalSection(&m_observersMutex);
        if (i >= m_observers.size()) {
            ::LeaveCriticalSection(&m_observersMutex);
            break;
        }
        TaskObserver* observer = m_observers[i];
        ::LeaveCriticalSection(&m_observersMutex);
        if (observer)
            observer->willProcessTask();
    }
    clearEmptyObservers();
}

void WebThreadImpl::didProcessTasks()
{
    for (size_t i = 0; ; ++i) {
        ::EnterCriticalSection(&m_observersMutex);
        if (i >= m_observers.size()) {
            ::LeaveCriticalSection(&m_observersMutex);
            break;
        }
        TaskObserver* observer = m_observers[i];
        ::LeaveCriticalSection(&m_observersMutex);
        if (observer)
            observer->didProcessTask();
    }
    clearEmptyObservers();
}

void WebThreadImpl::clearEmptyObservers()
{
    if (!m_isObserversDirty)
        return;
    m_isObserversDirty = false;

    ::EnterCriticalSection(&m_observersMutex);
    std::vector<WebThreadImpl::TaskObserver*>::iterator it = m_observers.begin();
    for (; it != m_observers.end();) {
        if (nullptr == *it)
            it = m_observers.erase(it);
        else
            ++it;

        if (it == m_observers.end())
            break;
    }
    ::LeaveCriticalSection(&m_observersMutex);
}

blink::WebScheduler* WebThreadImpl::scheduler() const
{
    return m_webSchedulerImpl;
}

void WebThreadImpl::deleteUnusedTimers()
{
    // Shutdown drains the complete retained set. Remove ownership before a
    // destructor can reenter; no provider/producer lock spans deref().
    while (!m_unusedTimersToDelete.empty()) {
        WebTimerBase* timer = m_unusedTimersToDelete.back();
        m_unusedTimersToDelete.pop_back();
        timer->deref();
    }
}

void WebThreadImpl::retireCompletedTasksForOwner()
{
    if (!isCurrentThread() || m_willExit || m_firingTimers || m_retiringCompletedTasks) return;
    m_retiringCompletedTasks = true;
    struct RetirementGuard { bool& active; ~RetirementGuard() { active = false; } } guard = { m_retiringCompletedTasks };
    for (unsigned count = 0; count < 32 && !m_unusedTimersToDelete.empty(); ++count) {
        WebTimerBase* timer = m_unusedTimersToDelete.back();
        m_unusedTimersToDelete.pop_back();
        timer->deref(); // Completed task only, outside callbacks and all locks.
    }
    if (!m_unusedTimersToDelete.empty()) {
        if (m_hEvent) ::SetEvent(m_hEvent);
        else if (m_isMainThread && !requestSharedTimerContinuation()) armSharedTimerRecovery();
    }
}

void WebThreadImpl::deleteTimersOnExit()
{
    for (size_t i = 0; i < m_timerHeap.size(); ++i) {
        WebTimerBase* timer = m_timerHeap[i];
        timer->deref();
    }
    m_timerHeap.clear();
}

void WebThreadImpl::deleteTaskPairsToPostOnExit()
{
    for (size_t i = 0; i < m_taskPairsToPost.size(); ++i) {
        TaskPair* taskPair = m_taskPairsToPost[i];
        m_pendingIndex.erase(taskPair->scheduling);
        delete taskPair;
    }
    m_taskPairsToPost.clear();
}

void WebThreadImpl::suspendTimerQueue()
{
    m_suspendTimerQueue = true;
}

void WebThreadImpl::resumeTimerQueue()
{
    m_suspendTimerQueue = false;
}

void WebThreadImpl::disableScheduler()
{
    ++m_firingTimers;
}

void WebThreadImpl::enableScheduler()
{
    ASSERT(m_firingTimers > 0);
    --m_firingTimers;
    if (!m_firingTimers) updateSharedTimer(); // Reopen after a consumed nested continuation.
}

void WebThreadImpl::fire()
{
    // Normal main-thread admission belongs to the same cooperative budget.
    if (!m_isMainThread)
        startTriggerTasks();
    schedulerTasks();
}

void WebThreadImpl::fireForShutdown()
{
    startTriggerTasks();
    schedulerTasks(false);
}

void WebThreadImpl::fireTimeOnExit()
{
    while (!m_timerHeap.empty()) {
        WebTimerBase* timer = m_timerHeap[0];
        timer->m_nextFireTime = 0;
        timer->heapDeleteMin();

        willProcessTasks();
        timer->fired(); // 可能会append m_timerHeap
        didProcessTasks();
    }
}

void WebThreadImpl::fireOnExit()
{
    startTriggerTasks();
    fireTimeOnExit();
    ASSERT(0 == m_timerHeap.size() && 0 == m_taskPairsToPost.size());

    deleteUnusedTimers();
    deleteTimersOnExit();
    deleteTaskPairsToPostOnExit();
}

void WebThreadImpl::schedulerTasks()
{
    schedulerTasks(true);
}

void WebThreadImpl::schedulerTasks(bool allowMainThreadBudget)
{
    // Do a re-entrancy check.
    if (m_firingTimers > 0 || m_retiringCompletedTasks /*|| m_suspendTimerQueue*/)
        return;
    retireCompletedTasksForOwner();
    ++m_firingTimers;

    ASSERT(m_threadId == WTF::currentThread());

    double fireTime = monotonicallyIncreasingTime();
    double timeToQuit = fireTime + (allowMainThreadBudget ? maxDurationOfFiringTimers : 0.050);
    const double mainThreadTimeToQuit = fireTime + mainThreadTimerBudget;
    unsigned completedTasks = 0;
    unsigned remainingAdmissions = mainThreadTaskBudget;
    const bool boundedTurn = allowMainThreadBudget && !m_willExit;
    const bool boundedMainThread = boundedTurn && m_isMainThread;

#ifdef _DEBUG
    std::vector<WebTimerBase*> dumpTimerHeap = m_timerHeap;
    for (size_t i = 0; i < dumpTimerHeap.size(); ++i) {
        if (!gActivatingTimerCheck->isActivating(dumpTimerHeap[i]))
            DebugBreak();
    }
#endif

    startTriggerTasks(boundedTurn ? &remainingAdmissions : nullptr, mainThreadTimeToQuit); // 如果不加这句，且下面的循环在本线程不停添加定时器，则startTriggerTasks里的就没机会执行了。
    
    fireTime = monotonicallyIncreasingTime();
    bool hasFire = false;
    while (!m_timerHeap.empty() && (m_timerHeap[0]->m_nextFireTime <= fireTime || m_willExit)
        && (!boundedTurn || monotonicallyIncreasingTime() < mainThreadTimeToQuit)) {
        hasFire = true;
        auto* selected = m_timerIndex.select(fireTime, m_willExit);
        ASSERT(selected);
        WebTimerBase* timer = static_cast<WebTimerBase*>(selected->value);
        timer->m_nextFireTime = 0;
        timer->heapDelete();

        double interval = timer->repeatInterval();
        timer->setNextFireTime(interval ? fireTime + interval : 0, nullptr);
#ifdef _DEBUG
        size_t count = gActivatingTimerCheck->count();
        WebTimerBase* timerDump = timer;
        if (!gActivatingTimerCheck->isActivating(timer))
            DebugBreak();
#endif
        // Once the timer has been fired, it may be deleted, so do nothing else with it after this point.
        willProcessTasks();
        timer->fired();
        didProcessTasks();
        ++completedTasks;

        startTriggerTasks(boundedTurn ? &remainingAdmissions : nullptr, mainThreadTimeToQuit);
        fireTime = monotonicallyIncreasingTime();

        // A callback or observer may exceed this cooperative budget. Keep
        // pending timers intact and return to the owner pump between batches.
        const bool mainThreadBudgetReached = allowMainThreadBudget && m_isMainThread && !m_willExit
            && (completedTasks >= mainThreadTaskBudget || mainThreadTimeToQuit < monotonicallyIncreasingTime());
        if (!m_firingTimers || timeToQuit < monotonicallyIncreasingTime() || mainThreadBudgetReached
            || (allowMainThreadBudget && !m_willExit && completedTasks >= mainThreadTaskBudget)) {
            break;
        }
    }
    if (!hasFire) {
        willProcessTasks();
        didProcessTasks();
    }
    
    --m_firingTimers;
    retireCompletedTasksForOwner(); // Last task releases resources without another idle pulse.

    updateSharedTimer();
    if (boundedMainThread && !m_willExit && !m_firingTimers
        && (hasPendingTaskAdmission()
            || (!m_timerHeap.empty() && m_timerHeap[0]->m_nextFireTime <= monotonicallyIncreasingTime())))
        requestSharedTimerContinuation();
}

void WebThreadImpl::serviceOwnerOpportunity()
{
    if (!m_isMainThread || m_willExit || m_firingTimers || m_retiringCompletedTasks || !isCurrentThread() || CheckReEnter::getEnterCount()) return;
    retireCompletedTasksForOwner();
    if (hasPendingTaskAdmission() || (!m_timerHeap.empty() && m_timerHeap[0]->m_nextFireTime <= monotonicallyIncreasingTime())) {
        CheckReEnter::incrementEnterCount();
        sharedTimerFiredFunction();
        CheckReEnter::decrementEnterCount();
    } else serviceSharedTimerOwnerOpportunity();
}

void WebThreadImpl::updateSharedTimer()
{
    if (!m_isMainThread || m_willExit || m_firingTimers) return;
    initializeOffScreenTimerWindow();
    const bool ready = !m_unusedTimersToDelete.empty() || hasPendingTaskAdmission()
        || (!m_timerHeap.empty() && m_timerHeap[0]->m_nextFireTime <= monotonicallyIncreasingTime());
    if (ready) {
        cancelSharedTimerDeadline();
        if (!requestSharedTimerContinuation()) armSharedTimerRecovery();
    } else setSharedTimerFireDeadline(m_timerHeap.empty() ? 0 : m_timerHeap[0]->m_nextFireTime);
}

std::vector<WebTimerBase*>& WebThreadImpl::timerHeap()
{
    return m_timerHeap;
}

} // namespace content