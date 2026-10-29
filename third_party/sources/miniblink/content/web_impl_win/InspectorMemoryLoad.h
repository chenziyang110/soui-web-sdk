#ifndef InspectorMemoryLoad_h
#define InspectorMemoryLoad_h
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"
#include "third_party/WebKit/Source/platform/network/InspectorResourceSnapshot.h"
#include <memory>

namespace blink { class WebURLLoader; class WebURLLoaderClient; struct WebURLError; class WebData; }
namespace content {
class InspectorMemoryLoad final : public std::enable_shared_from_this<InspectorMemoryLoad> {
public:
    static std::shared_ptr<InspectorMemoryLoad> create(const blink::WebURLRequest&,
        blink::WebURLLoader*, blink::WebURLLoaderClient*);
    static bool synchronous(const blink::WebURLRequest&, blink::WebURLResponse&,
        blink::WebURLError&, blink::WebData&);
    ~InspectorMemoryLoad();
    void start();
    void cancel();
private:
    InspectorMemoryLoad(const blink::WebURLRequest&, blink::WebURLLoader*, blink::WebURLLoaderClient*);
    void pump();
    void fail();
    void retire(bool cancelled);
    std::shared_ptr<blink::InspectorResourceState> m_state;
    blink::WebURLLoader* m_loader;
    blink::WebURLLoaderClient* m_client;
    blink::WebURLRequest m_request;
    blink::InspectorResourceBytes m_bytes;
    unsigned int m_offset = 0;
    bool m_rejected = true, m_responseSent = false, m_cancelled = false,
        m_terminal = false, m_borrowHeld = false, m_inCallback = false;
};
} // namespace content
#endif
