/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "config.h"

#include "core/workers/WorkerThread.h"

#include "bindings/core/v8/ScriptSourceCode.h"
#include "bindings/core/v8/V8GCController.h"
#include "bindings/core/v8/V8Initializer.h"
#include "core/dom/Microtask.h"
#include "core/inspector/InspectorInstrumentation.h"
#include "core/inspector/WorkerInspectorController.h"
#include "core/workers/DedicatedWorkerGlobalScope.h"
#include "core/workers/WorkerClients.h"
#include "core/workers/WorkerReportingProxy.h"
#include "core/workers/WorkerThreadStartupData.h"
#include "platform/Task.h"
#include "platform/ThreadSafeFunctional.h"
#include "platform/heap/SafePoint.h"
#include "platform/heap/ThreadState.h"
#include "platform/weborigin/KURL.h"
#include "public/platform/Platform.h"
#include "public/platform/WebScheduler.h"
#include "public/platform/WebThread.h"
#include "public/platform/WebWaitableEvent.h"
#include "wtf/Noncopyable.h"
#include "wtf/WeakPtr.h"
#include "wtf/text/WTFString.h"

namespace blink {

namespace {
const double kLongIdlePeriodSecs = 1.0;

} // namespace

#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
namespace {
struct DiagnosticMessageContext {
    WorkerShutdownRecord* record; unsigned generation, ordinal;
};
thread_local DiagnosticMessageContext diagnosticMessageContext = { nullptr, 0, 0 };
class ScopedDiagnosticMessage final {
public:
    ScopedDiagnosticMessage(WorkerShutdownRecord* record, unsigned generation, unsigned ordinal)
        : m_previous(diagnosticMessageContext)
    { diagnosticMessageContext = { record, generation, ordinal }; }
    ~ScopedDiagnosticMessage() { diagnosticMessageContext = m_previous; }
private:
    DiagnosticMessageContext m_previous;
};
} // namespace
#endif

class TrackedWorkerTask final : public WebThread::Task {
public:
    TrackedWorkerTask(WorkerThread* worker, PassOwnPtr<WebThread::Task> task, WorkerShutdownRecord::TaskKind kind
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
        , unsigned generation = 0, unsigned ordinal = 0
#endif
        )
        : m_worker(worker), m_record(worker->shutdownRecord()), m_task(task), m_kind(kind),
          m_admitted(m_record->admit(kind)), m_running(false)
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
          , m_diagnosticGeneration(generation), m_diagnosticOrdinal(ordinal), m_diagnosticDestroyed(false)
#endif
    { if (!m_admitted) m_task.clear();
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
        if (!m_admitted) retireDiagnosticPayload();
#endif
    }
    ~TrackedWorkerTask() override
    {
        m_task.clear();
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
        retireDiagnosticPayload();
#endif
        if (m_admitted) m_record->finish(m_kind, m_running);
    }
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    void diagnosticQueueCommitted() noexcept override
    {
        if (m_diagnosticGeneration && m_admitted)
            m_record->backlogDiagnostic().committed(m_diagnosticGeneration, m_diagnosticOrdinal);
    }
#endif
    void run() override
    {
        if (!m_admitted || !m_record->begin(m_kind)) return;
        m_running = true;
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
        if (m_diagnosticGeneration)
            m_record->backlogDiagnostic().begun(m_diagnosticGeneration, m_diagnosticOrdinal);
        {
            ScopedDiagnosticMessage context(m_diagnosticGeneration ? m_record.get() : nullptr,
                m_diagnosticGeneration, m_diagnosticOrdinal);
#endif
        m_task->run();
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
        }
#endif
        m_task.clear();
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
        retireDiagnosticPayload();
#endif
        // The raw lease remains held through the final access to the Worker.
        m_worker->afterTrackedTask();
    }
private:
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    void retireDiagnosticPayload() noexcept
    {
        if (m_diagnosticGeneration && !m_diagnosticDestroyed) {
            m_diagnosticDestroyed = true;
            m_record->backlogDiagnostic().destroyed(m_diagnosticGeneration, m_diagnosticOrdinal);
        }
    }
#endif
    WorkerThread* m_worker;
    RefPtr<WorkerShutdownRecord> m_record;
    OwnPtr<WebThread::Task> m_task;
    WorkerShutdownRecord::TaskKind m_kind;
    bool m_admitted, m_running;
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    unsigned m_diagnosticGeneration, m_diagnosticOrdinal;
    bool m_diagnosticDestroyed;
#endif
};

class TrackedWorkerIdleTask final : public WebThread::IdleTask {
public:
    TrackedWorkerIdleTask(WorkerThread* worker, PassOwnPtr<Function<void(double)>> task)
        : m_worker(worker), m_record(worker->shutdownRecord()), m_task(task),
          m_admitted(m_record->admit(WorkerShutdownRecord::Idle)), m_running(false) { if (!m_admitted) m_task.clear(); }
    ~TrackedWorkerIdleTask() override
    {
        m_task.clear();
        if (m_admitted) m_record->finish(WorkerShutdownRecord::Idle, m_running);
    }
    void run(double deadline) override
    {
        if (!m_admitted || !m_record->begin(WorkerShutdownRecord::Idle)) return;
        m_running = true;
        (*m_task)(deadline);
        m_task.clear();
        m_worker->afterTrackedTask();
    }
private:
    WorkerThread* m_worker;
    RefPtr<WorkerShutdownRecord> m_record;
    OwnPtr<Function<void(double)>> m_task;
    bool m_admitted, m_running;
};

class WorkerMicrotaskRunner : public WebThread::TaskObserver {
public:
    explicit WorkerMicrotaskRunner(WorkerThread* workerThread)
        : m_workerThread(workerThread)
    {
    }

    virtual void willProcessTask() override
    {
        if (m_workerThread->managedShutdown())
            return; // Closing tasks are cancellation/control payloads, not JS work.
        // No tasks should get executed after we have closed.
        WorkerGlobalScope* globalScope = m_workerThread->workerGlobalScope();
        ASSERT_UNUSED(globalScope, !globalScope || !globalScope->isClosing());
    }

    virtual void didProcessTask() override
    {
        RefPtr<WorkerShutdownRecord> record = m_workerThread->managedShutdown() ? m_workerThread->shutdownRecord() : nullptr;
        if (record && !record->beginObserver()) return;
        struct ObserverLease {
            ObserverLease(WorkerThread* worker, WorkerShutdownRecord* record) : worker(worker), record(record) { }
            ~ObserverLease() { if (record) { record->finishObserver(); if (!record->rawAccessQuiescent() && record->closing()) worker->postShutdownTask(); } }
            WorkerThread* worker;
            RefPtr<WorkerShutdownRecord> record;
        } observerLease(m_workerThread, record.get());
        if (record && record->closing()) {
            m_workerThread->postShutdownTask();
            return;
        }
        Microtask::performCheckpoint(m_workerThread->isolate());
        if (WorkerGlobalScope* globalScope = m_workerThread->workerGlobalScope()) {
            if (WorkerScriptController* scriptController = globalScope->script())
                scriptController->rejectedPromises()->processQueue();
            if (globalScope->isClosing()) {
                m_workerThread->workerReportingProxy().workerGlobalScopeClosed();
                if (record) {
                    record->closeAdmission();
                    m_workerThread->postShutdownTask();
                } else m_workerThread->shutdown();
            }
        }
    }

private:
    // Thread owns the microtask runner; reference remains
    // valid for the lifetime of this object.
    WorkerThread* m_workerThread;
};

static Mutex& threadSetMutex()
{
    AtomicallyInitializedStaticReference(Mutex, mutex, new Mutex);
    return mutex;
}

static HashSet<WorkerThread*>& workerThreads()
{
    DEFINE_STATIC_LOCAL(HashSet<WorkerThread*>, threads, ());
    return threads;
}

unsigned WorkerThread::workerThreadCount()
{
    MutexLocker lock(threadSetMutex());
    return workerThreads().size();
}

class WorkerThreadTask : public WebThread::Task {
    WTF_MAKE_NONCOPYABLE(WorkerThreadTask); WTF_MAKE_FAST_ALLOCATED(WorkerThreadTask);
public:
    static PassOwnPtr<WorkerThreadTask> create(WorkerThread& workerThread, PassOwnPtr<ExecutionContextTask> task, bool isInstrumented)
    {
        return adoptPtr(new WorkerThreadTask(workerThread, task, isInstrumented));
    }

    virtual ~WorkerThreadTask() { }

    virtual void run() override
    {
        WorkerGlobalScope* workerGlobalScope = m_workerThread.workerGlobalScope();
        // If the thread is terminated before it had a chance initialize (see
        // WorkerThread::Initialize()), we mustn't run any of the posted tasks.
        if (!workerGlobalScope) {
            ASSERT(m_workerThread.terminated());
            return;
        }

        if (m_isInstrumented)
            InspectorInstrumentation::willPerformExecutionContextTask(workerGlobalScope, m_task.get());
        m_task->performTask(workerGlobalScope);
        if (m_isInstrumented)
            InspectorInstrumentation::didPerformExecutionContextTask(workerGlobalScope);
    }

private:
    WorkerThreadTask(WorkerThread& workerThread, PassOwnPtr<ExecutionContextTask> task, bool isInstrumented)
        : m_workerThread(workerThread)
        , m_task(task)
        , m_isInstrumented(isInstrumented)
    {
        if (m_isInstrumented)
            m_isInstrumented = !m_task->taskNameForInstrumentation().isEmpty();
        if (m_isInstrumented)
            InspectorInstrumentation::didPostExecutionContextTask(m_workerThread.workerGlobalScope(), m_task.get());
    }

    WorkerThread& m_workerThread;
    OwnPtr<ExecutionContextTask> m_task;
    bool m_isInstrumented;
};

WorkerThread::WorkerThread(PassRefPtr<WorkerLoaderProxy> workerLoaderProxy, WorkerReportingProxy& workerReportingProxy)
    : m_shutdownRecord(WorkerShutdownRecord::create())
    , m_managedShutdown(false)
    , m_started(false)
    , m_terminated(false)
    , m_shutdown(false)
    , m_workerLoaderProxy(workerLoaderProxy)
    , m_workerReportingProxy(workerReportingProxy)
    , m_webScheduler(nullptr)
    , m_isolate(nullptr)
    , m_shutdownEvent(adoptPtr(Platform::current()->createWaitableEvent()))
    , m_terminationEvent(adoptPtr(Platform::current()->createWaitableEvent()))
{
    MutexLocker lock(threadSetMutex());
    workerThreads().add(this);
}

WorkerThread::~WorkerThread()
{
    MutexLocker lock(threadSetMutex());
    ASSERT(workerThreads().contains(this));
    workerThreads().remove(this);
}

void WorkerThread::start(PassOwnPtr<WorkerThreadStartupData> startupData)
{
    ASSERT(isMainThread());

    if (m_started)
        return;

    m_started = true;
    backingThread().postTask(FROM_HERE, wrapRawTask(adoptPtr(new Task(threadSafeBind(&WorkerThread::initialize, AllowCrossThreadAccess(this), startupData))), WorkerShutdownRecord::Initialize));
}

void WorkerThread::interruptAndDispatchInspectorCommands()
{
    MutexLocker locker(m_workerInspectorControllerMutex);
    if ((!m_managedShutdown || m_shutdownRecord->acceptsOrdinary()) && m_workerInspectorController)
        m_workerInspectorController->interruptAndDispatchInspectorCommands();
}

PlatformThreadId WorkerThread::platformThreadId()
{
    if (!m_started)
        return 0;
    return backingThread().platformThread().threadId();
}

void WorkerThread::initialize(PassOwnPtr<WorkerThreadStartupData> startupData)
{
    KURL scriptURL = startupData->m_scriptURL;
    String sourceCode = startupData->m_sourceCode;
    WorkerThreadStartMode startMode = startupData->m_startMode;
    OwnPtr<Vector<char>> cachedMetaData = startupData->m_cachedMetaData.release();
    V8CacheOptions v8CacheOptions = startupData->m_v8CacheOptions;

    {
        MutexLocker lock(m_threadStateMutex);

        // The worker was terminated before the thread had a chance to run.
        if (m_terminated) {
            // Notify the proxy that the WorkerGlobalScope has been disposed of.
            // This can free this thread object, hence it must not be touched afterwards.
            if (m_managedShutdown) m_shutdownRecord->scopePrepared();
            else m_workerReportingProxy.workerThreadTerminated();
            // Notify the main thread that it is safe to deallocate our resources.
            m_terminationEvent->signal();
            return;
        }

        m_microtaskRunner = adoptPtr(new WorkerMicrotaskRunner(this));
        initializeBackingThread();
        backingThread().addTaskObserver(m_microtaskRunner.get());

        m_isolate = initializeIsolate();
        m_workerGlobalScope = createWorkerGlobalScope(startupData);
        m_workerGlobalScope->scriptLoaded(sourceCode.length(), cachedMetaData.get() ? cachedMetaData->size() : 0);
    }
    if (m_managedShutdown) m_shutdownRecord->initialized();
    m_webScheduler = backingThread().platformThread().scheduler();

    // The corresponding call to didStopRunLoop() is in ~WorkerScriptController().
    didStartRunLoop();

    // Notify proxy that a new WorkerGlobalScope has been created and started.
    m_workerReportingProxy.workerGlobalScopeStarted(m_workerGlobalScope.get());

    WorkerScriptController* script = m_workerGlobalScope->script();
    if (!script->isExecutionForbidden())
        script->initializeContextIfNeeded();
    m_workerGlobalScope->workerInspectorController()->workerContextInitialized(startMode == PauseWorkerGlobalScopeOnStart);

    OwnPtr<CachedMetadataHandler> handler(workerGlobalScope()->createWorkerScriptCachedMetadataHandler(scriptURL, cachedMetaData.get()));
    bool success = script->evaluate(ScriptSourceCode(sourceCode, scriptURL), nullptr, handler.get(), v8CacheOptions);
    m_workerGlobalScope->didEvaluateWorkerScript();
    m_workerReportingProxy.didEvaluateWorkerScript(success);

    postInitialize();

    if (m_managedShutdown)
        m_webScheduler->postIdleTaskAfterWakeup(FROM_HERE, new TrackedWorkerIdleTask(this, WTF::bind<double>(&WorkerThread::performIdleWork, this)));
    else m_webScheduler->postIdleTaskAfterWakeup(FROM_HERE, WTF::bind<double>(&WorkerThread::performIdleWork, this));
}

void WorkerThread::shutdown()
{
    ASSERT(isCurrentThread());
    if (m_managedShutdown) {
        m_shutdownRecord->closeAdmission();
        if (!m_shutdownRecord->soleRawAccess()) return; // Nested task/interrupt/observer still uses raw state.
    }
    {
        MutexLocker lock(m_threadStateMutex);
        if (m_shutdown) return;
        ASSERT(!m_shutdown);
        m_shutdown = true;
    }

    if (m_managedShutdown) {
        { MutexLocker lock(m_workerInspectorControllerMutex); m_workerInspectorController = nullptr; m_debuggerMessageQueue.kill(); }
        m_debuggerMessageQueue.kill();
        while (OwnPtr<WebThread::Task> task = m_debuggerMessageQueue.tryGetMessageIgnoringKilled()) { }
    }
    workerGlobalScope()->dispose();
    willDestroyIsolate();

    // This should be called before we start the shutdown procedure.
    workerReportingProxy().willDestroyWorkerGlobalScope();

    // The below assignment will destroy the context, which will in turn notify messaging proxy.
    // We cannot let any objects survive past thread exit, because no other thread will run GC or otherwise destroy them.
    // If Oilpan is enabled, we detach of the context/global scope, with the final heap cleanup below sweeping it out.
#if !ENABLE(OILPAN)
    ASSERT(m_workerGlobalScope->hasOneRef());
#endif
    m_workerGlobalScope->notifyContextDestroyed();
    m_workerGlobalScope = nullptr;

    backingThread().removeTaskObserver(m_microtaskRunner.get());
    shutdownBackingThread();
    destroyIsolate();
    m_isolate = nullptr;

    m_microtaskRunner = nullptr;

    // Notify the proxy that the WorkerGlobalScope has been disposed of.
    // This can free this thread object, hence it must not be touched afterwards.
    if (m_managedShutdown) m_shutdownRecord->scopePrepared();
    else workerReportingProxy().workerThreadTerminated();

    m_terminationEvent->signal();
}

void WorkerThread::terminate()
{
    // Prevent the deadlock between GC and an attempt to terminate a thread.
    SafePointScope safePointScope(ThreadState::HeapPointersOnStack);
    terminateInternal();
}

void WorkerThread::terminateAndWait()
{
    terminate();
    m_terminationEvent->wait();
}

bool WorkerThread::terminated()
{
    MutexLocker lock(m_threadStateMutex);
    return m_terminated;
}

void WorkerThread::terminateInternal()
{
    ASSERT(isMainThread());

    // Protect against this method, initialize() or termination via the global scope racing each other.
    MutexLocker lock(m_threadStateMutex);

    // If terminateInternal has already been called, just return.
    if (m_terminated)
        return;
    m_terminated = true;
    if (m_managedShutdown) {
        m_shutdownRecord->closeAdmission();
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
        m_shutdownRecord->backlogDiagnostic().terminatedWhileHeld();
#endif
        if (!m_started) {
            m_shutdownRecord->scopePrepared(); // NeverStarted owns no native thread.
            scheduleWorkerRetirementPoll();
            return;
        }
    }

    // Signal the thread to notify that the thread's stopping.
    if (m_shutdownEvent)
        m_shutdownEvent->signal();

    // If the thread has already initiated shut down, just return.
    if (m_shutdown)
        return;

    // If the worker thread was never initialized, don't start another
    // shutdown, but still wait for the thread to signal when termination has
    // completed.
    if (!m_workerGlobalScope)
        return;

    // Ensure that tasks are being handled by thread event loop. If script execution weren't forbidden, a while(1) loop in JS could keep the thread alive forever.
    m_workerGlobalScope->script()->willScheduleExecutionTermination();
    terminateV8Execution();

    InspectorInstrumentation::didKillAllExecutionContextTasks(m_workerGlobalScope.get());
    m_debuggerMessageQueue.kill();
    postShutdownTask();
}

void WorkerThread::didStartRunLoop()
{
    ASSERT(isCurrentThread());
    Platform::current()->didStartWorkerRunLoop();
}

void WorkerThread::didStopRunLoop()
{
    ASSERT(isCurrentThread());
    Platform::current()->didStopWorkerRunLoop();
}

void WorkerThread::terminateAndWaitForAllWorkers()
{
    // Keep this lock to prevent WorkerThread instances from being destroyed.
    MutexLocker lock(threadSetMutex());
    HashSet<WorkerThread*> threads = workerThreads();
    for (WorkerThread* thread : threads)
        thread->terminateInternal();

    for (WorkerThread* thread : threads)
        thread->m_terminationEvent->wait();
}

bool WorkerThread::isCurrentThread()
{
    return m_started && backingThread().isCurrentThread();
}

void WorkerThread::performIdleWork(double deadlineSeconds)
{
    if (m_shutdown) // weolar
        return;
    
    double gcDeadlineSeconds = deadlineSeconds;

    // The V8 GC does some GC steps (e.g. compaction) only when the idle notification is ~1s.
    // TODO(rmcilroy): Refactor so extending the deadline like this this isn't needed.
    if (m_webScheduler->canExceedIdleDeadlineIfRequired())
        gcDeadlineSeconds = Platform::current()->monotonicallyIncreasingTime() + kLongIdlePeriodSecs;

    if (m_managedShutdown) {
        if (!m_shutdownRecord->acceptsOrdinary()) return;
        const bool finished = doIdleGc(gcDeadlineSeconds);
        WebThread::IdleTask* next = new TrackedWorkerIdleTask(this, WTF::bind<double>(&WorkerThread::performIdleWork, this));
        if (finished) m_webScheduler->postIdleTaskAfterWakeup(FROM_HERE, next);
        else m_webScheduler->postIdleTask(FROM_HERE, next);
    } else if (doIdleGc(gcDeadlineSeconds))
        m_webScheduler->postIdleTaskAfterWakeup(FROM_HERE, WTF::bind<double>(&WorkerThread::performIdleWork, this));
    else
        m_webScheduler->postIdleTask(FROM_HERE, WTF::bind<double>(&WorkerThread::performIdleWork, this));
}

bool WorkerThread::doIdleGc(double deadlineSeconds)
{
    bool gcFinished = false;
    if (deadlineSeconds > Platform::current()->monotonicallyIncreasingTime())
        gcFinished = isolate()->IdleNotificationDeadline(deadlineSeconds);
    return gcFinished;
}

void WorkerThread::postTask(const WebTraceLocation& location, PassOwnPtr<ExecutionContextTask> task)
{
    if (m_managedShutdown && !m_shutdownRecord->acceptsOrdinary()) return;
    backingThread().postTask(location, wrapRawTask(WorkerThreadTask::create(*this, task, !m_managedShutdown), WorkerShutdownRecord::Ordinary));
}

#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
void WorkerThread::postDiagnosticMessageTask(const WebTraceLocation& location,
    PassOwnPtr<ExecutionContextTask> task, unsigned generation, unsigned ordinal)
{
    ASSERT(m_managedShutdown);
    backingThread().postTask(location, new TrackedWorkerTask(this,
        WorkerThreadTask::create(*this, task, false), WorkerShutdownRecord::Ordinary, generation, ordinal));
}

bool WorkerThread::diagnosticMessageCallbackEntered()
{
    const DiagnosticMessageContext context = diagnosticMessageContext;
    if (!context.record) return true; // An ordinary/unarmed message keeps its original behavior.
    ThreadState* state = ThreadState::current();
    if (!state || state->isAtSafePoint()) {
        context.record->backlogDiagnostic().abort(); return false;
    }
    bool entered;
    {
        // Blocking with conservative stack roots cannot prevent another thread's GC.
        SafePointScope safePoint(ThreadState::HeapPointersOnStack, state);
        entered = context.record->backlogDiagnostic().enterCallback(
            context.generation, context.ordinal, static_cast<unsigned>(WTF::currentThread()));
    }
    return entered && !context.record->closing();
}
#endif

void WorkerThread::postDelayedTask(const WebTraceLocation& location, PassOwnPtr<ExecutionContextTask> task, long long delayMs)
{
    if (m_managedShutdown && !m_shutdownRecord->acceptsOrdinary()) return;
    backingThread().postDelayedTask(location, wrapRawTask(WorkerThreadTask::create(*this, task, !m_managedShutdown), WorkerShutdownRecord::Ordinary), delayMs);
}

void WorkerThread::initializeBackingThread()
{
    ASSERT(isCurrentThread());
    backingThread().initialize();
}

void WorkerThread::shutdownBackingThread()
{
    ASSERT(isCurrentThread());
    backingThread().shutdown();
}

v8::Isolate* WorkerThread::initializeIsolate()
{
    ASSERT(isCurrentThread());
    ASSERT(!m_isolate);
    v8::Isolate* isolate = V8PerIsolateData::initialize();
    V8Initializer::initializeWorker(isolate);

    m_interruptor = adoptPtr(new V8IsolateInterruptor(isolate));
    ThreadState::current()->addInterruptor(m_interruptor.get());
    ThreadState::current()->registerTraceDOMWrappers(isolate, V8GCController::traceDOMWrappers);

    return isolate;
}

void WorkerThread::willDestroyIsolate()
{
    ASSERT(isCurrentThread());
    ASSERT(m_isolate);
    V8PerIsolateData::willBeDestroyed(m_isolate);
    ThreadState::current()->removeInterruptor(m_interruptor.get());
}

void WorkerThread::destroyIsolate()
{
    ASSERT(isCurrentThread());
    V8PerIsolateData::destroy(m_isolate);
}

void WorkerThread::terminateV8Execution()
{
    ASSERT(isMainThread());
    v8::V8::TerminateExecution(m_isolate);
}

bool WorkerThread::appendDebuggerTask(PassOwnPtr<WebThread::Task> task)
{
    OwnPtr<WebThread::Task> admitted = adoptPtr(wrapRawTask(task, WorkerShutdownRecord::Debugger));
    MutexLocker admissionLock(m_workerInspectorControllerMutex);
    if (m_managedShutdown && !m_shutdownRecord->acceptsOrdinary()) return false;
    return m_debuggerMessageQueue.append(admitted.release());
    // On rejection the mutex unwinds before the earlier-declared payload owner.
}

void WorkerThread::enableShutdownRetirement(PassRefPtr<WorkerShutdownRecord> record)
{
    ASSERT(isMainThread() && !m_started && supportsShutdownRetirement());
    m_shutdownRecord = record; // Set before publication; never replaced or cleared afterwards.
    m_managedShutdown = true;
}

WebThread::Task* WorkerThread::wrapRawTask(PassOwnPtr<WebThread::Task> task, WorkerShutdownRecord::TaskKind kind)
{
    if (!m_managedShutdown) return task.leakPtr();
    return new TrackedWorkerTask(this, task, kind);
}

void WorkerThread::postShutdownTask()
{
    if (!m_managedShutdown) {
        backingThread().postTask(FROM_HERE, new Task(threadSafeBind(&WorkerThread::shutdown, AllowCrossThreadAccess(this))));
        return;
    }
    if (m_shutdownRecord->reserveShutdown())
        backingThread().postTask(FROM_HERE, wrapRawTask(adoptPtr(new Task(threadSafeBind(&WorkerThread::shutdown, AllowCrossThreadAccess(this)))), WorkerShutdownRecord::Shutdown));
}

void WorkerThread::afterTrackedTask()
{
    if (m_shutdownRecord->closing()) postShutdownTask();
}

bool WorkerThread::allWorkersRetirementSupported()
{
    MutexLocker lock(threadSetMutex());
    for (WorkerThread* worker : workerThreads())
        if (!worker->m_managedShutdown) return false;
    return true; // No wait, callback, RefPtr transfer or destruction while set mutex is held.
}

MessageQueueWaitResult WorkerThread::runDebuggerTask(WaitMode waitMode)
{
    ASSERT(isCurrentThread());
    MessageQueueWaitResult result;
    double absoluteTime = MessageQueue<WebThread::Task>::infiniteTime();
    OwnPtr<WebThread::Task> task;
    {
        if (waitMode == DontWaitForMessage)
            absoluteTime = 0.0;
        SafePointScope safePointScope(ThreadState::NoHeapPointersOnStack);
        task = m_debuggerMessageQueue.waitForMessageWithTimeout(result, absoluteTime);
    }

    if (result == MessageQueueMessageReceived) {
        InspectorInstrumentation::willProcessTask(workerGlobalScope());
        task->run();
        InspectorInstrumentation::didProcessTask(workerGlobalScope());
    }

    return result;
}

void WorkerThread::willEnterNestedLoop()
{
    InspectorInstrumentation::willEnterNestedRunLoop(m_workerGlobalScope.get());
}

void WorkerThread::didLeaveNestedLoop()
{
    InspectorInstrumentation::didLeaveNestedRunLoop(m_workerGlobalScope.get());
}

void WorkerThread::setWorkerInspectorController(WorkerInspectorController* workerInspectorController)
{
    MutexLocker locker(m_workerInspectorControllerMutex);
    m_workerInspectorController = workerInspectorController;
}

} // namespace blink
