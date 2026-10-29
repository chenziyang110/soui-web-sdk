#ifndef RequestExtraData_h
#define RequestExtraData_h

#include "content/browser/WebPage.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/Source/platform/network/InspectorResourceSnapshot.h"
#include <atomic>
#include <mutex>
#include <set>

namespace blink {
class WebLocalFrame;
}

#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
namespace content {
class WebPage;
}
#endif

namespace content { class LocalResourceState; }

namespace net {

class RequestExtraData : public blink::WebURLRequest::ExtraData {
public:
    RequestExtraData() : m_frameId(0), m_isDownload(false)
    {
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
        page = nullptr;
#endif
    }
    // Ordinary requests keep their original allocation path. Only metadata
    // manufactured by the controlled Page binding enters the type registry.
    struct InspectorTag { };
    explicit RequestExtraData(InspectorTag) : RequestExtraData()
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        registry().insert(this);
        m_registered = true;
        registryCount().fetch_add(1, std::memory_order_release);
    }
    struct LocalResourceTag { };
    explicit RequestExtraData(LocalResourceTag) : RequestExtraData(InspectorTag{}) { }
    virtual ~RequestExtraData() override
    {
        if (!m_registered) return;
        std::lock_guard<std::mutex> lock(registryMutex());
        registry().erase(this);
        registryCount().fetch_sub(1, std::memory_order_release);
    }
    // ExtraData has no RTTI contract. Only pointers registered by this exact
    // native type may be cast; unknown metadata on a controlled Page is denied.
    static RequestExtraData* from(blink::WebURLRequest::ExtraData* value)
    {
        if (!value || !registryCount().load(std::memory_order_acquire)) return nullptr;
        std::lock_guard<std::mutex> lock(registryMutex());
        return registry().count(value) ? static_cast<RequestExtraData*>(value) : nullptr;
    }
    static bool isInspector(const blink::WebURLRequest& request)
    {
        auto* extra = from(request.extraData());
        return extra && (extra->inspectorState || extra->inspectorRejected);
    }
    static bool isLocalResource(const blink::WebURLRequest& request)
    {
        auto* extra = from(request.extraData());
        return extra && (extra->localResourceState || extra->localResourceRejected);
    }
    std::shared_ptr<content::LocalResourceState> localResourceState;
    bool localResourceRejected = false;
    std::shared_ptr<blink::InspectorResourceState> inspectorState;
    bool inspectorRejected = false;

    void setIsDownload(const String& downloadName)
    {
        m_downloadName = downloadName;
        m_isDownload = true;
    }

    bool isDownload() const
    {
        return m_isDownload;
    }

    String getDownloadName() const
    {
        return m_downloadName;
    }

    blink::WebLocalFrame* getFrame()
    {
        if (!page)
            return nullptr;

        blink::WebFrame* frame = page->getWebFrameFromFrameId(m_frameId);
        if (!frame)
            return nullptr;

        if (!frame->isWebLocalFrame())
            return nullptr;

        return frame->toWebLocalFrame();
    }

    void setFrame(blink::WebLocalFrame* frame)
    {
        m_frameId = page->getFrameIdByBlinkFrame(reinterpret_cast<const blink::WebFrame*>(frame));
    }

private:
    bool m_registered = false;
    static std::atomic<unsigned int>& registryCount() { static std::atomic<unsigned int> value{0}; return value; }
    static std::mutex& registryMutex() { static std::mutex value; return value; }
    static std::set<blink::WebURLRequest::ExtraData*>& registry()
    { static std::set<blink::WebURLRequest::ExtraData*> value; return value; }
    //blink::WebLocalFrame* frame;
    int64_t m_frameId;

public:
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    content::WebPage* page;
#endif
    String m_downloadName;
    bool m_isDownload;
};

} // net

#endif // RequestExtraData_h