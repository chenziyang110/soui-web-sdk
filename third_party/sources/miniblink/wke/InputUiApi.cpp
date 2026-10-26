#include "wke/wke.h"
#include "content/common/InputFrameTrace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace {
namespace trace = content::input_frame_trace;
static_assert(sizeof(trace::event) == WKE_INPUT_UI_EVENT_BYTES, "Trace event ABI mismatch");
static_assert(trace::stage_count <= WKE_INPUT_UI_STAGE_CAPACITY, "Trace stage ABI capacity");
static_assert(trace::counter_count <= WKE_INPUT_UI_COUNTER_CAPACITY, "Trace counter ABI capacity");
static_assert(trace::duration_metric_count <= WKE_INPUT_UI_DURATION_CAPACITY, "Trace metric ABI capacity");
static_assert(trace::duration_buckets == WKE_INPUT_UI_DURATION_BUCKETS, "Trace histogram ABI mismatch");

std::mutex drainMutex;
std::atomic<unsigned long long> drainedEvents{0};
std::atomic<bool> exportLimitReached{false};

int WKE_INPUT_UI_CALL configureTrace(const wkeInputUiTraceConfig* config)
{
    if (!config || config->structSize != sizeof(*config)
        || config->abiVersion != WKE_INPUT_UI_ABI_VERSION || config->mode > 1
        || !(config->runIdHigh || config->runIdLow))
        return WKE_INPUT_UI_INVALID;
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
        WKE_INPUT_UI_CAP_TRACE, &configureTrace, &stopTrace, &snapshotTrace, &drainTrace,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    return requestedVersion == WKE_INPUT_UI_ABI_VERSION ? &api : nullptr;
}
