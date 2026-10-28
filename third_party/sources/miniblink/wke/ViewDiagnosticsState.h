#ifndef SOUI_WKE_VIEW_DIAGNOSTICS_STATE_H
#define SOUI_WKE_VIEW_DIAGNOSTICS_STATE_H

#include "wke_view_diagnostics.h"
#include <limits>

namespace wke {
namespace diagnostics {

// All accesses serialized by wke.cpp's existing DLL cache lock. No allocation.
class view_lifetime_state final {
public:
    constexpr view_lifetime_state() noexcept : values_{} {}

    void initialized(uint64_t pid, uint64_t birth, uint64_t owner, uint64_t hz,
        uint64_t qpc, bool identity_valid, bool clock_valid) noexcept
    {
        if (values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED) {
            values_.flags |= WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH;
            return;
        }
        values_.processId = pid;
        values_.processCreationFileTime = birth;
        values_.engineOwnerThreadId = owner;
        values_.qpcFrequency = hz;
        values_.recordingEpoch = qpc;
        values_.lastMutationQpc = qpc;
        values_.mutationSequence = 1;
        values_.flags |= WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED;
        if (identity_valid && pid && birth && owner)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_IDENTITY_VALID;
        else values_.flags |= WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH;
        if (clock_valid && hz && qpc)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_CLOCK_VALID;
        else values_.flags |= WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE;
    }

    void created(uint64_t owner, uint64_t qpc, bool clock_valid) noexcept
    {
        mutate(owner, qpc, clock_valid);
        increment(values_.viewsCreatedTotal);
        increment(values_.viewsLive);
    }

    void destroyed(uint64_t owner, uint64_t qpc, bool clock_valid) noexcept
    {
        mutate(owner, qpc, clock_valid);
        increment(values_.viewsDestroyedTotal);
        if (values_.viewsLive) --values_.viewsLive;
        else values_.flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
    }

    void finalized(uint64_t owner, uint64_t qpc, bool clock_valid) noexcept
    {
        if (values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED) return;
        mutate(owner, qpc, clock_valid);
        values_.flags |= WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED;
        values_.engineFinalizedQpc = clock_valid ? qpc : 0;
        if (values_.viewsLive) values_.flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
    }

    wkeViewLifetimeSnapshot snapshot() const noexcept
    {
        auto result = values_;
        result.structSize = static_cast<uint32_t>(sizeof(result));
        result.abiVersion = WKE_VIEW_DIAGNOSTICS_ABI_VERSION;
        constexpr uint32_t required = WKE_RENDER_DIAGNOSTICS_IDENTITY_VALID |
            WKE_RENDER_DIAGNOSTICS_CLOCK_VALID | WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED;
        constexpr uint32_t faults = WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT |
            WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH | WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE;
        result.status = WKE_RENDER_DIAGNOSTICS_NOT_STARTED;
        if (result.flags & faults) result.status = WKE_RENDER_DIAGNOSTICS_UNAVAILABLE;
        else if ((result.flags & required) == required)
            result.status = (result.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED)
                ? WKE_RENDER_DIAGNOSTICS_CLOSED : WKE_RENDER_DIAGNOSTICS_READY;
        return result;
    }

private:
    void increment(uint64_t& value) noexcept
    {
        if (value == (std::numeric_limits<uint64_t>::max)())
            values_.flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
        else ++value;
    }
    void mutate(uint64_t owner, uint64_t qpc, bool clock_valid) noexcept
    {
        if (!(values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED) ||
            (values_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED))
            values_.flags |= WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT;
        if (!owner || owner != values_.engineOwnerThreadId)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH;
        if (!clock_valid || !qpc || qpc < values_.lastMutationQpc)
            values_.flags |= WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE;
        else values_.lastMutationQpc = qpc;
        increment(values_.mutationSequence);
    }
    wkeViewLifetimeSnapshot values_;
};

// Real CWebView base construction/destruction hooks; no caller reset exists.
void view_created() noexcept;
void view_destroyed() noexcept;

} // namespace diagnostics
} // namespace wke
#endif
