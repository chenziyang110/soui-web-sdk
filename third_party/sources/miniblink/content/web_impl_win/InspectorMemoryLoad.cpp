#include "config.h"
#include "content/web_impl_win/InspectorMemoryLoad.h"
#include "net/RequestExtraData.h"
#include "third_party/WebKit/public/platform/WebData.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLLoaderClient.h"
#include "third_party/WebKit/Source/platform/weborigin/KURL.h"
#include "third_party/WebKit/Source/wtf/CurrentTime.h"
#include "third_party/WebKit/Source/wtf/text/CString.h"
#include <algorithm>

namespace content {
namespace {
struct CallbackBorrow {
    bool& active;
    explicit CallbackBorrow(bool& value) : active(value) { active = true; }
    ~CallbackBorrow() { active = false; }
};
std::string serializedURL(const blink::WebURLRequest& request) {
    const auto value = blink::KURL(request.url()).string().utf8();
    return std::string(value.data(), value.length());
}
void resourceError(blink::WebURLError& error) {
    error.reason = -7401;
    error.domain = blink::WebString::fromUTF8("SOUIInspectorResource");
    error.localizedDescription = blink::WebString::fromUTF8("Controlled inspector resource denied");
}
const char* mimeName(unsigned int mime) {
    static const char* const types[] = { "application/octet-stream", "text/html", "text/javascript",
        "text/css", "application/json", "image/png", "image/svg+xml", "image/gif", "image/jpeg",
        "font/woff", "font/woff2", "text/plain", "application/wasm" };
    return mime < sizeof(types) / sizeof(*types) ? types[mime] : types[0];
}
void responseFor(const blink::WebURLRequest& request, const blink::InspectorResourceBytes& bytes,
    blink::WebURLResponse& response) {
    response.initialize(); response.setURL(request.url());
    response.setHTTPStatusCode(200); response.setHTTPStatusText(blink::WebString::fromUTF8("OK"));
    response.setExpectedContentLength(bytes.size);
    response.setMIMEType(blink::WebString::fromUTF8(mimeName(bytes.mime)));
    response.setHTTPHeaderField(blink::WebString::fromUTF8("Content-Type"), blink::WebString::fromUTF8(mimeName(bytes.mime)));
    response.setHTTPHeaderField(blink::WebString::fromUTF8("Cache-Control"), blink::WebString::fromUTF8("no-store"));
    response.setHTTPHeaderField(blink::WebString::fromUTF8("X-Content-Type-Options"), blink::WebString::fromUTF8("nosniff"));
    response.setHTTPHeaderField(blink::WebString::fromUTF8("Content-Security-Policy"), blink::WebString::fromUTF8(
        "default-src 'self'; script-src 'self' 'unsafe-eval'; style-src 'self' 'unsafe-inline'; "
        "img-src 'self'; font-src 'self'; connect-src 'self'; child-src 'none'; object-src 'none'; "
        "media-src 'none'; base-uri 'none'; form-action 'none'"));
}
bool controlled(const blink::WebURLRequest& request) {
    return net::RequestExtraData::isInspector(request) || blink::InspectorResourceState::reservedURL(serializedURL(request));
}
}
InspectorMemoryLoad::InspectorMemoryLoad(const blink::WebURLRequest& request, blink::WebURLLoader* loader,
    blink::WebURLLoaderClient* client) : m_loader(loader), m_client(client), m_request(request) {
    auto* extra = net::RequestExtraData::from(request.extraData());
    if (extra) m_state = extra->inspectorState;
    m_rejected = !extra || extra->inspectorRejected || !m_state || request.httpMethod() != blink::WebString::fromUTF8("GET");
    if (m_state) m_borrowHeld = m_state->beginOperation();
    if (!m_borrowHeld) m_rejected = true;
}
std::shared_ptr<InspectorMemoryLoad> InspectorMemoryLoad::create(const blink::WebURLRequest& request,
    blink::WebURLLoader* loader, blink::WebURLLoaderClient* client) {
    if (!controlled(request)) return nullptr;
    return std::shared_ptr<InspectorMemoryLoad>(new InspectorMemoryLoad(request, loader, client));
}
InspectorMemoryLoad::~InspectorMemoryLoad() { cancel(); }
void InspectorMemoryLoad::retire(bool wasCancelled) {
    if (!m_borrowHeld) return;
    m_borrowHeld = false; m_state->endOperation(wasCancelled);
}
void InspectorMemoryLoad::cancel() {
    m_cancelled = true; m_client = nullptr; m_loader = nullptr;
    // An in-progress callback owns its borrow until it returns to pump/fail.
    if (!m_terminal) { m_terminal = true; if (!m_inCallback) retire(true); }
}
void InspectorMemoryLoad::start() {
    auto retained = shared_from_this();
    if (!m_client) { cancel(); return; }
    if (m_rejected || !m_state->post([retained] { retained->pump(); })) { fail(); return; }
}
void InspectorMemoryLoad::fail() {
    blink::InspectorResourceCallbackBorrow nativeCallout(m_state);
    if (m_terminal || m_cancelled) { retire(true); return; }
    m_terminal = true;
    auto* client = m_client; auto* loader = m_loader; m_client = nullptr;
    blink::WebURLError error; resourceError(error);
    try { CallbackBorrow callback(m_inCallback); if (client) client->didFail(loader, error); }
    catch (...) { if (m_state) m_state->close(); }
    retire(false);
}
void InspectorMemoryLoad::pump() {
    auto retained = shared_from_this();
    blink::InspectorResourceCallbackBorrow nativeCallout(m_state);
    if (m_terminal || m_cancelled || !m_client) { retire(true); return; }
    if (m_rejected || m_state->closed()) { fail(); return; }
    try {
        if (!m_responseSent) {
            if (!m_state->lookup(serializedURL(m_request), m_bytes)) { fail(); return; }
            m_responseSent = true;
            blink::WebURLResponse response; responseFor(m_request, m_bytes, response);
            { CallbackBorrow callback(m_inCallback); m_client->didReceiveResponse(m_loader, response); }
        } else if (m_offset < m_bytes.size) {
            const unsigned int count = (std::min)(32768U, m_bytes.size - m_offset);
            const unsigned int offset = m_offset; m_offset += count;
            { CallbackBorrow callback(m_inCallback);
              m_client->didReceiveData(m_loader, reinterpret_cast<const char*>(m_bytes.data + offset), count, count); }
            m_state->deliveredBytes.fetch_add(count);
        } else {
            m_terminal = true;
            auto* client = m_client; auto* loader = m_loader; m_client = nullptr;
            { CallbackBorrow callback(m_inCallback); client->didFinishLoading(loader, WTF::monotonicallyIncreasingTime(), m_bytes.size); }
            retire(false); return;
        }
        // Any callback can synchronously cancel and destroy the URLLoader.
        // From here on access only this separately retained operation storage.
        if (m_cancelled || m_terminal || !m_client) { retire(true); return; }
        if (!m_state->post([retained] { retained->pump(); })) fail();
    } catch (...) {
        m_state->close();
        if (m_terminal) retire(true); else fail();
    }
}
bool InspectorMemoryLoad::synchronous(const blink::WebURLRequest& request, blink::WebURLResponse& response,
    blink::WebURLError& error, blink::WebData& data) {
    if (!controlled(request)) return false;
    auto* extra = net::RequestExtraData::from(request.extraData());
    auto state = extra ? extra->inspectorState : nullptr;
    const bool borrowed = state && state->beginOperation();
    struct Borrow { std::shared_ptr<blink::InspectorResourceState> state; bool held;
        ~Borrow() { if (held) state->endOperation(false); } } borrow{ state, borrowed };
    blink::InspectorResourceBytes bytes;
    if (!borrowed || extra->inspectorRejected || request.httpMethod() != blink::WebString::fromUTF8("GET")
        || !state->lookup(serializedURL(request), bytes)) { resourceError(error); return true; }
    responseFor(request, bytes, response);
    data.assign(reinterpret_cast<const char*>(bytes.data), bytes.size);
    state->deliveredBytes.fetch_add(bytes.size);
    return true;
}
} // namespace content
