#include "content/browser/WebPageImpl.h"
#include "wke/RendererModelState.h"

#include "base/basictypes.h"
#include "base/rand_util.h"
#include "base/WindowsVersion.h"
#include "third_party/WebKit/Source/wtf/text/qt4/UnicodeQt4.h"
#include "third_party/WebKit/Source/core/frame/Frame.h"
#include "third_party/WebKit/Source/core/frame/Settings.h"
#include "third_party/WebKit/Source/core/frame/FrameView.h"
#include "third_party/WebKit/Source/core/frame/LocalFrame.h"
#include "third_party/WebKit/Source/core/page/Page.h"
#include "third_party/WebKit/Source/core/page/FocusController.h"
#include "third_party/WebKit/Source/core/input/EventHandler.h"
#include "third_party/WebKit/Source/core/loader/FrameLoadRequest.h"
#include "third_party/WebKit/Source/core/fetch/MemoryCache.h"
#include "third_party/WebKit/Source/core/editing/FrameSelection.h"
#include "third_party/WebKit/Source/core/editing/Editor.h"
#include "third_party/WebKit/Source/platform/Task.h"
#include "third_party/WebKit/Source/platform/weborigin/KURL.h"
#include "third_party/WebKit/Source/platform/weborigin/SecurityOrigin.h"
#include "third_party/WebKit/Source/platform/graphics/GraphicsContext.h"
#include "third_party/WebKit/Source/platform/graphics/paint/DisplayItemList.h"
#include "third_party/WebKit/Source/platform/geometry/IntRect.h"
#include "third_party/WebKit/Source/platform/geometry/win/IntRectWin.h"
#include "third_party/WebKit/Source/platform/win/PlatformMouseEventWin.h"
#include "third_party/WebKit/Source/platform/network/ResourceRequest.h"
#include "third_party/WebKit/Source/platform/image-encoders/gdiplus/GDIPlusImageEncoder.h" // TODO
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebWindowFeatures.h"
#include "third_party/WebKit/public/web/WebKit.h"
#include "third_party/WebKit/public/web/WebFrameClient.h"
#include "third_party/WebKit/public/web/WebDraggableRegion.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/Source/web/WebViewImpl.h"
#include "third_party/WebKit/Source/web/FrameLoaderClientImpl.h"
#include "third_party/WebKit/Source/web/WebLocalFrameImpl.h"
#include "third_party/WebKit/Source/web/WebSettingsImpl.h"
#include "third_party/WebKit/Source/wtf/MainThread.h"
#include "third_party/WebKit/Source/wtf/Functional.h"
#include "third_party/WebKit/Source/wtf/RefCountedLeakCounter.h"
#include "third_party/WebKit/Source/wtf/text/WTFStringUtil.h"
#include "third_party/WebKit/Source/bindings/core/v8/V8GCController.h"
#include "content/browser/WebPage.h"
#include "content/browser/PlatformEventHandler.h"
#include "content/browser/WebFrameClientImpl.h"
#include "content/browser/NavigationController.h"
#include "content/browser/CheckReEnter.h"
#include "content/ui/PopupMenuWin.h"
#include "content/ui/PlatformCursor.h"
#include "content/ui/RunFileChooserImpl.h"
#include "content/ui/DragHandle.h"
#include "content/ui/ToolTip.h"
#include "content/web_impl_win/BlinkPlatformImpl.h"
#include "content/web_impl_win/WebThreadImpl.h"
#include "content/web_impl_win/npapi/PluginDatabase.h"
#include "content/devtools/DevToolsClient.h"
#include "content/devtools/DevToolsAgent.h"
#include "cc/trees/LayerTreeHost.h"
#include "cc/base/BdColor.h"
#include "ui/gfx/win/dpi.h"
#include <algorithm>
#include <vector>
#include <atomic>
#include <memory>
#include <limits>
#include <stdexcept>

#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
#include "wke/wkeWebView.h"
#include "wke/wkeUtil.h"
#include "wke/wkeWebWindow.h"
#include "wke/wkeGlobalVar.h"
#include "wke/CursorDescriptorBroker.h"
#endif
#include "skia/ext/bitmap_platform_device_win.h"
#include <cmath>
#if 0
#include <fstream>
#endif

using namespace blink;

extern DWORD g_paintToMemoryCanvasInUiThreadCount;
extern DWORD g_mouseCount;
extern bool g_isTouchEnabled;
extern bool g_isMouseEnabled;

namespace blink {
bool saveDumpFile(const String& url, char* buffer, unsigned int size);
}

namespace content {
namespace {

// Miniblink 49 keeps the screen contract as a cached WebScreenInfo, while a
// per-monitor-aware host can move the child WebView between monitors without
// recreating the page. Resolve the DPI APIs dynamically so the same DLL still
// loads on Windows 7, and refresh the cached scale from the actual WebView
// window whenever Blink asks for screen information.
float deviceScaleFactorForWindow(HWND window)
{
    float fallback = gfx::win::GetDeviceScaleFactor();
    if (fallback <= 0.0f)
        fallback = 1.0f;
    if (!window)
        return fallback;

    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn getDpiForWindow = [] {
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        return user32 ? reinterpret_cast<GetDpiForWindowFn>(
            ::GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
    }();
    if (getDpiForWindow) {
        UINT dpi = getDpiForWindow(window);
        if (dpi > 0)
            return static_cast<float>(dpi) / 96.0f;
    }

    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static HMODULE shcore = ::LoadLibraryW(L"shcore.dll");
    static GetDpiForMonitorFn getDpiForMonitor = shcore
        ? reinterpret_cast<GetDpiForMonitorFn>(::GetProcAddress(shcore, "GetDpiForMonitor"))
        : nullptr;
    if (getDpiForMonitor) {
        HMONITOR monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        UINT dpiX = 0;
        UINT dpiY = 0;
        if (monitor && SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY)) && dpiX > 0)
            return static_cast<float>(dpiX) / 96.0f;
    }
    return fallback;
}

void refreshDeviceScaleFactor(blink::WebScreenInfo* info, HWND window)
{
    if (!info)
        return;
    const float scale = deviceScaleFactorForWindow(window);
    if (scale > 0.0f && std::fabs(info->deviceScaleFactor - scale) > 0.01f)
        info->deviceScaleFactor = scale;
}

}  // namespace

// Opt-in source experiment. Bounded main-thread trace; no per-frame disk I/O.
struct SourceFrameTrace {
    struct Entry { const char* kind; const void* page; LONGLONG tick; };
    wchar_t path[MAX_PATH] = {};
    std::vector<Entry> entries;
    unsigned count = 0, dropped = 0;
    SourceFrameTrace() {
        DWORD length = GetEnvironmentVariableW(L"SOUI_KERNEL_FRAME_TRACE", path, MAX_PATH);
        if (!length || length >= MAX_PATH) path[0] = 0;
        else entries.reserve(65536);
    }
    void record(const char* kind, const void* page) {
        if (!path[0] || !WTF::isMainThread()) return;
        if (count == 65536) { ++dropped; return; }
        LARGE_INTEGER tick; QueryPerformanceCounter(&tick);
        entries.push_back(Entry{ kind, page, tick.QuadPart });
        ++count;
    }
    ~SourceFrameTrace() {
        if (!path[0]) return;
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        char line[160]; DWORD written; LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
        int length = sprintf_s(line, "frequency,%lld,dropped,%u\nkind,page,tick\n", frequency.QuadPart, dropped);
        WriteFile(file, line, length, &written, nullptr);
        for (unsigned i = 0; i < count; ++i) {
            length = sprintf_s(line, "%s,%p,%lld\n", entries[i].kind, entries[i].page, entries[i].tick);
            WriteFile(file, line, length, &written, nullptr);
        }
        CloseHandle(file);
    }
};
static void traceSourceFrame(const char* kind, const void* page) {
    static SourceFrameTrace trace;
    trace.record(kind, page);
}

// This is a diagnostic-only switch. Cache it once per process so the normal
// CPU fallback paint path does not query the process environment on every
// frame. A new Demo process is used when toggling the A/B baseline.
static bool cpuFullFrameCopyRequested() {
    static int requested = -1;
    if (requested < 0) {
        char value[2] = {};
        requested = (GetEnvironmentVariableA("SOUI_CPU_FULL_FRAME_COPY", value,
                                             sizeof(value)) == 1 && value[0] == '1') ? 1 : 0;
    }
    return requested == 1;
}

// The SOUI host consumes the paint callback HDC directly and does not call
// wkeGetViewDC. Keep the legacy mirror available by default for embedders, but
// let a host that owns the child window explicitly skip this second CPU-side
// canvas copy. Cache the decision once so the normal frame path does not query
// the process environment on every paint.
static bool cpuUiMirrorDisabled() {
    static int disabled = -1;
    if (disabled < 0) {
        char value[2] = {};
        disabled = (GetEnvironmentVariableA("SOUI_CPU_SKIP_UI_MIRROR", value,
                                           sizeof(value)) == 1 && value[0] == '1') ? 1 : 0;
    }
    return disabled == 1;
}

void WebPageImpl::initBlink()
{
    BlinkPlatformImpl::initialize();
}

void WebPageImpl::registerDestroyNotif(DestroyNotif* destroyNotif)
{
    WTF::Locker<WTF::Mutex> locker(m_destroyNotifsMutex);
    size_t pos = m_destroyNotifs.find(destroyNotif);
    if (WTF::kNotFound == pos)
        m_destroyNotifs.append(destroyNotif);
}

void WebPageImpl::unregisterDestroyNotif(DestroyNotif* destroyNotif)
{
    RELEASE_ASSERT(WTF::isMainThread());
    WTF::Locker<WTF::Mutex> locker(m_destroyNotifsMutex);
    size_t pos = m_destroyNotifs.find(destroyNotif);
    if (WTF::kNotFound != pos)
        m_destroyNotifs.remove(pos);
}

int64_t WebPageImpl::m_firstFrameId = 0;

WebPageImpl::WebPageImpl(COLORREF bdColor)
{
    m_pagePtr = 0;
    m_bdColor = bdColor; //  RGB(199, 237, 204) | 0xff000000;
    m_createDevToolsAgentTaskObserver = nullptr;
    n_needAutoDrawToHwnd = true;
    m_webViewImpl = nullptr;
    m_debugCount = 0;
    m_enterCount = 0;
    m_hWnd = NULL;
    m_state = pageUninited;
    m_platformEventHandler = nullptr;
    m_postMouseLeave = false;
    //m_needsCommit = 0;
    m_commitCount = 0;
    m_needsLayout = 1;
    m_layerDirty = 1;
    m_executeMainFrameCount = 0;
    m_lastFrameTimeMonotonic = 0;
    m_mainFrameInterval = 0;
    m_nextMainFrameTime = 0;
    m_popupHandle = nullptr;
    m_postCloseWidgetSoonMessage = false;
    m_navigationController = new NavigationController(this);
    m_layerTreeHost = new cc::LayerTreeHost(this, this);
    m_memoryCanvasForUi = nullptr;
    m_disablePaint = false;
    m_firstDrawCount = 0;
    m_webFrameClient = new content::WebFrameClientImpl();

    m_devToolsClient = nullptr;
    m_devToolsAgent = nullptr;
    m_isEnterDebugLoop = false;
    m_draggableRegion = ::CreateRectRgn(0, 0, 0, 0); // Create a HRGN representing the draggable window area.

    WebPageImpl* self = this;
    m_dragHandle = new DragHandle(
        [self] { self->onEnterDragSimulate(); },
        [self] { self->onLeaveDragSimulate(); },
        [self] { self->onDraggingSimulate(); });
    m_isDragging = false;
    m_isFirstEnterDrag = false;
    m_autoRecordActionsCount = 0;
    m_runningInMouseMessage = false;

    m_screenInfo = nullptr;

    m_toolTip = new ToolTip(true, 0.02);
    m_validationMessageTip = new ToolTip(false, 1);
    
    WebLocalFrameImpl* webLocalFrameImpl = (WebLocalFrameImpl*)WebLocalFrame::create(WebTreeScopeType::Document, m_webFrameClient);
    m_webViewImpl = WebViewImpl::create(this);
    m_webViewImpl->setMainFrame(webLocalFrameImpl);

    setBackgroundColor(m_bdColor);

    Frame* frame = toCoreFrame(m_webViewImpl->mainFrame());
    if (0 == m_firstFrameId)
        m_firstFrameId  = frame->frameID();

    initSetting();

    m_platformEventHandler = new PlatformEventHandler(m_webViewImpl, m_webViewImpl);

    m_layerTreeHost->setWebGestureCurveTarget(m_webViewImpl);
}

WebPageImpl::~WebPageImpl()
{
    ASSERT(pageDestroyed == m_state);
    m_platformCursor.dispose();
    m_state = pageDestroyed;

    if (m_createDevToolsAgentTaskObserver) {
        blink::Platform::current()->currentThread()->removeTaskObserver(m_createDevToolsAgentTaskObserver);
        delete m_createDevToolsAgentTaskObserver;
    }

    if (m_screenInfo)
        delete m_screenInfo;

    delete m_toolTip;
    delete m_validationMessageTip;
    if (m_draggableRegion)
        ::DeleteObject(m_draggableRegion);

    if (m_memoryCanvasForUi)
        delete m_memoryCanvasForUi;
    m_memoryCanvasForUi = nullptr;
    
    m_navigationController = nullptr;

    delete m_layerTreeHost;
    m_layerTreeHost = nullptr;

    // 鍦≒age::~Page()涓攢姣?
    if (m_webFrameClient)
        delete m_webFrameClient;
    m_webFrameClient = nullptr;

    delete m_platformEventHandler;
    m_platformEventHandler = nullptr;

    m_pagePtr = 0;
    m_popupHandle = nullptr;

//     BlinkPlatformImpl* platform = (BlinkPlatformImpl*)blink::Platform::current();
//     platform->setGcTimer(0);

    String output = String::format("WebPageImpl::~WebPageImpl: %p\n", this);
    OutputDebugStringA(output.utf8().data());
}

#if ENABLE_WKE == 1
wke::CWebView* WebPageImpl::wkeWebView() const
{
    if (!m_pagePtr)
        return nullptr;
    return m_pagePtr->wkeWebView();
}
#endif

class CreateDevToolsAgentTaskObserver : public blink::WebThread::TaskObserver {
public:
    CreateDevToolsAgentTaskObserver(WebPageImpl* parent)
    {
        m_parent = parent;
    }
    virtual ~CreateDevToolsAgentTaskObserver()
    {
    }

    virtual void willProcessTask() override {}
    virtual void didProcessTask() override
    {
        if (!m_parent->isDevToolsClient())
            m_parent->createOrGetDevToolsAgent();
        blink::Platform::current()->currentThread()->removeTaskObserver(this);
        m_parent->didRunCreateDevToolsAgentTaskObserver();
        delete this;
    }

private:
    WebPageImpl* m_parent;
};

void WebPageImpl::didRunCreateDevToolsAgentTaskObserver()
{
    m_createDevToolsAgentTaskObserver = nullptr;
}

void WebPageImpl::init(WebPage* pagePtr, HWND hWnd)
{
    if (hWnd) {
        setHWND(hWnd);

        LONG windowStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
        bool useLayeredBuffer = !!(windowStyle & WS_EX_LAYERED);
        m_layerTreeHost->setHasTransparentBackground(useLayeredBuffer);
    }
    m_pagePtr = pagePtr;
    m_webFrameClient->setWebPage(m_pagePtr);

    m_webViewImpl->setFocus(true);

    String output = String::format("WebPageImpl::init: %p\n", this);
    OutputDebugStringA(output.utf8().data());

    // DevToolsAgent蹇呴』鍏堝垱寤猴紝涓嶇劧鏃犳硶璁板綍鎵ц鐜锛屼細瀵艰嚧console鏃犳硶鎵цjs
    m_createDevToolsAgentTaskObserver = new CreateDevToolsAgentTaskObserver(this);
    blink::Platform::current()->currentThread()->addTaskObserver(m_createDevToolsAgentTaskObserver);

    m_state = pageInited;
}

bool WebPageImpl::checkForRepeatEnter()
{
    if (m_isDragging && m_runningInMouseMessage)
        return true;

    if (m_enterCount == 0 && 0 == CheckReEnter::getEnterCount())
        return true;
    return false;
}

class AutoRecordActions {
public:
    AutoRecordActions(WebPageImpl* page, cc::LayerTreeHost* host, bool isComefromMainFrame)
    {
        m_isDragging = page->m_isDragging;
        m_forceExit = false;

        if (page->m_autoRecordActionsCount > 0) {
            //RELEASE_ASSERT(page->m_isDragging && 1 == page->m_autoRecordActionsCount);
            if (!(page->m_isDragging && 1 == page->m_autoRecordActionsCount)) {
                m_forceExit = true;
                return; // 鍦ㄩ紶鏍囩偣鍑绘椂鍊欒皟鐢╟reatewebview鐨勬椂鍊欒皟鐢╳keWake浼氳Е鍙戣繖
            }

            init(page, host, false);
            leave();
        }
        init(page, host, isComefromMainFrame);
        enter();
    }

    void init(WebPageImpl* page, cc::LayerTreeHost* host, bool isComefromMainFrame)
    {
        m_host = host;
        m_page = page;
        m_isComefromMainFrame = isComefromMainFrame;
    }

    void enter()
    {
        m_page->m_autoRecordActionsCount++;

        if (!m_host)
            return;

        m_lastFrameTimeMonotonic = WTF::monotonicallyIncreasingTime();

        int layerDirty = m_page->m_layerDirty; 
        int needsLayout = m_page->m_needsLayout; 
        if (m_isComefromMainFrame) {
            InterlockedExchange(reinterpret_cast<long volatile*>(&m_page->m_layerDirty), 0);
            InterlockedExchange(reinterpret_cast<long volatile*>(&m_page->m_needsLayout), 0);
        }
        m_isLayout = (0 != layerDirty || 0 != needsLayout);
        m_host->beginRecordActions(m_isComefromMainFrame);
    }

    ~AutoRecordActions()
    {
        if (m_forceExit)
            return;
        leave();
        if (m_isDragging)
            enter();
    }

    void leave()
    {
        m_page->m_autoRecordActionsCount--;
        if (!m_host)
            return;
        
        int layerDirty = m_page->m_layerDirty;
        int needsLayout = m_page->m_needsLayout;
        if (m_isComefromMainFrame) {
            InterlockedExchange(reinterpret_cast<long volatile*>(&m_page->m_layerDirty), 0);
            InterlockedExchange(reinterpret_cast<long volatile*>(&m_page->m_needsLayout), 0);
        }
        bool isLayout = 0 != layerDirty || 0 != needsLayout;

        if ((isLayout || m_isLayout) && m_isComefromMainFrame) {
            if (m_page->m_mainFrameInterval <= 0 || m_lastFrameTimeMonotonic >= m_page->m_nextMainFrameTime) {
                WebBeginFrameArgs frameArgs(m_lastFrameTimeMonotonic, 0, m_lastFrameTimeMonotonic - m_page->m_lastFrameTimeMonotonic);
                if (m_page->m_mainFrameInterval > 0) {
                    double& deadline = m_page->m_nextMainFrameTime;
                    const double interval = m_page->m_mainFrameInterval;
                    if (deadline <= 0) deadline = m_lastFrameTimeMonotonic + interval;
                    else {
                        // Preserve fractional phase, skipping missed frames instead of bursting.
                        while (deadline <= m_lastFrameTimeMonotonic) {
                            const double behind = m_lastFrameTimeMonotonic - deadline;
                            const long long missed = static_cast<long long>(behind / interval);
                            deadline += (missed + 1) * interval;
                        }
                    }
                }
                if (m_page->m_mainFrameInterval > 0)
                    m_page->m_lastFrameTimeMonotonic = m_lastFrameTimeMonotonic;
                traceSourceFrame("frame", m_page);
                m_page->m_webViewImpl->beginFrame(frameArgs);
            } else {
                // Keep animation pending while allowing layout/paint actions to flush.
                m_page->setNeedsCommit();
            }
            m_page->m_webViewImpl->layout();
        }

        if (m_isComefromMainFrame)
            m_host->recordDraw();
        else if (isLayout || m_isLayout)
            m_page->setNeedsCommitAndNotLayout();

        m_host->endRecordActions();

        if (m_page->m_mainFrameInterval <= 0)
            m_page->m_lastFrameTimeMonotonic = m_lastFrameTimeMonotonic;
    }

private:
    WebPageImpl* m_page;
    cc::LayerTreeHost* m_host;
    bool m_isLayout;
    bool m_isComefromMainFrame;
    bool m_isDragging;
    double m_lastFrameTimeMonotonic;

    bool m_forceExit;
};

#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
static WebView* createWkeViewDefault(HWND parent, const WebString& name, const WTF::CString& url)
{
    // A default browser popup is a raw view, unlike independent standard
    // select/menu helpers. Controlled creation must come from a host endpoint.
    if (wke::brokerActive())
        return nullptr;
    wke::CWebWindow* window = new wke::CWebWindow(cc::s_kBgColor);
    WTF::String nameString = name;
    Vector<UChar> nameBuf = WTF::ensureUTF16UChar(nameString, true);

    wkeWindowCreateInfo info;
    info.size = sizeof(wkeWindowCreateInfo);
    info.style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    info.styleEx = 0;
    info.x = 100;
    info.y = 100;
    info.width = 570;
    info.height = 570;
    info.color = cc::s_kBgColor;

    window->createWindow(&info);

    WebPage* webPage = window->webPage();
    if (!webPage)
        return nullptr;

    window->setName((const wchar_t*)nameBuf.data());
    window->loadURL(url.data());
    ::ShowWindow(window->windowHandle(), 1);
    ::UpdateWindow(window->windowHandle());

    return webPage->webViewImpl();
}

WebView* WebPageImpl::createWkeView(WebLocalFrame* creator,
    const WebURLRequest& request,
    const WebWindowFeatures& features,
    const WebString& name,
    WebNavigationPolicy policy,
    bool suppressOpener)
{
    WTF::CString url = request.url().spec();

    wke::CWebViewHandler& handler = m_pagePtr->wkeHandler();
    if (!handler.createViewCallback)
        return createWkeViewDefault(/*m_hWnd*/nullptr, name, url);
    
    wkeNavigationType type = WKE_NAVIGATION_TYPE_LINKCLICK;
    wke::CString wkeUrl(url.data(), url.length());
    wkeWindowFeatures windowFeatures;
    windowFeatures.x = features.xSet ? features.x : CW_USEDEFAULT;
    windowFeatures.y = features.ySet ? features.y : CW_USEDEFAULT;
    windowFeatures.width = features.widthSet ? features.width : CW_USEDEFAULT;
    windowFeatures.height = features.heightSet ? features.height : CW_USEDEFAULT;
    windowFeatures.locationBarVisible = features.locationBarVisible;
    windowFeatures.menuBarVisible = features.menuBarVisible;
    windowFeatures.resizable = features.resizable;
    windowFeatures.statusBarVisible = features.statusBarVisible;
    windowFeatures.toolBarVisible = features.toolBarVisible;
    windowFeatures.fullscreen = features.fullscreen;

    wke::CWebView* createdWebView = handler.createViewCallback(m_pagePtr->wkeWebView(), handler.createViewCallbackParam, type, &wkeUrl, &windowFeatures);
    if (!createdWebView || createdWebView == m_pagePtr->wkeWebView())
        return nullptr;

    if (!createdWebView->webPage())
        return nullptr; 
    return createdWebView->webPage()->webViewImpl();
}
#endif

WebView* WebPageImpl::createView(WebLocalFrame* creator,
    const WebURLRequest& request,
    const WebWindowFeatures& features,
    const WebString& name,
    WebNavigationPolicy policy,
    bool suppressOpener)
{
    wke::diagnostics::observe_renderer_factory(WKE_RENDER_MODEL_FACTORY_EXTERNAL_VIEW);
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    if (m_pagePtr->wkeWebView())
        return createWkeView(creator, request, features, name, policy, suppressOpener);
#endif
    return nullptr;
}

DevToolsAgent* WebPageImpl::createOrGetDevToolsAgent()
{
    if (m_devToolsAgent)
        return m_devToolsAgent;
    m_devToolsAgent = new DevToolsAgent(m_pagePtr, (blink::WebLocalFrame*)m_webViewImpl->mainFrame());
    return m_devToolsAgent;    
}

DevToolsClient* WebPageImpl::createOrGetDevToolsClient()
{
    if (m_devToolsClient)
        return m_devToolsClient;
    m_devToolsClient = new DevToolsClient(m_pagePtr, (blink::WebLocalFrame*)m_webViewImpl->mainFrame());
    return m_devToolsClient;
}

void WebPageImpl::testPaint()
{
}

void WebPageImpl::freeV8TempObejctOnOneFrameBefore()
{
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    wke::freeV8TempObejctOnOneFrameBefore();
#endif
}

void WebPageImpl::close()
{
    ASSERT(isMainThread());
    if (wke::brokerOwnsView(m_pagePtr->wkeWebView())) {
        wke::brokerBeginClose(m_pagePtr->wkeWebView());
        if (!wke::brokerDrained(m_pagePtr->wkeWebView()))
            return;
    }
    if (pageDestroying == m_state && m_enterCount <= 1) {
        doClose();
        return;
    }
    if (pageInited != m_state)
        return;

    m_state = pageDestroying;

    if (m_enterCount == 0 && !wke::brokerOwnsView(m_pagePtr->wkeWebView())) { // 鎶奷ragState().m_dragSrc涔嬬被鐨勫崰鐢ㄦ姷娑?
        WTF::TemporaryChange<int> temporaryChange(m_enterCount, 0);
        LPARAM lParam = MAKELONG(-10000, -10000);
        fireMouseEvent(m_hWnd, WM_LBUTTONUP, 0, lParam, nullptr);
        fireMouseEvent(m_hWnd, WM_MOUSEMOVE, 0, lParam, nullptr);
        fireMouseEvent(m_hWnd, WM_LBUTTONDOWN, 0, lParam, nullptr);
        fireMouseEvent(m_hWnd, WM_MBUTTONUP, 0, lParam, nullptr);
        fireMouseEvent(m_hWnd, WM_RBUTTONUP, 0, lParam, nullptr);
    }

    // 鍦↘FrameLoaderClient::frameLoaderDestroyed()涔熶細璋冪敤鍒版锛屾墍浠ュ湪缁欒剼鏈彂娑堟伅鐨勬椂鍊欐敞鎰忎竴涓?
    // WebCore::WebPage::windowCloseRequested+0x1a           
    // WebCore::KFrameLoaderClient::frameLoaderDestroyed+0xc  
    // WebCore::FrameLoader::~FrameLoader+0x7e                
    // WebCore::Frame::~Frame+0xd0                            
    // WTF::RefCounted<WebCore::Frame>::deref+0x16            
    // WebCore::Frame::lifeSupportTimerFired+0x5              
    // WebCore::Timer<WebCore::MainResourceLoader>::fired+0xc 
    // WebCore::ThreadTimers::sharedTimerFiredInternal+0x8b   
    // WebCore::ThreadTimers::sharedTimerFired+0xe            
    // WebCore::WebPage::windowCloseRequested+0x99
    // 鎵€浠ヨ繖閲屽彲鑳芥湁閲嶅叆
    //SharedTimerKd::inst()->timerEvent(); // 寰堝寮傛娓呯悊璧勬簮鐨勫伐浣滀細鏀惧湪瀹氭椂鍣ㄩ噷锛屾墍浠ユ渶鍚庡啀鎵ц涓€閬?

    // TODO_Weolar
    // 鍙戠敓unload娑堟伅缁欒剼鏈?

    // 娓呯┖webkit鐨勮祫婧愮紦瀛?
    //memoryCache()->evictResources();

    if (m_enterCount <= 1)
        doClose();
}

void WebPageImpl::doClose()
{
    // Reentry destructors also reach here: actual owned task drain is mandatory.
    if (!wke::brokerDrained(m_pagePtr->wkeWebView()))
        return;
    m_platformCursor.dispose();
    m_layerTreeHost->requestApplyActionsToRunIntoCompositeThread(false);
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    if (!m_pagePtr->wkeHandler().isWke) {
#endif
        if (m_hWnd) {
            ::SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);
            ::KillTimer(m_hWnd, (UINT_PTR)this);
            ::DestroyWindow(m_hWnd);
        }
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    }
#endif


    if (m_hWnd && !wke::brokerOwnsView(m_pagePtr->wkeWebView())) {
        if (::IsWindow(m_hWnd)) { // 澶氱嚎绋嬫覆鏌撴椂锛寀i绾跨▼鍏堥攢姣佺獥鍙ｏ紝鍐嶈蛋鍒版澶?
            ::RevokeDragDrop(m_hWnd);
            ASSERT(0 == m_dragHandle->getRefCount());
        } else {
            ASSERT(0 == m_dragHandle->getRefCount());
        }
    }

    delete m_dragHandle;
    m_dragHandle = nullptr;

    content::WebThreadImpl* threadImpl = nullptr;
    threadImpl = (content::WebThreadImpl*)(blink::Platform::current()->currentThread());

    if (m_devToolsClient)
        delete m_devToolsClient;

    if (m_devToolsAgent) {
        m_devToolsAgent->onDetach();
        delete m_devToolsAgent;
    }

    //m_webViewImpl->mainFrameImpl()->close();
    m_webViewImpl->close();

    for (size_t i = 0; i < m_destroyNotifs.size(); ++i)
        m_destroyNotifs[i]->destroy();

    m_state = pageDestroyed;
}

void WebPageImpl::closeWidgetSoon()
{
    ASSERT(isMainThread());

#if (ENABLE_WKE == 1)
	wke::CWebViewHandler& handler = m_pagePtr->wkeHandler();
    if (handler.windowClosingCallback) {
        // 涓嶇杩斿洖鍊间簡锛屼篃鏆傛椂涓嶄富鍔ㄥ叧闂獥鍙?
		handler.windowClosingCallback(m_pagePtr->wkeWebView(), handler.windowClosingCallbackParam);
    }
#endif

    m_postCloseWidgetSoonMessage = true;
}

void WebPageImpl::gc()
{
    m_layerTreeHost->gc();
}

void WebPageImpl::showDebugNodeData()
{
#ifndef NDEBUG
    m_webViewImpl->mainFrameImpl()->frame()->document()->showTreeForThis();
#endif
}

#ifndef NDEBUG
DEFINE_DEBUG_ONLY_GLOBAL(WTF::RefCountedLeakCounter, commitTaskCounter, ("contentCommitTaskCounter"));
#endif

class CommitTask : public blink::WebThread::Task, public WebPageImpl::DestroyNotif {
public:
    CommitTask(WebPageImpl* client)
        : m_client(client)
    {
        m_client->registerDestroyNotif(this);
#ifndef NDEBUG
        commitTaskCounter.increment();
#endif
    }

    virtual ~CommitTask() override
    {
        if (m_client) {
            m_client->unregisterDestroyNotif(this);
        }
#ifndef NDEBUG
        commitTaskCounter.decrement();
#endif
    }

    virtual void destroy() override
    {
        m_client = nullptr;
    }

    virtual void run() override
    {
        if (!m_client)
            return;
        
        atomicDecrement(&m_client->m_commitCount);
        traceSourceFrame("commit", m_client);
        m_client->beginMainFrame();
    }

private:
    WebPageImpl* m_client;
};

void WebPageImpl::setNeedsCommitAndNotLayout()
{
    scheduleCommit(0);
}

void WebPageImpl::scheduleCommit(long long minimumDelayMs)
{
//     if (0 != m_needsCommit)
//         return;
//     atomicIncrement(&m_needsCommit);

    if (0 == m_commitCount) {
        atomicIncrement(&m_commitCount);
        blink::Platform* platfrom = blink::Platform::current();
        WebThreadImpl* threadImpl = (WebThreadImpl*)platfrom->mainThread();
        const double remainingMs = (m_nextMainFrameTime - WTF::monotonicallyIncreasingTime()) * 1000.0;
        long long delayMs = minimumDelayMs;
        if (m_mainFrameInterval > 0 && remainingMs > 0) {
            const long long frameDelayMs = remainingMs >= 34 ? 34 : static_cast<long long>(remainingMs + 0.999);
            delayMs = (std::max)(delayMs, frameDelayMs > 0 ? frameDelayMs : 1);
        }
        if (delayMs > 0) {
            threadImpl->postDelayedTask(FROM_HERE, new CommitTask(this), delayMs);
        } else {
            threadImpl->postTask(FROM_HERE, new CommitTask(this));
        }
    }
}

void WebPageImpl::setNeedsCommit()
{
    InterlockedExchange(reinterpret_cast<long volatile*>(&m_needsLayout), 1);
    setNeedsCommitAndNotLayout();
}

void WebPageImpl::beginMainFrame()
{
    //bool needsCommit = m_needsCommit;
    if (pageInited != m_state)
        return;

    if (/*needsCommit*/true) {
        executeMainFrame();
        m_layerTreeHost->requestDrawFrameToRunIntoCompositeThread();
    }
}

void WebPageImpl::executeMainFrame()
{
    freeV8TempObejctOnOneFrameBefore();
    //atomicDecrement(&m_needsCommit);

    if (0 != m_executeMainFrameCount || m_isEnterDebugLoop)
        return;
    atomicIncrement(&m_executeMainFrameCount);

    //double lastFrameTimeMonotonic = WTF::monotonicallyIncreasingTime();

    if (!m_layerTreeHost->canRecordActions()) {
        // Resource/fence pressure cannot make progress by executing the same
        // commit repeatedly in one ready-task drain. Keep one deferred retry.
        scheduleCommit(1);
        atomicDecrement(&m_executeMainFrameCount);
        return;
    }
    
    {
        AutoRecordActions autoRecordActions(this, m_layerTreeHost, true);
    }
    
#ifndef NDEBUG
    if (0) {
        showDebugNodeData();
        m_layerTreeHost->showDebug();
        blink::memoryCache()->evictResources();
        // V8GCController::collectGarbage(v8::Isolate::GetCurrent());
        Heap::collectGarbage(ThreadState::HeapPointersOnStack, ThreadState::GCWithSweep, Heap::ForcedGC);
        v8::Isolate::GetCurrent()->LowMemoryNotification();
    }
#endif
    atomicDecrement(&m_executeMainFrameCount);
}

void WebPageImpl::onLayerTreeDirty()
{
    InterlockedExchange(reinterpret_cast<long volatile*>(&m_layerDirty), 1);
    setNeedsCommitAndNotLayout();
}

void WebPageImpl::didUpdateLayout()
{
    setNeedsCommit();
}

bool WebPageImpl::fireTimerEvent()
{
    CHECK_FOR_REENTER(this, false);
        
    traceSourceFrame("timer", this);
    beginMainFrame();
    return false;
}

void WebPageImpl::fireResizeEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    freeV8TempObejctOnOneFrameBefore();
    if (pageInited != m_state)
        return;

    UINT cx, cy;
    cx = LOWORD(lParam);
    cy = HIWORD(lParam);

    if (!m_hWnd)
        m_hWnd = hWnd;

    // WebViewImpl is constructed before Miniblink attaches the native child
    // HWND, so its constructor can only see the process-wide 1x fallback.
    // Re-apply the real monitor scale after the HWND is known. Without this
    // hand-off Blink keeps a 1x backing store and Windows has to soften the
    // child surface when the product runs at 125/150/200% DPI.
    if (m_webViewImpl) {
        const blink::WebScreenInfo current = screenInfo();
        m_webViewImpl->setDeviceScaleFactor(current.deviceScaleFactor);
    }

    IntSize size(cx, cy);
    setViewportSize(size);
}

IntRect WebPageImpl::caretRect() const
{
    blink::LocalFrame* targetFrame = blink::toLocalFrame(m_webViewImpl->focusedCoreFrame());
    if (!targetFrame)
        return IntRect();

    blink::IntRect caret;
    if (RefPtrWillBeRawPtr<Range> range = targetFrame->selection().selection().toNormalizedRange())
        caret = targetFrame->editor().firstRectForRange(range.get());

    caret = targetFrame->view()->contentsToViewport(caret);

    if (!targetFrame->selection().hasEditableStyle() && !targetFrame->selection().isContentEditable())
        caret.setHeight(0);

    return caret;
}

void WebPageImpl::setViewportSize(const IntSize& size)
{
    if (size.isEmpty())
        return;

    if (m_viewportSize == size)
        return;
    m_viewportSize = size;

    ASSERT(pageInited == m_state);
    if (pageDestroying == m_state)
        return;

    if (size.isEmpty())
        return;

    if (checkForRepeatEnter()) {
        CheckReEnter checker(this);
        AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);
    }

    if (m_layerTreeHost)
        m_layerTreeHost->setViewportSize(size);

    m_webViewImpl->resize(size);
}

void WebPageImpl::firePaintEvent(HDC hdc, const RECT* paintRect)
{
    CHECK_FOR_REENTER(this, (void)0);
    freeV8TempObejctOnOneFrameBefore();

    traceSourceFrame("paint", this);
    beginMainFrame();

    if (m_layerTreeHost)
        m_layerTreeHost->firePaintEvent(hdc, paintRect);
}

HDC WebPageImpl::viewDC()
{
    bool useLayeredBuffer = m_layerTreeHost->getHasTransparentBackground();
    if (!m_memoryCanvasForUi && !m_viewportSize.isEmpty()) {
        m_memoryCanvasForUi = skia::CreatePlatformCanvas(m_viewportSize.width(), m_viewportSize.height(), !useLayeredBuffer);
        cc::LayerTreeHost::clearCanvas(m_memoryCanvasForUi, IntRect(0, 0, m_viewportSize.width(), m_viewportSize.height()), useLayeredBuffer);
    }
    if (!m_memoryCanvasForUi)
        return nullptr;

    skia::BitmapPlatformDevice* device = (skia::BitmapPlatformDevice*)skia::GetPlatformDevice(skia::GetTopDevice(*m_memoryCanvasForUi));
    if (!device)
        return nullptr;
    HDC hDC = device->GetBitmapDCUgly(m_hWnd);
    return hDC;
}

void WebPageImpl::copyToMemoryCanvasForUi(const IntRect& paintRect)
{
    if (cpuUiMirrorDisabled())
        return;

    bool useLayeredBuffer = m_layerTreeHost->getHasTransparentBackground();
    if (m_hWnd && !useLayeredBuffer) {
        LONG windowStyle = GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
        useLayeredBuffer = !!(windowStyle & WS_EX_LAYERED);
        m_layerTreeHost->setHasTransparentBackground(useLayeredBuffer);
    }

    SkCanvas* memoryCanvas = m_layerTreeHost->getMemoryCanvasLocked();
    if (!memoryCanvas) {
        m_layerTreeHost->releaseMemoryCanvasLocked();
        return;
    }
    
    bool isCreatedThisTime = false;
    int width = memoryCanvas->imageInfo().width();
    int height = memoryCanvas->imageInfo().height();
    if (0 != width && 0 != height) {
        if ((!m_memoryCanvasForUi && 0 != width * height) || 
            (m_memoryCanvasForUi && (width != m_memoryCanvasForUi->imageInfo().width() || height != m_memoryCanvasForUi->imageInfo().height()))) {
            if (m_memoryCanvasForUi)
                delete m_memoryCanvasForUi;
            m_memoryCanvasForUi = skia::CreatePlatformCanvas(width, height, !useLayeredBuffer);
            cc::LayerTreeHost::clearCanvas(m_memoryCanvasForUi, IntRect(0, 0, width, height), useLayeredBuffer);
            isCreatedThisTime = true;
        }
    } else if (m_memoryCanvasForUi) {
        delete m_memoryCanvasForUi;
        m_memoryCanvasForUi = nullptr;
    }

    if (!m_memoryCanvasForUi) {
        m_layerTreeHost->releaseMemoryCanvasLocked();
        return;
    }

    if (useLayeredBuffer && !isCreatedThisTime)
        cc::LayerTreeHost::clearCanvas(m_memoryCanvasForUi, IntRect(0, 0, width, height), useLayeredBuffer);

    const bool forceFullCopy = cpuFullFrameCopyRequested();
    SkBaseDevice* device = skia::GetTopDevice(*memoryCanvas);
    if (device) {
        const SkBitmap& memoryCanvasBitmap = device->accessBitmap(false);
        // The compositor already provides the dirty rectangle. Copy the full
        // bitmap only when the UI canvas is first created; subsequent frames
        // clip the copy to the changed region so CPU fallback does not spend
        // O(viewport) work for a small animated control.
        if (isCreatedThisTime || paintRect.isEmpty() || useLayeredBuffer || forceFullCopy) {
            m_memoryCanvasForUi->drawBitmap(memoryCanvasBitmap, 0, 0, nullptr);
        } else {
            // Submit only the changed source pixels. A canvas clip prevents
            // writes outside the dirty rectangle, but drawBitmap() can still
            // walk/filter the entire viewport-sized bitmap. The source and
            // destination rectangles are identical here, so drawBitmapRect
            // preserves pixel placement while keeping CPU fallback work
            // proportional to the compositor's dirty region.
            IntRect dirtyRect = paintRect;
            dirtyRect.intersect(IntRect(0, 0, width, height));
            if (!dirtyRect.isEmpty()) {
                const SkIRect source = SkIRect::MakeXYWH(
                    dirtyRect.x(), dirtyRect.y(), dirtyRect.width(), dirtyRect.height());
                const SkRect destination = SkRect::MakeXYWH(
                    dirtyRect.x(), dirtyRect.y(), dirtyRect.width(), dirtyRect.height());
                m_memoryCanvasForUi->drawBitmapRect(memoryCanvasBitmap, &source,
                                                    destination, nullptr);
            }
        }

    }
    m_layerTreeHost->releaseMemoryCanvasLocked();
}

bool WebPageImpl::isDrawDirty()
{
    if (m_layerTreeHost)
        return m_layerTreeHost->isDrawDirty();
    return false;
}

void drawDebugLine(void* ptr, SkCanvas* memoryCanvas, const IntRect& paintRect)
{
    static int g_debugCount = 0;
    ++g_debugCount;

#if 1 // debug
    if (blink::RuntimeEnabledFeatures::drawDirtyDebugLineEnabled()) {
        OwnPtr<GraphicsContext> context = GraphicsContext::deprecatedCreateWithCanvas(memoryCanvas, GraphicsContext::NothingDisabled);
        context->setStrokeStyle(SolidStroke);
        context->setStrokeColor(0xff000000 | (::GetTickCount() + base::RandInt(0, 0x1223345)));
        context->drawLine(IntPoint(paintRect.x(), paintRect.y()), IntPoint(paintRect.maxX(), paintRect.maxY()));
        context->drawLine(IntPoint(paintRect.maxX(), paintRect.y()), IntPoint(paintRect.x(), paintRect.maxY()));
        context->strokeRect(paintRect, 2);
    }
#endif

#if 0
    String outString = String::format("drawDebugLine:%p, %d %d %d %d, %d\n", ptr, paintRect.x(), paintRect.y(), paintRect.width(), paintRect.height(), g_debugCount);
    OutputDebugStringW(outString.charactersWithNullTermination().data());
#endif
}

void WebPageImpl::disablePaint()
{
    m_disablePaint = true;
}

void WebPageImpl::enablePaint()
{
    m_disablePaint = false;
}

static bool canPaintToScreen(blink::WebViewImpl* webViewImpl)
{
    blink::Page* page = webViewImpl->page();
    if (!page)
        return true;

    blink::LocalFrame* frame = page->deprecatedLocalMainFrame();
    Document* document = frame->document();
    if (!document)
        return true;

    if (!document->timing().firstLayout())
        return false;

    if (document->hasActiveParser())
        return false;

    return true;
}

bool WebPageImpl::needDrawToScreen(HWND hWnd) const
{
    if (!hWnd)
        return false;

    if (blink::RuntimeEnabledFeatures::updataInOtherThreadEnabled() && !m_devToolsClient)
        return false;
    return n_needAutoDrawToHwnd;
}

void WebPageImpl::drawLayeredWindow(HWND hWnd, SkCanvas* canvas, HDC hdc, const IntRect& paintRect, HDC hMemoryDC) const
{
    RECT rtWnd;
    ::GetWindowRect(hWnd, &rtWnd);

    RECT rc = blink::intRectToWinRect(paintRect);
    if (skia::DrawToNativeLayeredContext(canvas, hdc, &rc, &rtWnd))
        return;
    
    BITMAP bmp = { 0 };
    HBITMAP hBmp = (HBITMAP)::GetCurrentObject(hMemoryDC, OBJ_BITMAP);
    ::GetObject(hBmp, sizeof(BITMAP), (LPSTR)&bmp);

    POINT pointSource = { 0, 0 };
    SIZE sizeDest = { 0 };
    sizeDest.cx = bmp.bmWidth;
    sizeDest.cy = bmp.bmHeight;

    HDC hdcMemory = ::CreateCompatibleDC(hdc);
    HBITMAP hbmpMemory = ::CreateCompatibleBitmap(hdc, sizeDest.cx, sizeDest.cy);
    HBITMAP hbmpOld = (HBITMAP)::SelectObject(hdcMemory, hbmpMemory);

    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    ::BitBlt(hdcMemory, 0, 0, sizeDest.cx, sizeDest.cy, hdc, 0, 0, SRCCOPY | CAPTUREBLT);
    ::UpdateLayeredWindow(m_hWnd, hdc, nullptr, &sizeDest, hMemoryDC, &pointSource, RGB(0xFF, 0xFF, 0xFF), &blend, ULW_ALPHA);

    ::SelectObject(hdcMemory, (HGDIOBJ)hbmpOld);
    ::DeleteObject((HGDIOBJ)hbmpMemory);
    ::DeleteDC(hdcMemory);
}

#if 0
bool HDCToFile(HDC Context)
{
    static int i = 0;
    ++i;
    String savePath;
    savePath = String::format("C:\\Users\\weo\\Desktop\\mantan\\DumpFile_2_%d.png", i);

    uint16_t BitsPerPixel = 24;
    RECT Area = {0, 0, 300, 300};

    uint32_t Width = Area.right - Area.left;
    uint32_t Height = Area.bottom - Area.top;
    BITMAPINFO Info;
    BITMAPFILEHEADER Header;
    memset(&Info, 0, sizeof(Info));
    memset(&Header, 0, sizeof(Header));
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = BitsPerPixel;
    Info.bmiHeader.biCompression = BI_RGB;
    Info.bmiHeader.biSizeImage = Width * Height * (BitsPerPixel > 24 ? 4 : 3);
    Header.bfType = 0x4D42;
    Header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    char* Pixels = NULL;
    HDC MemDC = CreateCompatibleDC(Context);
    HBITMAP Section = CreateDIBSection(Context, &Info, DIB_RGB_COLORS, (void**)&Pixels, 0, 0);
    DeleteObject(SelectObject(MemDC, Section));
    BitBlt(MemDC, 0, 0, Width, Height, Context, Area.left, Area.top, SRCCOPY);
    DeleteDC(MemDC);
    std::fstream hFile(savePath.utf8().data(), std::ios::out | std::ios::binary);
    if (hFile.is_open()) {
        hFile.write((char*)&Header, sizeof(Header));
        hFile.write((char*)&Info.bmiHeader, sizeof(Info.bmiHeader));
        hFile.write(Pixels, (((BitsPerPixel * Width + 31) & ~31) / 8) * Height);
        hFile.close();
        DeleteObject(Section);
        return true;
    }
    DeleteObject(Section);
    return false;
}
#endif

// 鏈嚱鏁板彲鑳借璋冪敤鍦╱i绾跨▼锛屼篃鍙互鏄悎鎴愮嚎绋嬨€傚紑鍚绾跨▼缁樺埗锛屽垯鍦ㄥ悎鎴愮嚎绋?
void WebPageImpl::paintToMemoryCanvasInUiThread(SkCanvas* canvas, const IntRect& paintRect)
{
    if (m_disablePaint)
        return;

    //if (0 == m_firstDrawCount && !canPaintToScreen(m_webViewImpl)) { }
    ++m_firstDrawCount;

    HWND hWnd = m_pagePtr->getHWND();
    HDC hMemoryDC = nullptr;
    hMemoryDC = skia::BeginPlatformPaint(hWnd, canvas);

    drawDebugLine(this, canvas, paintRect);
    
    g_paintToMemoryCanvasInUiThreadCount++;

    if (needDrawToScreen(hWnd)) { // 浣跨敤wke鎺ュ彛涓嶇敱姝や笂灞?
        HDC hdc = ::GetDC(hWnd);
#if 0
        COLORREF c = ::GetPixel(hdc, 100, 100);
        c = (c & 0x00ffffff);
        if (/*c == 0x00ffffff &&*/ paintRect.height() > 600) {
            HDCToFile(hMemoryDC);
        }
#endif

        if (m_layerTreeHost->getHasTransparentBackground()) {
            drawLayeredWindow(hWnd, canvas, hdc, paintRect, hMemoryDC);
        } else {
            RECT rc = blink::intRectToWinRect(paintRect);
            skia::DrawToNativeContext(canvas, hdc, paintRect.x(), paintRect.y(), &rc);
        }

#if 0
        HBRUSH hbrush;
        HPEN hpen;
        hbrush = ::CreateSolidBrush(rand());
        ::SelectObject(hdc, hbrush);
        //::Rectangle(hdc, m_paintRect.x(), m_paintRect.y(), m_paintRect.maxX(), m_paintRect.maxY());
        ::Rectangle(hdc, 220, 40, 366, 266);
        ::DeleteObject(hbrush);
#endif

        ::ReleaseDC(hWnd, hdc);
    }

    copyToMemoryCanvasForUi(paintRect);

#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    if (m_pagePtr->wkeHandler().paintUpdatedCallback) {
        m_pagePtr->wkeHandler().paintUpdatedCallback(
            m_pagePtr->wkeWebView(),
            m_pagePtr->wkeHandler().paintUpdatedCallbackParam,
            hMemoryDC, paintRect.x(), paintRect.y(), paintRect.width(), paintRect.height());
    }
#endif

    skia::EndPlatformPaint(canvas);

    cc::LayerTreeHost::BitInfo* bitInfo = nullptr;
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    if (m_pagePtr->wkeHandler().paintBitUpdatedCallback) {
        bitInfo = m_layerTreeHost->getBitBegin();
        if (bitInfo) {
            wkeRect r = { paintRect.x(), paintRect.y(), paintRect.width(), paintRect.height() };
            wke::CWebView* webview = m_pagePtr->wkeWebView();
            void* paintBitUpdatedCallbackParam = m_pagePtr->wkeHandler().paintBitUpdatedCallbackParam;

            m_pagePtr->wkeHandler().paintBitUpdatedCallback(webview, paintBitUpdatedCallbackParam,
                bitInfo->pixels, &r, bitInfo->width, bitInfo->height);
            m_layerTreeHost->getBitEnd(bitInfo);
        }
    }
#endif
}

void WebPageImpl::paintToBit(void* bits, int pitch)
{
    CHECK_FOR_REENTER(this, (void)0);

    traceSourceFrame("bits", this);
    beginMainFrame();

    if (m_layerTreeHost)
        m_layerTreeHost->paintToBit(bits, pitch);
}

void WebPageImpl::setMainFrameRate(int rate)
{
    ASSERT(isMainThread());
    if (rate != 0 && (rate < 30 || rate > 240)) return;
    const double interval = rate ? 1.0 / rate : 0;
    if (interval == m_mainFrameInterval) return;
    m_mainFrameInterval = interval;
    m_nextMainFrameTime = 0;
    if (pageInited == m_state) setNeedsCommit();
}
void WebPageImpl::setDrawMinInterval(double drawMinInterval)
{
    if (m_layerTreeHost)
        m_layerTreeHost->setDrawMinInterval(drawMinInterval);
}

void WebPageImpl::repaintRequested(const IntRect& windowRect, bool forceRepaintIfEmptyRect)
{
    freeV8TempObejctOnOneFrameBefore();
    IntRect r = windowRect;
    if (forceRepaintIfEmptyRect && r.isEmpty())
        r = m_layerTreeHost->getClientRect();

    if (pageInited != m_state || r.isEmpty() || r.maxY() < 0 || r.maxX() < 0)
        return;

    if (m_layerTreeHost)
        m_layerTreeHost->postPaintMessage(r);
    setNeedsCommitAndNotLayout();
}

// Called when a region of the WebWidget needs to be re-painted.
void WebPageImpl::didInvalidateRect(const WebRect& r)
{
    IntRect windowRect(r);
    if (-1 == windowRect.width() || -1 == windowRect.height())
        windowRect = m_layerTreeHost->getClientRect();
    repaintRequested(windowRect, false);
}

// Called when the Widget has changed size as a result of an auto-resize.
void WebPageImpl::didAutoResize(const WebSize& newSize)
{
    //notImplemented();
}

void WebPageImpl::didUpdateLayoutSize(const WebSize& newSize)
{
    //notImplemented();
}

void WebPageImpl::scheduleAnimation()
{
    setNeedsCommit/*AndNotLayout*/();
}

void WebPageImpl::onLayerTreeSetNeedsCommit()
{
    setNeedsCommit();
}

void WebPageImpl::onLayerTreeInvalidateRect(const blink::IntRect& r)
{
    didInvalidateRect(r);
}

void WebPageImpl::initializeLayerTreeView()
{
    ;
}

WebLayerTreeView* WebPageImpl::layerTreeView()
{
    return layerTreeHost();
}

void WebPageImpl::didChangeCursor(const WebCursorInfo& cursor)
{
    // Publish before legacy type deduplication; custom A -> custom B is valid.
    if (wke::brokerPublishCursor(m_pagePtr->wkeWebView(), cursor)) {
        m_cursor = cursor;
        return;
    }
    if (m_cursor.type == cursor.type)
        return;

    m_cursor = cursor;

    if (m_hWnd)
        ::PostMessage(m_hWnd, WM_SETCURSOR, (WPARAM)m_hWnd, MAKELONG(HTCLIENT, 0)); // COleControl::OnSetCursor
}

int WebPageImpl::getCursorInfoType() const
{
    return (int)m_cursor.type;
}

void WebPageImpl::setCursorInfoType(int type)
{
    m_cursor.type = (blink::WebCursorInfo::Type)type;
    wke::brokerPublishCursor(m_pagePtr->wkeWebView(), m_cursor);
}

void WebPageImpl::fireCursorEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, BOOL* handle)
{
    if (wke::brokerOwnsView(m_pagePtr->wkeWebView())) {
        if (handle) *handle = FALSE;
        return; // Descriptor mode never selects a native cursor on the engine.
    }
    CHECK_FOR_REENTER(this, (void)0);
    freeV8TempObejctOnOneFrameBefore();

    if (handle)
        *handle = FALSE;

    HCURSOR hCur = NULL;
    switch (m_cursor.type) {
    case WebCursorInfo::TypeIBeam:
        hCur = ::LoadCursor(NULL, IDC_IBEAM);
        break;
        //////////////////////////////////////////////////////////////////////////
    case WebCursorInfo::TypeProgress:
        hCur = ::LoadCursor(NULL, IDC_APPSTARTING);
        break;
    case WebCursorInfo::TypeCross:
        hCur = ::LoadCursor(NULL, IDC_CROSS);
        break;
    case WebCursorInfo::TypeMove:
        hCur = ::LoadCursor(NULL, IDC_SIZEALL);
        break;

    case WebCursorInfo::TypeColumnResize:
        hCur = ::LoadCursor(NULL, IDC_SIZEWE);
        break;
    case WebCursorInfo::TypeRowResize:
        hCur = ::LoadCursor(NULL, IDC_SIZENS);
        break;
        //////////////////////////////////////////////////////////////////////////
        
    case WebCursorInfo::TypeHand:
        hCur = ::LoadCursor(NULL, IDC_HAND);
        break;
    case WebCursorInfo::TypeWait:
        hCur = ::LoadCursor(NULL, IDC_WAIT);
        break;
    case WebCursorInfo::TypeHelp:
        hCur = ::LoadCursor(NULL, IDC_HELP);
        break;
    case WebCursorInfo::TypeEastResize:
        hCur = ::LoadCursor(NULL, IDC_SIZEWE);
        break;
    case WebCursorInfo::TypeNorthResize:
        hCur = ::LoadCursor(NULL, IDC_SIZENS);
        break;
    case WebCursorInfo::TypeSouthWestResize:
    case WebCursorInfo::TypeNorthEastResize:
        hCur = ::LoadCursor(NULL, IDC_SIZENESW);
        break;
    case WebCursorInfo::TypeSouthResize:
    case WebCursorInfo::TypeNorthSouthResize:
        hCur = ::LoadCursor(NULL, IDC_SIZENS);
        break;
    case WebCursorInfo::TypeNorthWestResize:
    case WebCursorInfo::TypeSouthEastResize:
        hCur = ::LoadCursor(NULL, IDC_SIZENWSE);
        break;
    case WebCursorInfo::TypeWestResize:
    case WebCursorInfo::TypeEastWestResize:
        hCur = ::LoadCursor(NULL, IDC_SIZEWE);
        break;
    case WebCursorInfo::TypeNorthEastSouthWestResize:
    case WebCursorInfo::TypeNorthWestSouthEastResize:
        hCur = ::LoadCursor(NULL, IDC_SIZEALL);
        break;
    case WebCursorInfo::TypeNoDrop:
    case WebCursorInfo::TypeNotAllowed:
        hCur = ::LoadCursor(NULL, IDC_NO);
        break;
    case WebCursorInfo::TypeCustom:
        if (m_platformCursor.update(hWnd, [this] { return createOwnedCursorImpl(m_cursor); }) && handle)
            *handle = TRUE;
        return;
    default:
        break;
    }

    if (hCur) {
        ::SetCursor(hCur);
        if (handle)
            *handle = TRUE;
    }
}

LRESULT WebPageImpl::fireWheelEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CHECK_FOR_REENTER(this, 0);
    freeV8TempObejctOnOneFrameBefore();
    AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);
    
    if (m_popup && m_popup->isVisible())
        m_popup->fireWheelEvent(message, wParam, lParam);
    else
        m_platformEventHandler->fireWheelEvent(hWnd, message, wParam, lParam);

    return 0;
}

bool WebPageImpl::fireKeyUpEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CHECK_FOR_REENTER(this, false);
    freeV8TempObejctOnOneFrameBefore();
    AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);
    
    WebKeyboardEvent keyEvent = PlatformEventHandler::buildKeyboardEvent(WebInputEvent::KeyUp, message, wParam, lParam);
    return m_webViewImpl->handleInputEvent(keyEvent);
}

bool WebPageImpl::fireKeyPressEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CHECK_FOR_REENTER(this, false);
    freeV8TempObejctOnOneFrameBefore();
    AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);

    WebKeyboardEvent keyEvent = PlatformEventHandler::buildKeyboardEvent(WebInputEvent::Char, message, wParam, lParam);
    return m_webViewImpl->handleInputEvent(keyEvent);
}

bool WebPageImpl::fireKeyDownEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CHECK_FOR_REENTER(this, false);
    freeV8TempObejctOnOneFrameBefore();
    AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);

    if (m_popup && m_popup->isVisible())
        return m_popup->fireKeyUpEvent(message, wParam, lParam);

    unsigned int virtualKeyCode = wParam;
    WebKeyboardEvent keyEvent = PlatformEventHandler::buildKeyboardEvent(WebInputEvent::RawKeyDown, message, wParam, lParam);
    bool handled = m_webViewImpl->handleInputEvent(keyEvent);
    bool systemKey = false;
    // These events cannot be canceled, and we have no default handling for them.
    // FIXME: match IE list more closely, see <http://msdn2.microsoft.com/en-us/library/ms536938.aspx>.
    if (systemKey && virtualKeyCode != VK_RETURN)
        return false;
    
    return handled;
}

bool WebPageImpl::handleCurrentKeyboardEvent()
{
    return false;
}

void WebPageImpl::fireCaptureChangedEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CHECK_FOR_REENTER(this, (void)0);
    freeV8TempObejctOnOneFrameBefore();
    AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);

    m_platformEventHandler->fireCaptureChangedEvent(hWnd, message, wParam, lParam);
}

void WebPageImpl::fireSetFocusEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CHECK_FOR_REENTER(this, (void)0);
    freeV8TempObejctOnOneFrameBefore();
    // 瑙丳latformEventHandler::fireMouseEvent锛岄噷闈篃澶勭悊浜嗚缃劍鐐广€傚洜涓鸿繖閲屾湁闃查噸鍏ユ満鍒?
    m_webViewImpl->setFocus(true);
    m_webViewImpl->setIsActive(true);
}

void WebPageImpl::fireKillFocusEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CHECK_FOR_REENTER(this, (void)0);
    freeV8TempObejctOnOneFrameBefore();

    HWND currentFocus = ::GetFocus();
    if (currentFocus == m_popupHandle)
        return;

    if (m_popup)
        m_popup->hide();

//     m_webViewImpl->setFocus(false);
//     m_webViewImpl->setFocusedFrame(nullptr);
//     m_webViewImpl->clearFocusedElement();
    m_popupHandle = nullptr;
}

void WebPageImpl::fireTouchEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    freeV8TempObejctOnOneFrameBefore();

    m_platformEventHandler->fireTouchEvent(hWnd, message, wParam, lParam);
}

LRESULT WebPageImpl::fireMouseEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, BOOL* bHandle)
{
    TemporaryChange<bool> autoSet(m_runningInMouseMessage, true);
    CHECK_FOR_REENTER(this, 0);
    freeV8TempObejctOnOneFrameBefore();
    AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);
    blink::UserGestureIndicator gestureIndicator(blink::DefinitelyProcessingUserGesture);

    if (blink::RuntimeEnabledFeatures::touchEnabled())
        fireTouchEvent(hWnd, message, wParam, lParam);

    PlatformEventHandler::MouseEvtInfo info = { true, pageDestroying == m_state, m_draggableRegion };
    if (g_isMouseEnabled) {
        if (m_isDragging) {
            handleMouseWhenDraging(message);
        } else
            m_platformEventHandler->fireMouseEvent(hWnd, message, wParam, lParam, info, bHandle);
    }
    return 0;
}

void WebPageImpl::handleMouseWhenDraging(UINT message)
{
    // Controlled OLE callbacks own UI COM data; legacy engine simulation must
    // never evaluate getDragData() or select its synthetic drag cursors.
    if (wke::brokerOwnsView(m_pagePtr->wkeWebView()))
        return;
    POINT screenPoint = { 0 };
    ::GetCursorPos(&screenPoint);

    POINT clientPoint = screenPoint;
    ::ScreenToClient(m_hWnd, &clientPoint);

    POINTL pt = { screenPoint.x, screenPoint.y };
    DWORD pdwEffect;

    if (WM_MOUSEMOVE == message) {
        if (!m_isFirstEnterDrag) {
            m_dragHandle->DragEnter(m_dragHandle->getDragData(), 0, pt, &pdwEffect);
            m_isFirstEnterDrag = true;
        } else
            m_dragHandle->DragOver(0, pt, &pdwEffect);

        blink::WebDragOperation op = DragHandle::dragCursorTodragOperation(pdwEffect);
        blink::WebCursorInfo cursor;
        cursor.type = blink::WebCursorInfo::TypeNoDrop;
        if (blink::WebDragOperationNone != op)
            cursor.type = blink::WebCursorInfo::TypeHand;
        didChangeCursor(cursor);
    } else if (WM_LBUTTONUP == message) {
        m_isFirstEnterDrag = false;
        m_dragHandle->Drop(m_dragHandle->getDragData(), 0, pt, &pdwEffect);
        m_dragHandle->DragLeave();
    }
}

void WebPageImpl::onEnterDragSimulate()
{
    m_isDragging = true;
}

void WebPageImpl::onLeaveDragSimulate()
{
    m_isFirstEnterDrag = false;
    m_isDragging = false;
}

void WebPageImpl::onDraggingSimulate()
{
    traceSourceFrame("drag", this);
    beginMainFrame();

    content::WebThreadImpl* threadImpl = (content::WebThreadImpl*)(blink::Platform::current()->currentThread());
    threadImpl->fire();
}

static void initWkeWebDragDataItem(wkeWebDragData::Item* item)
{
    item->storageType = wkeWebDragData::Item::StorageTypeString;
    item->stringType = nullptr; // wkeCreateStringW(L"", 0);
    item->stringData = nullptr; // wkeCreateStringW(L"", 0);
    item->filenameData = nullptr; // wkeCreateStringW(L"", 0);
    item->displayNameData = nullptr; // wkeCreateStringW(L"", 0);
    item->binaryData = nullptr;
    item->title = nullptr; // wkeCreateStringW(L"", 0);
    item->fileSystemURL = nullptr; // wkeCreateStringW(L"", 0);
    item->fileSystemFileSize = 0;
    item->baseURL = nullptr; // wkeCreateStringW(L"", 0);
}

static void freeUtf8String(wkeMemBuf* str)
{
    if (!str)
        return;
    wkeFreeMemBuf(str);
}

static void destroyWkeDragData(wkeWebDragData* data)
{
    wkeWebDragData::Item* items = data->m_itemList;
    for (int i = 0; i < data->m_itemListLength; ++i) {
        wkeWebDragData::Item* it = items + i;

        if (wkeWebDragData::Item::StorageTypeFilename == it->storageType) {
            freeUtf8String(it->filenameData);
        } else if (wkeWebDragData::Item::StorageTypeFileSystemFile == it->storageType) {
            freeUtf8String(it->fileSystemURL);
        } else if (wkeWebDragData::Item::StorageTypeString == it->storageType) {
            freeUtf8String(it->stringType);
            freeUtf8String(it->stringData);
        }
    }
    delete[] items;
    delete data;
}

static wkeMemBuf* createUtf8String(const char* str, size_t len)
{
    if (!str)
        return nullptr;
    return wkeCreateMemBuf(nullptr, (void*)str, len);
}

static wkeWebDragData* webDropDataToWkeDragData(const blink::WebDragData& data, bool bounded)
{
    blink::WebVector<blink::WebDragData::Item> items = data.items();
    const size_t size = items.size();
    if (bounded && size > 256)
        return nullptr;
    if (size > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::length_error("drag item count exceeds int");

    std::unique_ptr<wkeWebDragData, decltype(&destroyWkeDragData)> result(new wkeWebDragData(), &destroyWkeDragData);
    result->m_filesystemId = nullptr;
    result->m_modifierKeyState = 0;
    result->m_itemList = nullptr;
    result->m_itemListLength = 0;

    result->m_itemList = new wkeWebDragData::Item[size];
    for (size_t i = 0; i < size; ++i)
        initWkeWebDragDataItem(&result->m_itemList[i]);
    result->m_itemListLength = static_cast<int>(size);
    for (size_t i = 0; i < size; i++) {
        blink::WebDragData::Item& item = items[i];

        if (blink::WebDragData::Item::StorageTypeFilename == item.storageType) {
            result->m_itemList[i].storageType = wkeWebDragData::Item::StorageTypeFilename;

            std::string fileName = item.filenameData.utf8();
            result->m_itemList[i].filenameData = createUtf8String(fileName.c_str(), fileName.size());

        } else if (blink::WebDragData::Item::StorageTypeFileSystemFile == item.storageType) {
            result->m_itemList[i].storageType = wkeWebDragData::Item::StorageTypeFileSystemFile;

            blink::KURL fileSystemURL = item.fileSystemURL;
            String fileSystemURLString = fileSystemURL.getUTF8String();
            result->m_itemList[i].fileSystemURL = createUtf8String((const utf8*)fileSystemURLString.characters8(), fileSystemURLString.length());
        } else if (blink::WebDragData::Item::StorageTypeString == item.storageType) {
            result->m_itemList[i].storageType = wkeWebDragData::Item::StorageTypeString;

            std::string stringType = item.stringType.utf8();
            result->m_itemList[i].stringType = createUtf8String(stringType.c_str(), stringType.size());
            std::string stringData = item.stringData.utf8();
            result->m_itemList[i].stringData = createUtf8String(stringData.c_str(), stringData.size());
        } else if (blink::WebDragData::Item::StorageTypeBinaryData == item.storageType) {
            result->m_itemList[i].storageType = wkeWebDragData::Item::StorageTypeBinaryData;
        }
    }

    return result.release();
}

class DragSimulateScope {
public:
    explicit DragSimulateScope(WebPageImpl* page) : m_page(page)
    {
        m_page->onEnterDragSimulate();
        CheckReEnter::decrementEnterCount();
    }

    ~DragSimulateScope()
    {
        CheckReEnter::incrementEnterCount();
        m_page->onLeaveDragSimulate();
    }

    DragSimulateScope(const DragSimulateScope&) = delete;
    DragSimulateScope& operator=(const DragSimulateScope&) = delete;

private:
    WebPageImpl* m_page;
};

void WebPageImpl::startDragging(blink::WebLocalFrame* frame, const blink::WebDragData& data,
    blink::WebDragOperationsMask mask, const blink::WebImage& image, const blink::WebPoint& dragImageOffset)
{
    BlinkPlatformImpl::AutoDisableGC autoDisableGC;

    wkeStartDraggingCallback callback = m_pagePtr->wkeHandler().startDraggingCallback;
    const bool bounded = !callback && wke::brokerOwnsView(m_pagePtr->wkeWebView());
    std::unique_ptr<wkeWebDragData, decltype(&destroyWkeDragData)> dragDate(webDropDataToWkeDragData(data, bounded), &destroyWkeDragData);
    if (!callback) {
        m_dragHandle->startDragging(frame, dragDate.get(), mask, image, dragImageOffset);
        return;
    }

    void* param = m_pagePtr->wkeHandler().startDraggingCallbackParam;
    wkePoint offset = { dragImageOffset.x, dragImageOffset.y };

    DragSimulateScope dragScope(this);
    callback(m_pagePtr->wkeWebView(), param,
        wke::CWebView::frameIdTowkeWebFrameHandle(m_pagePtr, getFrameIdByBlinkFrame(frame)),
        dragDate.get(), (wkeWebDragOperationsMask)mask, nullptr, &offset);
}

void WebPageImpl::loadHistoryItem(int64 frameId, const WebHistoryItem& item, WebHistoryLoadType type, WebURLRequest::CachePolicy policy)
{
    WebFrame* webFrame = getWebFrameFromFrameId(frameId);
    if (!webFrame)
        return;

    //AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);
    webFrame->loadHistoryItem(item, type, policy);
    m_webViewImpl->setFocus(true);
}

void WebPageImpl::loadURL(int64 frameId, const wchar_t* url, const blink::Referrer& referrer, const wchar_t* extraHeaders)
{
    int length = wcslen(url);
    String urlW((const UChar*)url, length);
    blink::KURL kurl(ParsedURLString, urlW.utf8().data());

    blink::WebURL webURL = kurl;
    blink::WebURLRequest request(webURL);
    request.setHTTPReferrer(referrer.referrer, blink::WebReferrerPolicyOrigin);
    loadRequest(frameId, request);
}

void WebPageImpl::loadRequest(int64 frameId, const blink::WebURLRequest& request)
{
    if (!m_webViewImpl || !m_webViewImpl->mainFrame())
        return;

    blink::WebURLRequest requestWrap(request);
    WebFrame* webFrame = getWebFrameFromFrameId(frameId);
    if (!webFrame)
        return;

    //AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);
    
    requestWrap.setHTTPHeaderField(WebString::fromLatin1("Accept"), WebString::fromLatin1("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"));
    webFrame->loadRequest(requestWrap);
    m_webViewImpl->setFocus(true);
}

void WebPageImpl::loadHTMLString(int64 frameId, const WebData& html, const WebURL& baseURL, const WebURL& unreachableURL, bool replace)
{
    if (!m_webViewImpl || !m_webViewImpl->mainFrame())
        return;

    WebFrame* webFrame = getWebFrameFromFrameId(frameId);
    if (!webFrame)
        return;

    //AutoRecordActions autoRecordActions(this, m_layerTreeHost, false);
    webFrame->loadHTMLString(html, baseURL, unreachableURL, replace);
    m_webViewImpl->setFocus(true);
}

void WebPageImpl::setBackgroundColor(COLORREF c)
{
    m_bdColor = c;

    if (m_layerTreeHost)
        m_layerTreeHost->setBackgroundColor(cc::getRealColor(false, c));
    if (m_webViewImpl)
        m_webViewImpl->setBaseBackgroundColor(cc::getRealColor(false, c));
}

void WebPageImpl::setHwndRenderOffset(const blink::IntPoint& offset)
{
    m_hwndRenderOffset = offset;
    m_platformEventHandler->setHwndRenderOffset(offset);
}

blink::IntPoint WebPageImpl::getHwndRenderOffset() const
{
    return m_platformEventHandler->getHwndRenderOffset();
}

void WebPageImpl::setTransparent(bool transparent)
{
    m_layerTreeHost->setHasTransparentBackground(transparent);
    m_layerTreeHost->setBackgroundColor(cc::getRealColor(transparent, cc::s_kBgColor));
    m_webViewImpl->setIsTransparent(transparent);
    m_webViewImpl->setBaseBackgroundColor(cc::getRealColor(transparent, cc::s_kBgColor));
}

struct RegisterDragDropTask {
    RegisterDragDropTask(int id, HWND hWnd, DragHandle* dragHandle)
    {
        m_id = id;
        m_hWnd = hWnd;
        m_dragHandle = dragHandle;
    }

    static void WKE_CALL_TYPE registerDragDropInUiThread(HWND hWnd, void* param)
    {
        ::OleInitialize(nullptr);

        RegisterDragDropTask* self = (RegisterDragDropTask*)param;

        do {
            if (!wkeIsWebviewAlive(self->m_id))
                break;
            ::RegisterDragDrop(self->m_hWnd, self->m_dragHandle);
        } while (false);

        delete self;
    }

private:
    int m_id;
    HWND m_hWnd;
    DragHandle* m_dragHandle;
};

class PostTaskWrap {
public:
    PostTaskWrap(HWND hWnd, wkeUiThreadRunCallback callback, void* param)
    {
        m_hWnd = hWnd;
        m_callback = callback;
        m_param = param;
    }

    static void init()
    {
        if (wke::g_wkeUiThreadPostTaskCallback)
            return;
        wke::g_wkeUiThreadPostTaskCallback = PostTaskWrap::uiThreadPostTaskCallback;

        m_uiPostTasks = new std::vector<PostTaskWrap*>();
        ::InitializeCriticalSection(&m_uiPostTasksMutex);
    }

    WTF::String getProp() const { return m_prop; }

    static void WINAPI timerProc(HWND hWnd, UINT message, UINT_PTR iTimerID, DWORD dwTime)
    {
        ::KillTimer(hWnd, kPostTaskTimerId);
        
        ::EnterCriticalSection(&m_uiPostTasksMutex);
        std::vector<PostTaskWrap*>* tasksToRun = new std::vector<PostTaskWrap*>();
        std::vector<PostTaskWrap*>* tasksToSave = new std::vector<PostTaskWrap*>();

        for (size_t i = 0; i < m_uiPostTasks->size(); ++i) {
            PostTaskWrap* task = m_uiPostTasks->at(i);
            if (task->m_hWnd == hWnd)
                tasksToRun->push_back(task);
            else
                tasksToSave->push_back(task);
        }
        delete m_uiPostTasks;
        m_uiPostTasks = tasksToSave;
        ::LeaveCriticalSection(&m_uiPostTasksMutex);

        for (size_t i = 0; i < tasksToRun->size(); ++i) {
            PostTaskWrap* task = tasksToRun->at(i);
            task->m_callback(task->m_hWnd, task->m_param);
            delete task;
        }
    }

    static int WKE_CALL_TYPE uiThreadPostTaskCallback(HWND hWnd, wkeUiThreadRunCallback callback, void* param)
    {
        PostTaskWrap* task = new PostTaskWrap(hWnd, callback, param);

        ::EnterCriticalSection(&m_uiPostTasksMutex);
        m_uiPostTasks->push_back(task);
        ::LeaveCriticalSection(&m_uiPostTasksMutex);

        ::SetTimer(hWnd, kPostTaskTimerId, 10, &timerProc);
        return 1;
    }

private:
    HWND m_hWnd;
    wkeUiThreadRunCallback m_callback;
    void* m_param;
    WTF::String m_prop;

    static const int kPostTaskTimerId = 0x15324546;
    static std::vector<PostTaskWrap*>* m_uiPostTasks;
    static CRITICAL_SECTION m_uiPostTasksMutex;
};
std::vector<PostTaskWrap*>* PostTaskWrap::m_uiPostTasks = nullptr;
CRITICAL_SECTION PostTaskWrap::m_uiPostTasksMutex;

void WebPageImpl::setHWND(HWND hWnd)
{
    m_hWnd = hWnd;
    if (!m_hWnd)
        return;

    // The first screenInfo() call normally happened while m_hWnd was null
    // during WebViewImpl construction. Refresh it now and push the value into
    // the page/compositor before the first real resize/paint.
    if (m_webViewImpl) {
        const blink::WebScreenInfo current = screenInfo();
        m_webViewImpl->setDeviceScaleFactor(current.deviceScaleFactor);
    }

    DWORD processID;
    DWORD threadID;
    threadID = ::GetWindowThreadProcessId(hWnd, &processID);
    if (threadID != ::GetCurrentThreadId())
        PostTaskWrap::init();
    
    const ::wkeWebView ownedView = m_pagePtr->wkeWebView();
    if (wke::brokerOwnsView(ownedView)) {
        m_dragHandle->setViewWindow(m_hWnd, m_webViewImpl, ownedView);
        auto oleInitialized = std::make_shared<std::atomic<bool>>(false);
        // Every registered OLE target is revoked on the actual UI owner before
        // any engine object or HWND may disappear. Failed receipts stay failed.
        wke::brokerAddCloseAction(ownedView, [ownedView, hWnd, oleInitialized] {
            if (!wke::brokerPostUiTask(ownedView, WKE_CURSOR_BROKER_NATIVE_OLE_REVOKE,
                [ownedView, hWnd, oleInitialized](bool cancelled) {
                    if (cancelled) { wke::brokerRecordFailure(ownedView); return; }
                    wke::BrokerNativeScope scope(ownedView, WKE_CURSOR_BROKER_NATIVE_OLE_REVOKE);
                    if (!scope.entered()) { wke::brokerRecordFailure(ownedView); return; }
                    const HRESULT result = ::RevokeDragDrop(hWnd);
                    if (SUCCEEDED(result) || result == DRAGDROP_E_NOTREGISTERED) {
                        wke::brokerSetOleRegistered(ownedView, false);
                        if (oleInitialized->exchange(false))
                            ::OleUninitialize();
                    } else
                        wke::brokerRecordFailure(ownedView);
                }, true))
                wke::brokerRecordFailure(ownedView);
        });
        if (wke::g_isSetDragDropEnable) {
            DragHandle* dragHandle = m_dragHandle;
            if (!wke::brokerPostUiTask(ownedView, WKE_CURSOR_BROKER_NATIVE_OLE_REGISTER,
                [ownedView, hWnd, dragHandle, oleInitialized](bool cancelled) {
                    if (cancelled) return;
                    wke::BrokerNativeScope scope(ownedView, WKE_CURSOR_BROKER_NATIVE_OLE_REGISTER);
                    if (!scope.entered()) { wke::brokerRecordFailure(ownedView); return; }
                    const HRESULT initialized = ::OleInitialize(nullptr);
                    if (FAILED(initialized)) { wke::brokerRecordFailure(ownedView); return; }
                    const HRESULT result = ::RegisterDragDrop(hWnd, dragHandle);
                    if (SUCCEEDED(result)) {
                        oleInitialized->store(true);
                        wke::brokerSetOleRegistered(ownedView, true);
                    }
                    else {
                        ::OleUninitialize();
                        wke::brokerRecordFailure(ownedView);
                    }
                }))
                wke::brokerRecordFailure(ownedView);
        }
        return;
    }
    if (wke::g_isSetDragDropEnable) {
        if (wke::g_wkeUiThreadPostTaskCallback) {
            m_dragHandle->setViewWindow(m_hWnd, m_webViewImpl);
            wke::g_wkeUiThreadPostTaskCallback(m_hWnd, RegisterDragDropTask::registerDragDropInUiThread, new RegisterDragDropTask(m_pagePtr->wkeWebView()->getId(), m_hWnd, m_dragHandle));
        } else if (!blink::RuntimeEnabledFeatures::updataInOtherThreadEnabled()) {
            m_dragHandle->setViewWindow(m_hWnd, m_webViewImpl);
            ::RegisterDragDrop(m_hWnd, m_dragHandle);
        }
    }
}

WebPageImpl* WebPageImpl::getSelfForCurrentContext()
{
    blink::WebLocalFrame* frame = blink::WebLocalFrame::frameForCurrentContext();
    if (!frame)
        return nullptr;
    blink::WebViewImpl* impl = (blink::WebViewImpl*)frame->view();
    if (!impl)
        return nullptr;

    content::WebPageImpl* page = (content::WebPageImpl*)impl->client();
    return page;
}

void WebPageImpl::didCommitProvisionalLoad(blink::WebLocalFrame* frame, const blink::WebHistoryItem& history, 
    blink::WebHistoryCommitType type, bool isSameDocument)
{
    m_navigationController->insertOrReplaceEntry(history, type, isSameDocument);
}

void WebPageImpl::navigateBackForwardSoon(int offset)
{
    m_navigationController->navigateBackForwardSoon(offset);
}

int WebPageImpl::historyBackListCount()
{
    return m_navigationController->historyBackListCount();
}

int WebPageImpl::historyForwardListCount()
{
    return m_navigationController->historyForwardListCount();
}

void WebPageImpl::navigateToIndex(int index)
{
    m_navigationController->navigateToIndex(index);
}

WebFrame* WebPageImpl::getWebFrameFromFrameId(int64_t frameId)
{
    blink::WebFrame* webFrame = nullptr;
    if (WebPage::kMainFrameId != frameId) {
        Frame* frame = toCoreFrame(m_webViewImpl->mainFrame());

        while (frame && frame->frameID() != frameId)
            frame = frame->tree().traverseNext();
        if (!frame)
            return nullptr;

        webFrame = WebFrame::fromFrame(frame);
    } else
        webFrame = m_webViewImpl->mainFrame();

    return webFrame;
}

int64_t WebPageImpl::getFrameIdByBlinkFrame(const blink::WebFrame* frame)
{
    if (!frame)
        return content::WebPage::kInvalidFrameId;

    blink::Frame* blinkFrame = blink::toCoreFrame(frame);
    if (!blinkFrame)
        return content::WebPage::kInvalidFrameId;
    return blinkFrame->frameID();
}

int64_t WebPageImpl::getFirstFrameId()
{
    return m_firstFrameId;
}

WebStorageNamespace* WebPageImpl::createSessionStorageNamespace()
{
    return ((content::BlinkPlatformImpl*)Platform::current())->createSessionStorageNamespace();
}

WebString WebPageImpl::acceptLanguages()
{
    if (m_webViewImpl) {
        blink::Page *page = m_webViewImpl->page();
        if (page) {
            blink::Settings &setings = page->settings();
            return setings.language();
        }
    }
    return WebString::fromUTF8("zh-CN,cn");
}

void WebPageImpl::setScreenInfo(const WebScreenInfo& info)
{
    if (m_screenInfo)
        delete m_screenInfo;
    m_screenInfo = new WebScreenInfo();
    *m_screenInfo = info;
    refreshDeviceScaleFactor(m_screenInfo, m_hWnd);
}

WebScreenInfo WebPageImpl::screenInfo()
{
    if (m_screenInfo) {
        refreshDeviceScaleFactor(m_screenInfo, m_hWnd);
        return *m_screenInfo;
    }
    POINT pt = { 0, 0 };
    HMONITOR hMonitor = ::MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi = { 0 };
    mi.cbSize = sizeof(MONITORINFO);
    ::GetMonitorInfo(hMonitor, &mi);

    m_screenInfo = new blink::WebScreenInfo();
    m_screenInfo->rect = WebRect(winRectToIntRect(mi.rcMonitor));
    m_screenInfo->availableRect = WebRect(winRectToIntRect(mi.rcWork));
    // The old embedder initialized the process-wide DPI helper but never
    // copied it into WebScreenInfo. Miniblink then painted a 1x backing store
    // which Windows bitmap-scaled on high-DPI displays, making text and SVG
    // edges visibly soft. Keep the actual child-window monitor scale in the
    // WebView's screen contract so Blink, Skia fonts and the compositor use
    // the same device scale (including monitor changes during the session).
    refreshDeviceScaleFactor(m_screenInfo, m_hWnd);

    return *m_screenInfo;
}

void WebPageImpl::setMouseOverURL(const blink::WebURL& url)
{
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    wke::CString string(((KURL)url).string());
    if (m_pagePtr->wkeHandler().mouseOverUrlChangedCallback) {
        m_pagePtr->wkeHandler().mouseOverUrlChangedCallback(
            m_pagePtr->wkeWebView(), m_pagePtr->wkeHandler().mouseOverUrlChangedCallbackParam, &string);
    }
#endif
}

void WebPageImpl::setToolTipText(const blink::WebString& toolTip, blink::WebTextDirection hint)
{
    m_toolTip->show(WTF::ensureUTF16UChar((String)toolTip, true).data(), nullptr);
}

void WebPageImpl::onMouseDown(const blink::WebNode& mouseDownNode)
{
    if (mouseDownNode.isDraggable())
        m_platformEventHandler->setIsDraggableNodeMousedown();
}

void WebPageImpl::printPage(blink::WebLocalFrame* frame)
{
    wkeWebFrameHandle handle = wke::CWebView::frameIdTowkeWebFrameHandle(m_pagePtr, getFrameIdByBlinkFrame(frame));

    if (m_pagePtr->wkeHandler().printCallback)
        m_pagePtr->wkeHandler().printCallback(m_pagePtr->wkeWebView(), m_pagePtr->wkeHandler().printCallbackParam, handle, nullptr);
}

void WebPageImpl::draggableRegionsChanged()
{
    WebFrame* frame = m_webViewImpl->mainFrame();
    if (!frame)
        return;
    blink::WebDocument doc = frame->document();
    blink::WebVector<blink::WebDraggableRegion> regions = doc.draggableRegions();
    ::SetRectRgn(m_draggableRegion, 0, 0, 0, 0);

    wkeDraggableRegion* wkeRegions = nullptr;
    size_t size = regions.size();
    if (0 != size)
        wkeRegions = new wkeDraggableRegion[size];

    for (size_t i = 0; i < size; ++i) {
        blink::IntRect r = regions[i].bounds;

        wkeRegions[i].bounds.left = r.x();
        wkeRegions[i].bounds.top = r.y();
        wkeRegions[i].bounds.right = r.maxX();
        wkeRegions[i].bounds.bottom = r.maxY();
        wkeRegions[i].draggable = regions[i].draggable;

        HRGN region = ::CreateRectRgn(wkeRegions[i].bounds.left, wkeRegions[i].bounds.top, wkeRegions[i].bounds.right, wkeRegions[i].bounds.bottom);
        ::CombineRgn(m_draggableRegion, m_draggableRegion, region,
            wkeRegions[i].draggable ? RGN_OR : RGN_DIFF);
        ::DeleteObject(region);
    }

#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    if (m_pagePtr->wkeHandler().draggableRegionsChangedCallback) {
        m_pagePtr->wkeHandler().draggableRegionsChangedCallback(m_pagePtr->wkeWebView(),
            m_pagePtr->wkeHandler().draggableRegionsChangedCallbackParam, wkeRegions, regions.size());
    }
#endif
    if (wkeRegions)
        delete[] wkeRegions;
}

WebWidget* WebPageImpl::createPopupMenu(WebPopupType type)
{
    wke::diagnostics::observe_renderer_factory(WKE_RENDER_MODEL_FACTORY_EXTERNAL_VIEW);
    if (!m_hWnd)
        m_hWnd = ::GetActiveWindow();
    
    PopupMenuWin* popup = nullptr;
    blink::WebWidget* result = PopupMenuWin::create(this, m_hWnd, m_hwndRenderOffset, m_webViewImpl, type, &popup);
    m_popup = popup;
    m_popupHandle = popup->popupHandle();
    return result;
}

void WebPageImpl::onPopupMenuCreate(HWND hWnd)
{
    m_popupHandle = hWnd;
}

void WebPageImpl::onPopupMenuHide()
{
    //m_popup = nullptr;
}

void WebPageImpl::showValidationMessage(
    const blink::WebRect& anchorInViewport,
    const blink::WebString& mainText,
    blink::WebTextDirection mainTextDir,
    const blink::WebString& supplementalText,
    blink::WebTextDirection supplementalTextDir
    )
{
    wke::diagnostics::observe_renderer_factory(WKE_RENDER_MODEL_FACTORY_EXTERNAL_VIEW);
    POINT pos = { anchorInViewport.x, anchorInViewport.y };
    ::ClientToScreen(m_hWnd, &pos);
    m_validationMessageTip->show(WTF::ensureUTF16UChar((String)mainText, true).data(), &pos);
}

void WebPageImpl::hideValidationMessage()
{

}

void WebPageImpl::moveValidationMessage(const blink::WebRect& anchorInViewport)
{

}

void WebPageImpl::didStartProvisionalLoad()
{
    m_firstDrawCount = 0;
}

class RootWndAutoDisable {
public:
    RootWndAutoDisable(HWND hWnd)
    {
        HWND hTempWnd = hWnd;
        m_hRootWnd = NULL;
        while (hTempWnd) {
            m_hRootWnd = hTempWnd;
            hTempWnd = ::GetParent(hTempWnd);
        }
        if (m_hRootWnd)
            ::EnableWindow(m_hRootWnd, FALSE);
    }

    ~RootWndAutoDisable()
    {
        if (m_hRootWnd)
            ::EnableWindow(m_hRootWnd, TRUE);
    }

private:
    HWND m_hRootWnd;
};

class DelayPopupAterFileChooserTask : public blink::WebThread::Task {
public:
    DelayPopupAterFileChooserTask(HWND hWnd) { m_hWnd = hWnd; }
    virtual ~DelayPopupAterFileChooserTask() { }
    virtual void run() override { ::SetForegroundWindow(m_hWnd); }
private:
    HWND m_hWnd;
};

bool WebPageImpl::runFileChooser(const blink::WebFileChooserParams& params, blink::WebFileChooserCompletion* completion)
{
    wke::diagnostics::observe_renderer_factory(WKE_RENDER_MODEL_FACTORY_EXTERNAL_VIEW);
    RootWndAutoDisable rootWndAutoDisable(m_hWnd);
    bool b = runFileChooserImpl(params, completion);
    //wke::g_wkeUiThreadPostTaskCallback(m_hWnd, onDelayPopupAterFileChooser, nullptr);
    blink::Platform::current()->currentThread()->postDelayedTask(FROM_HERE, new DelayPopupAterFileChooserTask(m_hWnd), 1000);
    return b;
}

void WebPageImpl::willEnterDebugLoop()
{
    if (m_devToolsAgent)
        m_isEnterDebugLoop = true;

    if (m_devToolsClient)
        m_webViewImpl->setIgnoreInputEvents(false);
}

void WebPageImpl::didExitDebugLoop()
{
    if (m_devToolsAgent)
        m_isEnterDebugLoop = false;

    if (m_devToolsClient)
        m_webViewImpl->setIgnoreInputEvents(true);
}

bool WebPageImpl::initSetting()
{
    WebSettingsImpl* settings = m_webViewImpl->settingsImpl();
    if (!settings)
        return false;
    settings->setTextAreasAreResizable(true);
    
    //settings->setStandardFontFamily(WebString(L"寰蒋闆呴粦", 4));
    settings->setStandardFontFamily(blink::WebString(L"\u5B8B\u4F53", 2));
    settings->setUsesEncodingDetector(true);
    settings->setJavaScriptEnabled(true);
    settings->setAllowFileAccessFromFileURLs(true);
    settings->setAcceleratedCompositingEnabled(true);
    settings->setPreferCompositingToLCDTextEnabled(true);
    settings->setUseSolidColorScrollbars(false);
    settings->setPinchOverlayScrollbarThickness(8);
    //settings->setSpatialNavigationEnabled(true);
    settings->setLocalStorageEnabled(true);
    settings->setMinimumFontSize(10);
    settings->setMinimumLogicalFontSize(10);
    settings->setDefaultFontSize(16);
    settings->setDefaultFixedFontSize(16);
    settings->setLoadsImagesAutomatically(true);
    settings->setPluginsEnabled(!wke::brokerActive());
    settings->setJavaScriptCanOpenWindowsAutomatically(true);
    settings->setJavaScriptCanAccessClipboard(true);
    settings->setPrimaryPointerType(blink::WebSettings::PointerTypeFine);
	settings->setAllowScriptsToCloseWindows(true);

    PluginDatabase::installedPlugins()->refresh();

    return true;
}

} // blink
