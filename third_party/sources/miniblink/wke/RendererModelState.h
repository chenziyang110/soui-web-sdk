#ifndef SOUI_WKE_RENDERER_MODEL_STATE_H
#define SOUI_WKE_RENDERER_MODEL_STATE_H

#include "wke_renderer_model_diagnostics.h"
#include <limits>

namespace wke { namespace diagnostics {
class renderer_backend_model_state final {
public:
    constexpr renderer_backend_model_state() noexcept : values_{} {}
    void platform_initialized(bool matched, uint64_t qpc, bool clock_valid) noexcept
    {
        if (!matched || (values_.flags & WKE_RENDER_MODEL_PROVIDER_OBSERVED)) {
            increment(values_.platformMismatchTotal);
            return;
        }
        values_.flags |= WKE_RENDER_MODEL_PROVIDER_OBSERVED | WKE_RENDER_MODEL_IDENTITY_MATCHED;
        values_.providerId = WKE_RENDER_MODEL_PROVIDER_COMPATIBILITY;
        values_.factoryCoverageMask = WKE_RENDER_MODEL_KNOWN_COVERAGE;
        if (clock_valid && qpc) values_.backendInitializationQpc = qpc;
        else values_.flags |= WKE_RENDER_MODEL_CLOCK_FAILURE;
    }
    void backend_created() noexcept
    {
        // The existing LayerTreeHost completed-construction hook calls this.
        values_.backendId = WKE_RENDER_MODEL_BACKEND_COMPATIBILITY_COMPOSITOR;
        values_.flags |= WKE_RENDER_MODEL_BACKEND_OBSERVED;
    }
    void compilation_observed(bool gpu) noexcept
    {
        // Called only from the successful initialization path, never a read.
        if (compilation_observed_ && values_.compiledGpuSupport != (gpu ? 1U : 0U))
            values_.flags |= WKE_RENDER_MODEL_COUNTER_FAULT;
        values_.compiledGpuSupport = gpu ? 1U : 0U;
        compilation_observed_ = true;
    }
    void factory_entered(uint32_t kind) noexcept
    {
        switch (kind) {
        case WKE_RENDER_MODEL_FACTORY_MEDIA: increment(values_.mediaFactoryAttempts); break;
        case WKE_RENDER_MODEL_FACTORY_PLUGIN: increment(values_.pluginFactoryAttempts); break;
        case WKE_RENDER_MODEL_FACTORY_OFFSCREEN: increment(values_.offscreenFactoryAttempts); break;
        case WKE_RENDER_MODEL_FACTORY_SHARED_CONTEXT: increment(values_.sharedContextFactoryAttempts); break;
        case WKE_RENDER_MODEL_FACTORY_EXTERNAL_VIEW:
        case WKE_RENDER_MODEL_FACTORY_RTC_STREAM: increment(values_.externalFactoryAttempts); break;
        default: increment(values_.unknownRouteTotal); break;
        }
    }
    wkeRendererModelSnapshot snapshot(const wkeRendererProcessSnapshot& compositor) const noexcept
    {
        auto output = values_;
        output.structSize = static_cast<uint32_t>(sizeof(output));
        output.abiVersion = WKE_RENDER_MODEL_ABI_VERSION;
        output.compositor = compositor;
        output.status = compositor.status;
        if (!compilation_observed_) output.compiledGpuSupport = WKE_RENDER_MODEL_GPU_UNKNOWN;
        const uint32_t required = WKE_RENDER_MODEL_PROVIDER_OBSERVED | WKE_RENDER_MODEL_IDENTITY_MATCHED |
            WKE_RENDER_MODEL_BACKEND_OBSERVED;
        if ((output.flags & (WKE_RENDER_MODEL_COUNTER_FAULT | WKE_RENDER_MODEL_CLOCK_FAILURE)) ||
            output.platformMismatchTotal || output.unknownRouteTotal)
            output.status = WKE_RENDER_DIAGNOSTICS_UNAVAILABLE;
        else if ((output.flags & required) != required || !compilation_observed_)
            output.status = WKE_RENDER_DIAGNOSTICS_NOT_STARTED;
        return output;
    }
private:
    void increment(uint64_t& counter) noexcept
    {
        if (counter == (std::numeric_limits<uint64_t>::max)()) values_.flags |= WKE_RENDER_MODEL_COUNTER_FAULT;
        else ++counter;
    }
    wkeRendererModelSnapshot values_;
    bool compilation_observed_{false};
};
void renderer_platform_initialized(bool matched) noexcept;
void renderer_factory_entered(uint32_t kind) noexcept;
// Content/compositor TUs also have a non-WKE configuration. The wrappers do
// not introduce external diagnostic symbols in that configuration.
inline void observe_renderer_platform(bool matched) noexcept
{
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    renderer_platform_initialized(matched);
#else
    (void)matched;
#endif
}
inline void observe_renderer_factory(uint32_t kind) noexcept
{
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    renderer_factory_entered(kind);
#else
    (void)kind;
#endif
}
} } // namespace wke::diagnostics

#endif // SOUI_WKE_RENDERER_MODEL_STATE_H
