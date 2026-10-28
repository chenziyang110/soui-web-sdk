#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)

//////////////////////////////////////////////////////////////////////////
#define BUILDING_wke 1

#include "wke/wkeString.h"
#include "content/common/InputFrameTrace.h"
#include "wke/wkeWebView.h"
#include "wke/wkeWebWindow.h"
#include "wke/wkeGlobalVar.h"
#include "wke/wke2.h"
#include "wke/RendererDiagnosticsState.h"
#include "wke/RendererModelState.h"
#include "wke/ViewDiagnosticsState.h"
#include "core/fetch/MemoryCache.h"
#include "third_party/skia/src/core/SkCacheObservation.h"
#include "third_party/skia/src/core/SkResourceCache.h"
#include "core/workers/WorkerMessagingProxy.h"
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
#include "platform/heap/SafePoint.h"
#endif
#include <exception>
#include "wke/CursorDescriptorBroker.h"
#include "content/browser/WebPage.h"
#include "content/browser/WebPageImpl.h"
#include "content/browser/CheckReEnter.h"
#include "cc/trees/LayerTreeHost.h"
#include "content/web_impl_win/BlinkPlatformImpl.h"
#include "content/web_impl_win/WebThreadImpl.h"
#include "content/web_impl_win/npapi/WebPluginImpl.h"
#include "content/web_impl_win/npapi/PluginDatabase.h"
#include "net/WebURLLoaderManager.h"
#include "net/ActivatingObjCheck.h"
#include "net/DefaultFullPath.h"
//#include "net/cookies/WebCookieJarCurlImpl.h"
#include "third_party/WebKit/public/web/WebKit.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebDragOperation.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebCustomElement.h"
#include "third_party/WebKit/public/web/WebUserGestureIndicator.h"
#include "third_party/WebKit/public/web/WebPageSerializer.h"
#include "third_party/WebKit/public/platform/WebDragData.h"
#include "third_party/WebKit/Source/web/WebViewImpl.h"
#include "third_party/WebKit/Source/web/WebSettingsImpl.h"
#include "third_party/WebKit/Source/core/frame/Settings.h"
#include "third_party/WebKit/Source/core/page/Page.h"
#include "gen/blink/platform/RuntimeEnabledFeatures.h"
#include "wtf/text/WTFString.h"
#include "wtf/text/WTFStringUtil.h"
#include "wtf/text/Base64.h"
#include "wtf/CurrentTime.h"
#include "cc/base/BdColor.h"
#include <v8.h>
#include "libplatform/libplatform.h"
#include <shlwapi.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>

 namespace net {
 
 //void setCookieJarPath(const WCHAR* path);
 //void setCookieJarFullPath(const WCHAR* path);
 //
 String getDefaultLocalStorageFullPath();
 void setDefaultLocalStorageFullPath(const String& path);
 
 }

namespace blink {
extern char* g_navigatorPlatform;
}

//////////////////////////////////////////////////////////////////////////

namespace wke {
DWORD wkeThreadId = 0;
bool inputUiOwnerAvailable();
}

namespace wke {
namespace diagnostics {
namespace {

SRWLOCK renderer_cache_lock = SRWLOCK_INIT;
renderer_process_state renderer_cache;
renderer_backend_model_state renderer_model_cache;
view_lifetime_state view_cache;

class renderer_write_lock final {
public:
    explicit renderer_write_lock(SRWLOCK& lock) noexcept : lock_(lock)
    {
        AcquireSRWLockExclusive(&lock_);
    }
    ~renderer_write_lock() { ReleaseSRWLockExclusive(&lock_); }
    renderer_write_lock(const renderer_write_lock&) = delete;
    renderer_write_lock& operator=(const renderer_write_lock&) = delete;
    renderer_write_lock(renderer_write_lock&&) = delete;
    renderer_write_lock& operator=(renderer_write_lock&&) = delete;
private:
    SRWLOCK& lock_;
};

class renderer_read_lock final {
public:
    // Construct only after the caller successfully acquired the shared lock.
    explicit renderer_read_lock(SRWLOCK& lock) noexcept : lock_(lock) {}
    ~renderer_read_lock() { ReleaseSRWLockShared(&lock_); }
    renderer_read_lock(const renderer_read_lock&) = delete;
    renderer_read_lock& operator=(const renderer_read_lock&) = delete;
    renderer_read_lock(renderer_read_lock&&) = delete;
    renderer_read_lock& operator=(renderer_read_lock&&) = delete;
private:
    SRWLOCK& lock_;
};

bool renderer_qpc(uint64_t& value) noexcept
{
    LARGE_INTEGER now{};
    const bool valid = QueryPerformanceCounter(&now) != FALSE && now.QuadPart >= 0;
    value = valid ? static_cast<uint64_t>(now.QuadPart) : 0;
    return valid;
}

uint32_t WKE_RENDER_DIAGNOSTICS_CALL read_renderer_process_snapshot(
    wkeRendererProcessSnapshot* output)
{
    if (!output || output->structSize != sizeof(wkeRendererProcessSnapshot))
        return WKE_RENDER_DIAGNOSTICS_INVALID_ARGUMENT;
    auto snapshot = busy_snapshot();
    if (TryAcquireSRWLockShared(&renderer_cache_lock)) {
        renderer_read_lock held(renderer_cache_lock);
        snapshot = renderer_cache.snapshot();
    }
    // Caller owns output; never hold the cache lock while writing caller memory.
    *output = snapshot;
    return snapshot.status;
}

const wkeRendererDiagnosticsApi renderer_api = {
    sizeof(wkeRendererDiagnosticsApi),
    WKE_RENDER_DIAGNOSTICS_ABI_VERSION,
    WKE_RENDER_DIAGNOSTICS_CAP_PROCESS_CACHE
        | WKE_RENDER_DIAGNOSTICS_CAP_COMPLETED_DRAW_CALLS,
    &read_renderer_process_snapshot
};

uint32_t WKE_RENDER_DIAGNOSTICS_CALL read_view_lifetimes(wkeViewLifetimeSnapshot* output)
{
    if (!output || output->structSize != sizeof(wkeViewLifetimeSnapshot))
        return WKE_RENDER_DIAGNOSTICS_INVALID_ARGUMENT;
    wkeViewLifetimeSnapshot snapshot{};
    snapshot.structSize = sizeof(snapshot);
    snapshot.abiVersion = WKE_VIEW_DIAGNOSTICS_ABI_VERSION;
    snapshot.status = WKE_RENDER_DIAGNOSTICS_BUSY;
    if (TryAcquireSRWLockShared(&renderer_cache_lock)) {
        renderer_read_lock held(renderer_cache_lock);
        snapshot = view_cache.snapshot();
    }
    *output = snapshot;
    return snapshot.status;
}

const wkeViewDiagnosticsApi view_api = {
    sizeof(wkeViewDiagnosticsApi), WKE_VIEW_DIAGNOSTICS_ABI_VERSION,
    WKE_VIEW_DIAGNOSTICS_CAP_DLL_LIFETIMES, &read_view_lifetimes
};

} // namespace

uint32_t WKE_RENDER_DIAGNOSTICS_CALL read_renderer_model_snapshot(wkeRendererModelSnapshot* output)
{
    if (!output || output->structSize != sizeof(wkeRendererModelSnapshot))
        return WKE_RENDER_DIAGNOSTICS_INVALID_ARGUMENT;
    wkeRendererModelSnapshot snapshot{};
    snapshot.structSize = sizeof(snapshot);
    snapshot.abiVersion = WKE_RENDER_MODEL_ABI_VERSION;
    snapshot.status = WKE_RENDER_DIAGNOSTICS_BUSY;
    snapshot.compiledGpuSupport = WKE_RENDER_MODEL_GPU_UNKNOWN;
    if (TryAcquireSRWLockShared(&renderer_cache_lock)) {
        renderer_read_lock held(renderer_cache_lock);
        snapshot = renderer_model_cache.snapshot(renderer_cache.snapshot());
    }
    *output = snapshot;
    return snapshot.status;
}
const wkeRendererModelDiagnosticsApi renderer_model_api = {
    sizeof(wkeRendererModelDiagnosticsApi), WKE_RENDER_MODEL_ABI_VERSION,
    WKE_RENDER_MODEL_CAP_BOUND_BACKEND | WKE_RENDER_MODEL_CAP_FACTORY_HISTORY
        | WKE_RENDER_MODEL_CAP_COHERENT_COMPOSITOR, &read_renderer_model_snapshot
};
uint32_t WKE_CACHE_CALL sample_existing_caches(wkeCacheSnapshot* output)
{
    if (!output || output->structSize != sizeof(wkeCacheSnapshot))
        return WKE_CACHE_INVALID_ARGUMENT;
    wkeCacheSnapshot value{};
    value.structSize = sizeof(value);
    value.abiVersion = WKE_CACHE_ABI_VERSION;
    wkeRendererProcessSnapshot identity{};
    identity.structSize = sizeof(identity);
    const uint32_t identityStatus = read_renderer_process_snapshot(&identity);
    value.status = identityStatus == WKE_RENDER_DIAGNOSTICS_BUSY ? WKE_CACHE_BUSY : WKE_CACHE_NOT_STARTED;
    constexpr uint32_t required = WKE_RENDER_DIAGNOSTICS_IDENTITY_VALID
        | WKE_RENDER_DIAGNOSTICS_CLOCK_VALID | WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED;
    constexpr uint32_t faults = WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT
        | WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH | WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE
        | WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED;
    if (identity.flags & faults) {
        value.status = WKE_CACHE_UNAVAILABLE;
    } else if ((identityStatus == WKE_RENDER_DIAGNOSTICS_READY || identityStatus == WKE_RENDER_DIAGNOSTICS_CLOSED
        || identityStatus == WKE_RENDER_DIAGNOSTICS_NOT_STARTED) && (identity.flags & required) == required) {
        // Initialized identity precedes the compositor's first draw. Draw history
        // is not a prerequisite for these independently named cache owners.
        value.status = WKE_CACHE_UNAVAILABLE;
        if (identity.engineOwnerThreadId == GetCurrentThreadId()) {
            value.processId = identity.processId;
            value.processCreationFileTime = identity.processCreationFileTime;
            value.engineOwnerThreadId = identity.engineOwnerThreadId;
            value.recordingEpoch = identity.recordingEpoch;
            value.initializedQpc = identity.initializedQpc;
            value.qpcFrequency = identity.qpcFrequency;
            const auto memory = blink::peekMemoryCache();
            const auto glyph = SkPeekAttachedGlyphCache();
            const auto resource = SkResourceCache::PeekRetainedGlobalCache();
            value.blinkStatus = memory.status;
            value.blinkLiveBytes = memory.liveBytes; value.blinkDeadBytes = memory.deadBytes;
            value.blinkCapacity = memory.capacity;
            value.blinkMinDeadCapacity = memory.minDeadCapacity; value.blinkMaxDeadCapacity = memory.maxDeadCapacity;
            value.glyphStatus = glyph.status;
            value.glyphAttachedBytes = glyph.bytes; value.glyphAttachedCount = glyph.count;
            value.glyphByteLimit = glyph.byteLimit; value.glyphCountLimit = glyph.countLimit;
            value.resourceStatus = resource.status; value.resourceDiscardable = resource.discardable;
            value.resourceRetainedBytes = resource.bytes; value.resourceRetainedCount = resource.count;
            value.resourceByteLimit = resource.byteLimit; value.resourceCountLimit = resource.countLimit;
            value.resourceRawSingleLimit = resource.rawSingleLimit;
            value.resourceEffectiveSingleLimit = resource.effectiveSingleLimit;
            if (renderer_qpc(value.sampleQpc) && value.sampleQpc >= value.initializedQpc)
                value.status = WKE_CACHE_READY; // Completed sample, not all-component completeness.
        } else {
            value.status = WKE_CACHE_WRONG_OWNER;
        }
    } else if (identityStatus != WKE_RENDER_DIAGNOSTICS_NOT_STARTED
        && identityStatus != WKE_RENDER_DIAGNOSTICS_BUSY) value.status = WKE_CACHE_UNAVAILABLE;
    *output = value;
    return value.status;
}
const wkeCacheDiagnosticsApi cache_api = {
    sizeof(wkeCacheDiagnosticsApi), WKE_CACHE_ABI_VERSION, WKE_CACHE_CAP_NAMED_SCALARS, &sample_existing_caches
};

void renderer_platform_initialized(bool matched) noexcept
{
    renderer_write_lock held(renderer_cache_lock);
    uint64_t qpc = 0;
    const bool valid = renderer_qpc(qpc) && qpc != 0;
    renderer_model_cache.platform_initialized(matched, qpc, valid);
}
void renderer_factory_entered(uint32_t kind) noexcept
{
    renderer_write_lock held(renderer_cache_lock);
    renderer_model_cache.factory_entered(kind);
}
void renderer_initialized() noexcept
{
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool identity_valid =
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE;
    const uint64_t creation =
        (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    LARGE_INTEGER frequency{};
    const bool clock_valid = QueryPerformanceFrequency(&frequency) != FALSE
        && frequency.QuadPart > 0;
    const uint64_t hz = clock_valid ? static_cast<uint64_t>(frequency.QuadPart) : 0;
    renderer_write_lock held(renderer_cache_lock);
    uint64_t initialized_qpc = 0;
    const bool initialized_clock_valid = renderer_qpc(initialized_qpc)
        && initialized_qpc != 0;
    renderer_cache.initialized(GetCurrentProcessId(), creation, GetCurrentThreadId(),
        hz, initialized_qpc, identity_valid, clock_valid && initialized_clock_valid);
    view_cache.initialized(GetCurrentProcessId(), creation, GetCurrentThreadId(),
        hz, initialized_qpc, identity_valid, clock_valid && initialized_clock_valid);
#ifdef SOUI_GPU_COMPOSITOR
    renderer_model_cache.compilation_observed(true);
#else
    renderer_model_cache.compilation_observed(false);
#endif
}

void renderer_created() noexcept
{
    renderer_write_lock held(renderer_cache_lock);
    renderer_cache.renderer_created();
    renderer_model_cache.backend_created();
}

void renderer_closed() noexcept
{
    renderer_write_lock held(renderer_cache_lock);
    uint64_t qpc = 0;
    const bool clock_valid = renderer_qpc(qpc);
    renderer_cache.renderer_closed(qpc, clock_valid);
}

void view_created() noexcept
{
    renderer_write_lock held(renderer_cache_lock);
    uint64_t qpc = 0;
    const bool valid = renderer_qpc(qpc);
    view_cache.created(GetCurrentThreadId(), qpc, valid);
}

void view_destroyed() noexcept
{
    renderer_write_lock held(renderer_cache_lock);
    uint64_t qpc = 0;
    const bool valid = renderer_qpc(qpc);
    view_cache.destroyed(GetCurrentThreadId(), qpc, valid);
}

void renderer_draw_completed(uint32_t kind) noexcept
{
    renderer_write_lock held(renderer_cache_lock);
    uint64_t qpc = 0;
    const bool clock_valid = renderer_qpc(qpc);
    renderer_cache.draw_completed(kind, GetCurrentProcessId(), GetCurrentThreadId(),
        qpc, clock_valid);
}

void renderer_finalized() noexcept
{
    renderer_write_lock held(renderer_cache_lock);
    uint64_t qpc = 0;
    const bool clock_valid = renderer_qpc(qpc);
    renderer_cache.finalized(qpc, clock_valid);
    view_cache.finalized(GetCurrentThreadId(), qpc, clock_valid);
}

const wkeRendererDiagnosticsApi* renderer_api_for_version(unsigned int version) noexcept
{
    return version == WKE_RENDER_DIAGNOSTICS_ABI_VERSION ? &renderer_api : nullptr;
}

} // namespace diagnostics
} // namespace wke

// Optional read-only export. Existing generated WKE tables and ABIs stay exact.
extern "C" __declspec(dllexport) const wkeViewDiagnosticsApi* WKE_RENDER_DIAGNOSTICS_CALL
wkeGetViewDiagnosticsApi(unsigned int version)
{
    return version == WKE_VIEW_DIAGNOSTICS_ABI_VERSION ? &wke::diagnostics::view_api : nullptr;
}

const wkeRendererModelDiagnosticsApi* WKE_CALL_TYPE wkeGetRendererModelDiagnosticsApi(unsigned int version)
{
    return version == WKE_RENDER_MODEL_ABI_VERSION ? &wke::diagnostics::renderer_model_api : nullptr;
}
const wkeCacheDiagnosticsApi* WKE_CALL_TYPE wkeGetCacheDiagnosticsApi(unsigned int version)
{
    return version == WKE_CACHE_ABI_VERSION ? &wke::diagnostics::cache_api : nullptr;
}
const wkeRendererDiagnosticsApi* WKE_CALL_TYPE wkeGetRendererDiagnosticsApi(
    unsigned int requestedVersion)
{
    // No WKE initialization/thread check and no raw instance access.
    return wke::diagnostics::renderer_api_for_version(requestedVersion);
}


namespace {

struct InteractionEndpointImpl {
    InteractionEndpointImpl(wkeWebView owner, cc::LayerTreeHost* compositor,
        unsigned __int64 generation)
        : references(2) // one caller reference and one registry reference
        , webView(owner)
        , layerTreeHost(compositor)
        , viewGeneration(generation)
        , actualViewId(owner->getId() > 0 ? static_cast<uint64_t>(owner->getId()) : 0)
        , active(true)
        , nextForInvalidation(nullptr)
    {
    }

    std::atomic<unsigned int> references;
    std::mutex mutex;
    wkeWebView webView;
    cc::LayerTreeHost* layerTreeHost;
    unsigned __int64 viewGeneration;
    const uint64_t actualViewId; // WKE identity, distinct from the scroll node key.
    bool active;
    // A withdrawn registry reference owns this private, allocation-free chain.
    InteractionEndpointImpl* nextForInvalidation;
};

std::mutex s_interactionEndpointsMutex;
std::vector<InteractionEndpointImpl*> s_interactionEndpoints;

void releaseInteractionReference(InteractionEndpointImpl* endpoint)
{
    if (endpoint && endpoint->references.fetch_sub(1) == 1)
        delete endpoint;
}

bool removeInteractionEndpointFromRegistry(InteractionEndpointImpl* endpoint)
{
    std::lock_guard<std::mutex> lock(s_interactionEndpointsMutex);
    std::vector<InteractionEndpointImpl*>::iterator found =
        std::find(s_interactionEndpoints.begin(), s_interactionEndpoints.end(), endpoint);
    if (found == s_interactionEndpoints.end())
        return false;
    s_interactionEndpoints.erase(found);
    return true;
}

void invalidateInteractionEndpoint(InteractionEndpointImpl* endpoint)
{
    if (!endpoint)
        return;
    const bool removed = removeInteractionEndpointFromRegistry(endpoint);
    {
        std::lock_guard<std::mutex> lock(endpoint->mutex);
        endpoint->active = false;
        endpoint->webView = nullptr;
        endpoint->layerTreeHost = nullptr;
    }
    if (removed)
        releaseInteractionReference(endpoint);
}

void invalidateInteractionEndpointsForWebView(wkeWebView webView)
{
    InteractionEndpointImpl* removed = nullptr;
    InteractionEndpointImpl** nextRemoved = &removed;
    {
        std::lock_guard<std::mutex> lock(s_interactionEndpointsMutex);
        for (std::vector<InteractionEndpointImpl*>::iterator it = s_interactionEndpoints.begin();
             it != s_interactionEndpoints.end();) {
            if ((*it)->webView == webView) {
                InteractionEndpointImpl* endpoint = *it;
                // Transfer the registry reference before any endpoint callout.
                *nextRemoved = endpoint;
                nextRemoved = &endpoint->nextForInvalidation;
                it = s_interactionEndpoints.erase(it);
            } else {
                ++it;
            }
        }
    }
    while (removed) {
        InteractionEndpointImpl* endpoint = removed;
        removed = endpoint->nextForInvalidation;
        endpoint->nextForInvalidation = nullptr;
        {
            std::lock_guard<std::mutex> lock(endpoint->mutex);
            endpoint->active = false;
            endpoint->webView = nullptr;
            endpoint->layerTreeHost = nullptr;
        }
        releaseInteractionReference(endpoint);
    }
}

wkeInteractionEndpoint WKE_CALL_TYPE createInteractionEndpoint(
    wkeWebView webView, unsigned __int64 viewGeneration)
{
    try {
        if (!wke::inputUiOwnerAvailable() || !webView || !viewGeneration
            || wke::g_liveWebViews.find(webView) == wke::g_liveWebViews.end()
            || !webView->webPage()
            || !webView->webPage()->webPageImpl()) {
            return nullptr;
        }
        cc::LayerTreeHost* layerTreeHost =
            webView->webPage()->webPageImpl()->layerTreeHost();
        if (!layerTreeHost)
            return nullptr;
        std::unique_ptr<InteractionEndpointImpl> endpoint(
            new InteractionEndpointImpl(webView, layerTreeHost, viewGeneration));
        {
            std::lock_guard<std::mutex> lock(s_interactionEndpointsMutex);
            s_interactionEndpoints.push_back(endpoint.get());
        }
        // Publish the caller reference only after registry admission commits.
        return endpoint.release();
    } catch (...) {
        return nullptr;
    }
}

void WKE_CALL_TYPE retainInteractionEndpoint(wkeInteractionEndpoint opaque)
{
    InteractionEndpointImpl* endpoint = static_cast<InteractionEndpointImpl*>(opaque);
    if (endpoint)
        endpoint->references.fetch_add(1);
}

void WKE_CALL_TYPE invalidateInteractionEndpointApi(wkeInteractionEndpoint opaque)
{
    invalidateInteractionEndpoint(static_cast<InteractionEndpointImpl*>(opaque));
}

void WKE_CALL_TYPE releaseInteractionEndpointApi(wkeInteractionEndpoint opaque)
{
    InteractionEndpointImpl* endpoint = static_cast<InteractionEndpointImpl*>(opaque);
    if (!endpoint)
        return;
    invalidateInteractionEndpoint(endpoint);
    releaseInteractionReference(endpoint);
}

int WKE_CALL_TYPE configureInteractionPresentation(
    wkeInteractionEndpoint opaque, unsigned int flags)
{
    if (flags & ~WKE_INTERACTION_PRESENT_DIRECT_HWND)
        return 0;
    InteractionEndpointImpl* endpoint =
        static_cast<InteractionEndpointImpl*>(opaque);
    if (!endpoint)
        return 0;

    std::lock_guard<std::mutex> lock(endpoint->mutex);
    if (!endpoint->active || !endpoint->layerTreeHost)
        return 0;
    endpoint->layerTreeHost->setIndependentPresentationEnabled(
        (flags & WKE_INTERACTION_PRESENT_DIRECT_HWND) != 0);
    return 1;
}

int WKE_CALL_TYPE submitInteractionWheel(wkeInteractionEndpoint opaque,
    const wkeInteractionWheelInput* input, wkeInteractionScrollSample* output)
{
    InteractionEndpointImpl* endpoint = static_cast<InteractionEndpointImpl*>(opaque);
    if (!endpoint || !input || !output
        || input->structSize < sizeof(wkeInteractionWheelInput)
        || output->structSize < sizeof(wkeInteractionScrollSample)
        || !input->inputSequence) {
        return 0;
    }
    output->accepted = 0;
    output->sceneVersion = 0;
    output->inputSequence = input->inputSequence;
    output->offsetX = 0;
    output->offsetY = 0;

    std::lock_guard<std::mutex> lock(endpoint->mutex);
    if (!endpoint->active || !endpoint->layerTreeHost
        || input->viewGeneration != endpoint->viewGeneration) {
        return 0;
    }

    content::input_frame_trace::event identity;
    identity.view_id = endpoint->actualViewId;
    identity.view_generation = input->viewGeneration;
    identity.document_generation = input->documentGeneration;
    identity.geometry_generation = input->geometryVersion;
    identity.input_first = identity.input_last = input->inputSequence;
    identity.qpc = content::input_frame_trace::now_qpc();
    identity.thread_id = ::GetCurrentThreadId();
    cc::CompositorScrollReceipt receipt;
    // The endpoint can be called from a host thread whose monotonic clock has
    // a different epoch. Animation and latching must use Blink's clock.
    const double compositorTime = WTF::monotonicallyIncreasingTime();
    const bool accepted = endpoint->layerTreeHost->handleCompositorWheelInput(
        input->x, input->y, input->deltaX, input->deltaY,
        compositorTime, input->smooth != 0, &receipt, &identity);
    if (!accepted)
        return 0;
    output->accepted = 1;
    output->sceneVersion = receipt.sceneVersion;
    output->inputSequence = receipt.inputSequence;
    output->offsetX = receipt.offsetX;
    output->offsetY = receipt.offsetY;
    return 1;
}

int WKE_CALL_TYPE captureInteractionScrollBarrier(
    wkeInteractionEndpoint opaque,
    const wkeInteractionBarrierRequest* request,
    wkeInteractionScrollBarrier* output)
{
    InteractionEndpointImpl* endpoint =
        static_cast<InteractionEndpointImpl*>(opaque);
    if (!endpoint || !request || !output
        || request->structSize < sizeof(wkeInteractionBarrierRequest)
        || output->structSize < sizeof(wkeInteractionScrollBarrier)) {
        return 0;
    }

    output->valid = 0;
    output->viewGeneration = request->viewGeneration;
    output->sceneVersion = 0;
    output->viewId = 0;
    output->nodeId = 0;
    output->nodeGeneration = 0;
    output->scrollEpoch = 0;
    output->sampleSequence = 0;
    output->inputSequence = 0;
    output->consumedTotalX = 0;
    output->consumedTotalY = 0;
    output->offsetX = 0;
    output->offsetY = 0;

    std::lock_guard<std::mutex> lock(endpoint->mutex);
    if (!endpoint->active || !endpoint->layerTreeHost
        || request->viewGeneration != endpoint->viewGeneration) {
        return 0;
    }

    content::input_frame_trace::event identity;
    identity.view_id = endpoint->actualViewId;
    identity.view_generation = request->viewGeneration;
    identity.document_generation = request->documentGeneration;
    identity.geometry_generation = request->geometryVersion;
    cc::ScrollBarrier barrier;
    if (!endpoint->layerTreeHost->captureCompositorScrollBarrier(
            request->x, request->y, &barrier, &identity)) {
        return 0;
    }

    output->valid = 1;
    output->sceneVersion = barrier.sceneVersion;
    output->viewId = barrier.key.viewId;
    output->nodeId = barrier.key.nodeId;
    output->nodeGeneration = barrier.key.nodeGeneration;
    output->scrollEpoch = barrier.scrollEpoch;
    output->sampleSequence = barrier.sampleSequence;
    output->inputSequence = barrier.throughInputSequence;
    output->consumedTotalX = barrier.consumedTotal.x;
    output->consumedTotalY = barrier.consumedTotal.y;
    output->offsetX = barrier.visualOffset.x;
    output->offsetY = barrier.visualOffset.y;
    return 1;
}

int WKE_CALL_TYPE synchronizeInteractionScrollBarrier(
    wkeInteractionEndpoint opaque,
    const wkeInteractionScrollBarrier* input)
{
    InteractionEndpointImpl* endpoint =
        static_cast<InteractionEndpointImpl*>(opaque);
    if (!endpoint || !input
        || input->structSize < sizeof(wkeInteractionScrollBarrier)
        || !input->valid) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(endpoint->mutex);
    if (!endpoint->active || !endpoint->layerTreeHost
        || input->viewGeneration != endpoint->viewGeneration) {
        return 0;
    }

    cc::ScrollBarrier barrier;
    barrier.valid = true;
    barrier.key = cc::ScrollNodeKey(input->viewId,
        input->viewGeneration, input->nodeId, input->nodeGeneration);
    barrier.sceneVersion = input->sceneVersion;
    barrier.scrollEpoch = input->scrollEpoch;
    barrier.sampleSequence = input->sampleSequence;
    barrier.throughInputSequence = input->inputSequence;
    barrier.consumedTotal = cc::ScrollVector(input->consumedTotalX,
        input->consumedTotalY);
    barrier.visualOffset = cc::ScrollVector(input->offsetX, input->offsetY);
    return endpoint->layerTreeHost->synchronizeCompositorScrollBarrier(barrier)
        ? 1 : 0;
}

} // namespace

const wkeInteractionApi* WKE_CALL_TYPE wkeGetInteractionApi(unsigned int requestedVersion)
{
    static const wkeInteractionApi api = {
        sizeof(wkeInteractionApi),
        WKE_INTERACTION_ABI_VERSION,
        WKE_INTERACTION_CAP_DIRECT_WHEEL
            | WKE_INTERACTION_CAP_CLICK_BARRIER
            | WKE_INTERACTION_CAP_DIRECT_PRESENTATION,
        &createInteractionEndpoint,
        &retainInteractionEndpoint,
        &invalidateInteractionEndpointApi,
        &releaseInteractionEndpointApi,
        &configureInteractionPresentation,
        &submitInteractionWheel,
        &captureInteractionScrollBarrier,
        &synchronizeInteractionScrollBarrier
    };
    return requestedVersion == WKE_INTERACTION_ABI_VERSION ? &api : nullptr;
}

const wkeCursorBrokerApi* WKE_CALL_TYPE wkeGetCursorBrokerApi(unsigned int requestedVersion)
{
    return wke::cursorBrokerApi(requestedVersion);
}

extern "C" __declspec(dllexport) const SouiLocalResourceApi* SOUI_LOCAL_RESOURCE_CALL
wkeGetLocalResourceApi(uint32_t requestedVersion)
{
    return wke::localResourceApi(requestedVersion);
}

void WKE_CALL_TYPE wkeInitialize()
{
    if (wke::wkeIsInit)
        return;

    if (!wke::CWebView::resumePaintRuntime())
        return; // Never reuse a runtime whose paint/close receipts are live.
    wke::wkeThreadId = ::GetCurrentThreadId();

    //double-precision float
    _controlfp(_PC_53, _MCW_PC);

    CoInitialize(NULL);

    (void)content::input_frame_trace::register_current_thread();
    content::WebPage::initBlink();
    wke::wkeIsInit = true;
    wke::diagnostics::renderer_initialized();
}

//struct wkeProxyInfo {
//    net::ProxyType proxyType;
//    String hostname;
//    String username;
//    String password;
//
//    static WTF::PassOwnPtr<wkeProxyInfo> create(const wkeProxy& proxy) {
//        WTF::PassOwnPtr<wkeProxyInfo> info = WTF::adoptPtr(new wkeProxyInfo());
//        info->proxyType = net::HTTP;
//
//        if (proxy.hostname[0] != 0 && proxy.type >= WKE_PROXY_HTTP && proxy.type <= WKE_PROXY_SOCKS5HOSTNAME) {
//            switch (proxy.type) {
//            case WKE_PROXY_HTTP:           info->proxyType = net::HTTP; break;
//            case WKE_PROXY_SOCKS4:         info->proxyType = net::Socks4; break;
//            case WKE_PROXY_SOCKS4A:        info->proxyType = net::Socks4A; break;
//            case WKE_PROXY_SOCKS5:         info->proxyType = net::Socks5; break;
//            case WKE_PROXY_SOCKS5HOSTNAME: info->proxyType = net::Socks5Hostname; break;
//            }
//
//            info->hostname = String::fromUTF8(proxy.hostname);
//            info->username = String::fromUTF8(proxy.username);
//            info->password = String::fromUTF8(proxy.password);
//        }
//        return info;
//    }
//};

//void WKE_CALL_TYPE wkeSetProxy(const wkeProxy* proxy)
//{
//    wke::checkThreadCallIsValid(__FUNCTION__);
//    if (!proxy)
//        return;
//
//    WTF::OwnPtr<wkeProxyInfo> info = wkeProxyInfo::create(*proxy);
//
//    if (net::WebURLLoaderManager::sharedInstance())
//        net::WebURLLoaderManager::sharedInstance()->setProxyInfo(info->hostname, proxy->port, info->proxyType, info->username, info->password);
//}
//
//void WKE_CALL_TYPE wkeSetViewProxy(wkeWebView webView, wkeProxy* proxy)
//{
//    wke::checkThreadCallIsValid(__FUNCTION__);
//    if (!webView || !proxy)
//        return;
//    WTF::OwnPtr<wkeProxyInfo> info = wkeProxyInfo::create(*proxy);
//    webView->setProxyInfo(info->hostname, proxy->port, info->proxyType, info->username, info->password);
//}

void WKE_CALL_TYPE wkeSetViewNetInterface(wkeWebView webView, const char* netInterface)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webView || !netInterface)
        return;

    webView->setNetInterface(netInterface);
}

void WKE_CALL_TYPE wkeConfigure(const wkeSettings* settings)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!settings)
        return;
    //if (settings->mask & WKE_SETTING_PROXY)
    //    wkeSetProxy(&settings->proxy);
//     if (settings->mask & WKE_SETTING_PAINTCALLBACK_IN_OTHER_THREAD)
//         blink::RuntimeEnabledFeatures::setUpdataInOtherThreadEnabled(true);
}

void WKE_CALL_TYPE wkeInitializeEx(const wkeSettings* settings)
{
    wkeInitialize();
    wkeConfigure(settings);
}

bool WKE_CALL_TYPE wkeIsInitialize()
{
    return wke::wkeIsInit;
}

namespace {
bool workerFinalizationPrepared = false;
bool workerFinalizationCompleted = false;
}
namespace wke {
bool inputUiOwnerAvailable()
{
    // Read mutable lifecycle flags only on their actual owner.
    return ::GetCurrentThreadId() == wkeThreadId && wkeIsInit
        && !workerFinalizationPrepared && !workerFinalizationCompleted;
}
}


unsigned WKE_CALL_TYPE wkeWorkerRetirementWitnessCount()
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return blink::WorkerMessagingProxy::diagnosticWitnessCount();
}
void* WKE_CALL_TYPE wkeCaptureWorkerRetirementWitness(unsigned index)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return blink::WorkerMessagingProxy::captureRetirementWitness(index);
}
void* WKE_CALL_TYPE wkeWorkerRetirementNativeHandle(void* token)
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    const auto* witness = static_cast<blink::WorkerMessagingProxy::RetirementWitness*>(token);
    return witness ? witness->native.handle : nullptr;
#else
    return nullptr;
#endif
}
bool WKE_CALL_TYPE wkeReadWorkerRetirementWitness(void* token, unsigned* values, unsigned count)
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    const auto* witness = static_cast<blink::WorkerMessagingProxy::RetirementWitness*>(token);
    if (!witness || !values || count != 8) return false;
    const auto state = witness->record->snapshot();
    values[0] = state.payloads; values[1] = state.rawActive;
    values[2] = state.callbackActive; values[3] = state.callbacks;
    values[4] = state.prepared; values[5] = state.joined; values[6] = state.failed;
    values[7] = witness->record->nativeTargetsRetired();
    return true; // Independent record, callable after engine exit; no raw Worker/platform.
#else
    return false;
#endif
}
void WKE_CALL_TYPE wkeReleaseWorkerRetirementWitness(void* token)
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    auto* witness = static_cast<blink::WorkerMessagingProxy::RetirementWitness*>(token);
    if (!witness) return;
    witness->record->backlogDiagnostic().abort();
    if (witness->native.handle && !::CloseHandle(witness->native.handle)) std::terminate();
    delete witness; // Independent record/cancelled tombstones only, never a raw Worker.
#endif
}

bool WKE_CALL_TYPE wkeArmWorkerMessageBacklog(void* token, unsigned generation, unsigned timeoutMs)
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    wke::checkThreadCallIsValid(__FUNCTION__);
    auto* witness = static_cast<blink::WorkerMessagingProxy::RetirementWitness*>(token);
    return witness && witness->native.handle && witness->native.threadId
        && witness->record->canArmBacklogDiagnostic()
        && witness->record->backlogDiagnostic().arm(generation, witness->native.threadId, timeoutMs);
#else
    return false;
#endif
}
bool WKE_CALL_TYPE wkeWaitWorkerMessageBacklogEntered(void* token, unsigned generation, unsigned timeoutMs)
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    wke::checkThreadCallIsValid(__FUNCTION__);
    auto* witness = static_cast<blink::WorkerMessagingProxy::RetirementWitness*>(token);
    if (!witness) return false;
    blink::ThreadState* state = blink::ThreadState::current();
    if (!state || state->isAtSafePoint()) {
        witness->record->backlogDiagnostic().abort(); return false;
    }
    blink::SafePointScope safePoint(blink::ThreadState::HeapPointersOnStack, state);
    return witness->record->backlogDiagnostic().waitEntered(generation, timeoutMs);
#else
    return false;
#endif
}
bool WKE_CALL_TYPE wkeReadWorkerMessageBacklog(void* token, unsigned* values, unsigned count)
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    auto* witness = static_cast<blink::WorkerMessagingProxy::RetirementWitness*>(token);
    if (!witness || !values || count != 18) return false;
    witness->record->backlogDiagnostic().read(values);
    values[17] = witness->record->closing(); // No diagnostic lock held while ordinary record is read.
    return true; // Same independently retained record; valid borrowed token required.
#else
    return false;
#endif
}
bool WKE_CALL_TYPE wkeReleaseWorkerMessageBacklog(void* token, unsigned generation)
{
#if defined(SOUI_WORKER_SHUTDOWN_DIAGNOSTICS) && SOUI_WORKER_SHUTDOWN_DIAGNOSTICS
    auto* witness = static_cast<blink::WorkerMessagingProxy::RetirementWitness*>(token);
    return witness && witness->record->backlogDiagnostic().release(generation);
#else
    return false;
#endif
}

// Internal opt-in SDK handshake. No blocking wait or general JS/Windows pump.
unsigned WKE_CALL_TYPE wkePrepareWorkerShutdown()
{
    if (::GetCurrentThreadId() != wke::wkeThreadId || !wke::wkeIsInit) return 1;
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (workerFinalizationCompleted) return 0;
    if (!wke::CWebView::preparePaintShutdown()) return 1;
    const unsigned retired = static_cast<unsigned>(blink::WorkerMessagingProxy::prepareAllWorkerRetirements());
    if (retired != 0) return retired;
    if (!wke::brokerMayFinalize())
        return 1; // Pending broker callbacks must not stop the SDK owner loop.
    workerFinalizationPrepared = true;
    return 0;
}

unsigned WKE_CALL_TYPE wkeFinalizePrepared()
{
    if (::GetCurrentThreadId() != wke::wkeThreadId || !wke::wkeIsInit) return 1;
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (workerFinalizationCompleted) return 0;
    if (!wke::brokerMayFinalize()) return 1;
    const unsigned prepared = wkePrepareWorkerShutdown();
    if (prepared != 0) return prepared;
    wkeFinalize();
    return workerFinalizationCompleted ? 0 : 2;
}

void WKE_CALL_TYPE wkeFinalize()
{
    if (::GetCurrentThreadId() != wke::wkeThreadId || !wke::wkeIsInit) return;
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (workerFinalizationCompleted)
        return;
    if (!wke::brokerMayFinalize())
        return;
    if (wkePrepareWorkerShutdown() != 0) return; // Before the first destructive loader/main/IO step.
    content::BlinkPlatformImpl* platform = (content::BlinkPlatformImpl*)blink::Platform::current();
    platform->shutdown();

    CoUninitialize();

    if (wke::s_versionString)
        delete wke::s_versionString;
    wke::s_versionString = nullptr;
    workerFinalizationCompleted = true;
    wke::diagnostics::renderer_finalized();
    content::input_frame_trace::detach_current_thread();
}

void WKE_CALL_TYPE wkeSetMemoryCacheEnable(wkeWebView webView, bool b)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    blink::RuntimeEnabledFeatures::setMemoryCacheEnabled(b);
}

bool g_isMouseEnabled = true;

void WKE_CALL_TYPE wkeSetTouchEnabled(wkeWebView webView, bool b)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    blink::RuntimeEnabledFeatures::setTouchEnabled(b);
}

void WKE_CALL_TYPE wkeSetContextMenuEnabled(wkeWebView webView, bool b)
{
    content::WebPage* webpage = nullptr;
    if (webView)
        webpage = webView->getWebPage();
    if (webpage)
        webpage->setContextMenuEnabled(b);
}

void WKE_CALL_TYPE wkeSetMouseEnabled(wkeWebView webView, bool b)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    g_isMouseEnabled = b;
}

void WKE_CALL_TYPE wkeSetNavigationToNewWindowEnable(wkeWebView webView, bool b)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    blink::RuntimeEnabledFeatures::setNavigationToNewWindowEnabled(b);
}

void WKE_CALL_TYPE wkeSetCspCheckEnable(wkeWebView webView, bool b)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    blink::RuntimeEnabledFeatures::setCspCheckEnabled(b);
}

void WKE_CALL_TYPE wkeSetNpapiPluginsEnabled(wkeWebView webView, bool b)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::brokerActive())
        return;
    blink::RuntimeEnabledFeatures::setNpapiPluginsEnabled(b);
}

void WKE_CALL_TYPE wkeSetHeadlessEnabled(wkeWebView webView, bool b)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    blink::RuntimeEnabledFeatures::setHeadlessEnabled(b);
}

void WKE_CALL_TYPE wkeSetDragEnable(wkeWebView webView, bool b)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    wke::g_isSetDragEnable = b;
}

void WKE_CALL_TYPE wkeSetDragDropEnable(wkeWebView webView, bool b)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    wke::g_isSetDragDropEnable = b;
}

void WKE_CALL_TYPE wkeSetContextMenuItemShow(wkeWebView webView, wkeMenuItemId item, bool isShow)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (isShow)
        wke::g_contextMenuItemMask |= item;
    else
        wke::g_contextMenuItemMask &= (~item);
}

static std::vector<char> convertCookiesPathToUtf8(const WCHAR* path)
{
    std::wstring pathStr(path);
    if (pathStr[pathStr.size() - 1] != L'\\' && pathStr[pathStr.size() - 1] != L'/')
        pathStr += L'\\';
    if (!::PathIsDirectoryW(pathStr.c_str()))
        return std::vector<char>();
    pathStr += L"cookies.dat";

    std::vector<char> pathStrA;
    WTF::WCharToMByte(pathStr.c_str(), pathStr.size(), &pathStrA, CP_ACP);
    if (0 == pathStrA.size())
        return std::vector<char>();
    pathStrA.push_back('\0');

    return pathStrA;
}

void WKE_CALL_TYPE wkeSetDebugConfig(wkeWebView webview, const char* debugString, const char* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);

    if (nullptr != strstr(debugString, "paintcallbackInOtherThread")) {
        blink::RuntimeEnabledFeatures::setUpdataInOtherThreadEnabled(true);
        return;
    }

    if (wke::setDebugConfig(webview, debugString, param))
        return;

    content::WebPage* webpage = nullptr;
    blink::WebViewImpl* webViewImpl = nullptr;
    blink::WebSettingsImpl* settings = nullptr;
    if (webview)
        webpage = webview->getWebPage();
    if (webpage)
        webViewImpl = webpage->webViewImpl();
    if (webViewImpl)
        settings = webViewImpl->settingsImpl();

    String stringDebug(debugString);
    Vector<String> result;
    stringDebug.split(",", result);
    for (size_t i = 0; i < result.size(); ++i) {
        String item = result[i];
        if ("alwaysIsNotSolideColor" == item) {
            blink::RuntimeEnabledFeatures::setAlwaysIsNotSolideColorEnabled(true);
        } else if ("drawDirtyDebugLine" == item) {
            blink::RuntimeEnabledFeatures::setDrawDirtyDebugLineEnabled(true);
        } else if ("drawTileLine" == item) {
            blink::RuntimeEnabledFeatures::setDrawTileLineEnabled(true);
        } else if ("alwaysInflateDirtyRect" == item) {

        } else if ("decodeUrlRequest" == item) {
            wke::g_isDecodeUrlRequest = true;
        } else if ("showDevTools" == item) {
            webview->showDevTools(param, nullptr, nullptr);
        } else if ("setDNS" == item) {
            wke::g_DNS.reset(param ? new std::string(param) : nullptr);
        } else if ("setCurlSetopt" == item) {
            wke::g_curlSetoptCallback = reinterpret_cast<wkeOnNetCurlSetoptCallback>(const_cast<char*>(param));
        } else if ("wakeMinInterval" == item) {
            wke::g_kWakeMinInterval = atoi(param);
        } else if ("mainFrameRate" == item) {
            if (webpage && param) {
                char* end = nullptr;
                const long rate = strtol(param, &end, 10);
                if (end != param && *end == 0 && (rate == 0 || (rate >= 30 && rate <= 240)))
                    webpage->setMainFrameRate(static_cast<int>(rate));
            }
        } else if ("gpuPresent" == item) {
            if (webview)
                webview->setGpuPresent(param && atoi(param) != 0);
        } else if ("drawMinInterval" == item) {
            // Public debug setting is milliseconds; the compositor expects
            // fractional seconds. An int here silently truncated 1..999ms to 0.
            double drawMinInterval = atoi(param) / 1000.0;
            webpage->setDrawMinInterval(drawMinInterval);
        } else if ("minimumFontSize" == item) {
            if (settings)
                settings->setMinimumFontSize(atoi(param));
        } else if ("minimumLogicalFontSize" == item) {
            if (settings)
                settings->setMinimumLogicalFontSize(atoi(param));
        } else if ("defaultFontSize" == item) {
            if (settings)
                settings->setDefaultFontSize(atoi(param));
        } else if ("defaultFixedFontSize" == item) {
            if (settings)
                settings->setDefaultFixedFontSize(atoi(param));
        } else if ("tipPaintCallback" == item) {
            wke::g_tipPaintCallback = (void*)param;
        } else if ("contentScale" == item) {
            wke::g_contentScale = atoi(param) / 100.0;
        } else if ("antiAlias" == item) {
            wke::g_rendererAntiAlias = atoi(param) == 1;
        } else if ("diskCache" == item) {
            wke::g_diskCacheEnable = atoi(param) == 1;
        } else if ("consoleOutput" == item) {
            wke::g_consoleOutputEnable = atoi(param) == 1;
        } else if ("setStackLimit" == item) {
            uintptr_t currentStackPosition = reinterpret_cast<uintptr_t>(&currentStackPosition);
            v8::Isolate::GetCurrent()->SetStackLimit(currentStackPosition - (uintptr_t)atoi(param));
        }
    }
}

void* WKE_CALL_TYPE wkeGetDebugConfig(wkeWebView webview, const char* debugString)
{
    wke::checkThreadCallIsValid(__FUNCTION__);

    void* ret = NULL;
    if (wke::getDebugConfig(webview, debugString, &ret))
        return ret;

    content::WebPage* webpage = nullptr;
    blink::WebViewImpl* webViewImpl = nullptr;
    blink::WebSettingsImpl* settings = nullptr;
    if (webview)
        webpage = webview->getWebPage();
    if (webpage)
        webViewImpl = webpage->webViewImpl();
    if (webViewImpl)
        settings = webViewImpl->settingsImpl();

    String stringDebug(debugString);
    Vector<String> result;
    stringDebug.split(",", result);
    for (size_t i = 0; i < result.size(); ++i) {
        String item = result[i];
        if ("alwaysIsNotSolideColor" == item) {

        }
    }
    return NULL;
}

void WKE_CALL_TYPE wkeSetLanguage(wkeWebView webview, const char* language)
{
    webview->webPage()->webViewImpl()->page()->settings().setLanguage(String(language));
}

void WKE_CALL_TYPE wkeUpdate()
{
    if (wke::inputUiOwnerAvailable() && !content::CheckReEnter::getEnterCount())
        wke::CWebView::serviceDeferredPaintCloses();
//     static HWND hTimer = NULL;
//     if (!hTimer)
//         hTimer = FindWindow(L"TimerWindowClass", NULL);
// 
//     if (hTimer) {
//         MSG msg;
//         while(PeekMessage(&msg, hTimer, 0, 0, PM_REMOVE))
//         {
//             TranslateMessage(&msg);
//             DispatchMessage(&msg);
//         }
//     }
}

#define MAJOR_VERSION   (1)
#define MINOR_VERSION   (2)
#define WEBKIT_BUILD    (98096)

unsigned int WKE_CALL_TYPE wkeGetVersion()
{
    return (MAJOR_VERSION << 8) + MINOR_VERSION;
}

const utf8* WKE_CALL_TYPE wkeGetVersionString()
{
    if (wke::s_versionString)
        return wke::s_versionString->c_str();

    String versionString = String::format("wke version %d.%02d\n"
        "blink build %d\n"
        "build time %s\n",
        MAJOR_VERSION,
        MINOR_VERSION,
        WEBKIT_BUILD,
        __TIMESTAMP__);

    wke::s_versionString = new std::string(versionString.utf8().data());
    return wke::s_versionString->c_str();
}

const char* WKE_CALL_TYPE wkeGetName(wkeWebView webView)
{
    return webView->name();
}

void WKE_CALL_TYPE wkeSetName(wkeWebView webView, const char* name)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setName(name);
}

void WKE_CALL_TYPE wkeSetHandle(wkeWebView webView, HWND wnd)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setHandle(wnd);
    webView->webPage()->setNeedAutoDrawToHwnd(wnd != nullptr);
}

void WKE_CALL_TYPE wkeSetHandleOffset(wkeWebView webView, int x, int y)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setHandleOffset(x, y);
}

void WKE_CALL_TYPE wkeSetViewSettings(wkeWebView webView, const wkeViewSettings* settings)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setViewSettings(settings);
}

bool WKE_CALL_TYPE wkeIsTransparent(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->isTransparent();
}

void WKE_CALL_TYPE wkeSetTransparent(wkeWebView webView, bool transparent)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setTransparent(transparent);
}

void WKE_CALL_TYPE wkeSetUserAgent(wkeWebView webView, const utf8* userAgent)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setUserAgent(userAgent);
}

const utf8* WKE_CALL_TYPE wkeGetUserAgent(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return content::BlinkPlatformImpl::getUserAgent();
}

void WKE_CALL_TYPE wkeSetUserAgentW(wkeWebView webView, const wchar_t* userAgent)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setUserAgent(userAgent);
}

void WKE_CALL_TYPE wkeShowDevtools(wkeWebView webView, const wchar_t* path, wkeOnShowDevtoolsCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    std::vector<char> pathUtf8;
    WTF::WCharToMByte(path, wcslen(path), &pathUtf8, CP_UTF8);
    pathUtf8.push_back('\0');
    webView->showDevTools(&pathUtf8[0], callback, param);
}

void WKE_CALL_TYPE wkePostURL(wkeWebView wkeView,const utf8 * url,const char *szPostData,int nLen)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    wkeView->loadPostURL(url,szPostData,nLen);
}

void WKE_CALL_TYPE wkePostURLW(wkeWebView wkeView,const wchar_t * url,const char *szPostData,int nLen)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    wkeView->loadPostURL(url,szPostData,nLen);
}

void WKE_CALL_TYPE wkeLoadW(wkeWebView webView, const wchar_t* url)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    wkeLoadURLW(webView, url);
}

void WKE_CALL_TYPE wkeLoadURL(wkeWebView webView, const utf8* url)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->loadURL(url);
}

void WKE_CALL_TYPE wkeLoadURLW(wkeWebView webView, const wchar_t* url)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->loadURL(url);
}

void WKE_CALL_TYPE wkeLoadHTML(wkeWebView webView, const utf8* html)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->loadHTML(html);
}

void WKE_CALL_TYPE wkeLoadHtmlWithBaseUrl(wkeWebView webView, const utf8* html, const utf8* baseUrl)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->loadHtmlWithBaseUrl(html, baseUrl);
}

void WKE_CALL_TYPE wkeLoadHTMLW(wkeWebView webView, const wchar_t* html)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->loadHTML(html);
}

void WKE_CALL_TYPE wkeLoadFile(wkeWebView webView, const utf8* filename)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->loadFile(filename);
}

void WKE_CALL_TYPE wkeLoadFileW(wkeWebView webView, const wchar_t* filename)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->loadFile(filename);
}

const utf8* WKE_CALL_TYPE wkeGetURL(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->url();
}

bool WKE_CALL_TYPE wkeIsLoading(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->isLoading();
}

bool WKE_CALL_TYPE wkeIsLoadingSucceeded(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->isLoadingSucceeded();
}

bool WKE_CALL_TYPE wkeIsLoadingFailed(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->isLoadingFailed();
}

bool WKE_CALL_TYPE wkeIsLoadingCompleted(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->isLoadingCompleted();
}

bool WKE_CALL_TYPE wkeIsDocumentReady(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->isDocumentReady();
}

void WKE_CALL_TYPE wkeStopLoading(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->stopLoading();
}

void WKE_CALL_TYPE wkeReload(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->reload();
}

void WKE_CALL_TYPE wkeGoToOffset(wkeWebView webView, int offset)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->goToOffset(offset);
}

void WKE_CALL_TYPE wkeGoToIndex(wkeWebView webView, int index)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->goToIndex(index);
}

const utf8* WKE_CALL_TYPE wkeGetTitle(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->title();
}

const wchar_t* WKE_CALL_TYPE wkeGetTitleW(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->titleW();
}

void WKE_CALL_TYPE wkeResize(wkeWebView webView, int w, int h)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->resize(w, h);
}

int WKE_CALL_TYPE wkeGetWidth(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->width();
}

int WKE_CALL_TYPE wkeGetHeight(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->height();
}

int WKE_CALL_TYPE wkeGetContentWidth(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->contentWidth();
}

int WKE_CALL_TYPE wkeGetContentHeight(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->contentHeight();
}

void WKE_CALL_TYPE wkeSetDirty(wkeWebView webView, bool dirty)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setDirty(dirty);
}

bool WKE_CALL_TYPE wkeIsDirty(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->isDirty();
}

void WKE_CALL_TYPE wkeAddDirtyArea(wkeWebView webView, int x, int y, int w, int h)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->addDirtyArea(x, y, w, h);
}

void WKE_CALL_TYPE wkeLayoutIfNeeded(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->layoutIfNeeded();
}

void WKE_CALL_TYPE wkePaint2(wkeWebView webView, void* bits, int bufWid, int bufHei, int xDst, int yDst, int w, int h, int xSrc, int ySrc, bool bCopyAlpha)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->paint(bits, bufWid, bufHei, xDst, yDst, w, h, xSrc, ySrc, bCopyAlpha);
}

void WKE_CALL_TYPE wkePaint(wkeWebView webView, void* bits, int pitch)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->paint(bits, pitch);
}

void WKE_CALL_TYPE wkeRepaintIfNeeded(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (webView)
        webView->repaintIfNeeded();
}

HDC WKE_CALL_TYPE wkeGetViewDC(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->viewDC();
}

HWND WKE_CALL_TYPE wkeGetHostHWND(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->windowHandle();
}

bool WKE_CALL_TYPE wkeCanGoBack(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->canGoBack();
}

bool WKE_CALL_TYPE wkeGoBack(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->goBack();
}

bool WKE_CALL_TYPE wkeCanGoForward(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->canGoForward();
}

bool WKE_CALL_TYPE wkeGoForward(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->goForward();
}

void WKE_CALL_TYPE wkeEditorSelectAll(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->editorSelectAll();
}

void WKE_CALL_TYPE wkeEditorUnSelect(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->editorUnSelect();
}

void WKE_CALL_TYPE wkeEditorCopy(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->editorCopy();
}

void WKE_CALL_TYPE wkeEditorCut(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->editorCut();
}

void WKE_CALL_TYPE wkeEditorPaste(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->editorPaste();
}

void WKE_CALL_TYPE wkeEditorDelete(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->editorDelete();
}

void WKE_CALL_TYPE wkeEditorUndo(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->editorUndo();
}

void WKE_CALL_TYPE wkeEditorRedo(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->editorRedo();
}

const wchar_t* WKE_CALL_TYPE wkeGetCookieW(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->cookieW();
}

const utf8* WKE_CALL_TYPE wkeGetCookie(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->cookie();
}


void WKE_CALL_TYPE wkeSetCookieEnabled(wkeWebView webView, bool enable)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setCookieEnabled(enable);
}

bool WKE_CALL_TYPE wkeIsCookieEnabled(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->isCookieEnabled();
}


void WKE_CALL_TYPE wkeSetLocalStorageFullPath(wkeWebView webView, const WCHAR* path)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!path)
        return;

	String pathString(path);
    net::setDefaultLocalStorageFullPath(pathString);
}

void WKE_CALL_TYPE wkeAddPluginDirectory(wkeWebView webView, const WCHAR* path)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    String directory(path);
    content::PluginDatabase::installedPlugins()->addExtraPluginDirectory(directory);
}

void WKE_CALL_TYPE wkeSetMediaVolume(wkeWebView webView, float volume)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setMediaVolume(volume);
}

float WKE_CALL_TYPE wkeGetMediaVolume(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->mediaVolume();
}

bool WKE_CALL_TYPE wkeFireMouseEvent(wkeWebView webView, unsigned int message, int x, int y, unsigned int flags)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->fireMouseEvent(message, x, y, flags);
}

bool WKE_CALL_TYPE wkeFireContextMenuEvent(wkeWebView webView, int x, int y, unsigned int flags)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->fireContextMenuEvent(x, y, flags);
}

bool WKE_CALL_TYPE wkeFireMouseWheelEvent(wkeWebView webView, int x, int y, int delta, unsigned int flags)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->fireMouseWheelEvent(x, y, delta, flags);
}

bool WKE_CALL_TYPE wkeFireKeyUpEvent(wkeWebView webView, unsigned int virtualKeyCode, unsigned int flags, bool systemKey)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->fireKeyUpEvent(virtualKeyCode, flags, systemKey);
}

bool WKE_CALL_TYPE wkeFireKeyDownEvent(wkeWebView webView, unsigned int virtualKeyCode, unsigned int flags, bool systemKey)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->fireKeyDownEvent(virtualKeyCode, flags, systemKey);
}

bool WKE_CALL_TYPE wkeFireKeyPressEvent(wkeWebView webView, unsigned int charCode, unsigned int flags, bool systemKey)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->fireKeyPressEvent(charCode, flags, systemKey);
}

bool WKE_CALL_TYPE wkeSetComposition(wkeWebView webView,
    const wchar_t* text, int selectionStart, int selectionEnd)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webView || !text || !webView->webPage()
        || !webView->webPage()->webViewImpl()) {
        return false;
    }
    const size_t length = wcslen(text);
    blink::WebVector<blink::WebCompositionUnderline> underlines(
        static_cast<size_t>(length ? 1 : 0));
    if (length) {
        underlines[0] = blink::WebCompositionUnderline(0,
            static_cast<unsigned>(length), 0xff202020, false, 0);
    }
    const int safeStart = std::max(0,
        std::min(selectionStart, static_cast<int>(length)));
    const int safeEnd = std::max(safeStart,
        std::min(selectionEnd, static_cast<int>(length)));
    return webView->webPage()->webViewImpl()->setComposition(
        blink::WebString(reinterpret_cast<const blink::WebUChar*>(text),
            length), underlines, safeStart, safeEnd);
}

bool WKE_CALL_TYPE wkeConfirmComposition(wkeWebView webView,
    const wchar_t* text)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webView || !text || !webView->webPage()
        || !webView->webPage()->webViewImpl()) {
        return false;
    }
    const size_t length = wcslen(text);
    return webView->webPage()->webViewImpl()->confirmComposition(
        blink::WebString(reinterpret_cast<const blink::WebUChar*>(text),
            length));
}

bool WKE_CALL_TYPE wkeCancelComposition(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webView || !webView->webPage()
        || !webView->webPage()->webViewImpl()) {
        return false;
    }
    return webView->webPage()->webViewImpl()->setComposition(
        blink::WebString(), blink::WebVector<blink::WebCompositionUnderline>(),
        0, 0);
}

bool WKE_CALL_TYPE wkeFireWindowsMessage(wkeWebView webView, HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->fireWindowsMessage(hWnd, message, wParam, lParam, result);
}

void WKE_CALL_TYPE wkeSetFocus(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webView)
        return;
    webView->setFocus();

    if (webView->windowHandle())
        ::SetFocus(webView->windowHandle());
    //OutputDebugStringA("wkeSetFocus\n");
}

void WKE_CALL_TYPE wkeKillFocus(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webView)
        return;
    webView->killFocus();
    //OutputDebugStringA("killFocus\n");
}

wkeRect WKE_CALL_TYPE wkeGetCaretRect(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->caretRect();
}

jsValue WKE_CALL_TYPE wkeRunJS(wkeWebView webView, const utf8* script)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->runJS(script);
}

jsValue WKE_CALL_TYPE wkeRunJSW(wkeWebView webView, const wchar_t* script)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->runJS(script);
}

jsExecState WKE_CALL_TYPE wkeGlobalExec(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->globalExec();
}

jsExecState WKE_CALL_TYPE wkeGetGlobalExecByFrame(wkeWebView webView, wkeWebFrameHandle frameId)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->globalExecByFrame(frameId);
}

void WKE_CALL_TYPE wkeSleep(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->sleep();
}

void WKE_CALL_TYPE wkeWake(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (webView)
        webView->wake();

    static DWORD lastTime = 0;

    DWORD time = ::GetTickCount();
    if (time - lastTime < wke::g_kWakeMinInterval)
        return;

    lastTime = time;

    content::WebThreadImpl* threadImpl = (content::WebThreadImpl*)(blink::Platform::current()->currentThread());
    threadImpl->fire();
}

bool WKE_CALL_TYPE wkeIsAwake(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->isAwake();
}

void WKE_CALL_TYPE wkeSetZoomFactor(wkeWebView webView, float factor)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setZoomFactor(factor);
}

float WKE_CALL_TYPE wkeGetZoomFactor(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->zoomFactor();
}

void WKE_CALL_TYPE wkeSetEditable(wkeWebView webView, bool editable)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setEditable(editable);
}

void WKE_CALL_TYPE wkeOnTitleChanged(wkeWebView webView, wkeTitleChangedCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onTitleChanged(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnMouseOverUrlChanged(wkeWebView webView, wkeTitleChangedCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onMouseOverUrlChanged(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnURLChanged(wkeWebView webView, wkeURLChangedCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onURLChanged(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnURLChanged2(wkeWebView webView, wkeURLChangedCallback2 callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onURLChanged2(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnPaintUpdated(wkeWebView webView, wkePaintUpdatedCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onPaintUpdated(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnPaintBitUpdated(wkeWebView webView, wkePaintBitUpdatedCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onPaintBitUpdated(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnAlertBox(wkeWebView webView, wkeAlertBoxCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onAlertBox(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnConfirmBox(wkeWebView webView, wkeConfirmBoxCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onConfirmBox(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnPromptBox(wkeWebView webView, wkePromptBoxCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onPromptBox(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnNavigation(wkeWebView webView, wkeNavigationCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onNavigation(callback, param);
}

void WKE_CALL_TYPE wkeOnCreateView(wkeWebView webView, wkeCreateViewCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onCreateView(callback, param);
}

bool WKE_CALL_TYPE wkeIsProcessingUserGesture(wkeWebView webWindow)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    bool isUserGesture = blink::WebUserGestureIndicator::isProcessingUserGesture();
    return isUserGesture;
}

void WKE_CALL_TYPE wkeOnDocumentReady(wkeWebView webView, wkeDocumentReadyCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onDocumentReady(callback, param);
}

void WKE_CALL_TYPE wkeOnDocumentReady2(wkeWebView webView, wkeDocumentReady2Callback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onDocumentReady2(callback, param);
}

void WKE_CALL_TYPE wkeOnLoadingFinish(wkeWebView webView, wkeLoadingFinishCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onLoadingFinish(callback, param);
}

void WKE_CALL_TYPE wkeOnDownload(wkeWebView webView, wkeDownloadCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onDownload(callback, param);
}

void WKE_CALL_TYPE wkeOnDownload2(wkeWebView webView, wkeDownload2Callback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onDownload2(callback, param);
}

void WKE_CALL_TYPE wkeNetOnResponse(wkeWebView webView, wkeNetResponseCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onNetResponse(callback, param);
}

void WKE_CALL_TYPE wkeOnConsole(wkeWebView webView, wkeConsoleCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onConsole(callback, param);
}

void WKE_CALL_TYPE wkeUtilSetUiCallback(wkeUiThreadPostTaskCallback callback) 
{
    wke::g_wkeUiThreadPostTaskCallback = callback;
}

const utf8* WKE_CALL_TYPE wkeUtilSerializeToMHTML(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);

    blink::WebViewImpl* webviewImpl = webView->getWebPage()->webViewImpl();
    blink::WebCString result = blink::WebPageSerializer::serializeToMHTML(webviewImpl);
    const utf8* resultStr = wke::createTempCharString(result.data(), result.length());
    return resultStr;
}

void WKE_CALL_TYPE wkeSetUIThreadCallback(wkeWebView webView, wkeCallUiThread callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onCallUiThread(callback, param);
}

void WKE_CALL_TYPE wkeOnLoadUrlBegin(wkeWebView webView, wkeLoadUrlBeginCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onLoadUrlBegin(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnLoadUrlEnd(wkeWebView webView, wkeLoadUrlEndCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onLoadUrlEnd(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnLoadUrlFinish(wkeWebView webView, wkeLoadUrlFinishCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onLoadUrlFinish(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnLoadUrlHeadersReceived(wkeWebView webView, wkeLoadUrlHeadersReceivedCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onLoadUrlHeadersReceived(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnLoadUrlFail(wkeWebView webView, wkeLoadUrlFailCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onLoadUrlFail(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnDidCreateScriptContext(wkeWebView webView, wkeDidCreateScriptContextCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onDidCreateScriptContext(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnWillReleaseScriptContext(wkeWebView webView, wkeWillReleaseScriptContextCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onWillReleaseScriptContext(callback, callbackParam);
}

void WKE_CALL_TYPE wkeOnStartDragging(wkeWebView webView, wkeStartDraggingCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onStartDragging(callback, param);
}

void WKE_CALL_TYPE wkeOnPrint(wkeWebView webView, wkeOnPrintCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->onPrint(callback, param);
}

void WKE_CALL_TYPE wkeOnWillMediaLoad(wkeWebView webView, wkeWillMediaLoadCallback callback, void* callbackParam)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    wke::g_wkeWillMediaLoadCallback = callback;
    wke::g_wkeWillMediaLoadCallbackCallbackParam = callbackParam;
}

wkeTempCallbackInfo* WKE_CALL_TYPE wkeGetTempCallbackInfo(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return &wke::g_wkeTempCallbackInfo;
}

void WKE_CALL_TYPE wkeOnOtherLoad(wkeWebView webWindow, wkeOnOtherLoadCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webWindow->onOtherLoad(callback, param);
}

void WKE_CALL_TYPE wkeOnContextMenuItemClick(wkeWebView webWindow, wkeOnContextMenuItemClickCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webWindow->onContextMenuItemClick(callback, param);
}

void WKE_CALL_TYPE wkeDeleteWillSendRequestInfo(wkeWebView webWindow, wkeWillSendRequestInfo* info)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    wkeDeleteString(info->url);
    if (info->newUrl)
        wkeDeleteString(info->newUrl);
    wkeDeleteString(info->method);
    wkeDeleteString(info->referrer);
    delete info;
}

bool WKE_CALL_TYPE wkeIsMainFrame(wkeWebView webView, wkeWebFrameHandle frameId)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return false;
    blink::WebFrame* webFrame = page->getWebFrameFromFrameId(wke::CWebView::wkeWebFrameHandleToFrameId(page, frameId));
    if (!webFrame)
        return false;
    return !webFrame->parent();
}

bool WKE_CALL_TYPE wkeIsWebRemoteFrame(wkeWebView webView, wkeWebFrameHandle frameId)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return false;
    blink::WebFrame* webFrame = page->getWebFrameFromFrameId(wke::CWebView::wkeWebFrameHandleToFrameId(page, frameId));
    if (!webFrame)
        return false;
    return webFrame->isWebRemoteFrame();
}

wkeWebFrameHandle WKE_CALL_TYPE wkeWebFrameGetMainFrame(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return nullptr;
    blink::WebFrame* frame = page->mainFrame();
    if (!frame)
        return nullptr;
    return wke::CWebView::frameIdTowkeWebFrameHandle(page, page->getFrameIdByBlinkFrame(frame));
}

jsValue WKE_CALL_TYPE wkeRunJsByFrame(wkeWebView webView, wkeWebFrameHandle frameId, const utf8* script, bool isInClosure)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->runJsInFrame(frameId, script, isInClosure);
}

void WKE_CALL_TYPE wkeInsertCSSByFrame(wkeWebView webView, wkeWebFrameHandle frameId, const utf8* cssText)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return;
    blink::WebFrame* frame = page->mainFrame();
    if (!frame)
        return;
    frame->document().insertStyleSheet(blink::WebString::fromUTF8(cssText));
}

const utf8* WKE_CALL_TYPE wkeGetFrameUrl(wkeWebView webView, wkeWebFrameHandle frameId)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return "";
    blink::WebFrame* webFrame = page->getWebFrameFromFrameId(wke::CWebView::wkeWebFrameHandleToFrameId(page, frameId));
    if (!webFrame)
        return "";

    blink::WebDocument doc = webFrame->document();
    blink::KURL url = doc.baseURL();
    if (url.isNull() || url.isEmpty() || !url.isValid())
        return "";

    String urlString = url.getUTF8String();
    return wke::createTempCharString((const char *)urlString.characters8(), urlString.length());
}

void WKE_CALL_TYPE wkeWebFrameGetMainWorldScriptContext(wkeWebView webView, wkeWebFrameHandle frameId, v8ContextPtr contextOut)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return;
    blink::WebFrame* webFrame = page->getWebFrameFromFrameId(wke::CWebView::wkeWebFrameHandleToFrameId(page, frameId));
    if (!webFrame)
        return;
    v8::Local<v8::Context> result = webFrame->mainWorldScriptContext();
    v8::Local<v8::Context>* contextOutPtr = (v8::Local<v8::Context>*)contextOut;
    *contextOutPtr = result;
}

v8Isolate WKE_CALL_TYPE wkeGetBlinkMainThreadIsolate()
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return blink::mainThreadIsolate();
}

const utf8* WKE_CALL_TYPE wkeGetString(const wkeString s)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return s ? s->string() : "";
}

const wchar_t* WKE_CALL_TYPE wkeGetStringW(const wkeString string)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return string ? string->stringW() : L"";
}

void WKE_CALL_TYPE WKE_CALL_TYPE wkeSetString(wkeString string, const utf8* str, size_t len)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!string)
        return;

    if (nullptr == str) {
        str = "";
        len = 0;
    } else {
        if (len == 0)
            len = strlen(str);
    }

    string->setString(str, len);
}

void WKE_CALL_TYPE wkeSetStringW(wkeString string, const wchar_t* str, size_t len)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!string)
        return;

    if (nullptr == str) {
        str = L"";
        len = 0;
    } else {
        if (len == 0)
            len = wcslen(str);
    }

    string->setString(str, len);
}

wkeString WKE_CALL_TYPE wkeCreateString(const utf8* str, size_t len)
{
    //wke::checkThreadCallIsValid(__FUNCTION__);
    wkeString wkeStr = new wke::CString(str, len);
    return wkeStr;
}

wkeString WKE_CALL_TYPE wkeCreateStringW(const wchar_t* str, size_t len)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    wkeString wkeStr = new wke::CString(str, len);
    return wkeStr;
}

void WKE_CALL_TYPE WKE_CALL_TYPE wkeDeleteString(wkeString str)
{
    //wke::checkThreadCallIsValid(__FUNCTION__);
    delete str;
}

wkeWebView WKE_CALL_TYPE wkeGetWebViewForCurrentContext()
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* webpage = content::WebPage::getSelfForCurrentContext();
    if (!webpage)
        return nullptr;
    wkeWebView webview = webpage->wkeWebView();
    return webview;
}

void WKE_CALL_TYPE wkeSetUserKeyValue(wkeWebView webView, const char* key, void* value)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setUserKeyValue(key, value);
}

void* WKE_CALL_TYPE wkeGetUserKeyValue(wkeWebView webView, const char* key)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->getUserKeyValue(key);
}

int WKE_CALL_TYPE wkeGetCursorInfoType(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->getCursorInfoType();
}

void WKE_CALL_TYPE wkeSetCursorInfoType(wkeWebView webView, int type)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setCursorInfoType(type);
}

void WKE_CALL_TYPE wkeSetDragFiles(wkeWebView webView, const POINT* clintPos, const POINT* screenPos, wkeString files[], int filesCount)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    webView->setDragFiles(clintPos, screenPos, files, filesCount);
}

wkeWebView WKE_CALL_TYPE wkeCreateWebWindow(wkeWindowType type, HWND parent, int x, int y, int width, int height)
{
    wke::diagnostics::renderer_factory_entered(WKE_RENDER_MODEL_FACTORY_EXTERNAL_VIEW);
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!wke::brokerMayCreateRawView())
        return nullptr;
    wke::CWebWindow* webWindow = new wke::CWebWindow(cc::s_kBgColor);
    if (!webWindow->createWindow(parent, type, x, y, width, height)) {
        delete webWindow;
        return NULL;
    }

    return webWindow;
}

wkeWebView WKE_CALL_TYPE wkeCreateWebCustomWindow(const wkeWindowCreateInfo* info)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!wke::brokerMayCreateRawView())
        return nullptr;
    wke::CWebWindow* webWindow = new wke::CWebWindow(info->color);
    if (!webWindow->createWindow(info)) {
        delete webWindow;
        return NULL;
    }

    return webWindow;
}

wkeWebView WKE_CALL_TYPE wkeCreateWebView()
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!wke::brokerMayCreateRawView())
        return nullptr;
    wke::CWebView* webView = new wke::CWebView(cc::s_kBgColor);
    webView->webPage()->setNeedAutoDrawToHwnd(false);

    //s_webViews.append(webView);
    return webView;
}

void WKE_CALL_TYPE wkeDestroyWebView(wkeWebView webView)
{
    if (::GetCurrentThreadId() != wke::wkeThreadId || !wke::wkeIsInit
        || workerFinalizationCompleted || !webView)
        return;
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::g_liveWebViews.find(webView) == wke::g_liveWebViews.end()
        || !wke::brokerMayDestroyRawView(webView))
        return;
    auto permit = webView->acquirePaintClosePermit(content::PaintCloseKind::view);
    if (!permit)
        return;
    invalidateInteractionEndpointsForWebView(webView);
    const auto callback = webView->getWkeHandler()->windowDestroyCallback;
    const auto parameter = webView->getWkeHandler()->windowDestroyCallbackParam;
    if (callback && webView->takeWindowDestroyCallback())
        callback(webView, parameter);
    // The callback may request native close. Do not remove activation/live
    // identity until the actual native window ownership receipt is retired.
    if (!webView->prepareNativeWindowClose())
        return;
    net::ActivatingObjCheck::inst()->remove(webView->getId());
    delete webView; // permit retains its independent tombstone through disposal.
}

void WKE_CALL_TYPE wkeDestroyWebWindow(wkeWebView webWindow)
{
    if (::GetCurrentThreadId() != wke::wkeThreadId || !wke::wkeIsInit
        || workerFinalizationCompleted || !webWindow)
        return;
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::g_liveWebViews.find(webWindow) != wke::g_liveWebViews.end()
        && wke::brokerMayDestroyRawView(webWindow))
        webWindow->destroy(); // CWebWindow holds the window close permit.
}

HWND WKE_CALL_TYPE wkeGetWindowHandle(wkeWebView webWindow)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->windowHandle();
    else
        return NULL;
}

void WKE_CALL_TYPE wkeOnWindowClosing(wkeWebView webWindow, wkeWindowClosingCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->onClosing(callback, param);
}

void WKE_CALL_TYPE wkeOnWindowDestroy(wkeWebView webWindow, wkeWindowDestroyCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->onDestroy(callback, param);
}

void WKE_CALL_TYPE wkeOnDraggableRegionsChanged(wkeWebView webWindow, wkeDraggableRegionsChangedCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::CWebView* window = static_cast<wke::CWebView*>(webWindow))
        return window->onDraggableRegionsChanged(callback, param);
}

void WKE_CALL_TYPE wkeShowWindow(wkeWebView webWindow, bool showFlag)
{
    if (::GetCurrentThreadId() != wke::wkeThreadId || !wke::wkeIsInit
        || workerFinalizationCompleted || !webWindow
        || wke::g_liveWebViews.find(webWindow) == wke::g_liveWebViews.end())
        return;
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->show(showFlag);
}

void WKE_CALL_TYPE wkeEnableWindow(wkeWebView webWindow, bool enableFlag)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->enable(enableFlag);
}

void WKE_CALL_TYPE wkeMoveWindow(wkeWebView webWindow, int x, int y, int width, int height)
{
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->move(x, y, width, height);
}

void WKE_CALL_TYPE wkeMoveToCenter(wkeWebView webWindow)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->moveToCenter();
}

void WKE_CALL_TYPE wkeResizeWindow(wkeWebView webWindow, int width, int height)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->resize(width, height);
}

void WKE_CALL_TYPE wkeSetWindowTitle(wkeWebView webWindow, const utf8* title)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->setTitle(title);
}

void WKE_CALL_TYPE wkeSetWindowTitleW(wkeWebView webWindow, const wchar_t* title)
{
    if (wke::CWebWindow* window = static_cast<wke::CWebWindow*>(webWindow))
        return window->setTitle(title);
}

WKE_EXTERN_C wkeNodeOnCreateProcessCallback g_wkeNodeOnCreateProcessCallback = nullptr;
WKE_EXTERN_C void* g_wkeNodeOnCreateProcessCallbackparam = nullptr;

void WKE_CALL_TYPE wkeNodeOnCreateProcess(wkeWebView webWindow, wkeNodeOnCreateProcessCallback callback, void* param)
{
    g_wkeNodeOnCreateProcessCallback = callback;
    g_wkeNodeOnCreateProcessCallbackparam = param;
}

static String memBufToString(wkeMemBuf* stringType)
{
    if (!stringType || !stringType->length)
        return String();
    return blink::WebString::fromUTF8((const char*)stringType->data, stringType->length);
}

static void convertDragData(blink::WebDragData* data, const wkeWebDragData* webDragData)
{
    data->initialize();

    data->setFilesystemId(memBufToString(webDragData->m_filesystemId));
    for (int i = 0; i < webDragData->m_itemListLength; ++i) {
        wkeWebDragData::Item* it = &webDragData->m_itemList[i];
        blink::WebDragData::Item item;
        item.storageType = (blink::WebDragData::Item::StorageType)it->storageType;
        item.stringType = memBufToString(it->stringType);
        item.stringData = memBufToString(it->stringData);
        item.filenameData = memBufToString(it->filenameData);
        item.displayNameData = memBufToString(it->displayNameData);
        if (it->binaryData)
            item.binaryData.assign((const char *)it->binaryData->data, it->binaryData->length);
        item.title = memBufToString(it->title);
        if (it->fileSystemURL)
            item.fileSystemURL = blink::KURL(blink::ParsedURLString, memBufToString(it->fileSystemURL));
        item.fileSystemFileSize = it->fileSystemFileSize;
        item.baseURL = blink::KURL(blink::ParsedURLString, memBufToString(it->baseURL));

        data->addItem(item);
    }
}

wkeWebDragOperation WKE_CALL_TYPE wkeDragTargetDragEnter(wkeWebView webWindow, const wkeWebDragData* webDragData, const POINT* clientPoint, const POINT* screenPoint, wkeWebDragOperationsMask operationsAllowed, int modifiers)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webWindow->webPage())
        return wkeWebDragOperationNone;
    blink::WebViewImpl* view = webWindow->webPage()->webViewImpl();
    if (!view)
        return wkeWebDragOperationNone;

    blink::WebDragData data;
    convertDragData(&data, webDragData);
    blink::WebDragOperation op = view->dragTargetDragEnter(data,
        blink::WebPoint(clientPoint->x, clientPoint->y),
        blink::WebPoint(screenPoint->x, screenPoint->y),
        (blink::WebDragOperationsMask)operationsAllowed, modifiers);

    return (wkeWebDragOperation)op;
}

wkeWebDragOperation WKE_CALL_TYPE wkeDragTargetDragOver(wkeWebView webWindow, const POINT* clientPoint, const POINT* screenPoint, wkeWebDragOperationsMask operationsAllowed, int modifiers)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webWindow->webPage())
        return wkeWebDragOperationNone;
    blink::WebViewImpl* view = webWindow->webPage()->webViewImpl();
    if (!view)
        return wkeWebDragOperationNone;

    blink::WebDragOperation op = view->dragTargetDragOver(
        blink::WebPoint(clientPoint->x, clientPoint->y),
        blink::WebPoint(screenPoint->x, screenPoint->y),
        (blink::WebDragOperationsMask)operationsAllowed,
        modifiers);
    return (wkeWebDragOperation)op;
}

void WKE_CALL_TYPE wkeDragTargetDragLeave(wkeWebView webWindow)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webWindow->webPage())
        return;
    blink::WebViewImpl* view = webWindow->webPage()->webViewImpl();
    if (view)
        view->dragTargetDragLeave();
}

void WKE_CALL_TYPE wkeDragTargetDrop(wkeWebView webWindow, const POINT* clientPoint, const POINT* screenPoint, int modifiers)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webWindow->webPage())
        return;
    blink::WebViewImpl* view = webWindow->webPage()->webViewImpl();
    if (!view)
        return;

    view->dragTargetDrop(blink::WebPoint(clientPoint->x, clientPoint->y), blink::WebPoint(screenPoint->x, screenPoint->y), modifiers);
}

void WKE_CALL_TYPE wkeDragTargetEnd(wkeWebView webWindow, const POINT* clientPoint, const POINT* screenPoint, wkeWebDragOperation op)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!webWindow->webPage())
        return;
    blink::WebViewImpl* view = webWindow->webPage()->webViewImpl();
    if (!view)
        return;

    view->dragSourceEndedAt(blink::WebPoint(clientPoint->x, clientPoint->y), blink::WebPoint(screenPoint->x, screenPoint->y), (blink::WebDragOperation)op);
    view->dragSourceSystemDragEnded();
}

void WKE_CALL_TYPE wkeSetDeviceParameter(wkeWebView webView, const char* device, const char* paramStr, int paramInt, float paramFloat)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (0 == strcmp(device, "navigator.maxTouchPoints")) {
        blink::WebSettingsImpl* settings = webView->webPage()->webViewImpl()->settingsImpl();
        if (settings)
            settings->setMaxTouchPoints(paramInt);
    } else if (0 == strcmp(device, "navigator.platform")) {
        if (blink::g_navigatorPlatform)
            free(blink::g_navigatorPlatform);
        int length = strlen(paramStr);
        blink::g_navigatorPlatform = (char*)malloc(length + 1);
        memset(blink::g_navigatorPlatform, 0, length + 1);
        strncpy(blink::g_navigatorPlatform, paramStr, length);
    } else if (0 == strcmp(device, "navigator.hardwareConcurrency")) {
        content::BlinkPlatformImpl* platform = (content::BlinkPlatformImpl*)blink::Platform::current();
        platform->setNumberOfProcessors(paramInt);
    } else if (0 == strcmp(device, "navigator.vibrate")) {
        ;
    } else if (0 == strcmp(device, "screen.width")) {
        blink::WebScreenInfo info = webView->webPage()->screenInfo();
        info.rect.width = paramInt;
        webView->webPage()->setScreenInfo(info);
    } else if (0 == strcmp(device, "screen.height")) {
        blink::WebScreenInfo info = webView->webPage()->screenInfo();
        info.rect.height = paramInt;
        webView->webPage()->setScreenInfo(info);
    } else if (0 == strcmp(device, "screen.availWidth")) {
        blink::WebScreenInfo info = webView->webPage()->screenInfo();
        info.availableRect.width = paramInt;
        webView->webPage()->setScreenInfo(info);
    } else if (0 == strcmp(device, "screen.availHeight")) {
        blink::WebScreenInfo info = webView->webPage()->screenInfo();
        info.availableRect.height = paramInt;
        webView->webPage()->setScreenInfo(info);
    } else if (0 == strcmp(device, "screen.pixelDepth") || 0 == strcmp(device, "screen.pixelDepth")) {
        blink::WebScreenInfo info = webView->webPage()->screenInfo();
        info.depth = paramInt;
        webView->webPage()->setScreenInfo(info);
    } else if (0 == strcmp(device, "navigator.vendor")) {        
        webView->webPage()->webViewImpl()->page()->settings().setVendor(paramStr);
    } else if (0 == strcmp(device, "navigator.javaEnabled")) {
        webView->webPage()->webViewImpl()->page()->settings().setJavaEnabled(paramInt);
    }
    else if (0 == strcmp(device, "window.devicePixelRatio")) {
        wkeSetZoomFactor(webView, paramFloat);
    }
}

void WKE_CALL_TYPE wkeAddNpapiPlugin(wkeWebView webView, void* initializeFunc, void* getEntryPointsFunc, void* shutdownFunc)
{
    wke::diagnostics::renderer_factory_entered(WKE_RENDER_MODEL_FACTORY_PLUGIN);
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!wke::brokerPluginEntry())
        return;
    wke::checkThreadCallIsValid(__FUNCTION__);
    RefPtr<content::PluginPackage> package = content::PluginPackage::createVirtualPackage(
        (NP_InitializeFuncPtr)initializeFunc,
        (NP_GetEntryPointsFuncPtr) getEntryPointsFunc,
        (NPP_ShutdownProcPtr) shutdownFunc
        );

//     content::PluginDatabase* database = content::PluginDatabase::installedPlugins();
//     database->addVirtualPlugin(package);
//     database->setPreferredPluginForMIMEType(mime, package.get());
}

void WKE_CALL_TYPE wkeOnPluginFind(wkeWebView webView, const char* mime, wkeOnPluginFindCallback callback, void* param)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    if (!wke::brokerPluginEntry())
        return;
    wke::g_wkePluginFindcallback = callback;
    wke::g_wkePluginFindcallbackParam = param;
}

wkeWebView WKE_CALL_TYPE wkeGetWebViewByNData(void* ndata)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPluginImpl* plugin = (content::WebPluginImpl*)ndata;
    return plugin->getWkeWebView();
}

bool WKE_CALL_TYPE wkeRegisterEmbedderCustomElement(wkeWebView webView, wkeWebFrameHandle frameId, const char* name, void* options, void* outResult)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return false;
    blink::WebFrame* webFrame = page->getWebFrameFromFrameId(wke::CWebView::wkeWebFrameHandleToFrameId(page, frameId));
    if (!webFrame)
        return false;

    blink::WebExceptionCode c = 0;
    v8::Local<v8::Value>& optionsV8 = *(v8::Local<v8::Value>*)options;

    blink::WebString nameString = blink::WebString::fromUTF8(name);
    blink::WebCustomElement::addEmbedderCustomElementName(nameString);
    v8::Local<v8::Value> elementConstructor = webFrame->document().registerEmbedderCustomElement(nameString, optionsV8, c);
    v8::Persistent<v8::Value>* result = (v8::Persistent<v8::Value>*)outResult;
    result->Reset(v8::Isolate::GetCurrent(), elementConstructor);
    return true;
}

void WKE_CALL_TYPE wkeSetMediaPlayerFactory(wkeWebView webView, wkeMediaPlayerFactory factory, wkeOnIsMediaPlayerSupportsMIMEType callback)
{
    wke::diagnostics::renderer_factory_entered(WKE_RENDER_MODEL_FACTORY_MEDIA);
    wke::g_wkeMediaPlayerFactory = factory;
    wke::g_onIsMediaPlayerSupportsMIMETypeCallback = callback;
}

const utf8* WKE_CALL_TYPE wkeUtilDecodeURLEscape(const utf8* url)
{
    String result = blink::decodeURLEscapeSequences(url);
    if (result.isNull() || result.isEmpty())
        return url;
    Vector<char> buffer = WTF::ensureStringToUTF8(result, false);
    const char* resultStr = wke::createTempCharString((const char*)buffer.data(), buffer.size());
    return resultStr;
}

const utf8* WKE_CALL_TYPE wkeUtilEncodeURLEscape(const utf8* url)
{
    String result = blink::encodeWithURLEscapeSequences(String::fromUTF8(url));
    if (result.isNull() || result.isEmpty())
        return url;
    Vector<char> buffer = WTF::ensureStringToUTF8(result, false);
    const char* resultStr = wke::createTempCharString((const char*)buffer.data(), buffer.size());
    return resultStr;
}

int WKE_CALL_TYPE wkeGetWebviewId(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    return webView->getId();
}

bool WKE_CALL_TYPE wkeIsWebviewAlive(int id)
{
    return net::ActivatingObjCheck::inst()->isActivating(id);
}

const utf8* WKE_CALL_TYPE wkeGetDocumentCompleteURL(wkeWebView webView, wkeWebFrameHandle frameId, const utf8* partialURL)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return nullptr;
    blink::WebFrame* webFrame = page->getWebFrameFromFrameId(wke::CWebView::wkeWebFrameHandleToFrameId(page, frameId));
    if (!webFrame)
        return nullptr;

    blink::KURL url = webFrame->document().completeURL(blink::WebString::fromUTF8(partialURL));
    if (!url.isValid())
        return nullptr;

    String result = url.getUTF8String();
    return wke::createTempCharString((const char*)result.characters8(), result.length());
}

//////////////////////////////////////////////////////////////////////////
// V1 API

void WKE_CALL_TYPE wkeInit()
{
    wkeInitialize();
}

void WKE_CALL_TYPE wkeShutdown()
{
    wkeFinalize();
}

unsigned int WKE_CALL_TYPE wkeVersion()
{
    return wkeGetVersion();
}

const utf8* WKE_CALL_TYPE wkeVersionString()
{
    return wkeGetVersionString();
}

void WKE_CALL_TYPE wkeGC(wkeWebView webView, long intervalSec)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::BlinkPlatformImpl* platformImpl = (content::BlinkPlatformImpl*)blink::Platform::current();
    platformImpl->setGcTimer((double)intervalSec);
}

void WKE_CALL_TYPE wkeSetResourceGc(wkeWebView webView, long intervalSec)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::BlinkPlatformImpl* platformImpl = (content::BlinkPlatformImpl*)blink::Platform::current();
    platformImpl->setResGcTimer((double)intervalSec);
}

WKE_EXTERN_C void curl_set_file_system(
    WKE_FILE_OPEN pfnOpen,
    WKE_FILE_CLOSE pfnClose,
    WKE_FILE_SIZE pfnSize,
    WKE_FILE_READ pfnRead,
    WKE_FILE_SEEK pfnSeek,
    WKE_EXISTS_FILE pfnExistsFile);

void WKE_CALL_TYPE wkeSetFileSystem(WKE_FILE_OPEN pfnOpen, WKE_FILE_CLOSE pfnClose, WKE_FILE_SIZE pfnSize, WKE_FILE_READ pfnRead, WKE_FILE_SEEK pfnSeek)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    WKE_FILE_OPEN g_pfnOpen = pfnOpen;
    WKE_FILE_CLOSE g_pfnClose = pfnClose;
    //curl_set_file_system(pfnOpen, pfnClose, pfnSize, pfnRead, pfnSeek, nullptr);
}

const char* WKE_CALL_TYPE wkeWebViewName(wkeWebView webView)
{
    return wkeGetName(webView);
}

void WKE_CALL_TYPE wkeSetWebViewName(wkeWebView webView, const char* name)
{
    wkeSetName(webView, name);
}

bool WKE_CALL_TYPE wkeIsLoaded(wkeWebView webView)
{
    return wkeIsLoading(webView);
}

bool WKE_CALL_TYPE wkeIsLoadFailed(wkeWebView webView)
{
    return wkeIsLoadingFailed(webView);
}

bool WKE_CALL_TYPE wkeIsLoadComplete(wkeWebView webView)
{
    return wkeIsLoadingCompleted(webView);
}

const utf8* WKE_CALL_TYPE wkeGetSource(wkeWebView webView)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return nullptr;

    blink::WebFrame* webFrame = page->mainFrame();
    if (!webFrame)
        return nullptr;
    blink::WebString result = webFrame->contentAsMarkup();
    if (result.isNull() || result.isEmpty())
        return nullptr;

    std::string resultUtf8 = result.utf8();
    return wke::createTempCharString(resultUtf8.c_str(), resultUtf8.size());

    return nullptr;
}

const utf8* WKE_CALL_TYPE wkeTitle(wkeWebView webView)
{
    return wkeGetTitle(webView);
}

const wchar_t* WKE_CALL_TYPE wkeTitleW(wkeWebView webView)
{
    return wkeGetTitleW(webView);
}

int WKE_CALL_TYPE wkeWidth(wkeWebView webView)
{
    return wkeGetWidth(webView);
}

int WKE_CALL_TYPE wkeHeight(wkeWebView webView)
{
    return wkeGetHeight(webView);
}

int WKE_CALL_TYPE wkeContentsWidth(wkeWebView webView)
{
    return wkeGetContentWidth(webView);
}

int WKE_CALL_TYPE wkeContentsHeight(wkeWebView webView)
{
    return wkeGetContentHeight(webView);
}

void WKE_CALL_TYPE wkeSelectAll(wkeWebView webView)
{
    wkeEditorSelectAll(webView);
}

void WKE_CALL_TYPE wkeCopy(wkeWebView webView)
{
    wkeEditorCopy(webView);
}

void WKE_CALL_TYPE wkeCut(wkeWebView webView)
{
    wkeEditorCut(webView);
}

void WKE_CALL_TYPE wkePaste(wkeWebView webView)
{
    wkeEditorPaste(webView);
}

void WKE_CALL_TYPE wkeDelete(wkeWebView webView)
{
    wkeEditorDelete(webView);
}

bool WKE_CALL_TYPE wkeCookieEnabled(wkeWebView webView)
{
    return wkeIsCookieEnabled(webView);
}

float WKE_CALL_TYPE wkeMediaVolume(wkeWebView webView)
{
    return wkeGetMediaVolume(webView);
}

bool WKE_CALL_TYPE wkeMouseEvent(wkeWebView webView, unsigned int message, int x, int y, unsigned int flags)
{
    return wkeFireMouseEvent(webView, message, x, y, flags);
}

bool WKE_CALL_TYPE wkeContextMenuEvent(wkeWebView webView, int x, int y, unsigned int flags)
{
    return wkeFireContextMenuEvent(webView, x, y, flags);
}

bool WKE_CALL_TYPE wkeMouseWheel(wkeWebView webView, int x, int y, int delta, unsigned int flags)
{
    return wkeFireMouseWheelEvent(webView, x, y, delta, flags);
}

bool WKE_CALL_TYPE wkeKeyUp(wkeWebView webView, unsigned int virtualKeyCode, unsigned int flags, bool systemKey)
{
    return wkeFireKeyUpEvent(webView, virtualKeyCode, flags, systemKey);
}

bool WKE_CALL_TYPE wkeKeyDown(wkeWebView webView, unsigned int virtualKeyCode, unsigned int flags, bool systemKey)
{
    return wkeFireKeyDownEvent(webView, virtualKeyCode, flags, systemKey);
}

bool WKE_CALL_TYPE wkeKeyPress(wkeWebView webView, unsigned int charCode, unsigned int flags, bool systemKey)
{
    return wkeFireKeyPressEvent(webView, charCode, flags, systemKey);
}

void WKE_CALL_TYPE wkeFocus(wkeWebView webView)
{
    wkeSetFocus(webView);
}

void WKE_CALL_TYPE wkeUnfocus(wkeWebView webView)
{
    wkeKillFocus(webView);
}

wkeRect WKE_CALL_TYPE wkeGetCaret(wkeWebView webView)
{
    return wkeGetCaretRect(webView);
}

void WKE_CALL_TYPE wkeAwaken(wkeWebView webView)
{
    return wkeWake(webView);
}

float WKE_CALL_TYPE wkeZoomFactor(wkeWebView webView)
{
    return wkeGetZoomFactor(webView);
}

void WKE_CALL_TYPE wkeTitleChangedCallbackWrap(wkeWebView webView, void* param, const wkeString title)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    const wkeClientHandler* handler = (const wkeClientHandler*)param;
    handler->onTitleChanged(handler, title);
}

void WKE_CALL_TYPE wkeURLChangedCallbackWrap(wkeWebView webView, void* param, const wkeString url)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    const wkeClientHandler* handler = (const wkeClientHandler*)param;
    handler->onTitleChanged(handler, url);
}

void WKE_CALL_TYPE wkeSetClientHandler(wkeWebView webView, const wkeClientHandler* handler)
{
    webView->setClientHandler(handler);

    wkeOnTitleChanged(webView, wkeTitleChangedCallbackWrap, (void*)handler);
    wkeOnURLChanged(webView, wkeURLChangedCallbackWrap, (void*)handler);
}

const wkeClientHandler* WKE_CALL_TYPE wkeGetClientHandler(wkeWebView webView)
{
    return (const wkeClientHandler*)webView->getClientHandler();
}

const utf8* WKE_CALL_TYPE wkeGetContentAsMarkup(wkeWebView webView, wkeWebFrameHandle frameId, size_t* size)
{
    wke::checkThreadCallIsValid(__FUNCTION__);
    content::WebPage* page = webView->webPage();
    if (!page)
        return nullptr;
    blink::WebFrame* webFrame = page->getWebFrameFromFrameId(wke::CWebView::wkeWebFrameHandleToFrameId(page, frameId));
    if (!webFrame)
        return nullptr;
    blink::WebString result = webFrame->contentAsMarkup();
    if (result.isNull() || result.isEmpty())
        return nullptr;

    std::string resultUtf8 = result.utf8();
    if (size)
        *size = resultUtf8.size();
    return wke::createTempCharString(resultUtf8.c_str(), resultUtf8.size());
}

const utf8* WKE_CALL_TYPE wkeToString(const wkeString string)
{
    return wkeGetString(string);
}

const wchar_t* WKE_CALL_TYPE wkeToStringW(const wkeString string)
{
    return wkeGetStringW(string);
}

const utf8* WKE_CALL_TYPE wkeUtilBase64Encode(const utf8* str)
{
    if (!str)
        return nullptr;

    CString inStr(str);
    String result = WTF::base64Encode(inStr, WTF::Base64InsertLFs);

    if (result.isNull() || result.isEmpty() || !result.is8Bit())
        return nullptr;
    return wke::createTempCharString((const char *)(result.characters8()), result.length());
}

const utf8* WKE_CALL_TYPE wkeUtilBase64Decode(const utf8* str)
{
    CString inStr(str);
    Vector<char> result;
    bool ok = WTF::base64Decode(str, strlen(str), result);

    if (!ok || 0 == result.size())
        return nullptr;
    return wke::createTempCharString(result.data(), result.size());
}

const wkeMemBuf* WKE_CALL_TYPE wkeUtilCreateV8Snapshot(const utf8* str)
{
    //i::CpuFeatures::Probe(true);
    //v8::V8::InitializeICU();
    v8::Platform* platform = v8::platform::CreateDefaultPlatform();
    v8::V8::InitializePlatform(platform);
    v8::V8::Initialize();

    v8::StartupData blob = v8::V8::CreateSnapshotDataBlob(str);
    wkeMemBuf* result = wkeCreateMemBuf(nullptr, (void *)(blob.data), blob.raw_size);

    delete[] blob.data;

    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
    delete platform;

    return result;
}

void WKE_CALL_TYPE wkeSaveMemoryCache(wkeWebView webView)
{
    net::WebURLLoaderManager::sharedInstance()->saveDiskCache();
}

void WKE_CALL_TYPE wkeRunMessageLoop()
{
    MSG msg = { 0 };
    while (true) {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (WM_QUIT == msg.message)
                break;
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        wkeWake(nullptr);
        ::Sleep(2);
    }
}

// V1 API end
//////////////////////////////////////////////////////////////////////////

namespace wke {

bool checkThreadCallIsValid(const char* funcName)
{
    String output;
    if (!wke::wkeIsInit) {
        output = L"禁止初始化前调用此接口：";
        output.append(funcName);
        ::MessageBoxW(nullptr, output.charactersWithNullTermination().data(), L"警告！", MB_OK);
        ::TerminateProcess((HANDLE)-1, 5);
        return false;
    }

    if (WTF::isMainThread())
        return true;
        
    output = L"禁止多线程调用此接口：";
    output.append(funcName);
    output.append(L"。当前线程id：");
    output.append(String::number(::GetCurrentThreadId()));
    output.append(L"，主线程id：");
    output.append(String::number(wkeThreadId));
    
    ::MessageBoxW(nullptr, output.charactersWithNullTermination().data(), L"警告！", MB_OK);
    ::TerminateProcess((HANDLE)-1, 5);
    return false;
}

}

#endif
