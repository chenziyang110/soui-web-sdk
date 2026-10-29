#include "config.h"
#include "content/web_impl_win/LocalResourceAdmission.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/LocalFrame.h"
#include "platform/network/ResourceRequest.h"
#include "platform/weborigin/KURL.h"
#include "wtf/text/CString.h"
namespace blink {
namespace {
std::string text(const KURL& url) { const auto value = url.string().utf8(); return std::string(value.data(), value.length()); }
std::string method(const ResourceRequest& request) { const auto value = request.httpMethod().utf8(); return std::string(value.data(), value.length()); }
}
bool isLocalResourceNamespace(const KURL& url) {
    if (url.protocolIs("data") || url.protocolIs("blob") || url.protocolIs("javascript") || url.protocolIs("about")) return false;
    const auto host = url.host().utf8();
    if (!host.length()) return false;
    return content::LocalResourceState::reservedURL("https://" + std::string(host.data(), host.length()) + "/");
}
bool hasLocalResourceOwner(ExecutionContext* context) {
    return context && context->isDocument()
        && !!content::LocalResourceState::forPage(toDocument(context)->page());
}
void identifyLocalResourceRequest(const void* page, const KURL& documentUrl, bool mainFrame, ResourceRequest& request) {
    auto existing = request.localResourceAdmission();
    if (!isLocalResourceNamespace(request.url()) && !existing) return;
    auto owner = content::LocalResourceState::forPage(page);
    const bool source = mainFrame && owner && (request.frameType() == WebURLRequest::FrameTypeTopLevel
        ? owner->permitsDocument(text(request.url()), true) : owner->permitsDocument(text(documentUrl), false));
    if (request.localResourceAdmissionBlocked() || !source || !owner->permitsURL(text(request.url()))
        || method(request) != "GET" || (existing && !existing->matches(owner, text(request.url()), method(request)))) {
        request.setLocalResourceAdmissionBlocked(); return;
    }
    if (!existing) request.setLocalResourceAdmission(std::make_shared<LocalResourceAdmission>(owner, text(request.url()), method(request)));
}
bool admitLocalResourceRequest(ResourceRequest& request) {
    auto cell = request.localResourceAdmission();
    if (!isLocalResourceNamespace(request.url()) && !cell && !request.localResourceAdmissionBlocked()) return true;
    if (request.localResourceAdmissionBlocked() || !cell || !cell->matches(cell->owner(), text(request.url()), method(request))) return false;
    return cell->reserve();
}
bool prepareLocalResourceRequest(ExecutionContext* context, ResourceRequest& request) {
    if (!isLocalResourceNamespace(request.url()) && !request.localResourceAdmission()) return true;
    if (!context || !context->isDocument()) { request.setLocalResourceAdmissionBlocked(); return false; }
    auto* document = toDocument(context); auto* frame = document->frame();
    identifyLocalResourceRequest(document->page(), document->url(), frame && !frame->tree().parent(), request);
    return admitLocalResourceRequest(request);
}
void abandonLocalResourceRequest(ResourceRequest& request) {
    if (auto cell = request.localResourceAdmission()) cell->abandonUnclaimed();
    request.setLocalResourceAdmission(nullptr);
}
void claimLocalResourceLoaderReservation(ResourceRequest& request) {
    if (auto cell = request.localResourceAdmission()) cell->claimLoader();
}
void cancelLocalResourceLoaderReservation(ResourceRequest& request) {
    if (auto cell = request.localResourceAdmission()) cell->cancel();
    request.setLocalResourceAdmission(nullptr);
}
}
