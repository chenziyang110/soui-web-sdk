// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_DEVTOOLS_DEVTOOLS_CLIENT_H_
#define CONTENT_RENDERER_DEVTOOLS_DEVTOOLS_CLIENT_H_

#include "third_party/WebKit/public/web/WebDevToolsFrontendClient.h"
#include "third_party/WebKit/Source/wtf/Vector.h"
#include <string>
#include <memory>
#include <vector>

namespace blink {
class WebDevToolsFrontend;
class WebString;
class WebLocalFrame;
class LocalFrame;
class Page;
}

namespace base {
class Value;
class DictionaryValue;
}

namespace content {

class DevToolsAgent;
class DevToolsProtocolDispatcher;
class WebPage;
class ControlledDevToolsSession;

// Developer tools UI end of communication channel between the render process of
// the page being inspected and tools UI renderer process. All messages will
// go through browser process. On the side of the inspected page there's
// corresponding DevToolsAgent object.
// TODO(yurys): now the client is almost empty later it will delegate calls to
// code in glue
class DevToolsClient : public blink::WebDevToolsFrontendClient {
public:
    DevToolsClient(WebPage* page, blink::WebLocalFrame* frame);
    ~DevToolsClient() override;

    void setDevToolsAgent(DevToolsAgent* devToolsAgent);
    void setControlledSession(const std::shared_ptr<ControlledDevToolsSession>& session) { m_controlledSession = session; }
    void onMessageReceivedFromEmbedder(int callId, const std::string* response, const std::string* state);

    int getId() const { return m_id; }

    void onDocumentReady();

    void willEnterDebugLoop();
    void didExitDebugLoop();

private:
    // WebDevToolsFrontendClient implementation.
    virtual void sendMessageToEmbedder(const blink::WebString& message) override;
    virtual void sendMessageToBackend(const blink::WebString& message) override;
    bool isUnderTest() override;

    void closeDefersLoading();
    void openDefersLoading();

    void sendMessageAck(int request_id, const base::Value* arg);
    void callClientFunction(const std::string& functionName, const base::Value* arg1, const base::Value* arg2, const base::Value* arg3);

    blink::WebDevToolsFrontend* m_webToolsFrontend;

    DISALLOW_COPY_AND_ASSIGN(DevToolsClient);

    blink::WebLocalFrame* m_frame;
    base::DictionaryValue* m_preferences;
    DevToolsAgent* m_devToolsAgent;
    DevToolsProtocolDispatcher* m_devToolsProtocolDispatcher;
    WebPage* m_page;
    int m_id;
    struct LoadingDeferralState {
        Vector<blink::LocalFrame*> frames;
        bool controlled = false;
        blink::Page* controlledPage = nullptr; // Borrowed while the retained session's pause pins are held.
        bool pageWasDeferred = false;
        std::shared_ptr<ControlledDevToolsSession> retainedSession;
    };
    std::vector<LoadingDeferralState> m_defersLoadingFrameStack;
    std::shared_ptr<ControlledDevToolsSession> m_controlledSession;
};

}  // namespace content

#endif  // CONTENT_RENDERER_DEVTOOLS_DEVTOOLS_CLIENT_H_
