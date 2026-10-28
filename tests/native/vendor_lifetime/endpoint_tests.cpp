#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <memory>
#include <vector>
#include <cstdint>
#include <set>
#include "allocation_fault.h"

namespace cc { struct LayerTreeHost { int identity; }; }
struct TestPageImpl {
    cc::LayerTreeHost* compositor;
    cc::LayerTreeHost* layerTreeHost() { return compositor; }
};
struct TestPage {
    TestPageImpl* implementation;
    TestPageImpl* webPageImpl() { return implementation; }
};
struct TestView {
    int identity;
    TestPage* page;
    TestPage* webPage() { return page; }
    int getId() const { return identity; }
};
using wkeWebView = TestView*;
using wkeInteractionEndpoint = void*;
#define WKE_CALL_TYPE __cdecl

// Explicit owner/live-registry dependency fakes; not native thread evidence.
namespace wke {
static bool fixture_owner = true;
bool inputUiOwnerAvailable() { return fixture_owner; }
std::set<wkeWebView> g_liveWebViews;
}

// The ownership algorithm below is an exact mapped production-source fragment.
#include "interaction_endpoint_source.inc"

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const bool empty = std::strcmp(argv[1], "empty-registry") == 0;
    const bool second = std::strcmp(argv[1], "second-vector-growth-fault") == 0;
    const bool forbidden = std::strcmp(argv[1], "all-allocation-forbidden") == 0;
    const bool newFailure = std::strcmp(argv[1], "create-allocation-failure") == 0;
    const bool growthFailure = std::strcmp(argv[1], "create-registry-growth-failure") == 0;
    const bool createSuccess = std::strcmp(argv[1], "create-success") == 0;
    const bool invalidCreation = std::strcmp(argv[1], "create-invalid") == 0;
    if (!empty && !second && !forbidden && !newFailure && !growthFailure && !createSuccess && !invalidCreation) return 2;
    cc::LayerTreeHost compositor{1};
    TestPageImpl implementation{&compositor};
    TestPage page{&implementation};
    TestView target{41, &page}, other{2, &page};
    wke::g_liveWebViews.insert(&target); wke::g_liveWebViews.insert(&other);
    wke::fixture_owner = false;
    if (createInteractionEndpoint(&target, 1)) return 1;
    wke::fixture_owner = true;
    if (newFailure || growthFailure || createSuccess || invalidCreation) {
        allocation_fault::capture_size = sizeof(InteractionEndpointImpl);
        allocation_fault::arm(createSuccess ? -1 : (growthFailure ? 1 : 0));
        bool threw = false;
        InteractionEndpointImpl* result = nullptr;
        try { result = static_cast<InteractionEndpointImpl*>(createInteractionEndpoint(&target, invalidCreation ? 0 : 1)); }
        catch (const std::bad_alloc&) { threw = true; }
        allocation_fault::disarm();
        const long attempts = allocation_fault::attempts.load();
        const long failures = allocation_fault::failures.load();
        void* captured = allocation_fault::captured.load();
        const int capturedDeleted = allocation_fault::captured_deleted.load();
        allocation_fault::capture_size = 0;
        if (newFailure || growthFailure) {
            const long expectedAttempts = growthFailure ? 2 : 1;
            if (threw || result || !s_interactionEndpoints.empty()
                || attempts != expectedAttempts || failures != 1
                || (growthFailure ? (!captured || capturedDeleted != 1) : (captured || capturedDeleted))) {
                std::fprintf(stderr, "OBSERVED createThrew=%d attempts=%ld failures=%ld registrySize=%zu unpublishedAllocated=%d unpublishedDeleted=%d\n",
                    threw ? 1 : 0, attempts, failures, s_interactionEndpoints.size(), captured ? 1 : 0, capturedDeleted);
                // Exact old-path counterexample: registry growth failed after
                // allocation, before publication, leaving no production owner.
                if (growthFailure && !(threw && attempts == 2 && failures == 1 && captured
                    && !result && s_interactionEndpoints.empty() && capturedDeleted == 0)) return 3;
                if (captured && capturedDeleted == 0) delete static_cast<InteractionEndpointImpl*>(captured);
                std::fprintf(stderr, "FAIL creation escaped exception or lost unpublished ownership\n");
                return 1;
            }
        } else if (invalidCreation) {
            if (threw || result || attempts || failures || captured || !s_interactionEndpoints.empty()) return 1;
        } else {
            if (threw || !result || s_interactionEndpoints.size() != 1
                || result->references.load() != 2 || !result->active || result->actualViewId != 41) return 1;
            allocation_fault::track(result, 0);
            invalidateInteractionEndpoint(result);
            if (result->references.load() != 1 || result->active || !s_interactionEndpoints.empty()) return 1;
            releaseInteractionReference(result);
            if (allocation_fault::deleted[0] != 1 || allocation_fault::captured_deleted.load() != 1) return 1;
        }
        std::fprintf(stdout, "PASS %s containedFailure=1 unpublishedOwnershipBalanced=1\n", argv[1]);
        return 0;
    }
    InteractionEndpointImpl* endpoints[256] = {};
    const int targetCount = empty ? 0 : (second ? 3 : 129);
    const int otherCount = empty ? 0 : 2;
    const int count = targetCount + otherCount;
    s_interactionEndpoints.reserve(256);
    for (int i = 0; i != count; ++i) {
        const bool isOther = i == 1 || i == 3;
        endpoints[i] = new InteractionEndpointImpl(isOther ? &other : &target, &compositor, 1);
        allocation_fault::track(endpoints[i], i);
        s_interactionEndpoints.push_back(endpoints[i]);
    }
    allocation_fault::arm(second ? 1 : 0);
    bool threw = false;
    try { invalidateInteractionEndpointsForWebView(&target); }
    catch (const std::bad_alloc&) { threw = true; }
    allocation_fault::disarm();
    const long attempts = allocation_fault::attempts.load();
    const long failures = allocation_fault::failures.load();
    const bool firstWithdrawn = count && std::find(s_interactionEndpoints.begin(), s_interactionEndpoints.end(), endpoints[0]) == s_interactionEndpoints.end();
    if (threw) {
        std::fprintf(stderr, "OBSERVED threw=1 attempts=%ld failures=%ld firstWithdrawn=%d firstReferences=%u firstActive=%d registryRemaining=%zu\n",
            attempts, failures, firstWithdrawn ? 1 : 0, endpoints[0]->references.load(), endpoints[0]->active ? 1 : 0, s_interactionEndpoints.size());
        if (second && !(attempts == 2 && failures == 1 && firstWithdrawn
            && endpoints[0]->references.load() == 2 && endpoints[0]->active
            && s_interactionEndpoints.size() == static_cast<std::size_t>(count - 1))) {
            std::fprintf(stderr, "FAIL baseline did not demonstrate second growth and orphaned registry ownership\n");
            return 3;
        }
        // Counterexample cleanup is diagnostic only. It explicitly retires the
        // lost registry reference, which the old production path failed to own.
        for (int i = 0; i != count; ++i) {
            invalidateInteractionEndpoint(endpoints[i]);
            while (endpoints[i]->references.load() > 1) releaseInteractionReference(endpoints[i]);
            releaseInteractionReference(endpoints[i]);
        }
        std::fprintf(stderr, "FAIL expected all target endpoints invalidated without allocation\n");
        return 1;
    }
    if (attempts || failures || s_interactionEndpoints.size() != static_cast<std::size_t>(otherCount)) {
        std::fprintf(stderr, "FAIL unexpected allocation or registry size\n");
        return 1;
    }
    for (int i = 0; i != count; ++i) {
        const bool isOther = i == 1 || i == 3;
        if (endpoints[i]->references.load() != (isOther ? 2U : 1U)
            || endpoints[i]->active != isOther
            || endpoints[i]->webView != (isOther ? &other : nullptr)
            || endpoints[i]->layerTreeHost != (isOther ? &compositor : nullptr)) {
            std::fprintf(stderr, "FAIL endpoint state or exact registry-reference transfer at index %d\n", i);
            return 1;
        }
    }
    // A repeated withdrawal must neither consume caller references nor retain links.
    allocation_fault::arm(0);
    invalidateInteractionEndpointsForWebView(&target);
    allocation_fault::disarm();
    if (allocation_fault::attempts.load() || allocation_fault::failures.load()) return 1;
    for (int i = 0; i != count; ++i) {
        const bool isOther = i == 1 || i == 3;
        if (endpoints[i]->references.load() != (isOther ? 2U : 1U)) return 1;
        invalidateInteractionEndpoint(endpoints[i]);
        releaseInteractionReference(endpoints[i]);
        if (allocation_fault::deleted[i] != 1) return 1;
    }
    if (!s_interactionEndpoints.empty()) return 1;
    std::fprintf(stdout, "PASS %s targets=%d others=%d allocationAttempts=0 exactlyOnceDeleted=%d\n", argv[1], targetCount, otherCount, count);
    return 0;
}
