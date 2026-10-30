#include "config.h"
#include "core/loader/InspectorResourcePolicy.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "platform/weborigin/KURL.h"
#include "wtf/text/CString.h"

namespace blink {
const char* InspectorResourcePolicy::supplementName() { return "SOUI.Stage0.InspectorResourcePolicy.2"; }
InspectorResourcePolicy* InspectorResourcePolicy::from(Page* page) {
    return static_cast<InspectorResourcePolicy*>(WillBeHeapSupplement<Page>::from(page, supplementName()));
}
bool InspectorResourcePolicy::install(Page& page, const std::shared_ptr<InspectorResourceState>& state) {
    if (!state || from(&page)) return false;
    WillBeHeapSupplement<Page>::provideTo(page, supplementName(), adoptPtrWillBeNoop(new InspectorResourcePolicy(state)));
    return true;
}
InspectorResourcePolicy::~InspectorResourcePolicy() {
    m_state->close();
    m_state->finishCloseOnOwner();
}
bool InspectorResourcePolicy::isControlled(ExecutionContext* context) {
    // This increment rejects Worker/SharedWorker before creation from a marked
    // Document. No controlled Worker is admitted and no worker-origin inference
    // is used to manufacture a Page grant.
    if (!context || !context->isDocument()) return false;
    auto* document = toDocument(context);
    auto* policy = from(document->page());
    if (!policy) return false;
    ++policy->m_state->unsupported;
    return true;
}
bool InspectorResourcePolicy::permits(const KURL& url, bool mainNavigation) const {
    const auto encoded = url.string().utf8();
    const std::string serialized(encoded.data(), encoded.length());
    return mainNavigation ? m_state->allowMainNavigation(serialized) : m_state->permits(serialized);
}
} // namespace blink
