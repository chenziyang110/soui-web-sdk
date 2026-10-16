/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 * Copyright (C) 2009 Google Inc. All Rights Reserved.
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

#include "core/workers/WorkerMessagingProxy.h"

#include "core/dom/CrossThreadTask.h"
#include "core/dom/Document.h"
#include "core/events/ErrorEvent.h"
#include "core/events/MessageEvent.h"
#include "core/frame/Console.h"
#include "core/frame/FrameConsole.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/inspector/ScriptCallStack.h"
#include "core/inspector/WorkerDebuggerAgent.h"
#include "core/loader/DocumentLoadTiming.h"
#include "core/loader/DocumentLoader.h"
#include "core/workers/InProcessWorkerBase.h"
#include "core/workers/WorkerClients.h"
#include "core/workers/WorkerInspectorProxy.h"
#include "core/workers/WorkerObjectProxy.h"
#include "core/workers/WorkerThreadStartupData.h"
#include "platform/heap/Handle.h"
#include "wtf/Functional.h"
#include "wtf/MainThread.h"
#include "platform/Task.h"
#include "public/platform/Platform.h"
#include "wtf/HashSet.h"
#include <atomic>

namespace blink {

namespace {

HashSet<WorkerMessagingProxy*>& workerProxyOwners()
{
    DEFINE_STATIC_LOCAL(HashSet<WorkerMessagingProxy*>, owners, ());
    return owners; // Main-thread-only, not a cross-thread RefPtr registry.
}
std::atomic<bool> retirementPollPosted(false);
bool allWorkerAdmissionClosed = false; // Read/written by the main owner only.
bool retirementPollRunning = false;
bool workerPrepareRunning = false;
struct MainCallReservation {
    explicit MainCallReservation(bool& flag) : flag(flag) { flag = true; }
    ~MainCallReservation() { flag = false; }
    bool& flag;
};
Vector<RefPtr<WorkerShutdownRecord>> ownerRecords()
{
    Vector<RefPtr<WorkerShutdownRecord>> records;
    for (WorkerMessagingProxy* owner : workerProxyOwners()) records.append(owner->shutdownRecord());
    return records;
}
WorkerMessagingProxy* ownerForRecord(WorkerShutdownRecord* record)
{
    for (WorkerMessagingProxy* owner : workerProxyOwners())
        if (owner->shutdownRecord() == record) return owner;
    return nullptr; // Another owner may have retired during GC/refcount destruction.
}
class RetirementPollTask final : public WebThread::Task {
public:
    RetirementPollTask() : m_performed(false) { }
    ~RetirementPollTask() override { if (!m_performed) retirementPollPosted.store(false, std::memory_order_release); }
    void run() override
    {
        m_performed = true;
        retirementPollPosted.store(false, std::memory_order_release);
        if (WorkerMessagingProxy::pollWorkerRetirements(allWorkerAdmissionClosed))
            scheduleWorkerRetirementPoll();
    }
private:
    bool m_performed;
};

void processMessageOnWorkerGlobalScope(PassRefPtr<SerializedScriptValue> message, PassOwnPtr<MessagePortChannelArray> channels, WorkerObjectProxy* workerObjectProxy, ExecutionContext* scriptContext)
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    if (!WorkerThread::diagnosticMessageCallbackEntered()) return;
#endif
    WorkerGlobalScope* globalScope = toWorkerGlobalScope(scriptContext);
    MessagePortArray* ports = MessagePort::entanglePorts(*scriptContext, channels);
    globalScope->dispatchEvent(MessageEvent::create(ports, message));
    workerObjectProxy->confirmMessageFromWorkerObject(scriptContext->hasPendingActivity());
}

} // namespace

WorkerMessagingProxy::WorkerMessagingProxy(InProcessWorkerBase* workerObject, PassOwnPtrWillBeRawPtr<WorkerClients> workerClients)
    : m_shutdownRecord(WorkerShutdownRecord::create())
    , m_managedShutdown(false)
    , m_executionContext(workerObject->executionContext())
    , m_workerObjectProxy(WorkerObjectProxy::create(m_executionContext.get(), this))
    , m_workerObject(workerObject)
    , m_mayBeDestroyed(false)
    , m_loaderWorkerThread(nullptr)
    , m_providerDetached(false)
    , m_unconfirmedMessageCount(0)
    , m_workerThreadHadPendingActivity(false)
    , m_askedToTerminate(false)
    , m_workerInspectorProxy(WorkerInspectorProxy::create())
    , m_workerClients(workerClients)
{
    ASSERT(m_workerObject);
    ASSERT((m_executionContext->isDocument() && isMainThread())
        || (m_executionContext->isWorkerGlobalScope() && toWorkerGlobalScope(m_executionContext.get())->thread()->isCurrentThread()));
    m_workerInspectorProxy->setWorkerGlobalScopeProxy(this);
    workerProxyOwners().add(this);
}

WorkerMessagingProxy::~WorkerMessagingProxy()
{
    ASSERT(m_shutdownRecord->callbacksRetired());
    workerProxyOwners().remove(this);
    ASSERT(!m_workerObject);
    ASSERT((m_executionContext->isDocument() && isMainThread())
        || (m_executionContext->isWorkerGlobalScope() && toWorkerGlobalScope(m_executionContext.get())->thread()->isCurrentThread()));
    if (m_loaderProxy && !m_providerDetached)
        m_loaderProxy->detachProvider(this);
}

void WorkerMessagingProxy::startWorkerGlobalScope(const KURL& scriptURL, const String& userAgent, const String& sourceCode, WorkerThreadStartMode startMode)
{
    // FIXME: This need to be revisited when we support nested worker one day
    ASSERT(m_executionContext->isDocument());
    if (m_askedToTerminate || allWorkerAdmissionClosed) {
        // Worker.terminate() could be called from JS before the thread was created.
        return;
    }
    Document* document = toDocument(m_executionContext.get());
    SecurityOrigin* starterOrigin = document->securityOrigin();

    RefPtr<ContentSecurityPolicy> csp = m_workerObject->contentSecurityPolicy() ? m_workerObject->contentSecurityPolicy() : document->contentSecurityPolicy();
    ASSERT(csp);

    OwnPtr<WorkerThreadStartupData> startupData = WorkerThreadStartupData::create(scriptURL, userAgent, sourceCode, nullptr, startMode, csp->headers(), starterOrigin, m_workerClients.release());
    double originTime = document->loader() ? document->loader()->timing().referenceMonotonicTime() : monotonicallyIncreasingTime();

    m_loaderProxy = WorkerLoaderProxy::create(this);
    RefPtr<WorkerThread> thread = createWorkerThread(originTime);
    m_managedShutdown = thread->supportsShutdownRetirement();
    if (m_managedShutdown) {
        thread->enableShutdownRetirement(m_shutdownRecord);
        m_loaderProxy->setWorkerShutdownRecord(m_shutdownRecord);
        m_loaderWorkerThread = thread.get(); // Stable before native thread publication.
        m_startingWorkerThread = thread; // Startup failure must retain the real backing owner.
    }
    try {
        thread->start(startupData.release());
        workerThreadCreated(thread); // Preserve the original start -> publication order.
        m_startingWorkerThread.clear();
    } catch (...) {
        if (!m_managedShutdown) throw;
        m_shutdownRecord->fail();
        return; // Retain m_startingWorkerThread/provider/module; finalization refuses Failed.
    }
    m_workerInspectorProxy->workerThreadCreated(m_executionContext.get(), m_workerThread.get(), scriptURL);
}

void WorkerMessagingProxy::postMessageToWorkerObject(PassRefPtr<SerializedScriptValue> message, PassOwnPtr<MessagePortChannelArray> channels)
{
    if (!m_workerObject || m_askedToTerminate)
        return;

    MessagePortArray* ports = MessagePort::entanglePorts(*m_executionContext.get(), channels);
    m_workerObject->dispatchEvent(MessageEvent::create(ports, message));
}

void WorkerMessagingProxy::postMessageToWorkerGlobalScope(PassRefPtr<SerializedScriptValue> message, PassOwnPtr<MessagePortChannelArray> channels)
{
    if (m_askedToTerminate)
        return;

    OwnPtr<ExecutionContextTask> task = createCrossThreadTask(&processMessageOnWorkerGlobalScope, message, channels, AllowCrossThreadAccess(&workerObjectProxy()));
    if (m_workerThread) {
        ++m_unconfirmedMessageCount;
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
        unsigned generation = 0, ordinal = 0;
        if (m_managedShutdown && m_shutdownRecord->backlogDiagnostic().reserve(generation, ordinal)) {
            m_workerThread->postDiagnosticMessageTask(FROM_HERE, task.release(), generation, ordinal);
            return;
        }
#endif
        m_workerThread->postTask(FROM_HERE, task.release());
    } else {
        m_queuedEarlyTasks.append(task.release());
    }
}

bool WorkerMessagingProxy::postTaskToWorkerGlobalScope(PassOwnPtr<ExecutionContextTask> task)
{
    if (m_managedShutdown) {
        if (!m_shutdownRecord->acceptsOrdinary()) return false;
        ASSERT(m_loaderWorkerThread);
        m_loaderWorkerThread->postTask(FROM_HERE, task); // Provider borrow holds the owner independently.
        return true;
    }
    if (m_askedToTerminate) return false;
    ASSERT(m_workerThread);
    m_workerThread->postTask(FROM_HERE, task);
    return true;
}

void WorkerMessagingProxy::postTaskToLoader(PassOwnPtr<ExecutionContextTask> task)
{
    // FIXME: In case of nested workers, this should go directly to the root Document context.
    ASSERT(m_executionContext->isDocument());
    m_executionContext->postTask(FROM_HERE, wrapWorkerCallback(m_shutdownRecord.get(), task));
}

void WorkerMessagingProxy::reportException(const String& errorMessage, int lineNumber, int columnNumber, const String& sourceURL, int exceptionId)
{
    if (!m_workerObject)
        return;

    // We don't bother checking the askedToTerminate() flag here, because exceptions should *always* be reported even if the thread is terminated.
    // This is intentionally different than the behavior in MessageWorkerTask, because terminated workers no longer deliver messages (section 4.6 of the WebWorker spec), but they do report exceptions.

    RefPtrWillBeRawPtr<ErrorEvent> event = ErrorEvent::create(errorMessage, sourceURL, lineNumber, columnNumber, nullptr);
    bool errorHandled = !m_workerObject->dispatchEvent(event);

    // A late page error may still be reported after the Worker scope is disposed.
    // Do not capture that retired scope into a new raw Worker callback.
    if (m_managedShutdown && !m_shutdownRecord->acceptsOrdinary()) return;
    postTaskToWorkerGlobalScope(createCrossThreadTask(&WorkerGlobalScope::exceptionHandled, m_workerThread->workerGlobalScope(), exceptionId, errorHandled));
}

void WorkerMessagingProxy::reportConsoleMessage(MessageSource source, MessageLevel level, const String& message, int lineNumber, const String& sourceURL)
{
    if (m_askedToTerminate)
        return;
    // FIXME: In case of nested workers, this should go directly to the root Document context.
    ASSERT(m_executionContext->isDocument());
    Document* document = toDocument(m_executionContext.get());
    LocalFrame* frame = document->frame();
    if (!frame)
        return;

    RefPtrWillBeRawPtr<ConsoleMessage> consoleMessage = ConsoleMessage::create(source, level, message, sourceURL, lineNumber);
    consoleMessage->setWorkerGlobalScopeProxy(this);
    frame->console().addMessage(consoleMessage.release());
}

void WorkerMessagingProxy::workerThreadCreated(PassRefPtr<WorkerThread> workerThread)
{
    ASSERT(!m_askedToTerminate);
    m_workerThread = workerThread;

    ASSERT(!m_unconfirmedMessageCount);
    m_unconfirmedMessageCount = m_queuedEarlyTasks.size();
    m_workerThreadHadPendingActivity = true; // Worker initialization means a pending activity.

    for (auto& earlyTasks : m_queuedEarlyTasks)
        m_workerThread->postTask(FROM_HERE, earlyTasks.release());
    m_queuedEarlyTasks.clear();
}

void WorkerMessagingProxy::workerObjectDestroyed()
{
    m_workerObject = nullptr;
    if (allWorkerAdmissionClosed && !m_workerThread && !m_startingWorkerThread
        && m_shutdownRecord->callbacksRetired()) {
        m_mayBeDestroyed = true;
        pollRetirement(true); // No new raw Document payload after global Ready.
        return;
    }
    m_executionContext->postTask(FROM_HERE, wrapWorkerCallback(m_shutdownRecord.get(), createCrossThreadTask(&WorkerMessagingProxy::workerObjectDestroyedInternal, this)));
}

void WorkerMessagingProxy::workerObjectDestroyedInternal()
{
    m_mayBeDestroyed = true;
    if (m_managedShutdown) { terminateWorkerGlobalScope(); scheduleWorkerRetirementPoll(); return; }
    if (m_workerThread)
        terminateWorkerGlobalScope();
    else
        workerThreadTerminated();
}

void WorkerMessagingProxy::workerThreadTerminated()
{
    if (m_managedShutdown) { scheduleWorkerRetirementPoll(); return; }
    // This method is always the last to be performed, so the proxy is not needed for communication
    // in either side any more. However, the Worker object may still exist, and it assumes that the proxy exists, too.
    m_askedToTerminate = true;
    m_workerThread = nullptr;
    terminateInternally();
    if (m_mayBeDestroyed)
        scheduleWorkerRetirementPoll(); // Callback stack still owns its independent slot.
}

void WorkerMessagingProxy::terminateWorkerGlobalScope()
{
    if (m_askedToTerminate)
        return;
    m_askedToTerminate = true;

    if (m_workerThread)
        m_workerThread->terminate();

    terminateInternally();
}

void WorkerMessagingProxy::postMessageToPageInspector(const String& message)
{
    if (!m_workerInspectorProxy)
        return;
    WorkerInspectorProxy::PageInspector* pageInspector = m_workerInspectorProxy->pageInspector();
    if (pageInspector)
        pageInspector->dispatchMessageFromWorker(message);
}

void WorkerMessagingProxy::postWorkerConsoleAgentEnabled()
{
    if (!m_workerInspectorProxy)
        return;
    WorkerInspectorProxy::PageInspector* pageInspector = m_workerInspectorProxy->pageInspector();
    if (pageInspector)
        pageInspector->workerConsoleAgentEnabled(this);
}

WorkerInspectorProxy* WorkerMessagingProxy::workerInspectorProxy()
{
    return m_workerInspectorProxy.get();
}

void WorkerMessagingProxy::confirmMessageFromWorkerObject(bool hasPendingActivity)
{
    if (!m_askedToTerminate) {
        ASSERT(m_unconfirmedMessageCount);
        --m_unconfirmedMessageCount;
    }
    reportPendingActivity(hasPendingActivity);
}

void WorkerMessagingProxy::reportPendingActivity(bool hasPendingActivity)
{
    m_workerThreadHadPendingActivity = hasPendingActivity;
}

bool WorkerMessagingProxy::hasPendingActivity() const
{
    return (m_unconfirmedMessageCount || m_workerThreadHadPendingActivity) && !m_askedToTerminate;
}

void WorkerMessagingProxy::terminateInternally()
{
    m_workerInspectorProxy->workerThreadTerminated();

    // FIXME: This need to be revisited when we support nested worker one day
    ASSERT(m_executionContext->isDocument());
    Document* document = toDocument(m_executionContext.get());
    LocalFrame* frame = document->frame();
    if (frame)
        frame->console().adoptWorkerMessagesAfterTermination(this);
}

unsigned WorkerMessagingProxy::diagnosticWitnessCount()
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    ASSERT(isMainThread());
    unsigned count = 0;
    for (WorkerMessagingProxy* owner : workerProxyOwners()) if (owner->m_managedShutdown && owner->m_workerThread) ++count;
    return count;
#else
    return 0; // Default OFF, not a production permission or capability change.
#endif
}

WorkerMessagingProxy::RetirementWitness* WorkerMessagingProxy::captureRetirementWitness(unsigned index)
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    ASSERT(isMainThread());
    for (WorkerMessagingProxy* owner : workerProxyOwners()) {
        if (!owner->m_managedShutdown || !owner->m_workerThread) continue;
        if (index--) continue;
        OwnPtr<RetirementWitness> witness = adoptPtr(new RetirementWitness(owner->m_shutdownRecord.get()));
        if (!owner->m_workerThread->duplicateShutdownWitness(witness->native)) return nullptr;
        return witness.leakPtr();
    }
#endif
    return nullptr;
}

void scheduleWorkerRetirementPoll()
{
    if (!Platform::current() || retirementPollPosted.exchange(true, std::memory_order_acq_rel)) return;
    Platform::current()->mainThread()->postDelayedTask(FROM_HERE, new RetirementPollTask, 1);
    // void post drop destroys only the independent capsule and clears reservation.
}

WorkerRetirementResult WorkerMessagingProxy::pollRetirement(bool cancelCallbacks)
{
    ASSERT(isMainThread());
    if (!m_managedShutdown && m_workerThread) return WorkerRetirementResult::Unsupported;
    if (m_shutdownRecord->failed()) return WorkerRetirementResult::Failed;
    if (m_startingWorkerThread) return WorkerRetirementResult::Pending;
    if (m_workerThread) {
        if (!m_shutdownRecord->rawAccessQuiescent()) {
            if (m_shutdownRecord->closing()) m_workerThread->postShutdownTask();
            return WorkerRetirementResult::Pending;
        }
        if (cancelCallbacks) m_shutdownRecord->cancelCallbacks();
        if (m_loaderProxy && !m_providerDetached) {
            m_loaderProxy->detachProvider(this);
            m_providerDetached = true;
        }
        if (cancelCallbacks) m_shutdownRecord->cancelCallbacks(); // Cover posts racing the earlier cancel.
        if (!m_shutdownRecord->callbacksRetired()) return WorkerRetirementResult::Pending;
        const WorkerRetirementResult result = m_workerThread->retireBacking();
        if (result != WorkerRetirementResult::Ready) return result;
        if (!m_shutdownRecord->callbacksRetired()) return WorkerRetirementResult::Pending;
        // Final proxy notification is performed on the owning main thread,
        // after native join, actual task destruction and every raw callback stack.
        m_askedToTerminate = true;
        terminateInternally();
        m_workerThread = nullptr;
    } else if (cancelCallbacks) {
        m_shutdownRecord->cancelCallbacks();
        m_queuedEarlyTasks.clear();
        m_askedToTerminate = true;
        m_shutdownRecord->scopePrepared();
        m_shutdownRecord->nativeJoined(); // NeverStarted: no native handle was created.
    }
    if (!m_shutdownRecord->callbacksRetired()) return WorkerRetirementResult::Pending;
    if (!m_workerObject) m_mayBeDestroyed = true;
    if (m_mayBeDestroyed) delete this; // Sole deletion point after callback leases have ended.
    return WorkerRetirementResult::Ready;
}

bool WorkerMessagingProxy::pollWorkerRetirements(bool cancelCallbacks)
{
    ASSERT(isMainThread());
    if (retirementPollRunning) return true;
    MainCallReservation reservation(retirementPollRunning);
    const Vector<RefPtr<WorkerShutdownRecord>> records = ownerRecords();
    bool pendingJoin = false;
    for (const auto& record : records) {
        WorkerMessagingProxy* owner = ownerForRecord(record.get());
        if (!owner) continue;
        // Snapshot elements can retire themselves; no callback pump is performed.
        const bool eligible = owner->m_managedShutdown && owner->m_workerThread
            && owner->m_shutdownRecord->rawAccessQuiescent() && !owner->m_shutdownRecord->failed();
        const WorkerRetirementResult result = owner->pollRetirement(cancelCallbacks);
        if (eligible && result == WorkerRetirementResult::Pending) pendingJoin = true;
    }
    return pendingJoin;
}

WorkerRetirementResult WorkerMessagingProxy::prepareAllWorkerRetirements()
{
    ASSERT(isMainThread());
    if (workerPrepareRunning || retirementPollRunning) return WorkerRetirementResult::Pending;
    MainCallReservation reservation(workerPrepareRunning);
    // Unsupported Shared/Service/Compositor owners are an explicit failure,
    // before any loader/main/IO/platform teardown or legacy global wait.
    if (!WorkerThread::allWorkersRetirementSupported()) return WorkerRetirementResult::Unsupported;
    allWorkerAdmissionClosed = true;
    const Vector<RefPtr<WorkerShutdownRecord>> records = ownerRecords();
    for (const auto& record : records) {
        if (WorkerMessagingProxy* owner = ownerForRecord(record.get())) owner->terminateWorkerGlobalScope();
    }
    pollWorkerRetirements(true);
    for (WorkerMessagingProxy* owner : workerProxyOwners()) {
        if (owner->m_shutdownRecord->failed()) return WorkerRetirementResult::Failed;
        if (owner->m_workerThread || owner->m_startingWorkerThread || !owner->m_shutdownRecord->callbacksRetired()) return WorkerRetirementResult::Pending;
    }
    return WorkerRetirementResult::Ready;
}

} // namespace blink
