// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/devtools/DevToolsAgent.h"
#include "content/web_impl_win/LocalResourceState.h"
#include "content/devtools/DevToolsClient.h"
#include "content/devtools/DevToolsMgr.h"
#include "content/browser/CheckReEnter.h"
#include "content/browser/WebPageImpl.h"
#include "content/browser/WebPage.h"
#include "third_party/WebKit/public/web/WebDevToolsAgent.h"
#include "third_party/WebKit/public/web/WebScriptSource.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebThread.h"
#include "third_party/WebKit/public/platform/WebTraceLocation.h"
#include "third_party/WebKit/public/platform/Platform.h"
#include "third_party/WebKit/Source/bindings/core/v8/V8RecursionScope.h"
#include "gen/blink/platform/RuntimeEnabledFeatures.h"
#include "third_party/WebKit/Source/core/inspector/MainThreadDebugger.h"
#include "third_party/WebKit/Source/core/inspector/InspectorTaskRunner.h"
#include <v8.h>

namespace content {
namespace {
// Blink keeps one message-loop object for the process. The actual frame's
// client callbacks supply the current owner and retain its controlled session.
struct PauseOwner {
    DevToolsAgent* agent;
    int agentId;
    std::shared_ptr<ControlledDevToolsSession> session;
};
std::vector<PauseOwner> s_pauseOwners; // Engine owner only, strict LIFO.
// Engine-only reentry suppression for this servicing site; never an admission authority.
bool s_controlledInterruptDrainActive = false;
}


DevToolsAgent::DevToolsAgent(WebPage* page, blink::WebLocalFrame* frame)
{
    m_isAttached = false;
    m_devToolsClient = nullptr;
    m_id = DevToolsMgr::getInst()->getNewestId();
    DevToolsMgr::getInst()->addLivedId(m_id);

    m_page = page;
    m_frame = frame;
    m_frame->setDevToolsAgentClient(this);
}

DevToolsAgent::~DevToolsAgent()
{
    if (m_devToolsClient)
        m_devToolsClient->setDevToolsAgent(nullptr);
    DevToolsMgr::getInst()->removeLivedId(m_id);
}

void DevToolsAgent::onAttach(const std::string& hostId)
{
    blink::WebDevToolsAgent* webAgent = getWebAgent();
    if (webAgent) {
        webAgent->attach(blink::WebString::fromUTF8(hostId.c_str()));
        m_isAttached = true;
        if (m_controlledSession) m_controlledSession->attached();
    }
}

void DevToolsAgent::onDetach()
{
    blink::WebDevToolsAgent* webAgent = getWebAgent();
    if (webAgent) {
        webAgent->detach();
        m_isAttached = false;
        if (m_controlledSession) m_controlledSession->detached();
    }
}

void DevToolsAgent::discardControlledInterrupts()
{
    // Owner-only, closing, outside pause/dispatch. This is the inspector task
    // queue only, never the SDK FIFO. It also works before backend attach:
    // cancelled descriptors return no agent and retire their reservations.
    if (!m_controlledSession || m_controlledSession->acceptsInterrupt()) return;
    blink::MainThreadDebugger* debugger = blink::MainThreadDebugger::instance();
    if (debugger && debugger->taskRunner()) debugger->taskRunner()->runPendingTasks();
}

void DevToolsAgent::setDevToolsClient(DevToolsClient* devToolsClient)
{
    m_devToolsClient = devToolsClient;
}

void DevToolsAgent::inspectElementAt(int x, int y)
{
    blink::WebDevToolsAgent* webAgent = getWebAgent();
    if (webAgent)
        webAgent->inspectElementAt(blink::WebPoint(x, y));
}

blink::WebDevToolsAgent* DevToolsAgent::getWebAgent()
{
    return m_frame ? m_frame->devToolsAgent() : nullptr;
}

bool DevToolsAgent::isDevToolsClientConnet() const
{
    return !!m_devToolsClient;
}

class InterruptMessageImpl : public blink::WebDevToolsAgent::MessageDescriptor{
public:
    InterruptMessageImpl(const std::string& message, DevToolsAgent* agent,
        const std::shared_ptr<ControlledDevToolsSession>& session = nullptr)
        : m_msg(message)
        , m_agent(agent)
        , m_devToolsAgentId(m_agent->getId())
        , m_session(session)
    {
        // Reservation was admitted before descriptor allocation.
    }
    ~InterruptMessageImpl() override { if (m_session) m_session->interruptDisposed(m_msg.size()); }

    blink::WebDevToolsAgent* agent() override
    {
        if ((m_session && !m_session->acceptsInterrupt()) || !DevToolsMgr::getInst()->isLivedId(m_devToolsAgentId))
            return nullptr;
        return m_agent->getWebAgent();
    }

    blink::WebString message() override { return blink::WebString::fromUTF8(m_msg); }

private:
    std::string m_msg;
    DevToolsAgent* m_agent;
    int m_devToolsAgentId;
    std::shared_ptr<ControlledDevToolsSession> m_session;
};

void DevToolsAgent::onMessageReceivedFromFronEnd(const std::string* message)
{
    //TRACE_EVENT0("devtools", "DevToolsAgent::OnDispatchOnInspectorBackend");
    blink::WebDevToolsAgent* webAgent = getWebAgent();
    if (!webAgent)
        return;

    if (blink::WebDevToolsAgent::shouldInterruptForMessage(blink::WebString::fromUTF8(message->c_str()))) {
        const auto session = m_controlledSession;
        const int agentId = m_id;
        if (session && !session->reserveInterrupt(message->size())) return;
        std::unique_ptr<InterruptMessageImpl> descriptor;
        try { descriptor.reset(new InterruptMessageImpl(*message, this, session)); }
        catch (...) { if (session) session->interruptDisposed(message->size()); throw; }
        blink::WebDevToolsAgent::interruptAndDispatch(descriptor.release());
        // The interrupt enqueue call has returned; creationMutex is no longer held.
        // Never read the Engine-only pause LIFO before the actual owner proof.
        if (!session || !wke::controlledDevToolsEngineThread()) return;
        if (s_controlledInterruptDrainActive) return;
        const auto samePausedOwner = [this, agentId, &session]() {
            if (!wke::controlledDevToolsEngineThread()) return false;
            if (s_pauseOwners.empty()) return false;
            const PauseOwner& owner = s_pauseOwners.back();
            return owner.agent == this && owner.agentId == agentId && owner.session == session
                && DevToolsMgr::getInst()->isLivedId(agentId)
                && m_controlledSession == session && session->acceptsInterrupt();
        };
        if (!samePausedOwner()) return;
        blink::WebDevToolsAgent* pausedAgent = getWebAgent();
        if (!pausedAgent || !samePausedOwner()) return;
        s_controlledInterruptDrainActive = true;
        struct DrainExit {
            ~DrainExit() { s_controlledInterruptDrainActive = false; }
        } drainExit;
        // Existing Blink path services the already queued Inspector descriptors.
        // It does not dispatch this interrupt message a second time or pump SDK FIFO.
        pausedAgent->dispatchOnInspectorBackend(blink::WebString::fromUTF8(message->c_str()));
        if (!samePausedOwner()) return; // No further callout after close/identity change.
        return;
    }
    
    webAgent->dispatchOnInspectorBackend(blink::WebString::fromUTF8(message->c_str()));
}

class MessageToClientTask : public blink::WebThread::Task {
public:
    MessageToClientTask(DevToolsClient* devToolsClient, int callId, const blink::WebString& response, const blink::WebString& state)
    {
        m_devToolsClient = devToolsClient;
        m_devToolsClientId = devToolsClient->getId();

        m_callId = callId;
        m_response = new std::string(response.utf8());
        m_state = new std::string(state.utf8());
    }
    virtual ~MessageToClientTask() override
    {
        delete m_response;
        delete m_state;
    }
    virtual void run() override
    {
        if (!DevToolsMgr::getInst()->isLivedId(m_devToolsClientId))
            return;
        m_devToolsClient->onMessageReceivedFromEmbedder(m_callId, m_response, m_state);
    }

private:
    int m_callId;
    std::string* m_response;
    std::string* m_state;
    DevToolsClient* m_devToolsClient;
    int m_devToolsClientId;
};

void DevToolsAgent::sendProtocolMessage(int callId, const blink::WebString& response, const blink::WebString& state)
{
    if (!m_devToolsClient)
        return;

    if (m_controlledSession) {
        m_controlledSession->enqueueToClient(callId, response.utf8().data(), state.utf8().data());
        return;
    }

    blink::Platform::current()->currentThread()->postTask(FROM_HERE,
        new MessageToClientTask(m_devToolsClient, callId, response, state));
}

class WebKitClientMessageLoopImpl : public blink::WebDevToolsAgentClient::WebKitClientMessageLoop {
public:
    WebKitClientMessageLoopImpl() {}
    ~WebKitClientMessageLoopImpl() override {}

    void runImpl(DevToolsAgent* agent)
    {
        MSG message = {};
        BOOL result = 0;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            // Existing isolate-wide checkpoint stays observable. It may run
            // target microtasks too; no claim that all business JS is frozen.
            v8::Isolate* isolate = v8::Isolate::GetCurrent();
            blink::V8RecursionScope recursionScope(isolate);
            isolate->RunMicrotasks();
            const auto session = agent->controlledSession();
            if (session) session->pausedMicrotaskCheckpoint();
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (!m_quitStack.empty() && m_quitStack.back()) return;
        }
        // Preserve a real thread quit for the outer engine loop.
        if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
    }
    void run() override
    {
        if (s_pauseOwners.empty()) return;
        const PauseOwner owner = s_pauseOwners.back(); // Strong session borrow.
        if (!DevToolsMgr::getInst()->isLivedId(owner.agentId)) return;
        m_quitStack.push_back(false);
        owner.agent->willEnterDebugLoopInRun();
        runImpl(owner.agent);
        owner.agent->didExitDebugLoopInRun();
        m_quitStack.pop_back();
    }
    void quitNow() override
    {
        if (!m_quitStack.empty()) m_quitStack.back() = true;
    }
private:
    std::vector<bool> m_quitStack;
};

blink::WebDevToolsAgentClient::WebKitClientMessageLoop* DevToolsAgent::createClientMessageLoop()
{
    return new WebKitClientMessageLoopImpl();
}

void DevToolsAgent::willEnterDebugLoopInRun()
{
    // The controlled session pins client, agent and both broker views before
    // any nested Windows dispatch can request close.
    bool restoreCheckReEnter = false;
    if (blink::RuntimeEnabledFeatures::updataInOtherThreadEnabled()) {
        RELEASE_ASSERT(0 == CheckReEnter::getEnterCount());
    } else if (0 < CheckReEnter::getEnterCount()) {
        CheckReEnter::decrementEnterCount();
        restoreCheckReEnter = true;
    }
    m_debugLoopRestoreCheckReEnter.push_back(restoreCheckReEnter);
    blink::ThreadState::current()->enterGCForbiddenScope();
    m_page->willEnterDebugLoop();
    m_devToolsClient->willEnterDebugLoop();
}

void DevToolsAgent::didExitDebugLoopInRun()
{
    blink::ThreadState::current()->leaveGCForbiddenScope();
    // WebKit still uses page/agent after this callback. Session retirement is
    // an explicit outer-engine operation, never performed at this point.
    m_page->didExitDebugLoop();
    m_devToolsClient->didExitDebugLoop();
    if (blink::RuntimeEnabledFeatures::updataInOtherThreadEnabled()) {
        RELEASE_ASSERT(0 == CheckReEnter::getEnterCount());
    } else if (!m_debugLoopRestoreCheckReEnter.empty() && m_debugLoopRestoreCheckReEnter.back()) {
        CheckReEnter::incrementEnterCount();
    }
    if (!m_debugLoopRestoreCheckReEnter.empty()) m_debugLoopRestoreCheckReEnter.pop_back();

}

void DevToolsAgent::willEnterDebugLoop()
{
    // Called by ClientMessageLoopAdapter for the actual paused frame, before
    // input and active-object restoration are borrowed by its global loop.
    PauseOwner owner = { this, getId(), m_controlledSession };
    s_pauseOwners.push_back(owner);
    LocalResourceState::enterPause();
    if (owner.session) owner.session->enterPause();
}

void DevToolsAgent::didExitDebugLoop()
{
    RELEASE_ASSERT(!s_pauseOwners.empty() && s_pauseOwners.back().agent == this);
    const PauseOwner owner = s_pauseOwners.back();
    s_pauseOwners.pop_back();
    if (owner.session) owner.session->exitPause();
    LocalResourceState::leavePause();
}

void DevToolsAgent::enableTracing(const blink::WebString& category_filter)
{

}

void DevToolsAgent::disableTracing()
{

}

}