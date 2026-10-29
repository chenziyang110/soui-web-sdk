// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/devtools/DevToolsClient.h"

#include "content/devtools/DevToolsAgent.h"
#include "content/devtools/DevToolsMgr.h"
#include "content/devtools/DevToolsProtocolDispatcher.h"
#include "content/browser/WebPage.h"
#include "third_party/WebKit/Source/web/WebViewImpl.h"
#include "third_party/WebKit/Source/core/loader/InspectorResourcePolicy.h"
#include "third_party/WebKit/Source/core/page/Page.h"
#include "third_party/WebKit/public/web/WebDevToolsFrontend.h"
#include "third_party/WebKit/public/web/WebScriptSource.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/platform/WebThread.h"
#include "third_party/WebKit/public/platform/WebTraceLocation.h"
#include "third_party/WebKit/public/platform/Platform.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/Source/core/frame/Frame.h"
#include "third_party/WebKit/Source/core/frame/LocalFrame.h"
#include "base/values.h"
#include "base/json/json_writer.h"
#include "base/json/json_reader.h"

namespace content {

DevToolsClient::DevToolsClient(WebPage* page, blink::WebLocalFrame* frame)
{
    m_id = DevToolsMgr::getInst()->getNewestId();
    DevToolsMgr::getInst()->addLivedId(m_id);

    m_devToolsAgent = nullptr;
    m_page = page;
    m_frame = frame;
    m_webToolsFrontend = blink::WebDevToolsFrontend::create(frame, this, blink::WebString::fromLatin1("zh-cn"));
    m_preferences = new base::DictionaryValue();
    m_devToolsProtocolDispatcher = new DevToolsProtocolDispatcher(this);
}

DevToolsClient::~DevToolsClient()
{
    if (m_devToolsAgent)
        m_devToolsAgent->setDevToolsClient(nullptr);
    delete m_webToolsFrontend;
    delete m_preferences;
    delete m_devToolsProtocolDispatcher;
    DevToolsMgr::getInst()->removeLivedId(m_id);
}

void DevToolsClient::setDevToolsAgent(DevToolsAgent* devToolsAgent)
{
    m_devToolsAgent = devToolsAgent;
}

void DevToolsClient::onMessageReceivedFromEmbedder(int callId, const std::string* response, const std::string* state)
{
    std::string javascript = "DevToolsAPI.dispatchMessage(" + *response + ");";
    blink::WebScriptSource source(blink::WebString::fromUTF8(javascript.c_str()));
    m_frame->executeScript(source);
}

class MessageToAgentTask : public blink::WebThread::Task {
public:
    MessageToAgentTask(DevToolsAgent* devToolsAgent, const std::string& protocolMessage)
    {
        m_devToolsAgent = devToolsAgent;
        m_devToolsAgentId = devToolsAgent->getId();
        m_protocolMessage = new std::string(protocolMessage);
    }
    virtual ~MessageToAgentTask() override
    {
        delete m_protocolMessage;
    }
    virtual void run() override
    {
        if (!DevToolsMgr::getInst()->isLivedId(m_devToolsAgentId))
            return;
        m_devToolsAgent->onMessageReceivedFromFronEnd(m_protocolMessage);
    }

private:
    std::string* m_protocolMessage;
    DevToolsAgent* m_devToolsAgent;
    int m_devToolsAgentId;
};

class DocumentReadyToAgentTask : public blink::WebThread::Task {
public:
    DocumentReadyToAgentTask(int devToolsClientId, DevToolsAgent* devToolsAgent)
    {
        m_devToolsClientId = devToolsClientId;
        m_devToolsAgent = devToolsAgent;
        m_devToolsAgentId = devToolsAgent->getId();
    }
    virtual ~DocumentReadyToAgentTask() override
    {

    }
    virtual void run() override
    {
        if (!DevToolsMgr::getInst()->isLivedId(m_devToolsAgentId))
            return;
        String devToolsAgentId = String::format("%d", m_devToolsClientId);
        m_devToolsAgent->onAttach(devToolsAgentId.utf8().data());
    }

private:
    int m_devToolsClientId;
    DevToolsAgent* m_devToolsAgent;
    int m_devToolsAgentId;
};

void DevToolsClient::onDocumentReady()
{
    if (m_controlledSession) { m_controlledSession->enqueueAttach(getId()); return; }
    if (!m_devToolsAgent) return;
    blink::Platform::current()->currentThread()->postTask(FROM_HERE, 
        new DocumentReadyToAgentTask(getId(), m_devToolsAgent));
}

void DevToolsClient::sendMessageToBackend(const blink::WebString& message)
{
    String messageStr = message;
    String output = messageStr;

    if (m_controlledSession) { m_controlledSession->enqueueToAgent(messageStr.utf8().data()); return; }
    if (!m_devToolsProtocolDispatcher->dispatcher(messageStr.utf8().data()) && m_devToolsAgent != nullptr)
        blink::Platform::current()->currentThread()->postTask(FROM_HERE, new MessageToAgentTask(m_devToolsAgent, messageStr.utf8().data()));

//     output = "sendMessageToBackend:";
//     output.append(messageStr);
//     output.append("\n");
//     OutputDebugStringA(output.utf8().data());
}

void DevToolsClient::sendMessageToEmbedder(const blink::WebString& message)
{
    String messageStr = message;

    std::string method;
    base::ListValue* params = NULL;
    base::DictionaryValue* dict = NULL;
    std::unique_ptr<base::Value> parsed_message = base::JSONReader::Read(messageStr.utf8().data());
    if (!parsed_message || !parsed_message->GetAsDictionary(&dict) || !dict->GetString("method", &method))
        return;

    String output = messageStr;
    
    int requestId = 0;
    dict->GetInteger("id", &requestId);
    dict->GetList("params", &params);

    if (method == "dispatchProtocolMessage" && params && params->GetSize() == 1) {
        std::string protocolMessage;
        if (!params->GetString(0, &protocolMessage))
            return;

        if (m_controlledSession) {
            m_controlledSession->enqueueToAgent(protocolMessage);
        } else if (m_devToolsAgent && !m_devToolsProtocolDispatcher->dispatcher(protocolMessage)) {
            blink::Platform::current()->currentThread()->postTask(FROM_HERE, new MessageToAgentTask(m_devToolsAgent, protocolMessage));
        }
        
    } else if (method == "loadCompleted") {
        blink::WebScriptSource source(blink::WebString::fromUTF8("DevToolsAPI.setUseSoftMenu(true);"));
        m_frame->executeScript(source);
    } else if (method == "loadNetworkResource") {
        if (!m_controlledSession) return; // Preserve the legacy non-controlled path.
        auto retainedSession = m_controlledSession;
        auto* view = m_page ? m_page->webViewImpl() : nullptr;
        auto* policy = view ? blink::InspectorResourcePolicy::from(view->page()) : nullptr;
        auto resources = policy ? policy->state() : nullptr;
        blink::InspectorResourceCallbackBorrow nativeCallout(resources);
        const bool borrowed = resources && resources->beginOperation();
        struct Borrow { std::shared_ptr<blink::InspectorResourceState> state; bool held;
            ~Borrow() { if (held) state->endOperation(false); } } borrow{ resources, borrowed };
        std::string url;
        int streamId = 0;
        blink::InspectorResourceBytes bytes;
        bool allowed = borrowed && params && params->GetSize() == 3
            && params->GetString(0, &url) && url.size() <= 512
            && params->GetInteger(2, &streamId) && streamId > 0
            && resources->lookup(url, bytes) && (bytes.mime == 1 || bytes.mime == 2
                || bytes.mime == 3 || bytes.mime == 4 || bytes.mime == 11);
        // Native-approved text only. Headers are neither read nor forwarded.
        if (allowed && bytes.size && String::fromUTF8(reinterpret_cast<const char*>(bytes.data), bytes.size).isNull())
            allowed = false;
        if (allowed) {
            base::FundamentalValue stream(streamId);
            for (unsigned int offset = 0; offset < bytes.size && !resources->closed();) {
                unsigned int end = offset + ((bytes.size - offset) < 32768 ? bytes.size - offset : 32768);
                while (end < bytes.size && (bytes.data[end] & 0xc0) == 0x80) --end;
                if (end == offset) { allowed = false; break; }
                base::StringValue chunk(std::string(reinterpret_cast<const char*>(bytes.data + offset), end - offset));
                callClientFunction("DevToolsAPI.streamWrite", &stream, &chunk, nullptr);
                offset = end;
            }
            if (resources->closed()) allowed = false;
        }
        base::DictionaryValue response;
        response.SetInteger("statusCode", allowed ? 200 : 403);
        response.Set("headers", new base::DictionaryValue());
        sendMessageAck(requestId, &response);
        return;
    } else if (method == "getPreferences") {
        sendMessageAck(requestId, m_preferences);
        return;
    } else if (method == "setPreference") {
        std::string name;
        std::string value;
        if (!params->GetString(0, &name) || !params->GetString(1, &value))
            return;
        m_preferences->SetStringWithoutPathExpansion(name, value);
    } else if (method == "removePreference") {
        std::string name;
        if (!params->GetString(0, &name))
            return;
        m_preferences->RemoveWithoutPathExpansion(name, nullptr);
    } else if (method == "requestFileSystems") {
        blink::WebScriptSource source(blink::WebString::fromUTF8("DevToolsAPI.fileSystemsLoaded([]);"));
        m_frame->executeScript(source);
    } else {
        return;
    }

    if (requestId)
        sendMessageAck(requestId, nullptr);

    // Protocol bodies may contain page data; default logging is metadata-only.
}

bool DevToolsClient::isUnderTest()
{
    return false;
}

void DevToolsClient::callClientFunction(
    const std::string& functionName,
    const base::Value* arg1,
    const base::Value* arg2,
    const base::Value* arg3)
{
    std::string javascript = functionName + "(";
    if (arg1) {
        std::string json;
        base::JSONWriter::Write(*arg1, &json);
        javascript.append(json);
        if (arg2) {
            base::JSONWriter::Write(*arg2, &json);
            javascript.append(", ").append(json);
            if (arg3) {
                base::JSONWriter::Write(*arg3, &json);
                javascript.append(", ").append(json);
            }
        }
    }
    javascript.append(");");

    blink::WebScriptSource source(blink::WebString::fromUTF8(javascript.c_str()));
    m_frame->executeScript(source);
}


void DevToolsClient::sendMessageAck(int requestId, const base::Value* arg)
{
    base::FundamentalValue idValue(requestId);
    callClientFunction("DevToolsAPI.embedderMessageAck", &idValue, arg, nullptr);
}

void DevToolsClient::closeDefersLoading()
{
    const auto session = m_controlledSession;
    auto* view = m_page ? m_page->webViewImpl() : nullptr;
    auto* page = view ? view->page() : nullptr;
    auto* policy = page ? blink::InspectorResourcePolicy::from(page) : nullptr;
    // Only the native-bound current frontend may restore both existing and new loads.
    // FrameLoader alone leaves Page::defersLoading() set for subsequently created XHRs.
    if (session) {
        LoadingDeferralState state;
        state.controlled = true;
        state.retainedSession = session;
        if (session->acceptsInterrupt() && policy && policy->state() && !policy->state()->closed()) {
            state.controlledPage = page;
            state.pageWasDeferred = page->defersLoading();
        }
        auto* capturedPage = state.controlledPage;
        m_defersLoadingFrameStack.push_back(std::move(state)); // Complete the borrow before setter callouts.
        if (capturedPage) capturedPage->setDefersLoading(false);
        // A denied controlled borrow still has a paired no-op exit; it never falls through to legacy.
        return;
    }
    m_defersLoadingFrameStack.emplace_back();
    auto& frames = m_defersLoadingFrameStack.back().frames;
    for (blink::Frame* frame = blink::toCoreFrame(m_page->mainFrame()); frame; frame = frame->tree().traverseNext()) {
        if (!frame->isLocalFrame())
            continue;

        blink::LocalFrame* localFrame = blink::toLocalFrame(frame);
        frames.append(localFrame);
        localFrame->loader().setDefersLoading(false);
    }
}

void DevToolsClient::openDefersLoading()
{
    if (m_defersLoadingFrameStack.empty()) return;
    auto state = std::move(m_defersLoadingFrameStack.back());
    m_defersLoadingFrameStack.pop_back();
    if (state.controlled) {
        // Restore the captured Page/value even if the session was revoked in the nested loop.
        // The local retained session and existing pause/dispatch pins outlive this callout.
        if (state.controlledPage) state.controlledPage->setDefersLoading(state.pageWasDeferred);
        return;
    }
    auto& frames = state.frames;
    for (size_t i = 0; i < frames.size(); ++i) {
        blink::LocalFrame* localFrame = frames[i];
        localFrame->loader().setDefersLoading(true);
    }
}

void DevToolsClient::willEnterDebugLoop()
{
    closeDefersLoading();
    m_page->willEnterDebugLoop();
}

void DevToolsClient::didExitDebugLoop()
{
    m_page->didExitDebugLoop();
    openDefersLoading();
}

}
