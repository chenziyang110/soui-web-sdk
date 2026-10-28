// Private stage-0 Page authority. A closing policy remains installed as a deny
// tombstone; URL/referrer/worker origin never grants this authority.
#ifndef InspectorResourcePolicy_h
#define InspectorResourcePolicy_h
#include "core/CoreExport.h"
#include "core/page/Page.h"
#include "platform/Supplementable.h"
#include "platform/network/InspectorResourceSnapshot.h"

namespace blink {
class ExecutionContext;
class KURL;
class CORE_EXPORT InspectorResourcePolicy final
    : public NoBaseWillBeGarbageCollectedFinalized<InspectorResourcePolicy>, public WillBeHeapSupplement<Page> {
    WILL_BE_USING_GARBAGE_COLLECTED_MIXIN(InspectorResourcePolicy);
public:
    static InspectorResourcePolicy* from(Page*);
    static bool install(Page&, const std::shared_ptr<InspectorResourceState>&);
    static bool isControlled(ExecutionContext*);
    static const char* supplementName();
    const std::shared_ptr<InspectorResourceState>& state() const { return m_state; }
    bool permits(const KURL&, bool mainNavigation = false) const;
    ~InspectorResourcePolicy();
    DEFINE_INLINE_VIRTUAL_TRACE() { WillBeHeapSupplement<Page>::trace(visitor); }
private:
    explicit InspectorResourcePolicy(const std::shared_ptr<InspectorResourceState>& state) : m_state(state) { }
    std::shared_ptr<InspectorResourceState> m_state;
};
} // namespace blink
#endif
