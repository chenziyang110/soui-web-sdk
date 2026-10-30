// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WebURLLoaderImplCurl_h
#define WebURLLoaderImplCurl_h

#include "third_party/WebKit/public/platform/WebURLLoader.h"
#include "third_party/WebKit/Source/platform/Timer.h"
#include <memory>

namespace blink {
class WebURLRequest;
class WebURLLoaderClient;
}

namespace net {
class WebURLLoaderInternal;
class BlobResourceLoader;
}

using namespace blink;

namespace content {
class InspectorMemoryLoad;
class LocalResourceMemoryLoad;

class WebURLLoaderImplCurl : public blink::WebURLLoader {
public:
    WebURLLoaderImplCurl();
    virtual ~WebURLLoaderImplCurl();

    void init();

    // WebURLLoader methods:
    virtual void loadSynchronously(
        const blink::WebURLRequest& request,
        blink::WebURLResponse& response,
        blink::WebURLError& error,
        blink::WebData& data) OVERRIDE;
    virtual void loadAsynchronously(
        const blink::WebURLRequest& request,
        blink::WebURLLoaderClient* client) OVERRIDE;
    virtual void cancel() OVERRIDE;
    virtual void setDefersLoading(bool value) OVERRIDE;
    virtual void didChangePriority(blink::WebURLRequest::Priority new_priority,
        int intra_priority_value) OVERRIDE;
    virtual bool attachThreadedDataReceiver(
        blink::WebThreadedDataReceiver* threaded_data_receiver) OVERRIDE;

    void fileLoadImpl(const blink::KURL& url);

    // Local file subresources are dispatched through the same WebURLLoader
    // interface as network requests. The source Curl loader does not hand
    // file:// URLs to libcurl, so read trusted local subresources directly.
    bool requestCanceled() const { return m_requestCanceled; }

    //void fireFailure(blink::Timer<WebURLLoaderImplCurl>*);
   
    //net::WebURLLoaderInternal* loaderInterna() { return m_webURLLoaderInternal; }

private:
    std::shared_ptr<LocalResourceMemoryLoad> m_localResourceLoad;
    std::shared_ptr<InspectorMemoryLoad> m_inspectorLoad;
    bool* m_hadDestroied;
    int m_jobIds;

    net::BlobResourceLoader* m_blobLoader;
    blink::WebURLLoaderClient* m_client;
    bool m_requestCanceled;
};

}  // namespace content

#endif  // WebURLLoaderImplCurl_h
