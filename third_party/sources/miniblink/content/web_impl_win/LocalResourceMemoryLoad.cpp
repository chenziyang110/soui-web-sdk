#include "config.h"
#include "content/web_impl_win/LocalResourceMemoryLoad.h"
#include "content/web_impl_win/LocalResourceAdmission.h"
#include "platform/network/ResourceRequest.h"
#include "net/RequestExtraData.h"
#include "net/SharedMemoryDataConsumerHandle.h"
#include "net/FixedReceivedData.h"
#include "third_party/WebKit/public/platform/WebData.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLLoaderClient.h"
#include "third_party/WebKit/Source/platform/weborigin/KURL.h"
#include "third_party/WebKit/Source/wtf/CurrentTime.h"
#include "third_party/WebKit/Source/wtf/text/CString.h"
#include <algorithm>

namespace content {
namespace {
std::string localURL(const blink::WebURLRequest& request) {
    const auto text = blink::KURL(request.url()).string().utf8(); return std::string(text.data(), text.length());
}
bool controlled(const blink::WebURLRequest& request) {
    return net::RequestExtraData::isLocalResource(request) || LocalResourceState::reservedURL(localURL(request));
}
void resourceError(blink::WebURLError& error) {
    error.reason = -7402; error.domain = blink::WebString::fromUTF8("SOUIBusinessResource");
    error.localizedDescription = blink::WebString::fromUTF8("Owned local resource denied or unavailable");
}
const char* mimeName(unsigned int mime) {
    static const char* const types[] = { "application/octet-stream", "text/html", "text/javascript",
        "text/css", "application/json", "image/png", "image/svg+xml", "image/gif", "image/jpeg",
        "font/woff", "font/woff2", "text/plain", "application/wasm", "image/x-icon", "image/webp",
        "font/ttf", "font/otf", "application/xml", "application/pdf", "video/mp4", "audio/mpeg" };
    return mime < sizeof(types) / sizeof(*types) ? types[mime] : types[0];
}
void responseFor(const blink::WebURLRequest& request, const LocalResourceBytes& bytes, blink::WebURLResponse& out) {
    out.initialize(); out.setURL(request.url()); out.setHTTPStatusCode(bytes.status);
    out.setHTTPStatusText(blink::WebString::fromUTF8(bytes.status == 200 ? "OK" : "Not Found"));
    out.setExpectedContentLength(bytes.size); out.setMIMEType(blink::WebString::fromUTF8(mimeName(bytes.mime)));
    out.setHTTPHeaderField(blink::WebString::fromUTF8("Content-Type"), blink::WebString::fromUTF8(mimeName(bytes.mime)));
    out.setHTTPHeaderField(blink::WebString::fromUTF8("Cache-Control"), blink::WebString::fromUTF8("no-store"));
    out.setHTTPHeaderField(blink::WebString::fromUTF8("X-Content-Type-Options"), blink::WebString::fromUTF8("nosniff"));
    // Application policy, origin checks and fetch CORS remain Blink's. No
    // inspector CSP or wildcard Access-Control-Allow-Origin is installed here.
}
PassOwnPtr<blink::WebDataConsumerHandle> closedBodyFor(const LocalResourceBytes& bytes) {
    // fetch requires a real consumer handle even for an empty/404 body. Make
    // one closed, independent byte buffer while the snapshot borrow is live.
    // There is no live writer, response queue growth or SDK owner callback
    // after ownership is handed to Blink's Response/Body machinery.
    net::SharedMemoryDataConsumerHandle::Writer* rawWriter = nullptr;
    OwnPtr<blink::WebDataConsumerHandle> handle = adoptPtr(new net::SharedMemoryDataConsumerHandle(
        net::SharedMemoryDataConsumerHandle::kDoNotApplyBackpressure, &rawWriter));
    OwnPtr<net::SharedMemoryDataConsumerHandle::Writer> writer = adoptPtr(rawWriter);
    if (bytes.size) writer->addData(adoptPtr(new net::FixedReceivedData(
        reinterpret_cast<const char*>(bytes.data), bytes.size, bytes.size)));
    writer->close(); writer.clear();
    return handle.release();
}
}
LocalResourceMemoryLoad::LocalResourceMemoryLoad(const blink::WebURLRequest& request,
    blink::WebURLLoader* loader, blink::WebURLLoaderClient* client)
    : m_loader(loader), m_client(client), m_request(request) {
    auto* extra = net::RequestExtraData::from(request.extraData());
    if (extra) m_state = extra->localResourceState;
    m_admission = request.toResourceRequest().localResourceAdmission();
    m_rejected = !extra || extra->localResourceRejected || !m_state
        || !m_admission || request.toResourceRequest().localResourceAdmissionBlocked()
        || request.httpMethod() != blink::WebString::fromUTF8("GET")
        || !m_admission->matches(m_state, localURL(request), "GET");
    if (m_state && !m_state->ownerThread()) { m_state.reset(); m_rejected = true; }
}
std::shared_ptr<LocalResourceMemoryLoad> LocalResourceMemoryLoad::create(const blink::WebURLRequest& request,
    blink::WebURLLoader* loader, blink::WebURLLoaderClient* client) {
    if (!controlled(request)) return nullptr;
    return std::shared_ptr<LocalResourceMemoryLoad>(new LocalResourceMemoryLoad(request, loader, client));
}
LocalResourceMemoryLoad::~LocalResourceMemoryLoad() { cancel(); }
void LocalResourceMemoryLoad::retire(bool cancelled) {
    if (m_inRun) return;
    auto state = m_state;
    m_client = nullptr; m_loader = nullptr; m_bytes = {};
    if (m_admission) {
        if (m_ownsAdmission) m_admission->terminal(); else m_admission->cancel();
    }
    if (m_operationHeld) { m_operationHeld = false; state->endOperation(m_slot, m_borrowed, cancelled); }
    m_borrowed = 0; m_ownsAdmission = false; m_admission.reset(); m_state.reset(); m_request = blink::WebURLRequest();
}
void LocalResourceMemoryLoad::cancel() noexcept {
    if (m_terminal) { m_client = nullptr; m_loader = nullptr; retire(m_cancelled); return; }
    m_cancelled = m_terminal = true; m_client = nullptr; m_loader = nullptr;
    retire(true);
}
void LocalResourceMemoryLoad::start() {
    auto retained = shared_from_this();
    if (!m_client) { cancel(); return; }
    if (!m_rejected && m_admission && m_admission->transfer(retained, m_state, m_slot)) {
        m_operationHeld = m_ownsAdmission = true;
        m_state->post(retained); // OS failure retains the exact task as native FAILED.
        return;
    }
    // Legitimate XHR/fetch/parser paths reserve before Blink creates a loader.
    // This branch is only the final fail-closed fence for an unsupported raw
    // native loader or an invalidated/copy-replayed admission cell.
    if (m_admission) m_admission->cancel();
    m_rejected = true;
    m_state = LocalResourceState::rejection();
    if (!m_state || !m_state->beginOperation(retained, m_slot)) {
        // No operation was admitted; report the actual bounded admission
        // failure synchronously rather than accept an untracked pending job.
        fail(); return;
    }
    m_operationHeld = true;
    m_state->post(retained);
}
void LocalResourceMemoryLoad::fail() {
    if (m_terminal || m_cancelled) { retire(true); return; }
    m_terminal = true;
    auto* client = m_client; auto* loader = m_loader; m_client = nullptr; m_loader = nullptr;
    blink::WebURLError error; resourceError(error);
    // run() or this local borrow protects callbacks even if didFail destroys
    // the URLLoader or synchronously requests native resource retirement.
    auto state = m_state;
    if (state) {
        LocalResourceCallbackBorrow borrow(state);
        try { if (client) client->didFail(loader, error); } catch (...) { state->fault(); }
    } else if (client) client->didFail(loader, error);
    retire(false);
}
void LocalResourceMemoryLoad::run() noexcept {
    auto retained = shared_from_this();
    auto state = m_state;
    if (!state || m_cancelled || m_terminal || !m_client) { retire(true); return; }
    if (LocalResourceState::paused()) {
        // Defensive for direct native callers; the dispatcher checks before
        // popping too. No business callout is made in the nested pause loop.
        state->post(retained);
        return;
    }
    m_inRun = true;
    try {
        if (m_rejected || state->closed()) fail();
        else if (!m_responseSent) {
            if (!state->lookup(localURL(m_request), m_bytes) || !state->borrowBytes(m_bytes.size)) fail();
            else {
                m_borrowed = m_bytes.size; m_responseSent = true;
                blink::WebURLResponse response; responseFor(m_request, m_bytes, response);
                OwnPtr<blink::WebDataConsumerHandle> body;
                if (m_request.useStreamOnResponse()) body = closedBodyFor(m_bytes);
                m_client->didReceiveResponse(m_loader, response, body.leakPtr());
            }
        } else if (m_offset < m_bytes.size) {
            const unsigned int count = (std::min)(32768U, m_bytes.size - m_offset);
            const unsigned int offset = m_offset; m_offset += count;
            m_client->didReceiveData(m_loader, reinterpret_cast<const char*>(m_bytes.data + offset), count, count);
            state->addDelivered(count);
        } else {
            m_terminal = true;
            auto* client = m_client; auto* loader = m_loader; m_client = nullptr; m_loader = nullptr;
            client->didFinishLoading(loader, WTF::monotonicallyIncreasingTime(), m_bytes.size);
        }
        // Client callouts can cancel this operation and physically delete the
        // URLLoader. Access only this retained task and its separate identity.
        if (!m_terminal && !m_cancelled && m_client && !state->closed()) {
            state->post(retained);
        } else if (state->closed() && !m_terminal) cancel();
    } catch (...) { state->fault(); cancel(); }
    m_inRun = false;
    if (m_terminal || m_cancelled || !m_client) retire(m_cancelled);
}
bool LocalResourceMemoryLoad::synchronous(const blink::WebURLRequest& request, blink::WebURLResponse& response,
    blink::WebURLError& error, blink::WebData& data) {
    if (!controlled(request)) return false;
    auto* extra = net::RequestExtraData::from(request.extraData());
    auto state = extra ? extra->localResourceState : nullptr;
    auto admission = request.toResourceRequest().localResourceAdmission();
    if (!state || !state->ownerThread() || extra->localResourceRejected || LocalResourceState::paused()
        || !admission || request.toResourceRequest().localResourceAdmissionBlocked()
        || request.httpMethod() != blink::WebString::fromUTF8("GET")
        || !admission->matches(state, localURL(request), "GET")) {
        if (admission) admission->cancel(); resourceError(error); return true;
    }
    struct SyncTask : LocalResourceTask { bool cancelled = false;
        void run() noexcept override { } void cancel() noexcept override { cancelled = true; } };
    auto task = std::make_shared<SyncTask>(); unsigned int slot = 0;
    if (!admission->transfer(task, state, slot)) { admission->cancel(); resourceError(error); return true; }
    struct Operation { std::shared_ptr<LocalResourceState> state; std::shared_ptr<blink::LocalResourceAdmission> admission;
        unsigned int slot, bytes = 0;
        ~Operation() { admission->terminal(); state->endOperation(slot, bytes, state->closed()); } } operation{state, admission, slot};
    LocalResourceCallbackBorrow borrow(state); LocalResourceBytes bytes;
    try {
        if (!state->lookup(localURL(request), bytes) || task->cancelled || !state->borrowBytes(bytes.size)) { resourceError(error); return true; }
        operation.bytes = bytes.size;
        responseFor(request, bytes, response);
        if (bytes.size) data.assign(reinterpret_cast<const char*>(bytes.data), bytes.size); else data.reset();
        state->addDelivered(bytes.size);
    } catch (...) { state->fault(); resourceError(error); }
    return true;
}
}
