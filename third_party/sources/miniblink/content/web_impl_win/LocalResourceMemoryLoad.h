#ifndef SOUI_LOCAL_RESOURCE_MEMORY_LOAD_H
#define SOUI_LOCAL_RESOURCE_MEMORY_LOAD_H
#include "content/web_impl_win/LocalResourceState.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"
namespace blink { class WebURLLoader; class WebURLLoaderClient; struct WebURLError; class WebData; }
namespace blink { class LocalResourceAdmission; }
namespace content {
class LocalResourceMemoryLoad final : public LocalResourceTask,
    public std::enable_shared_from_this<LocalResourceMemoryLoad> {
public:
    static std::shared_ptr<LocalResourceMemoryLoad> create(const blink::WebURLRequest&,
        blink::WebURLLoader*, blink::WebURLLoaderClient*);
    static bool synchronous(const blink::WebURLRequest&, blink::WebURLResponse&, blink::WebURLError&, blink::WebData&);
    ~LocalResourceMemoryLoad();
    void start();
    void cancel() noexcept override;
    void run() noexcept override;
private:
    LocalResourceMemoryLoad(const blink::WebURLRequest&, blink::WebURLLoader*, blink::WebURLLoaderClient*);
    void fail();
    void retire(bool);
    std::shared_ptr<LocalResourceState> m_state;
    std::shared_ptr<blink::LocalResourceAdmission> m_admission;
    blink::WebURLLoader* m_loader = nullptr;
    blink::WebURLLoaderClient* m_client = nullptr;
    blink::WebURLRequest m_request;
    LocalResourceBytes m_bytes;
    unsigned int m_slot = 0, m_borrowed = 0, m_offset = 0;
    bool m_rejected = false, m_operationHeld = false, m_ownsAdmission = false, m_inRun = false,
        m_cancelled = false, m_terminal = false, m_responseSent = false;
};
}
#endif
