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
#include "core/dom/MessagePort.h"

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/ExceptionStatePlaceholder.h"
#include "bindings/core/v8/SerializedScriptValue.h"
#include "bindings/core/v8/SerializedScriptValueFactory.h"
#include "core/dom/CrossThreadTask.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/ExecutionContext.h"
#include "core/events/MessageEvent.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/workers/WorkerGlobalScope.h"
#include "public/platform/WebString.h"
#include "wtf/Functional.h"
#include "wtf/text/AtomicString.h"
#include "core/dom/ExecutionContextTask.h"
#include "wtf/ThreadSafeRefCounted.h"
#include "wtf/ThreadingPrimitives.h"
#include <limits>
#if defined(SOUI_MESSAGE_PORT_DISPATCH_DIAGNOSTICS)
#include <stdio.h>
#endif

namespace blink {

// Only this cell is shared with notification and task-destruction threads.
// It owns neither the port nor its execution context.
class MessagePort::DispatchState : public ThreadSafeRefCounted<DispatchState> {
    WTF_MAKE_NONCOPYABLE(DispatchState);
public:
    static PassRefPtr<DispatchState> create() { return adoptRef(new DispatchState); }

    ~DispatchState()
    {
        // Every reserved/running token has a task or a construction guard holding us.
        RELEASE_ASSERT(!m_pendingToken && !m_runningToken);
        traceLocked("destroy", 0);
    }

    DispatchToken reserveWake()
    {
        MutexLocker locker(m_mutex);
        if (m_phase == Running)
            m_phase = RunningDirty;
        if (m_phase != Idle)
            return 0;
        return reserveLocked();
    }

    bool begin(DispatchToken token)
    {
        MutexLocker locker(m_mutex);
        if (m_phase != Posted || m_pendingToken != token)
            return false;
        m_pendingToken = 0;
        m_runningToken = token;
        m_phase = Running;
        ++m_performed;
        traceLocked("begin", token);
        return true;
    }

    DispatchToken finish(DispatchToken token, bool canContinue, bool fullBatch)
    {
        MutexLocker locker(m_mutex);
        RELEASE_ASSERT(m_runningToken == token);
        m_runningToken = 0;
        const bool dirty = m_phase == RunningDirty;
        if (m_phase != Inactive)
            m_phase = Idle;
        traceLocked("finish", token);
        if (m_phase == Inactive || !canContinue || (!fullBatch && !dirty))
            return 0;
        return reserveLocked();
    }

    void cancel(DispatchToken token)
    {
        MutexLocker locker(m_mutex);
        // Destruction of an older capsule cannot cancel a later reservation.
        if (m_pendingToken != token)
            return;
        m_pendingToken = 0;
        if (m_phase == Posted)
            m_phase = Idle;
        ++m_cancelled;
        traceLocked("cancel", token);
    }

    void abandonRunning(DispatchToken token)
    {
        MutexLocker locker(m_mutex);
        if (m_runningToken != token)
            return;
        m_runningToken = 0;
        if (m_phase != Inactive)
            m_phase = Idle;
        traceLocked("abandon", token);
    }

    void deactivate()
    {
        MutexLocker locker(m_mutex);
        m_phase = Inactive;
        traceLocked("inactive", 0);
    }

private:
    enum Phase { Idle, Posted, Running, RunningDirty, Inactive };

    DispatchState()
        : m_phase(Idle)
        , m_nextToken(0)
        , m_pendingToken(0)
        , m_runningToken(0)
        , m_performed(0)
        , m_cancelled(0)
    {
        traceLocked("create", 0);
    }

    DispatchToken reserveLocked()
    {
        RELEASE_ASSERT(m_phase == Idle && !m_pendingToken && !m_runningToken);
        RELEASE_ASSERT(m_nextToken != std::numeric_limits<DispatchToken>::max());
        m_pendingToken = ++m_nextToken;
        m_phase = Posted;
        traceLocked("reserve", m_pendingToken);
        return m_pendingToken;
    }

    void traceLocked(const char* event, DispatchToken token) const
    {
#if defined(SOUI_MESSAGE_PORT_DISPATCH_DIAGNOSTICS)
        // Opt-in diagnostic builds only. This measures reservation state, not
        // backend acceptance, resource tolerance, scheduling latency or worker join.
        fprintf(stderr, "SOUI_MP_DISPATCH cell=%p event=%s token=%llu reserved=%llu performed=%llu cancelled=%llu pending=%u running=%u phase=%u\n",
            static_cast<const void*>(this), event,
            static_cast<unsigned long long>(token),
            static_cast<unsigned long long>(m_nextToken),
            static_cast<unsigned long long>(m_performed),
            static_cast<unsigned long long>(m_cancelled),
            m_pendingToken ? 1u : 0u, m_runningToken ? 1u : 0u,
            static_cast<unsigned>(m_phase));
#else
        (void)event;
        (void)token;
#endif
    }

    Mutex m_mutex;
    Phase m_phase;
    DispatchToken m_nextToken;
    DispatchToken m_pendingToken;
    DispatchToken m_runningToken;
    DispatchToken m_performed;
    DispatchToken m_cancelled;
};

class MessagePort::DispatchTask final : public ExecutionContextTask {
    WTF_MAKE_NONCOPYABLE(DispatchTask);
public:
    // Covers failure before a newly reserved token has an owning task capsule.
    class Reservation {
        WTF_MAKE_NONCOPYABLE(Reservation);
    public:
        Reservation(DispatchState* state, DispatchToken token)
            : m_state(state), m_token(token), m_armed(true) { }
        ~Reservation() { if (m_armed) m_state->cancel(m_token); }
        void handOff() { m_armed = false; }
    private:
        RefPtr<DispatchState> m_state;
        DispatchToken m_token;
        bool m_armed;
    };

    DispatchTask(DispatchState* state, const WeakPtr<MessagePort>& port, DispatchToken token)
        : m_state(state), m_port(port), m_token(token), m_performed(false) { }

    ~DispatchTask() override
    {
        // postTask returns void and may destroy a task without performing it.
        // Cancellation must never dereference the weak port on this thread.
        if (!m_performed)
            m_state->cancel(m_token);
    }

    void performTask(ExecutionContext*) override
    {
        RELEASE_ASSERT(!m_performed);
        if (!m_state->begin(m_token))
            return;
        m_performed = true;
        Running running(m_state.get(), m_token);
        // Only a running owner task retains the port, including its batch handoff.
        RefPtrWillBeRawPtr<MessagePort> protect(m_port.get());
        if (!protect) {
            m_state->deactivate();
            m_state->finish(m_token, false, false);
            running.finished();
            return;
        }
        const bool fullBatch = protect->dispatchMessages();
        const DispatchToken next = m_state->finish(m_token, protect->canDispatchMessages(), fullBatch);
        running.finished();
        if (next)
            protect->postReservedDispatch(next);
    }

private:
    class Running {
        WTF_MAKE_NONCOPYABLE(Running);
    public:
        Running(DispatchState* state, DispatchToken token)
            : m_state(state), m_token(token), m_armed(true) { }
        ~Running() { if (m_armed) m_state->abandonRunning(m_token); }
        void finished() { m_armed = false; }
    private:
        RefPtr<DispatchState> m_state;
        DispatchToken m_token;
        bool m_armed;
    };

    RefPtr<DispatchState> m_state;
    WeakPtr<MessagePort> m_port;
    DispatchToken m_token;
    bool m_performed;
};


MessagePort* MessagePort::create(ExecutionContext& executionContext)
{
    MessagePort* port = new MessagePort(executionContext);
    port->suspendIfNeeded();
    return port;
}

MessagePort::MessagePort(ExecutionContext& executionContext)
    : ActiveDOMObject(&executionContext)
    , m_started(false)
    , m_closed(false)
    , m_dispatchState(DispatchState::create())
    , m_weakFactory(this)
{
}

MessagePort::~MessagePort()
{
    close();
    if (m_scriptStateForConversion)
        m_scriptStateForConversion->disposePerContextData();
}

void MessagePort::postMessage(ExecutionContext* context, PassRefPtr<SerializedScriptValue> message, const MessagePortArray* ports, ExceptionState& exceptionState)
{
    if (!isEntangled())
        return;
    ASSERT(executionContext());
    ASSERT(m_entangledChannel);

    OwnPtr<MessagePortChannelArray> channels;
    // Make sure we aren't connected to any of the passed-in ports.
    if (ports) {
        for (unsigned i = 0; i < ports->size(); ++i) {
            MessagePort* dataPort = (*ports)[i].get();
            if (dataPort == this) {
                exceptionState.throwDOMException(DataCloneError, "Port at index " + String::number(i) + " contains the source port.");
                return;
            }
        }
        channels = MessagePort::disentanglePorts(context, ports, exceptionState);
        if (exceptionState.hadException())
            return;
    }

    WebString messageString = message->toWireString();
    OwnPtr<WebMessagePortChannelArray> webChannels = toWebMessagePortChannelArray(channels.release());
    m_entangledChannel->postMessage(messageString, webChannels.leakPtr());
}

// static
PassOwnPtr<WebMessagePortChannelArray> MessagePort::toWebMessagePortChannelArray(PassOwnPtr<MessagePortChannelArray> channels)
{
    OwnPtr<WebMessagePortChannelArray> webChannels;
    if (channels && channels->size()) {
        webChannels = adoptPtr(new WebMessagePortChannelArray(channels->size()));
        for (size_t i = 0; i < channels->size(); ++i)
            (*webChannels)[i] = (*channels)[i].leakPtr();
    }
    return webChannels.release();
}

// static
MessagePortArray* MessagePort::toMessagePortArray(ExecutionContext* context, const WebMessagePortChannelArray& webChannels)
{
    MessagePortArray* ports = nullptr;
    if (!webChannels.isEmpty()) {
        OwnPtr<MessagePortChannelArray> channels = adoptPtr(new MessagePortChannelArray(webChannels.size()));
        for (size_t i = 0; i < webChannels.size(); ++i)
            (*channels)[i] = adoptPtr(webChannels[i]);
        ports = MessagePort::entanglePorts(*context, channels.release());
    }
    return ports;
}

PassOwnPtr<WebMessagePortChannel> MessagePort::disentangle()
{
    ASSERT(m_entangledChannel);
    m_dispatchState->deactivate();
    m_entangledChannel->setClient(0);
    return m_entangledChannel.release();
}

// Invoked to notify us that there are messages available for this port.
// This code may be called from another thread, and so should not call any non-threadsafe APIs (i.e. should not call into the entangled channel or access mutable variables).
void MessagePort::messageAvailable()
{
    const DispatchToken token = m_dispatchState->reserveWake();
    if (token)
        postReservedDispatch(token);
}

void MessagePort::postReservedDispatch(DispatchToken token)
{
    ASSERT(executionContext());
    DispatchTask::Reservation reservation(m_dispatchState.get(), token);
    OwnPtr<DispatchTask> task = adoptPtr(new DispatchTask(m_dispatchState.get(), m_weakFactory.createWeakPtr(), token));
    reservation.handOff();
    // No state mutex is held across posting. A dropped capsule cancels its token.
    executionContext()->postTask(FROM_HERE, task.release());
}

void MessagePort::start()
{
    // Do nothing if we've been cloned or closed.
    if (!isEntangled())
        return;

    ASSERT(executionContext());
    if (m_started)
        return;

    m_started = true;
    messageAvailable();
}

void MessagePort::close()
{
    m_dispatchState->deactivate();
    if (isEntangled())
        m_entangledChannel->setClient(0);
    m_closed = true;
}

void MessagePort::entangle(PassOwnPtr<WebMessagePortChannel> remote)
{
    // Only invoked to set our initial entanglement.
    ASSERT(!m_entangledChannel);
    ASSERT(executionContext());

    m_entangledChannel = remote;
    m_entangledChannel->setClient(this);
}

const AtomicString& MessagePort::interfaceName() const
{
    return EventTargetNames::MessagePort;
}

static bool tryGetMessageFrom(WebMessagePortChannel& webChannel, RefPtr<SerializedScriptValue>& message, OwnPtr<MessagePortChannelArray>& channels)
{
    WebString messageString;
    WebMessagePortChannelArray webChannels;
    if (!webChannel.tryGetMessage(&messageString, webChannels))
        return false;

    if (webChannels.size()) {
        channels = adoptPtr(new MessagePortChannelArray(webChannels.size()));
        for (size_t i = 0; i < webChannels.size(); ++i)
            (*channels)[i] = adoptPtr(webChannels[i]);
    }
    message = SerializedScriptValueFactory::instance().createFromWire(messageString);
    return true;
}

bool MessagePort::tryGetMessage(RefPtr<SerializedScriptValue>& message, OwnPtr<MessagePortChannelArray>& channels)
{
    if (!m_entangledChannel)
        return false;
    return tryGetMessageFrom(*m_entangledChannel, message, channels);
}

bool MessagePort::canDispatchMessages() const
{
    return !m_closed && started() && isEntangled() && executionContext()
        && (!executionContext()->isWorkerGlobalScope() || !toWorkerGlobalScope(executionContext())->isClosing());
}

bool MessagePort::dispatchMessages()
{
    RefPtrWillBeRawPtr<MessagePort> protect(this);

    // Messages for contexts that are not fully active get dispatched too, but JSAbstractEventListener::handleEvent() doesn't call handlers for these.
    // The HTML5 spec specifies that any messages sent to a document that is not fully active should be dropped, so this behavior is OK.
    if (!canDispatchMessages())
        return false;

    RefPtr<SerializedScriptValue> message;
    OwnPtr<MessagePortChannelArray> channels;
    // Bound one owner task even when a callback refills this same receiving queue.
    const unsigned maxMessagesPerDispatch = 16;
    for (unsigned dispatched = 0; dispatched < maxMessagesPerDispatch; ++dispatched) {
        // A previous callback may close, transfer or destroy this context.
        if (!canDispatchMessages() || !tryGetMessage(message, channels))
            return false;

        MessagePortArray* ports = MessagePort::entanglePorts(*executionContext(), channels.release());
        RefPtrWillBeRawPtr<Event> evt = MessageEvent::create(ports, message.release());

        dispatchEvent(evt.release(), ASSERT_NO_EXCEPTION);
    }

    // A full batch asks the owning capsule for one continuation without peeking.
    // A dirty notification racing an empty final read is handled by the cell too.
    return canDispatchMessages();
}

bool MessagePort::hasPendingActivity() const
{
    // The spec says that entangled message ports should always be treated as if they have a strong reference.
    // We'll also stipulate that the queue needs to be open (if the app drops its reference to the port before start()-ing it, then it's not really entangled as it's unreachable).
    return m_started && isEntangled();
}

PassOwnPtr<MessagePortChannelArray> MessagePort::disentanglePorts(ExecutionContext* context, const MessagePortArray* ports, ExceptionState& exceptionState)
{
    if (!ports || !ports->size())
        return nullptr;

    // HashSet used to efficiently check for duplicates in the passed-in array.
    HashSet<MessagePort*> portSet;

    // Walk the incoming array - if there are any duplicate ports, or null ports or cloned ports, throw an error (per section 8.3.3 of the HTML5 spec).
    for (unsigned i = 0; i < ports->size(); ++i) {
        MessagePort* port = (*ports)[i].get();
        if (!port || port->isNeutered() || portSet.contains(port)) {
            String type;
            if (!port)
                type = "null";
            else if (port->isNeutered())
                type = "already neutered";
            else
                type = "a duplicate";
            exceptionState.throwDOMException(DataCloneError, "Port at index "  + String::number(i) + " is " + type + ".");
            return nullptr;
        }
        portSet.add(port);
    }

    UseCounter::count(context, UseCounter::MessagePortsTransferred);

    // Passed-in ports passed validity checks, so we can disentangle them.
    OwnPtr<MessagePortChannelArray> portArray = adoptPtr(new MessagePortChannelArray(ports->size()));
    for (unsigned i = 0; i < ports->size(); ++i)
        (*portArray)[i] = (*ports)[i]->disentangle();
    return portArray.release();
}

MessagePortArray* MessagePort::entanglePorts(ExecutionContext& context, PassOwnPtr<MessagePortChannelArray> channels)
{
    // https://html.spec.whatwg.org/multipage/comms.html#message-ports
    // |ports| should be an empty array, not null even when there is no ports.
    if (!channels || !channels->size())
        return new MessagePortArray;

    MessagePortArray* portArray = new MessagePortArray(channels->size());
    for (unsigned i = 0; i < channels->size(); ++i) {
        MessagePort* port = MessagePort::create(context);
        port->entangle((*channels)[i].release());
        (*portArray)[i] = port;
    }
    return portArray;
}

DEFINE_TRACE(MessagePort)
{
    ActiveDOMObject::trace(visitor);
    EventTargetWithInlineData::trace(visitor);
}

v8::Isolate* MessagePort::scriptIsolate()
{
    ASSERT(executionContext());
    return toIsolate(executionContext());
}

v8::Local<v8::Context> MessagePort::scriptContextForMessageConversion()
{
    ASSERT(executionContext());
    if (!m_scriptStateForConversion) {
        v8::Isolate* isolate = scriptIsolate();
        m_scriptStateForConversion = ScriptState::create(v8::Context::New(isolate), DOMWrapperWorld::create(isolate));
    }
    return m_scriptStateForConversion->context();
}

} // namespace blink
