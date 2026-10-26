#include "wke/wke.h"
#include "wke/wkeWebView.h"
#include "wke/wkeGlobalVar.h"
#include "content/browser/WebPage.h"
#include "content/browser/CheckReEnter.h"
#include "content/common/InputFrameTrace.h"
#include "content/web_impl_win/WebThreadImpl.h"
#include "third_party/WebKit/public/platform/Platform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace wke { bool inputUiOwnerAvailable(); }

namespace {
namespace trace = content::input_frame_trace;
static_assert(sizeof(trace::event) == WKE_INPUT_UI_EVENT_BYTES, "Trace event ABI mismatch");
static_assert(trace::stage_count <= WKE_INPUT_UI_STAGE_CAPACITY, "Trace stage ABI capacity");
static_assert(trace::counter_count <= WKE_INPUT_UI_COUNTER_CAPACITY, "Trace counter ABI capacity");
static_assert(trace::duration_metric_count <= WKE_INPUT_UI_DURATION_CAPACITY, "Trace metric ABI capacity");
static_assert(trace::duration_buckets == WKE_INPUT_UI_DURATION_BUCKETS, "Trace histogram ABI mismatch");

std::mutex drainMutex;
std::mutex configurationMutex; // Cold bootstrap only; never a producer/consumer lock.
std::atomic<unsigned long long> drainedEvents{0};
std::atomic<bool> exportLimitReached{false};

wke::CWebView* liveOwnerView(void* opaque)
{
    if (!wke::inputUiOwnerAvailable() || !opaque)
        return nullptr;
    const auto view = static_cast<wkeWebView>(opaque);
    if (wke::g_liveWebViews.find(view) == wke::g_liveWebViews.end())
        return nullptr;
    return static_cast<wke::CWebView*>(view);
}

void WKE_INPUT_UI_CALL serviceOwner()
{
    if (!wke::inputUiOwnerAvailable())
        return;
    auto* platform = blink::Platform::current();
    if (!platform || !platform->mainThread())
        return;
    static_cast<content::WebThreadImpl*>(platform->mainThread())->serviceOwnerOpportunity();
    if (wke::inputUiOwnerAvailable() && !content::CheckReEnter::getEnterCount())
        wke::CWebView::serviceDeferredPaintCloses();
}

int WKE_INPUT_UI_CALL cancelInput(void* opaque)
{
    auto* view = liveOwnerView(opaque);
    if (!view || !view->webPage())
        return WKE_INPUT_UI_UNAVAILABLE;
    content::PaintActivityScope activity(view->webPage()->paintLifetimeState());
    if (!activity)
        return WKE_INPUT_UI_REJECTED;
    try {
        return view->webPage()->cancelInputState() ? WKE_INPUT_UI_OK : WKE_INPUT_UI_REJECTED;
    } catch (...) {
        // The caller must enter limited recovery; partial cancellation is not success.
        return WKE_INPUT_UI_REJECTED;
    }
}

bool readViewEvent(wke::CWebView* view, const void* input, unsigned bytes, trace::event& value)
{
    if (!view || !input || bytes != sizeof(value))
        return false;
    std::memcpy(&value, input, sizeof(value));
    return value.struct_size == sizeof(value) && value.version == trace::abi_version
        && value.producer_slot == 0 && view->getId() > 0
        && value.view_id == static_cast<uint64_t>(view->getId())
        && value.view_generation && value.document_generation && value.geometry_generation
        && static_cast<unsigned>(value.point) > 0
        && static_cast<unsigned>(value.point) <= static_cast<unsigned>(trace::stage::scene_activated)
        && static_cast<unsigned>(value.why) <= static_cast<unsigned>(trace::reason::cancelled)
        && static_cast<unsigned>(value.outcome) <= static_cast<unsigned>(trace::terminal::rejected)
        && !(value.flags & ~7U)
        && ((!value.input_first && !value.input_last)
            || (value.input_first && value.input_last >= value.input_first));
}
int WKE_INPUT_UI_CALL setVisibility(void* opaque, unsigned visible)
{
    auto* view = liveOwnerView(opaque);
    if (!view || !view->webPage()) return WKE_INPUT_UI_UNAVAILABLE;
    if (visible > 1) return WKE_INPUT_UI_INVALID;
    return view->webPage()->setHostVisible(visible != 0) ? WKE_INPUT_UI_OK : WKE_INPUT_UI_REJECTED;
}
int WKE_INPUT_UI_CALL setMirror(void* opaque, unsigned required)
{
    auto* view = liveOwnerView(opaque);
    if (!view || !view->webPage()) return WKE_INPUT_UI_UNAVAILABLE;
    if (required > 1) return WKE_INPUT_UI_INVALID;
    view->webPage()->setUiMirrorConsumerRequired(required != 0);
    return WKE_INPUT_UI_OK;
}
int WKE_INPUT_UI_CALL queryMirror(void* opaque, unsigned* required)
{
    auto* view = liveOwnerView(opaque);
    if (!view || !view->webPage()) return WKE_INPUT_UI_UNAVAILABLE;
    if (!required) return WKE_INPUT_UI_INVALID;
    *required = view->webPage()->uiMirrorConsumerRequired() ? 1U : 0U;
    return WKE_INPUT_UI_OK;
}
int WKE_INPUT_UI_CALL setInputContext(void* opaque, const void* input, unsigned bytes)
{
    auto* view = liveOwnerView(opaque);
    if (!view || !view->webPage()) return WKE_INPUT_UI_UNAVAILABLE;
    trace::event value;
    if (!readViewEvent(view, input, bytes, value)) return WKE_INPUT_UI_INVALID;
    view->webPage()->setInputTraceContext(value);
    return WKE_INPUT_UI_OK;
}
int WKE_INPUT_UI_CALL setViewContext(void* opaque, const void* input, unsigned bytes)
{
    auto* view = liveOwnerView(opaque);
    if (!view || !view->webPage()) return WKE_INPUT_UI_UNAVAILABLE;
    trace::event value;
    if (!readViewEvent(view, input, bytes, value) || value.input_first || value.input_last)
        return WKE_INPUT_UI_INVALID;
    return view->webPage()->setViewTraceIdentity(value) ? WKE_INPUT_UI_OK : WKE_INPUT_UI_REJECTED;
}

int WKE_INPUT_UI_CALL configureTrace(const wkeInputUiTraceConfig* config)
{
    if (!config || config->structSize != sizeof(*config)
        || config->abiVersion != WKE_INPUT_UI_ABI_VERSION || config->mode > 1
        || !(config->runIdHigh || config->runIdLow))
        return WKE_INPUT_UI_INVALID;
    std::lock_guard<std::mutex> bootstrap(configurationMutex);
    trace::identity identity;
    identity.process_id = config->processId;
    identity.run_id_high = config->runIdHigh;
    identity.run_id_low = config->runIdLow;
    identity.process_incarnation = config->processIncarnation;
    identity.qpc_frequency = config->qpcFrequency;
    const auto current = trace::process_recorder().snapshot();
    if (!current.available)
        return WKE_INPUT_UI_UNAVAILABLE;
    if (current.stopped)
        return WKE_INPUT_UI_REJECTED;
    // Repeated bootstrap of the same single session must not reset its quota.
    if (current.session.run_id_high == identity.run_id_high
        && current.session.run_id_low == identity.run_id_low
        && current.session.process_id == identity.process_id
        && current.session.process_incarnation == identity.process_incarnation
        && current.session.qpc_frequency == identity.qpc_frequency
        && static_cast<unsigned>(current.recording_mode) == config->mode)
        return WKE_INPUT_UI_OK;
    return trace::process_recorder().configure(identity, static_cast<trace::mode>(config->mode))
        ? WKE_INPUT_UI_OK : WKE_INPUT_UI_REJECTED;
}

void WKE_INPUT_UI_CALL stopTrace()
{
    trace::process_recorder().stop();
}

int WKE_INPUT_UI_CALL snapshotTrace(wkeInputUiTraceSnapshot* output)
{
    if (!output || output->structSize != sizeof(*output)
        || output->abiVersion != WKE_INPUT_UI_ABI_VERSION)
        return WKE_INPUT_UI_INVALID;
    const auto source = trace::process_recorder().snapshot();
    wkeInputUiTraceSnapshot value = {};
    value.structSize = sizeof(value);
    value.abiVersion = WKE_INPUT_UI_ABI_VERSION;
    value.mode = static_cast<unsigned>(source.recording_mode);
    value.flags = (source.available ? WKE_INPUT_UI_TRACE_AVAILABLE : 0U)
        | (source.stopped ? WKE_INPUT_UI_TRACE_STOPPED : 0U)
        | (source.quiescent ? WKE_INPUT_UI_TRACE_QUIESCENT : 0U)
        | (source.counter_saturated ? WKE_INPUT_UI_TRACE_SATURATED : 0U)
        | (source.owner_violation ? WKE_INPUT_UI_TRACE_OWNER_VIOLATION : 0U)
        | (source.invalid_event ? WKE_INPUT_UI_TRACE_INVALID_EVENT : 0U)
        | (source.clock_invalid ? WKE_INPUT_UI_TRACE_CLOCK_INVALID : 0U)
        | (exportLimitReached.load(std::memory_order_acquire)
            || (drainedEvents.load(std::memory_order_acquire) == WKE_INPUT_UI_MAX_EXPORT_EVENTS
                && source.queued) ? WKE_INPUT_UI_TRACE_LIMIT_REACHED : 0U);
    value.registeredThreads = source.registered_threads;
    value.activeProducers = source.active_producers;
    value.pendingRegistrations = source.pending_registrations;
    value.processId = source.session.process_id;
    value.runIdHigh = source.session.run_id_high;
    value.runIdLow = source.session.run_id_low;
    value.processIncarnation = source.session.process_incarnation;
    value.qpcFrequency = source.session.qpc_frequency;
    value.attempted = source.attempted;
    value.recorded = source.recorded;
    value.dropped = source.dropped;
    value.queued = source.queued;
    value.unregisteredWrites = source.unregistered_writes;
    value.registrationRejections = source.registration_rejections;
    value.causalLost = source.causal_lost;
    value.stageCount = static_cast<unsigned>(trace::stage_count);
    value.counterCount = static_cast<unsigned>(trace::counter_count);
    value.durationMetricCount = static_cast<unsigned>(trace::duration_metric_count);
    value.durationBucketCount = static_cast<unsigned>(trace::duration_buckets);
    for (size_t n = 0; n < trace::stage_count; ++n)
        value.stages[n] = source.stages[n];
    for (size_t n = 0; n < trace::counter_count; ++n)
        value.counters[n] = source.counters[n];
    for (size_t n = 0; n < trace::duration_metric_count; ++n)
        for (size_t bucket = 0; bucket < trace::duration_buckets; ++bucket)
            value.durations[n][bucket] = source.durations[n][bucket];
    *output = value;
    return source.available ? WKE_INPUT_UI_OK : WKE_INPUT_UI_UNAVAILABLE;
}

int WKE_INPUT_UI_CALL drainTrace(void* output, unsigned eventBytes,
    unsigned capacity, unsigned* count)
{
    if (count)
        *count = 0;
    if (!output || !count || eventBytes != sizeof(trace::event)
        || !capacity || capacity > WKE_INPUT_UI_MAX_DRAIN_EVENTS)
        return WKE_INPUT_UI_INVALID;
    std::unique_lock<std::mutex> consumer(drainMutex, std::try_to_lock);
    if (!consumer.owns_lock())
        return WKE_INPUT_UI_BUSY;
    if (!trace::process_recorder().available())
        return WKE_INPUT_UI_UNAVAILABLE;
    const auto already = drainedEvents.load(std::memory_order_relaxed);
    if (already == WKE_INPUT_UI_MAX_EXPORT_EVENTS) {
        const auto snapshot = trace::process_recorder().snapshot();
        if (snapshot.queued) {
            if (!exportLimitReached.exchange(true, std::memory_order_acq_rel))
                trace::process_recorder().note_causal_loss();
            return WKE_INPUT_UI_EXPORT_LIMIT;
        }
        return WKE_INPUT_UI_OK;
    }
    capacity = (std::min)(capacity,
        static_cast<unsigned>(WKE_INPUT_UI_MAX_EXPORT_EVENTS - already));
    std::array<trace::event, WKE_INPUT_UI_MAX_DRAIN_EVENTS> events;
    const auto drained = trace::process_recorder().drain(events.data(), capacity);
    if (drained.busy)
        return WKE_INPUT_UI_BUSY;
    if (drained.count)
        std::memcpy(output, events.data(), drained.count * sizeof(trace::event));
    *count = static_cast<unsigned>(drained.count);
    drainedEvents.store(already + drained.count, std::memory_order_release);
    return WKE_INPUT_UI_OK;
}
} // namespace

const wkeInputUiApi* WKE_CALL_TYPE wkeGetInputUiApi(unsigned requestedVersion)
{
    // Fields become available only with their actual owner/view implementation.
    static const wkeInputUiApi api = { sizeof(wkeInputUiApi), WKE_INPUT_UI_ABI_VERSION,
        WKE_INPUT_UI_CAP_TRACE | WKE_INPUT_UI_CAP_OWNER_OPPORTUNITY
            | WKE_INPUT_UI_CAP_FRACTIONAL_FRAME_POLICY | WKE_INPUT_UI_CAP_MIRROR_CONSUMER
            | WKE_INPUT_UI_CAP_INPUT_CONTEXT | WKE_INPUT_UI_CAP_CANCEL_INPUT
            | WKE_INPUT_UI_CAP_VIEW_CONTEXT | WKE_INPUT_UI_CAP_SCENE_INPUT_IDENTITY
            | WKE_INPUT_UI_CAP_VISIBILITY,
        &configureTrace, &stopTrace, &snapshotTrace, &drainTrace,
        &serviceOwner, &setMirror, &queryMirror, &setInputContext, &cancelInput, &setViewContext,
        &setVisibility, nullptr, nullptr };
    return requestedVersion == WKE_INPUT_UI_ABI_VERSION ? &api : nullptr;
}
