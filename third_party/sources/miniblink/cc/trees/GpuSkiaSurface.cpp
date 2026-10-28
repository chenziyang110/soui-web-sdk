#include "config.h"
#include "cc/trees/GpuSkiaSurface.h"
#include "cc/trees/RenderDiagnosticTrace.h"

#include "skia/ext/platform_device.h"
#include "skia/ext/platform_canvas.h"
#include <cstring>
#include <fstream>
#include <vector>

namespace cc {
namespace {
// GPU-backed canvases used to advertise kUnknown_SkPixelGeometry. Skia then
// disables LCD text even when Blink requested ClearType, leaving every small
// label in the desktop shell softer than the same page in the GDI path. The
// presentation target is a normal horizontal RGB desktop surface; keep an
// escape hatch for BGR or non-LCD displays without changing the bridge API.
SkPixelGeometry gpuTextPixelGeometry()
{
    char option[8] = {};
    const DWORD length = GetEnvironmentVariableA("SOUI_GPU_LCD_TEXT", option, sizeof(option));
    if (length > 0 && length < sizeof(option)) {
        if (_stricmp(option, "0") == 0 || _stricmp(option, "off") == 0 ||
            _stricmp(option, "unknown") == 0)
            return kUnknown_SkPixelGeometry;
        if (_stricmp(option, "bgr") == 0)
            return kBGR_H_SkPixelGeometry;
    }
    return kRGB_H_SkPixelGeometry;
}

GLint gpuTextureFilter()
{
    char option[8] = {};
    const DWORD length = GetEnvironmentVariableA("SOUI_GPU_FILTER", option, sizeof(option));
    return length > 0 && length < sizeof(option) && _stricmp(option, "linear") == 0
        ? GL_LINEAR
        : GL_NEAREST;
}

typedef void (WINAPI* GenFramebuffersProc)(GLsizei, GLuint*);
typedef void (WINAPI* BindFramebufferProc)(GLenum, GLuint);
typedef void (WINAPI* FramebufferTexture2DProc)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (WINAPI* CheckFramebufferStatusProc)(GLenum);
typedef void (WINAPI* DeleteFramebuffersProc)(GLsizei, const GLuint*);
typedef void (WINAPI* BlitFramebufferProc)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef BOOL (WINAPI* SwapIntervalProc)(int);
const GLenum kFramebuffer = 0x8D40;
const GLenum kColorAttachment0 = 0x8CE0;
const GLenum kFramebufferComplete = 0x8CD5;
const GLenum kBgra = 0x80E1;

struct FramebufferFunctions {
    GenFramebuffersProc gen;
    BindFramebufferProc bind;
    FramebufferTexture2DProc attach;
    CheckFramebufferStatusProc status;
    DeleteFramebuffersProc destroy;
    BlitFramebufferProc blit;
};

FramebufferFunctions resolveFramebufferFunctions()
{
    FramebufferFunctions resolved{};
    resolved.gen = reinterpret_cast<GenFramebuffersProc>(wglGetProcAddress("glGenFramebuffers"));
    resolved.bind = reinterpret_cast<BindFramebufferProc>(wglGetProcAddress("glBindFramebuffer"));
    resolved.attach = reinterpret_cast<FramebufferTexture2DProc>(wglGetProcAddress("glFramebufferTexture2D"));
    resolved.status = reinterpret_cast<CheckFramebufferStatusProc>(wglGetProcAddress("glCheckFramebufferStatus"));
    resolved.destroy = reinterpret_cast<DeleteFramebuffersProc>(wglGetProcAddress("glDeleteFramebuffers"));
    resolved.blit = reinterpret_cast<BlitFramebufferProc>(wglGetProcAddress("glBlitFramebuffer"));
    return resolved;
}

bool loadFramebufferFunctions(FramebufferFunctions& functions)
{
    // WGL entry points are stable for the process once a context has been
    // made current. A function-local static is initialized exactly once by
    // the C++ runtime, so multi-window GPU startup cannot race the cache.
    // Keeping the lookup out of beginFrame/present/readback removes five
    // driver entry queries from every animation tick.
    static const FramebufferFunctions cached = resolveFramebufferFunctions();
    functions = cached;
    return cached.gen && cached.bind && cached.attach && cached.status && cached.destroy;
}

SwapIntervalProc resolveSwapIntervalFunction()
{
    return reinterpret_cast<SwapIntervalProc>(wglGetProcAddress("wglSwapIntervalEXT"));
}

SwapIntervalProc swapIntervalFunction()
{
    // The resolver runs after the surface has made its WGL context current.
    // Function-local static initialization is thread-safe and keeps this
    // optional extension lookup out of every present call.
    static const SwapIntervalProc function = resolveSwapIntervalFunction();
    return function;
}

bool gpuErrorDiagnosticsEnabled()
{
    static const bool enabled = [] {
        char option[2] = {};
        return GetEnvironmentVariableA("SOUI_GPU_DIAGNOSTIC_ERRORS", option,
                                      sizeof(option)) == 1 && option[0] == '1';
    }();
    return enabled;
}

bool softwareGlRendererAllowed()
{
    char option[2] = {};
    return GetEnvironmentVariableA("SOUI_GPU_ALLOW_SOFTWARE_GL", option,
                                  sizeof(option)) == 1 && option[0] == '1';
}

bool containsRendererMarker(const char* value, const char* marker)
{
    return value && marker && std::strstr(value, marker) != nullptr;
}

bool isKnownSoftwareRenderer(const char* vendor, const char* renderer)
{
    // WGL can successfully create a context backed by the Windows GDI
    // software rasterizer. Treat that as CPU fallback by default: the GL
    // bridge adds an extra copy/synchronization layer and is slower than the
    // existing Skia raster path on machines without a real adapter.
    return containsRendererMarker(renderer, "GDI Generic")
        || containsRendererMarker(renderer, "Microsoft Basic Render Driver")
        || containsRendererMarker(renderer, "Software Rasterizer")
        || containsRendererMarker(renderer, "SwiftShader")
        || containsRendererMarker(vendor, "Microsoft Basic Render Driver");
}

void writeGpuInitTrace(const char* result, const char* vendor, const char* renderer)
{
    char tracePath[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA("SOUI_GPU_INIT_TRACE", tracePath, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return;
    std::ofstream trace(tracePath, std::ios::trunc);
    trace << "result=" << (result ? result : "")
          << " vendor=" << (vendor ? vendor : "")
          << " renderer=" << (renderer ? renderer : "") << "\n";
}
}

GpuSkiaSurface::GpuSkiaSurface()
    : m_window(nullptr)
    , m_dc(nullptr)
    , m_glContext(nullptr)
    , m_glInterface(nullptr)
    , m_context(nullptr)
    , m_canvas(nullptr)
    , m_texture(0)
    , m_framebuffer(0)
    , m_width(0)
    , m_height(0)
    , m_presentCount(0)
    , m_directPresent(false)
    , m_vsyncEnabled(false)
    , m_frameDirty(false)
    , m_externalGlStateDirty(true)
{
}

GpuSkiaSurface::~GpuSkiaSurface()
{
    reset();
}

bool GpuSkiaSurface::initialize(HWND window, int width, int height)
{
    SetEnvironmentVariableA("SOUI_GPU_ACTIVE", "0");
    reset();
    if (!window || width <= 0 || height <= 0)
        return false;

    m_window = window;
    m_dc = GetDC(window);
    if (!m_dc)
        return false;

    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    const int pixelFormat = ChoosePixelFormat(m_dc, &format);
    if (!pixelFormat || !SetPixelFormat(m_dc, pixelFormat, &format)) {
        reset();
        return false;
    }
    m_glContext = wglCreateContext(m_dc);
    if (!m_glContext || !wglMakeCurrent(m_dc, m_glContext)) {
        reset();
        return false;
    }

    const char* glVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    writeGpuInitTrace("context-created", glVendor, glRenderer);
    if (isKnownSoftwareRenderer(glVendor, glRenderer) && !softwareGlRendererAllowed()) {
        writeGpuInitTrace("software-renderer-rejected", glVendor, glRenderer);
        reset();
        return false;
    }

    m_glInterface = GrGLCreateNativeInterface();
    if (!m_glInterface)
        return false;
    m_context = GrContext::Create(kOpenGL_GrBackend,
                                  reinterpret_cast<GrBackendContext>(m_glInterface));
    if (!m_context || !createFramebuffer(width, height)) {
        reset();
        return false;
    }
    m_width = width;
    m_height = height;
    m_externalGlStateDirty = true;
    m_frameDirty = false;
    char directPresent[2] = {};
    m_directPresent = GetEnvironmentVariableA("SOUI_GPU_DIRECT_PRESENT", directPresent,
                                              sizeof(directPresent)) == 1 && directPresent[0] == '1';
    if (m_directPresent) {
        const SwapIntervalProc swapInterval = swapIntervalFunction();
        m_vsyncEnabled = swapInterval && swapInterval(1) == TRUE;
    }
    // Environment variables are backed by separate CRT copies in the source
    // DLL and the SOUI host. Publish the live state on the shared child HWND
    // as well so the host can report the real compositor state.
    SetPropA(m_window, "SOUI_GPU_ACTIVE", reinterpret_cast<HANDLE>(1));
    SetEnvironmentVariableA("SOUI_GPU_ACTIVE", "1");
    return true;
}

bool GpuSkiaSurface::createFramebuffer(int width, int height)
{
    FramebufferFunctions functions{};
    if (!loadFramebufferFunctions(functions))
        return false;
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    // The presenter is normally a 1:1 copy from the Skia framebuffer to the
    // child window. Linear filtering softens text and thin SVG strokes on
    // older drivers, so keep the exact-pixel path as the default. Hosts that
    // intentionally scale the surface can opt into linear filtering.
    const GLint filter = gpuTextureFilter();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    functions.gen(1, &m_framebuffer);
    functions.bind(kFramebuffer, m_framebuffer);
    functions.attach(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, m_texture, 0);
    if (functions.status(kFramebuffer) != kFramebufferComplete)
        return false;

    GrBackendRenderTargetDesc target{};
    target.fWidth = width;
    target.fHeight = height;
    target.fConfig = kSkia8888_GrPixelConfig;
    target.fOrigin = kBottomLeft_GrSurfaceOrigin;
    target.fRenderTargetHandle = static_cast<GrBackendObject>(m_framebuffer);
    GrRenderTarget* wrapped = m_context->textureProvider()->wrapBackendRenderTarget(target);
    if (!wrapped)
        return false;
    const SkSurfaceProps props(0, gpuTextPixelGeometry());
    SkGpuDevice* device = SkGpuDevice::Create(wrapped, &props, SkGpuDevice::kUninit_InitContents);
    if (!device) {
        wrapped->unref();
        return false;
    }
    m_canvas = new SkCanvas(device);
    device->unref();
    wrapped->unref();

    // SkGpuDevice is intentionally created with uninitialized contents to
    // avoid a driver-side full clear on every surface allocation.  The
    // compositor normally paints only its dirty rectangle, though, so a
    // newly-created direct-present framebuffer must be initialized before
    // the first partial frame or stale driver memory can appear as visible
    // geometry (especially after resize/context recreation).
    m_canvas->clear(SK_ColorWHITE);
    m_context->flush();
    return true;
}

bool GpuSkiaSurface::resize(int width, int height)
{
    if (!m_window || width <= 0 || height <= 0)
        return false;
    if (width == m_width && height == m_height)
        return true;
    if (!m_glContext || !m_context)
        return initialize(m_window, width, height);
    if (!wglMakeCurrent(m_dc, m_glContext))
        return false;

    // Keep the window DC and WGL context alive during resize. Recreating the
    // context for every WM_SIZE flushes driver state and causes visible
    // stalls; only the framebuffer-backed Skia surface needs a new size.
    delete m_canvas;
    m_canvas = nullptr;
    destroyFramebuffer();
    m_context->resetContext();
    if (!createFramebuffer(width, height)) {
        reset();
        return false;
    }
    m_width = width;
    m_height = height;
    m_externalGlStateDirty = true;
    if (m_directPresent) {
        const SwapIntervalProc swapInterval = swapIntervalFunction();
        m_vsyncEnabled = swapInterval && swapInterval(1) == TRUE;
    }
    SetPropA(m_window, "SOUI_GPU_ACTIVE", reinterpret_cast<HANDLE>(1));
    SetEnvironmentVariableA("SOUI_GPU_ACTIVE", "1");
    return true;
}

void GpuSkiaSurface::beginFrame()
{
    if (!m_context || !m_glContext)
        return;
    // The compositor thread normally keeps this context current. Avoid a
    // redundant WGL context switch on every animation tick, while still
    // recovering correctly if another surface temporarily became current.
    if (wglGetCurrentContext() != m_glContext || wglGetCurrentDC() != m_dc) {
        if (!wglMakeCurrent(m_dc, m_glContext))
            return;
        m_externalGlStateDirty = true;
    }
    if (m_externalGlStateDirty) {
        // Only the framebuffer binding is modified by our raw GL
        // present/readback calls. Revalidating every backend state bit on
        // every frame adds avoidable driver work; reset at explicit state
        // invalidation boundaries instead.
        m_context->resetContext(kRenderTarget_GrGLBackendState);
        FramebufferFunctions functions{};
        if (loadFramebufferFunctions(functions))
            functions.bind(kFramebuffer, m_framebuffer);
        m_externalGlStateDirty = false;
    }
    glViewport(0, 0, m_width, m_height);
}

void GpuSkiaSurface::flush()
{
    if (!m_canvas || !m_context || !m_glContext)
        return;
    m_context->flush();
    m_frameDirty = true;
}

bool GpuSkiaSurface::present()
{
    if (!m_canvas || !m_context || !m_glContext) {
        recordGpuPresentDiagnostic(content::input_frame_trace::gpu_present_diagnostic_code::surface_unavailable);
        return false;
    }
    if (!m_directPresent)
        return false;
    if (!m_frameDirty)
        return true;
    const bool collectErrors = m_presentCount == 0 || gpuErrorDiagnosticsEnabled();
    // present() deliberately leaves raw GL state bound to the default
    // framebuffer. The caller has already called flush() for the complete
    // dirty-region batch; the next beginFrame() repairs Skia's state once.
    m_externalGlStateDirty = true;
    const GLenum flushError = collectErrors ? glGetError() : GL_NO_ERROR;
    (void)flushError;
    FramebufferFunctions functions{};
    if (!loadFramebufferFunctions(functions)) {
        recordGpuPresentDiagnostic(content::input_frame_trace::gpu_present_diagnostic_code::context_unavailable);
        return false;
    }
    const BlitFramebufferProc blit = functions.blit;
    if (blit) {
        functions.bind(0x8CA8 /* GL_READ_FRAMEBUFFER */, m_framebuffer);
        functions.bind(0x8CA9 /* GL_DRAW_FRAMEBUFFER */, 0);
        blit(0, 0, m_width, m_height, 0, 0, m_width, m_height,
             0x00004000 /* GL_COLOR_BUFFER_BIT */, 0x2600 /* GL_NEAREST */);
        const GLenum blitError = collectErrors ? glGetError() : GL_NO_ERROR;
        (void)blitError; // Preserve the existing diagnostic error read.
        const BOOL swapped = SwapBuffers(m_dc);
        if (swapped)
            recordPresented();
        recordGpuPresentDiagnostic(swapped ? content::input_frame_trace::gpu_present_diagnostic_code::swap_succeeded
            : content::input_frame_trace::gpu_present_diagnostic_code::swap_failed);
        m_frameDirty = swapped == FALSE;
        return swapped == TRUE;
    }
    functions.bind(kFramebuffer, 0);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_TEXTURE_2D);
    const GLenum drawError = collectErrors ? glGetError() : GL_NO_ERROR;
    const BOOL swapped = SwapBuffers(m_dc);
    if (swapped)
        recordPresented();
    const GLenum swapError = collectErrors ? glGetError() : GL_NO_ERROR;
    (void)drawError;
    (void)swapError;
    recordGpuPresentDiagnostic(swapped ? content::input_frame_trace::gpu_present_diagnostic_code::swap_succeeded
        : content::input_frame_trace::gpu_present_diagnostic_code::swap_failed);
    m_frameDirty = swapped == FALSE;
    return swapped == TRUE;
}

void GpuSkiaSurface::recordPresented()
{
    ++m_presentCount;
    for (HWND window = m_window; window; window = GetParent(window))
        SetPropA(window, "SOUI_GPU_PRESENT_COUNT",
                 reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(m_presentCount)));
    // Some host integrations expose a wrapper HWND instead of the child
    // surface HWND. Publish a low-frequency Win32 process fallback as well;
    // this is deliberately amortized so diagnostics do not add an
    // environment mutation to every present.
    if (m_presentCount == 1 || (m_presentCount & 15U) == 0) {
        char value[32] = {};
        sprintf_s(value, "%u", m_presentCount);
        SetEnvironmentVariableA("SOUI_GPU_PRESENT_COUNT", value);
    }
}

bool GpuSkiaSurface::readback(SkCanvas* destination, const SkIRect& rect)
{
    if (!m_canvas || !destination || rect.isEmpty())
        return false;
    SkBaseDevice* device = skia::GetTopDevice(*destination);
    if (!device)
        return false;
    const SkBitmap& bitmap = device->accessBitmap(true);
    if (!bitmap.getPixels())
        return false;

    const int width = rect.width();
    const int height = rect.height();
    FramebufferFunctions functions{};
    if (!loadFramebufferFunctions(functions))
        return false;
    functions.bind(kFramebuffer, m_framebuffer);
    const size_t pixelBytes = static_cast<size_t>(width) * height * 4;
    m_readbackPixels.resize(pixelBytes);
    glReadPixels(rect.x(), m_height - rect.bottom(), width, height,
                 kBgra, GL_UNSIGNED_BYTE, m_readbackPixels.data());
    unsigned char* output = static_cast<unsigned char*>(bitmap.getPixels());
    const size_t rowBytes = bitmap.rowBytes();
    for (int y = 0; y < height; ++y) {
        const unsigned char* source = m_readbackPixels.data() + static_cast<size_t>(height - 1 - y) * width * 4;
        unsigned char* target = output + static_cast<size_t>(rect.y() + y) * rowBytes + rect.x() * 4;
        memcpy(target, source, static_cast<size_t>(width) * 4);
    }
    m_externalGlStateDirty = true;
    return true;
}

void GpuSkiaSurface::destroyFramebuffer()
{
    if (!m_glContext)
        return;
    FramebufferFunctions functions{};
    if (loadFramebufferFunctions(functions) && m_framebuffer)
        functions.destroy(1, &m_framebuffer);
    if (m_texture)
        glDeleteTextures(1, &m_texture);
    m_framebuffer = 0;
    m_texture = 0;
}

void GpuSkiaSurface::reset()
{
    const bool hadContext = m_glContext != nullptr;
    const bool windowValid = m_window && IsWindow(m_window);
    const bool contextCurrentHere = m_glContext && wglGetCurrentContext() == m_glContext;
    SetEnvironmentVariableA("SOUI_GPU_ACTIVE", "0");
    if (m_window)
        RemovePropA(m_window, "SOUI_GPU_ACTIVE");
    for (HWND window = m_window; window; window = GetParent(window))
        RemovePropA(window, "SOUI_GPU_PRESENT_COUNT");
    SetEnvironmentVariableA("SOUI_GPU_PRESENT_COUNT", "0");
    const BOOL madeCurrent = m_glContext ? wglMakeCurrent(m_dc, m_glContext) : FALSE;
    delete m_canvas;
    m_canvas = nullptr;
    destroyFramebuffer();
    if (m_context)
        m_context->unref();
    m_context = nullptr;
    // GrContext retains its own reference; this is the reference returned by
    // GrGLCreateNativeInterface() for this surface.
    if (m_glInterface)
        m_glInterface->unref();
    m_glInterface = nullptr;
    const BOOL detached = m_glContext ? wglMakeCurrent(nullptr, nullptr) : FALSE;
    const BOOL deleted = m_glContext ? wglDeleteContext(m_glContext) : FALSE;
    const DWORD deleteError = deleted ? 0 : GetLastError();
    const int released = m_dc && m_window ? ReleaseDC(m_window, m_dc) : 0;
    char shutdownTrace[MAX_PATH] = {};
    const DWORD traceLength = GetEnvironmentVariableA("SOUI_GPU_SHUTDOWN_TRACE", shutdownTrace, MAX_PATH);
    if (traceLength && traceLength < MAX_PATH) {
        std::ofstream trace(shutdownTrace, std::ios::app);
        trace << "thread=" << GetCurrentThreadId() << " context=" << hadContext
              << " currentHere=" << contextCurrentHere << " windowValid=" << windowValid
              << " make=" << madeCurrent << " detach=" << detached
              << " delete=" << deleted << " deleteError=" << deleteError
              << " releaseDC=" << released << "\n";
    }
    m_window = nullptr;
    m_dc = nullptr;
    m_glContext = nullptr;
    m_width = 0;
    m_height = 0;
    m_presentCount = 0;
    m_readbackPixels.clear();
    m_readbackPixels.shrink_to_fit();
    m_directPresent = false;
    m_vsyncEnabled = false;
    m_frameDirty = false;
    m_externalGlStateDirty = true;
}

} // namespace cc
