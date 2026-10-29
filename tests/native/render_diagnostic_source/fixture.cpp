// Exact present/helper SOURCE with explicit GPU/recorder dependencies.
#include "cc/trees/RenderDiagnosticTrace.h"
#include <array>
#include <cstdint>
#include <cstdio>
namespace fixture {
int failures = 0, swaps = 0, errors = 0, contexts = 0, registrations = 0;
bool swapped = false, functions = true, blit = true;
std::array<content::input_frame_trace::event, 32> events{};
size_t count = 0;
void require(bool value) { if (!value) { ++failures; std::fputs("render diagnostic source failure\n", stderr); } }
}
namespace content { namespace input_frame_trace {
record_result record(event value) noexcept
{
    fixture::require(!value.qpc && !value.thread_id && !value.view_id && !value.view_generation
        && !value.document_generation && !value.geometry_generation && !value.input_first && !value.input_last
        && !value.task_id && !value.frame_id && !value.action_first && !value.action_last && !value.related_id
        && !value.flags && value.why == reason::none && value.outcome == terminal::none);
    fixture::require((value.point == stage::interaction_diagnostic && value.value && value.value <= 20)
        || (value.point == stage::gpu_present_diagnostic && value.value && value.value <= 4));
    if (fixture::count < fixture::events.size()) fixture::events[fixture::count++] = value;
    return record_result::scalar_only;
}
bool current_context(event&) noexcept { ++fixture::contexts; return false; }
bool register_current_thread() { ++fixture::registrations; return false; }
} }
using GLenum = unsigned; using BOOL = int; using GLuint = unsigned;
constexpr BOOL FALSE = 0, TRUE = 1;
constexpr GLenum GL_NO_ERROR = 0, GL_DEPTH_TEST = 1, GL_BLEND = 2, GL_COLOR_BUFFER_BIT = 3;
constexpr GLenum GL_TEXTURE_2D = 4, GL_TEXTURE_ENV = 5, GL_TEXTURE_ENV_MODE = 6, GL_REPLACE = 7;
constexpr GLenum GL_PROJECTION = 8, GL_MODELVIEW = 9, GL_QUADS = 10, kFramebuffer = 11;
GLenum glGetError() { ++fixture::errors; return 0; }
BOOL SwapBuffers(void*) { ++fixture::swaps; return fixture::swapped ? TRUE : FALSE; }
template<class... T> void glViewport(T...) { }
template<class... T> void glDisable(T...) { }
template<class... T> void glClearColor(T...) { }
template<class... T> void glClear(T...) { }
template<class... T> void glEnable(T...) { }
template<class... T> void glBindTexture(T...) { }
template<class... T> void glTexEnvi(T...) { }
template<class... T> void glColor4f(T...) { }
template<class... T> void glMatrixMode(T...) { }
void glPushMatrix() { } void glLoadIdentity() { } void glPopMatrix() { }
template<class... T> void glOrtho(T...) { }
template<class... T> void glBegin(T...) { }
template<class... T> void glTexCoord2f(T...) { }
template<class... T> void glVertex2f(T...) { }
void glEnd() { }
namespace cc {
using BlitFramebufferProc = void(*)(int, int, int, int, int, int, int, int, unsigned, unsigned);
void fakeBlit(int, int, int, int, int, int, int, int, unsigned, unsigned) { }
void fakeBind(unsigned, unsigned) { }
struct FramebufferFunctions { void(*bind)(unsigned, unsigned) = nullptr; BlitFramebufferProc blit = nullptr; };
bool loadFramebufferFunctions(FramebufferFunctions& value)
{ value.bind = fakeBind; value.blit = fixture::blit ? fakeBlit : nullptr; return fixture::functions; }
bool gpuErrorDiagnosticsEnabled() { return false; }
class GpuSkiaSurface {
public:
    void* m_canvas = this; void* m_context = this; void* m_glContext = this; void* m_dc = this;
    bool m_directPresent = true, m_frameDirty = true, m_externalGlStateDirty = false;
    unsigned m_presentCount = 0, m_framebuffer = 1, m_texture = 1;
    int m_width = 100, m_height = 100;
    bool present(); void recordPresented() { ++m_presentCount; }
};
#include "gpu_present.inc"
}
int main()
{
    using fixture::require;
    using Code = content::input_frame_trace::gpu_present_diagnostic_code;
    cc::recordInteractionDiagnostic(content::input_frame_trace::interaction_diagnostic_code::accepted);
    require(fixture::events[0].point == content::input_frame_trace::stage::interaction_diagnostic);
    {
        cc::GpuSkiaSurface surface; surface.m_canvas = nullptr;
        require(!surface.present() && fixture::swaps == 0);
        require(fixture::events[fixture::count - 1].value == static_cast<uint64_t>(Code::surface_unavailable));
    }
    {
        cc::GpuSkiaSurface surface; fixture::functions = false;
        require(!surface.present() && fixture::swaps == 0 && surface.m_frameDirty);
        require(fixture::events[fixture::count - 1].value == static_cast<uint64_t>(Code::context_unavailable));
        fixture::functions = true;
        surface.m_directPresent = false; const size_t before = fixture::count;
        require(!surface.present() && fixture::swaps == 0 && fixture::count == before);
        surface.m_directPresent = true; surface.m_frameDirty = false;
        require(surface.present() && fixture::swaps == 0 && fixture::count == before);
    }
    for (bool blit : {true, false}) {
        cc::GpuSkiaSurface surface; fixture::blit = blit; fixture::swapped = false;
        const int before = fixture::swaps;
        for (int attempt = 0; attempt != 3; ++attempt) {
            require(!surface.present() && surface.m_frameDirty && surface.m_presentCount == 0);
            require(fixture::events[fixture::count - 1].value == static_cast<uint64_t>(Code::swap_failed));
        }
        fixture::swapped = true;
        require(surface.present() && !surface.m_frameDirty && surface.m_presentCount == 1);
        require(surface.m_externalGlStateDirty && fixture::swaps == before + 4);
        require(fixture::events[fixture::count - 1].value == static_cast<uint64_t>(Code::swap_succeeded));
        const size_t submitted = fixture::count;
        require(surface.present() && fixture::swaps == before + 4 && fixture::count == submitted);
    }
    require(fixture::contexts == 0 && fixture::registrations == 0);
    return fixture::failures ? 1 : 0;
}
