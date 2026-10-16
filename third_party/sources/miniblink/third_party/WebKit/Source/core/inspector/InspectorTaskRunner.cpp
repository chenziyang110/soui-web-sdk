// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/inspector/InspectorTaskRunner.h"

#include "wtf/Deque.h"
#include "wtf/ThreadingPrimitives.h"
#include <v8.h>

namespace blink {

InspectorTaskRunner::IgnoreInterruptsScope::IgnoreInterruptsScope(InspectorTaskRunner* taskRunner)
    : m_wasIgnoring(taskRunner->m_ignoreInterrupts)
    , m_taskRunner(taskRunner)
{
    // There may be nested scopes e.g. when tasks are being executed on XHR breakpoint.
    m_taskRunner->m_ignoreInterrupts = true;
}

InspectorTaskRunner::IgnoreInterruptsScope::~IgnoreInterruptsScope()
{
    m_taskRunner->m_ignoreInterrupts = m_wasIgnoring;
}

class InspectorTaskRunner::ThreadSafeTaskQueue {
    WTF_MAKE_NONCOPYABLE(ThreadSafeTaskQueue);
public:
    ThreadSafeTaskQueue() : m_closed(false) { }
    PassOwnPtr<Task> tryTake()
    {
        MutexLocker lock(m_mutex);
        if (m_queue.isEmpty())
            return nullptr;
        return m_queue.takeFirst();
    }
    bool append(PassOwnPtr<Task> task)
    {
        MutexLocker lock(m_mutex);
        if (m_closed) return false;
        m_queue.append(task);
        return true;
    }
    void cancel()
    {
        Deque<OwnPtr<Task>> retired;
        { MutexLocker lock(m_mutex); m_closed = true; retired.swap(m_queue); }
        retired.clear(); // Do not run tasks or destroy payloads under queue mutex.
    }
private:
    bool m_closed;
    Mutex m_mutex;
    Deque<OwnPtr<Task>> m_queue;
};


class InspectorTaskRunner::InterruptCapsule final : public WorkerNativeCallbackCapsule {
public:
    InterruptCapsule(InspectorTaskRunner* runner, WorkerShutdownRecord* record) : m_runner(runner), m_record(record) { }
    InspectorTaskRunner* begin()
    {
        MutexLocker lock(m_mutex);
        if (!m_runner || !m_record->beginObserver()) return nullptr;
        return m_runner;
    }
    void finish() { m_record->finishObserver(); scheduleWorkerRetirementPoll(); }
    void retire() { MutexLocker lock(m_mutex); m_runner = nullptr; }
    bool rawTargetRetired() override { MutexLocker lock(m_mutex); return !m_runner; }
private:
    Mutex m_mutex;
    InspectorTaskRunner* m_runner;
    // Record retains this tombstone until native join; capsule does not own record.
    WorkerShutdownRecord* m_record;
};

void InspectorTaskRunner::setWorkerShutdownRecord(PassRefPtr<WorkerShutdownRecord> record)
{
    ASSERT(!m_workerShutdownRecord);
    m_workerShutdownRecord = record;
    m_interruptCapsule = adoptRef(new InterruptCapsule(this, m_workerShutdownRecord.get()));
    m_workerShutdownRecord->retainNativeCapsule(m_interruptCapsule);
}

void InspectorTaskRunner::retiredInterruptCallback(v8::Isolate*, void* data)
{
    // V8 may retain data after the runner's disposal. Only the stable capsule is
    // touched before its cancellation gate; no raw runner on the drop path.
    InterruptCapsule* capsule = static_cast<InterruptCapsule*>(data);
    InspectorTaskRunner* runner = capsule->begin();
    if (!runner) return;
    struct Lease { InterruptCapsule* capsule; ~Lease() { capsule->finish(); } } lease{capsule};
    if (!runner->m_ignoreInterrupts) runner->runPendingTasks();
}

InspectorTaskRunner::InspectorTaskRunner(v8::Isolate* isolate)
    : m_isolate(isolate)
    , m_taskQueue(adoptPtr(new ThreadSafeTaskQueue))
    , m_ignoreInterrupts(false)
{
}

InspectorTaskRunner::~InspectorTaskRunner()
{
}

void InspectorTaskRunner::interruptAndRun(PassOwnPtr<Task> task)
{
    if (m_workerShutdownRecord && !m_workerShutdownRecord->acceptsOrdinary()) return;
    if (m_taskQueue->append(task)) {
        if (m_interruptCapsule) m_isolate->RequestInterrupt(&retiredInterruptCallback, m_interruptCapsule.get());
        else m_isolate->RequestInterrupt(&v8InterruptCallback, this);
    }
}

void InspectorTaskRunner::cancelPendingTasks()
{
    if (m_interruptCapsule) m_interruptCapsule->retire();
    m_taskQueue->cancel();
}

void InspectorTaskRunner::runPendingTasks()
{
    RefPtr<WorkerShutdownRecord> record = m_workerShutdownRecord;
    if (record && !record->beginObserver()) return;
    struct InterruptLease {
        explicit InterruptLease(WorkerShutdownRecord* record) : record(record) { }
        ~InterruptLease() { if (record) record->finishObserver(); }
        RefPtr<WorkerShutdownRecord> record;
    } interruptLease(record.get());
    while (true) {
        OwnPtr<Task> task = m_taskQueue->tryTake();
        if (!task)
            return;
        task->run();
    }
}

void InspectorTaskRunner::v8InterruptCallback(v8::Isolate*, void* data)
{
    InspectorTaskRunner* runner = static_cast<InspectorTaskRunner*>(data);
    if (runner->m_ignoreInterrupts)
        return;
    runner->runPendingTasks();
}

}
