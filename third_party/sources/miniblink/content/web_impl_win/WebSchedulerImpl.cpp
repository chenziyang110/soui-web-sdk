#include "config.h"
#include "base/compiler_specific.h"
#include "third_party/WebKit/public/platform/Platform.h"
#include "third_party/WebKit/public/platform/WebScheduler.h"
#include "third_party/WebKit/public/platform/WebTraceLocation.h"
#include "WebSchedulerImpl.h"
#include "WebThreadImpl.h"
#include "third_party/WebKit/Source/wtf/CurrentTime.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace content {

class IdleTaskToTask : public blink::WebThread::Task {
public:
    IdleTaskToTask(blink::WebThread::IdleTask* task) { m_task = task; }

    virtual ~IdleTaskToTask() override { delete m_task; }

    virtual void run() override
    {
        const double idleBudgetSeconds = 0.008;
        m_task->run(WTF::monotonicallyIncreasingTime()
            + idleBudgetSeconds);
    }
private:
    blink::WebThread::IdleTask* m_task;
};

WebSchedulerImpl::WebSchedulerImpl(WebThreadImpl* thread)
    : m_thread(thread)
{

}

WebSchedulerImpl::~WebSchedulerImpl() { }

void WebSchedulerImpl::shutdown() 
{
}

bool WebSchedulerImpl::shouldYieldForHighPriorityWork()
{
    return m_thread->hasReadyTaskAtOrAbove(
        WebThreadImpl::kDefaultPriority);
}

bool WebSchedulerImpl::canExceedIdleDeadlineIfRequired() { return false; }

void WebSchedulerImpl::postIdleTask(const blink::WebTraceLocation& location, blink::WebThread::IdleTask* task)
{
    m_thread->postDelayedTaskWithPriorityCrossThread(location,
        new IdleTaskToTask(task), 0, WebThreadImpl::kIdlePriority);
}

void WebSchedulerImpl::postNonNestableIdleTask(const blink::WebTraceLocation& location, blink::WebThread::IdleTask* task)
{
    m_thread->postDelayedTaskWithPriorityCrossThread(location,
        new IdleTaskToTask(task), 0, WebThreadImpl::kIdlePriority);
}

void WebSchedulerImpl::postIdleTaskAfterWakeup(const blink::WebTraceLocation& location, blink::WebThread::IdleTask* task)
{ 
    m_thread->postDelayedTaskWithPriorityCrossThread(location,
        new IdleTaskToTask(task), 0, WebThreadImpl::kIdlePriority);
}

void WebSchedulerImpl::postLoadingTask(const blink::WebTraceLocation& location, blink::WebThread::Task* task) 
{
    m_thread->postDelayedTaskWithPriorityCrossThread(location, task, 0,
        WebThreadImpl::kLoadingPriority);
}

void WebSchedulerImpl::postTimerTask(const blink::WebTraceLocation& location, blink::WebThread::Task* task, long long delayMs)
{
    m_thread->postDelayedTask(location, task, delayMs);
}

void WebSchedulerImpl::postTimerTaskAt(const blink::WebTraceLocation& location,
    blink::WebThread::Task* task, double monotonicTime)
{
    m_thread->postTaskAtWithPriorityCrossThread(location, task, monotonicTime,
        WebThreadImpl::kDefaultPriority);
}

void WebSchedulerImpl::suspendTimerQueue() 
{
    m_thread->suspendTimerQueue();
}

void WebSchedulerImpl::resumeTimerQueue() 
{
    m_thread->resumeTimerQueue();
}

void WebSchedulerImpl::postIdleTask(const blink::WebTraceLocation&, PassOwnPtr<IdleTask>) { notImplemented(); }
void WebSchedulerImpl::postNonNestableIdleTask(const blink::WebTraceLocation&, PassOwnPtr<IdleTask>) { notImplemented(); }
void WebSchedulerImpl::postIdleTaskAfterWakeup(const blink::WebTraceLocation&, PassOwnPtr<IdleTask>) { notImplemented(); }
void WebSchedulerImpl::postLoadingTask(const blink::WebTraceLocation&, PassOwnPtr<Task>) { notImplemented(); }

} // content
