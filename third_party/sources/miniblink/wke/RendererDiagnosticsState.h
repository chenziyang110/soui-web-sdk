#ifndef SOUI_WKE_RENDERER_DIAGNOSTICS_STATE_H
#define SOUI_WKE_RENDERER_DIAGNOSTICS_STATE_H

#include "wke_renderer_diagnostics.h"
#include <limits>

namespace wke {
namespace diagnostics {

namespace detail {
inline void saturating_increment(uint64_t& value, uint32_t& flags) noexcept
{
    if (value == (std::numeric_limits<uint64_t>::max)())
        flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
    else
        ++value;
}
} // namespace detail

inline wkeRendererProcessSnapshot busy_snapshot() noexcept
{
    wkeRendererProcessSnapshot output{};
    output.structSize = static_cast<uint32_t>(sizeof(output));
    output.abiVersion = WKE_RENDER_DIAGNOSTICS_ABI_VERSION;
    output.status = WKE_RENDER_DIAGNOSTICS_BUSY;
    return output;
}

// Pure state transitions. The Native implementation serializes all accesses
// with one DLL-lifetime SRW lock; this class does not own platform resources.
class renderer_process_state final {
public:
    constexpr renderer_process_state() noexcept : values_{} {}

    void initialized(uint64_t pid, uint64_t creation, uint64_t owner,
        uint64_t frequency, uint64_t initialized_qpc,
        bool identity_valid, bool clock_valid) noexcept
    {
        if (values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED) {
            if ((values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED)
                || values_.processId != pid || values_.processCreationFileTime != creation
                || values_.engineOwnerThreadId != owner)
                values_.flags |= WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH;
            return;
        }
        values_.flags |= WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED;
        values_.recordingEpoch = clock_valid ? initialized_qpc : 0;
        values_.initializedQpc = clock_valid ? initialized_qpc : 0;
        values_.processId = pid;
        values_.processCreationFileTime = creation;
        values_.engineOwnerThreadId = owner;
        values_.qpcFrequency = frequency;
        if (identity_valid && pid && creation && owner)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_IDENTITY_VALID;
        if (clock_valid && frequency && initialized_qpc)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_CLOCK_VALID;
        else
            values_.flags |= WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE;
        if (values_.drawEventsTotal && values_.lastDrawProcessId != pid)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH;
    }

    void renderer_created() noexcept
    {
        increment(values_.renderersCreatedTotal);
        increment(values_.renderersLive);
        if (values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
    }

    void renderer_closed(uint64_t qpc, bool clock_valid) noexcept
    {
        increment(values_.renderersClosedTotal);
        if (values_.renderersLive) {
            --values_.renderersLive;
            record_clock(clock_valid);
            if (clock_valid) {
                values_.lastRendererCloseQpc = qpc;
                if (!values_.renderersLive)
                    values_.closedSnapshotQpc = qpc;
            }
        } else {
            values_.flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
        }
    }

    void draw_completed(uint32_t kind, uint64_t pid, uint64_t thread,
        uint64_t qpc, bool clock_valid) noexcept
    {
        // Writers never use try-lock and never drop an event on contention.
        increment(values_.drawEventsTotal);
        if (kind == WKE_RENDER_DIAGNOSTICS_DRAW_GPU_SKIA) {
            increment(values_.gpuSkiaDrawsTotal);
            values_.flags |= WKE_RENDER_DIAGNOSTICS_OBSERVED_GPU_SKIA;
        } else if (kind == WKE_RENDER_DIAGNOSTICS_DRAW_BITMAP_RASTER) {
            increment(values_.rasterDrawsTotal);
            values_.flags |= WKE_RENDER_DIAGNOSTICS_OBSERVED_RASTER;
        } else {
            values_.flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
        }
        if (!values_.renderersLive || !pid || !thread
            || (values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED))
            values_.flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
        if ((values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED)
            && values_.processId != pid)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH;
        record_clock(clock_valid);
        values_.lastDrawKind = kind;
        values_.lastDrawProcessId = pid;
        values_.lastDrawThreadId = thread;
        if (clock_valid)
            values_.lastDrawQpc = qpc;
    }

    void finalized(uint64_t qpc, bool clock_valid) noexcept
    {
        // Preserve the first real finalization timestamp and every draw/close
        // timestamp. Reading an already closed cache can never retimestamp it.
        if (values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED)
            return;
        values_.flags |= WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED;
        record_clock(clock_valid);
        if (clock_valid)
            values_.engineFinalizedQpc = qpc;
        if (values_.renderersLive)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
    }

    wkeRendererProcessSnapshot snapshot() const noexcept
    {
        auto output = values_;
        output.structSize = static_cast<uint32_t>(sizeof(output));
        output.abiVersion = WKE_RENDER_DIAGNOSTICS_ABI_VERSION;
        output.status = WKE_RENDER_DIAGNOSTICS_NOT_STARTED;
        const uint32_t faults = WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT
            | WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH
            | WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE;
        if (output.flags & faults)
            output.status = WKE_RENDER_DIAGNOSTICS_UNAVAILABLE;
        else if (output.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED) {
            const uint32_t required = WKE_RENDER_DIAGNOSTICS_IDENTITY_VALID
                | WKE_RENDER_DIAGNOSTICS_CLOCK_VALID;
            if ((output.flags & required) != required)
                output.status = WKE_RENDER_DIAGNOSTICS_UNAVAILABLE;
            else if (output.drawEventsTotal)
                output.status = output.renderersLive
                    ? WKE_RENDER_DIAGNOSTICS_READY : WKE_RENDER_DIAGNOSTICS_CLOSED;
        }
        return output;
    }

private:
    void increment(uint64_t& value) noexcept
    {
        detail::saturating_increment(value, values_.flags);
    }
    void record_clock(bool valid) noexcept
    {
        if (!valid)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE;
    }

    wkeRendererProcessSnapshot values_;
};

// Implemented in the existing wke.cpp TU. These declarations do not add a new
// Native build input or provide any reset/admission API to an SDK caller.
void renderer_created() noexcept;
void renderer_closed() noexcept;
void renderer_draw_completed(uint32_t kind) noexcept;

} // namespace diagnostics
} // namespace wke

#endif // SOUI_WKE_RENDERER_DIAGNOSTICS_STATE_H
