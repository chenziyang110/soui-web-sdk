
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
#include <exception>
#include <stdexcept>
#include <mmsystem.h>

namespace content {

// 100ms is about a perceptable delay in UI, so use a half of that as a threshold.
// This is to prevent UI freeze when there are too many timers or machine performance is low.
static const double maxDurationOfFiringTimers = 0.050;

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
    , m_currentFrameCreateTime(0)
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
    m_hadThreadInit.store(true, std::memory_order_release);

    while (!m_willExit) {
        DWORD dReturn = ::WaitForSingleObject(m_hEvent, INFINITE);

        startTriggerTasks();

        while (!m_timerHeap.empty() && !m_willExit) {
            schedulerTasks();
            if (m_timerHeap.empty() || m_willExit)
                break;
            // Wait for the actual next deadline rather than imposing a fixed
            // 10ms pause on raster/compositor work. A new task or shutdown
            // signals m_hEvent and interrupts this wait immediately.
            double remainingMs = (m_timerHeap[0]->m_nextFireTime - currentTime()) * 1000.0;
            DWORD waitMs = 1;
            if (remainingMs > 1.0)
                waitMs = remainingMs >= 1000.0 ? 1000 : static_cast<DWORD>(remainingMs + 0.999);
            ::WaitForSingleObject(m_hEvent, waitMs);
            startTriggerTasks();
        }
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
    long long delayMs, double* createTimeOnOtherThread, int priority, unsigned* heapInsertionOrder)
{
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
        timer.reset(WebTimerBase::create(this, location, payload.get(), priority));
        payload.release(); // Timer now owns the task, including shutdown drain.
        timer->startFromOtherThread((double)delayMs / 1000.0, createTimeOnOtherThread, heapInsertionOrder);
        timer.release(); // Timer heap now owns this self-deleting timer.
    } catch (...) {
        // RAII destroys whichever owner actually received the payload.
    }
}

void WebThreadImpl::postDelayedTaskWithPriorityCrossThread(
    const blink::WebTraceLocation& location, 
    blink::WebThread::Task* task,
    long long delayMs,
    int priority)
{
    // WebThread's public contract transfers ownership at post, not at run.
    std::unique_ptr<blink::WebThread::Task> payload(task);
    if (m_willExit) {
        if (m_hEvent)
            ::SetEvent(m_hEvent);
        return;
    }

    if (isCurrentThread()) {
        postDelayedTaskImpl(location, payload.release(), delayMs, &m_currentFrameCreateTime, priority, nullptr);
        return;
    }

    std::unique_ptr<TaskPair> pair;
    try {
        pair.reset(new TaskPair(location, payload.get(), delayMs, priority));
        payload.release();
    } catch (...) { return; }
    ::EnterCriticalSection(&m_taskPairsMutex);
    if (m_willExit) {
        ::LeaveCriticalSection(&m_taskPairsMutex);
        return; // Pair owns the rejected payload outside the critical section.
    }
    try {
        m_taskPairsToPost.push_back(pair.get());
        pair.release();
    } catch (...) {
        ::LeaveCriticalSection(&m_taskPairsMutex);
        return;
    }
    if (m_hEvent)
        ::SetEvent(m_hEvent);
    ::LeaveCriticalSection(&m_taskPairsMutex);
}

void WebThreadImpl::postDelayedTask(const blink::WebTraceLocation& location, blink::WebThread::Task* task, long long delayMs)
{
    postDelayedTaskWithPriorityCrossThread(location, task, delayMs, kDefaultPriority);
}

bool WebThreadImpl::hasReadyTaskAtOrAbove(int minimumPriority)
{
    const double now = currentTime();
    bool hasReadyTask = false;
    ::EnterCriticalSection(&m_taskPairsMutex);
    for (size_t i = 0; i < m_taskPairsToPost.size(); ++i) {
        const TaskPair* pair = m_taskPairsToPost[i];
        if (pair->priority >= minimumPriority
            && pair->createTime + static_cast<double>(pair->delayMs) / 1000.0 <= now) {
            hasReadyTask = true;
            break;
        }
    }
    ::LeaveCriticalSection(&m_taskPairsMutex);
    if (hasReadyTask || !isCurrentThread())
        return hasReadyTask;

    for (size_t i = 0; i < m_timerHeap.size(); ++i) {
        const WebTimerBase* timer = m_timerHeap[i];
        if (timer->m_priority >= minimumPriority
            && timer->m_nextFireTime <= now) {
            return true;
        }
    }
    return false;
}

WebThreadImpl::TaskPair::TaskPair(const blink::WebTraceLocation& location, blink::WebThread::Task* task, long long delayMs, int priority)
{
    this->location = location;
    this->task = task;
    this->delayMs = delayMs;
    this->priority = priority;
    this->createTime = WTF::currentTime();
    this->heapInsertionOrder = WebThreadImpl::getNewCurrentHeapInsertionOrder();
}

WebThreadImpl::TaskPair::~TaskPair()
{
    delete task; // Only untransferred work remains here (cancel-before-run).
}

void WebThreadImpl::TaskPair::sortByPriority(std::vector<WebThreadImpl::TaskPair*>* tasks)
{
    for (size_t i = 0; i < tasks->size(); ++i) {
        for (size_t j = i + 1; j < tasks->size(); ++j) {
            int a = tasks->at(i)->priority;
            int b = tasks->at(j)->priority;
            if (!(a < b))
                continue;

            WebThreadImpl::TaskPair* ptr = tasks->at(i);
            *(&tasks->at(i)) = tasks->at(j);
            *(&tasks->at(j)) = ptr;
        }
    }
}

void WebThreadImpl::startTriggerTasks()
{
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    wke::freeV8TempObejctOnOneFrameBefore();
#endif
    m_currentFrameCreateTime = WTF::currentTime();
    
    while (true) {
        ::EnterCriticalSection(&m_taskPairsMutex);
        if (0 == m_taskPairsToPost.size()) {
            LeaveCriticalSection(&m_taskPairsMutex);
            break;
        }
        
        std::vector<TaskPair*> taskPairsToPostCopy;
        taskPairsToPostCopy.swap(m_taskPairsToPost); // No allocation while locked.
        ::LeaveCriticalSection(&m_taskPairsMutex);

        for (size_t i = 0; i < taskPairsToPostCopy.size(); ++i) {
            TaskPair* taskPair = taskPairsToPostCopy[i];
            blink::WebThread::Task* payload = taskPair->task;
            taskPair->task = nullptr; // Ownership transfers even if admission fails.
            postDelayedTaskImpl(taskPair->location, payload, taskPair->delayMs, &taskPair->createTime, taskPair->priority, &taskPair->heapInsertionOrder);

            delete taskPair;
        }
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

    double fireTime = currentTime();
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
    for (size_t i = 0; i < m_unusedTimersToDelete.size(); ++i) {
        WebTimerBase* timer = m_unusedTimersToDelete[i];

#if 0
        if (1 == timer->refCount()) {
            for (size_t j = 0; j < m_timerHeap.size(); ++j) {
                WebTimerBase* timerOther = m_timerHeap[j];
                if (timerOther == timer)
                    DebugBreak();
            }
        }
#endif
        timer->deref();
    }
    m_unusedTimersToDelete.clear();
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
    --m_firingTimers;
}

void WebThreadImpl::fire()
{
    startTriggerTasks();
    schedulerTasks();
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
    // Do a re-entrancy check.
    if (m_firingTimers > 0 /*|| m_suspendTimerQueue*/) 
        return;
    ++m_firingTimers;

    ASSERT(m_threadId == WTF::currentThread());
    
    deleteUnusedTimers();

    double fireTime = currentTime();
    double timeToQuit = fireTime + maxDurationOfFiringTimers;

#ifdef _DEBUG
    std::vector<WebTimerBase*> dumpTimerHeap = m_timerHeap;
    for (size_t i = 0; i < dumpTimerHeap.size(); ++i) {
        if (!gActivatingTimerCheck->isActivating(dumpTimerHeap[i]))
            DebugBreak();
    }
#endif

    startTriggerTasks(); // 如果不加这句，且下面的循环在本线程不停添加定时器，则startTriggerTasks里的就没机会执行了。
    
    bool hasFire = false;
    while (!m_timerHeap.empty() && (m_timerHeap[0]->m_nextFireTime <= fireTime || m_willExit)) {
        hasFire = true;
        const size_t selectedIndex = task_scheduling::selectReadyTimer(
            m_timerHeap, fireTime, m_willExit, [](const WebTimerBase* value) {
                return task_scheduling::ReadyTask{value->m_nextFireTime, value->m_priority};
            });
        WebTimerBase* timer = m_timerHeap[selectedIndex];
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

        startTriggerTasks();
        fireTime = currentTime();

        // Catch the case where the timer asked timers to fire in a nested event loop, or we are over time limit.
        if (!m_firingTimers || timeToQuit < currentTime())
            break;
    }
    if (!hasFire) {
        willProcessTasks();
        didProcessTasks();
    }
    
    --m_firingTimers;

    updateSharedTimer();
}

void WebThreadImpl::updateSharedTimer()
{
    if (m_isMainThread)
        setSharedTimerFireInterval(0.016);
}

std::vector<WebTimerBase*>& WebThreadImpl::timerHeap()
{
    return m_timerHeap;
}

} // namespace content