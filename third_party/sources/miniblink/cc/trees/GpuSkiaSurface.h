#ifndef CC_TREES_GPU_SKIA_SURFACE_H_
#define CC_TREES_GPU_SKIA_SURFACE_H_

#include "SkCanvas.h"
#include "SkGpuDevice.h"
#include "GrContext.h"
#include "gl/GrGLInterface.h"
#include <windows.h>
#include <GL/gl.h>
#include <vector>

namespace cc {

// Experimental only: a WGL/Ganesh surface that can render a layer tree on the
// compositor thread and read back into the existing SOUI/GDI presentation path.
// The readback is deliberate until a DirectComposition presenter is available.
class GpuSkiaSurface {
public:
    GpuSkiaSurface();
    ~GpuSkiaSurface();

    bool initialize(HWND window, int width, int height);
    bool resize(int width, int height);
    bool available() const { return m_canvas != nullptr; }
    SkCanvas* canvas() const { return m_canvas; }
    void beginFrame();
    // Flush accumulated Skia commands without submitting a window buffer.
    // LayerTreeHost can draw several dirty regions and present them once.
    void flush();
    bool present();
    bool directPresent() const { return m_directPresent; }
    bool hasPendingFrame() const { return m_frameDirty; }
    bool readback(SkCanvas* destination, const SkIRect& rect);
    void recordPresented();
    void reset();

private:
    bool createFramebuffer(int width, int height);
    void destroyFramebuffer();

    HWND m_window;
    HDC m_dc;
    HGLRC m_glContext;
    const GrGLInterface* m_glInterface;
    GrContext* m_context;
    SkCanvas* m_canvas;
    GLuint m_texture;
    GLuint m_framebuffer;
    int m_width;
    int m_height;
    unsigned m_presentCount;
    // Reused by diagnostic/compatibility readback. The old path allocated a
    // fresh vector for every dirty rectangle, which made the CPU/GPU bridge
    // pay allocator and heap-lock cost on every animated frame.
    std::vector<unsigned char> m_readbackPixels;
    bool m_directPresent;
    bool m_vsyncEnabled;
    bool m_frameDirty;
    // Raw WGL/GL present and diagnostic readback calls can invalidate Skia's
    // cached render-target state. Keep the invalidation explicit so the
    // compositor does not reset the driver state on every frame when the
    // previous frame stayed inside Skia.
    bool m_externalGlStateDirty;
};

} // namespace cc

#endif // CC_TREES_GPU_SKIA_SURFACE_H_
