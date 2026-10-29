#ifndef CC_TREES_RENDER_DIAGNOSTIC_TRACE_H_
#define CC_TREES_RENDER_DIAGNOSTIC_TRACE_H_
#include "content/common/InputFrameTrace.h"
namespace cc {
inline void recordInteractionDiagnostic(content::input_frame_trace::interaction_diagnostic_code code)
{
    content::input_frame_trace::event value;
    value.point = content::input_frame_trace::stage::interaction_diagnostic;
    value.value = static_cast<uint64_t>(code);
    content::input_frame_trace::record(value);
}
inline void recordGpuPresentDiagnostic(content::input_frame_trace::gpu_present_diagnostic_code code)
{
    content::input_frame_trace::event value;
    value.point = content::input_frame_trace::stage::gpu_present_diagnostic;
    value.value = static_cast<uint64_t>(code);
    content::input_frame_trace::record(value);
}
} // namespace cc
#endif
