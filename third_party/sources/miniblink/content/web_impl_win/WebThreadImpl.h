#ifndef WebThreadImpl_h
#define WebThreadImpl_h

#include <vector>
#include <atomic>

#include "base/compiler_specific.h"
#include "third_party/WebKit/public/platform/Platform.h"
#include "third_party/WebKit/public/platform/WebThread.h"
#include "third_party/WebKit/public/platform/WebTraceLocation.h"

namespace blink {

class WebTraceLocation;
class Task;

} // blink

namespace WTF {
class Mutex;
} // WTF

namespace content {

class WebSchedulerImpl;
class WebTimerBase;

class WebThreadImpl : public blink::WebThread {
public:
    explicit WebThreadImpl(const char* name);
    virtual ~WebThreadImpl();
    
    static const int kIdlePriority = 0;
    static const int kLoadingPriority = 100;
    static const int kDefaultPriority = 200;
    static const int kInteractionPriority = 300;

    virtual void postTask(const blink::WebTraceLocation&, blink::WebThread::Task*) override;
    virtual void postDelayedTask(const blink::WebTraceLocation&, blink::WebThread::Task*, long long delayMs) override;
    void postDelayedTaskWithPriorityCrossThread(const blink::WebTraceLocation& location,
        blink::WebThread::Task* task,
        long long delayMs,
        int priority);
    bool hasReadyTaskAtOrAbove(int minimumPriority);

    virtual bool isCurrentThread() const override;
    virtual blink::PlatformThreadId threadId() const override;

    virtual void addTaskObserver(TaskObserver*) override;
    virtual void removeTaskObserver(TaskObserver*) override;

    // Returns the scheduler associated with the thread.
    virtual blink::WebScheduler* scheduler() const override;

    void willExit();

    void startTriggerTasks();
    void schedulerTasks();

    void fire();
    void fireForShutdown();
    
    void suspendTimerQueue();
    void resumeTimerQueue();

    void disableScheduler();
    void enableScheduler();

    std::vector<WebTimerBase*>& timerHeap();
    void updateSharedTimer();
    void appendUnusedTimerToDelete(WebTimerBase* timer) { m_unusedTimersToDelete.push_back(timer); }

    bool threadClosed() { return m_threadClosed.load(std::memory_order_acquire); }

    void shutdown();
    Retirement tryShutdownAndJoin() override;
    bool duplicateShutdownWitness(ShutdownWitness&) override;

    static unsigned getNewCurrentHeapInsertionOrder();

private:
    friend class BlinkPlatformImpl;

    void schedulerTasks(bool allowMainThreadBudget);
    void fireOnExit();
    void waitForExit();
    void fireTimeOnExit();

    struct TaskPair {
        TaskPair(const blink::WebTraceLocation& location, blink::WebThread::Task* task, long long delayMs, int priority);
        ~TaskPair();
        static void sortByPriority(std::vector<TaskPair*>*);

        blink::WebTraceLocation location;
        blink::WebThread::Task* task;
        long long delayMs;
        int priority;
        double createTime;
        unsigned heapInsertionOrder;
    };
    void postDelayedTaskImpl(
        const blink::WebTraceLocation& location, blink::WebThread::Task* task, long long delayMs, double* createTimeOnOtherThread, int priority, unsigned* heapInsertionOrder);
    
    static unsigned __stdcall WebThreadImplThreadEntryPoint(void* param);
    void threadEntryPoint();
    void deleteUnusedTimers();
    void deleteTimersOnExit();
    void deleteTaskPairsToPostOnExit();

    void willProcessTasks();
    void didProcessTasks();
    void clearEmptyObservers();

    HANDLE m_hEvent;
    blink::PlatformThreadId m_threadId;
    std::atomic<bool> m_willExit;
    std::atomic<bool> m_threadClosed;

    int m_firingTimers; // Reentrancy guard.
    WebSchedulerImpl* m_webSchedulerImpl;

    // 不能用wtf的函数，否则退出后wtf被关闭了，就不能访问了
    std::vector<WebTimerBase*> m_timerHeap;
    std::vector<WebTimerBase*> m_unusedTimersToDelete;
    std::vector<TaskPair*> m_taskPairsToPost;
    std::vector<TaskObserver*> m_observers;
    bool m_isObserversDirty;
    const char* m_name;

    CRITICAL_SECTION m_taskPairsMutex;
    CRITICAL_SECTION m_observersMutex;
    bool m_suspendTimerQueue;

    std::atomic<bool> m_hadThreadInit;
    HANDLE m_threadHandle;
    bool m_hasPlatformRegistration;

    double m_currentFrameCreateTime; // 当前帧全部使用这个创建时间
    static unsigned m_currentHeapInsertionOrder;

    bool m_isMainThread;
};

} // namespace content

#endif // WebThreadImpl_h