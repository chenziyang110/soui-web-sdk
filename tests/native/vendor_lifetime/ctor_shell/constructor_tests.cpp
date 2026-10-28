// Finite source-body regression only. Engine declarations below are support
// seams; no Blink GC, compositor, broker ABI, or vendor shutdown is executed.
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <new>
#include <exception>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include "net/ActivatingObjCheck.h"

enum class Fault { none, activation, handler, engine, liveInsert };
enum class AllocationStage { outside, activation, shell };
static Fault fault = Fault::none;
static AllocationStage allocationStage = AllocationStage::outside;
static int injected = 0, shellAllocations = 0, shellFrees = 0;
static int handlerAllocations = 0, handlerFrees = 0, handlerLive = 0;
static int registryAdds = 0, registryRemoves = 0, engineEntries = 0;
static int closeCalls = 0, pageDataLive = 0, brokerEntries = 0;
static int viewCreatedObservations = 0, viewDestroyedObservations = 0;
static bool viewObservationOrderInvalid = false;
static bool engineReturned = false;
static intptr_t expectedId = 0;
static const char* childSite = nullptr;

namespace content { class WebPage; class WebPageImpl {}; }
namespace wke {
// Declaration support only: callback signature/layout is not ABI qualification.
struct CWebViewHandler {
    void (*alertBoxCallback)();
    void (*confirmBoxCallback)();
    void (*promptBoxCallback)();
    bool isWke;
};
struct StringSupport { StringSupport(const char*, int) noexcept {} };
class CWebView {
public:
    CWebView(COLORREF);
    ~CWebView();
    static CWebView* createForCursorBroker(COLORREF);
    void _initPage(COLORREF);
    void _initHandler();
    void _initMemoryDC();
    HWND m_hWnd;
    bool m_transparent;
    int m_width, m_height;
    content::WebPage* m_webPage;
    float m_zoomFactor;
    bool m_awake;
    StringSupport m_title, m_cookie, m_name, m_url;
    bool m_isCokieEnabled, m_isCreatedDevTools;
    int m_id;
    struct { unsigned size; } m_settings;
};
static std::set<CWebView*> g_liveWebViews;
static bool brokerNoteViewCreated() noexcept {
    ++brokerEntries;
    allocationStage = AllocationStage::activation;
    return true; // Permission fixture; actual broker permission is not tested.
}
static bool brokerOwnedFactoryPermission() noexcept { return true; }
static void defaultRunAlertBox() {}
static void defaultRunConfirmBox() {}
static void defaultRunPromptBox() {}
}

struct alignas(std::max_align_t) AllocationHeader { bool handler; };
void* operator new(std::size_t size) {
    const bool isHandler = allocationStage == AllocationStage::shell
        && size == sizeof(wke::CWebViewHandler);
    const bool trigger = (fault == Fault::activation && allocationStage == AllocationStage::activation)
        || (fault == Fault::handler && isHandler)
        || (fault == Fault::liveInsert && engineReturned);
    if (trigger) { fault = Fault::none; ++injected; throw std::bad_alloc(); }
    if (size > static_cast<std::size_t>(-1) - sizeof(AllocationHeader)) throw std::bad_alloc();
    void* raw = std::malloc(sizeof(AllocationHeader) + (size ? size : 1));
    if (!raw) throw std::bad_alloc();
    auto* header = new (raw) AllocationHeader{isHandler};
    if (isHandler) { ++handlerAllocations; ++handlerLive; }
    return header + 1;
}
void operator delete(void* pointer) noexcept {
    if (!pointer) return;
    auto* header = static_cast<AllocationHeader*>(pointer) - 1;
    if (header->handler) { ++handlerFrees; --handlerLive; }
    std::free(header);
}
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

static void* successfulFastAllocationSupport(std::size_t size) {
    // Deliberately not a claim that actual PartitionAlloc OOM is recoverable.
    void* memory = ::HeapAlloc(::GetProcessHeap(), 0, size ? size : 1);
    if (!memory) std::abort();
    return memory;
}
namespace net {
class PageNetExtraData {
public:
    PageNetExtraData() noexcept { ++pageDataLive; }
    ~PageNetExtraData() noexcept { --pageDataLive; }
    static void* operator new(std::size_t size) { return successfulFastAllocationSupport(size); }
    static void operator delete(void* p) noexcept { ::HeapFree(::GetProcessHeap(), 0, p); }
};
}
namespace WTF {
template<class T> using RefPtr = std::unique_ptr<T>; // Declaration/lifetime support; not actual WTF/GC.
template<class T> class HashSet {
public:
    std::set<T> values;
    void add(T value) { values.insert(value); ++registryAdds; }
    void remove(T value) { values.erase(value); ++registryRemoves; }
    static void* operator new(std::size_t size) { return successfulFastAllocationSupport(size); }
    static void operator delete(void* p) noexcept { ::HeapFree(::GetProcessHeap(), 0, p); }
};
}
template<class T> std::unique_ptr<T> adoptRef(T* pointer) { return std::unique_ptr<T>(pointer); }
namespace content {
class WebPage {
public:
    explicit WebPage(void*);
    ~WebPage();
    static WTF::HashSet<WebPage*>* m_webPageSet;
    WebPageImpl* m_pageImpl;
    WTF::RefPtr<net::PageNetExtraData> m_pageNetExtraData;
    wke::CWebView* m_wkeWebView;
    void* m_wkeClientHandler;
    wke::CWebViewHandler* m_wkeHandler;
    bool m_isContextMenuEnable;
    static void* operator new(std::size_t size) {
        void* p = successfulFastAllocationSupport(size);
        ++shellAllocations;
        allocationStage = AllocationStage::shell;
        return p;
    }
    static void operator delete(void* p) noexcept {
        if (p) { ++shellFrees; ::HeapFree(::GetProcessHeap(), 0, p); }
    }
    void initWkeWebView(wke::CWebView* view) noexcept { m_wkeWebView = view; }
    bool init(HWND, COLORREF) {
        // Actual _initPage call executes this declaration support seam.
        ++engineEntries;
        if (fault == Fault::engine) { fault = Fault::none; ++injected; throw std::bad_alloc(); }
        engineReturned = true;
        return true;
    }
    wke::CWebViewHandler& wkeHandler() noexcept { return *m_wkeHandler; }
    void close() noexcept { ++closeCalls; } // No ordinary partial close implemented or qualified.
};
WTF::HashSet<WebPage*>* WebPage::m_webPageSet = nullptr;
#include "extracted/web_page.h"
}
namespace wke {
// Observation support only. The exact source bodies below call these hooks;
// these counters do not query DLL metrics or qualify native resource coverage.
namespace diagnostics {
void view_created() noexcept {
    ++viewCreatedObservations;
    if (g_liveWebViews.size() != 1 || !net::ActivatingObjCheck::inst()->isActivating(expectedId) ||
        !engineReturned || engineEntries != 1 || closeCalls != 0)
        viewObservationOrderInvalid = true;
}
void view_destroyed() noexcept {
    ++viewDestroyedObservations;
    if (!g_liveWebViews.empty() || net::ActivatingObjCheck::inst()->isActivating(expectedId) ||
        closeCalls != 1 || viewCreatedObservations != 1 || viewDestroyedObservations != 1)
        viewObservationOrderInvalid = true;
}
}
#include "extracted/web_view.h"
}

static bool require(bool condition, const char* reason) {
    if (condition) return true;
    std::fprintf(stderr, "FAIL: %s\n", reason);
    return false;
}
static bool emptyPageRegistry() noexcept {
    return !content::WebPage::m_webPageSet || content::WebPage::m_webPageSet->values.empty();
}
static void cleanup() {
    if (content::WebPage::m_webPageSet && content::WebPage::m_webPageSet->values.empty()) {
        delete content::WebPage::m_webPageSet;
        content::WebPage::m_webPageSet = nullptr;
    }
    net::ActivatingObjCheck::inst()->destroy(); // Real source teardown; final call in this process.
}
static void prepare(Fault desired) {
    auto* activating = net::ActivatingObjCheck::inst(); // Prewarm outside injected allocation.
    expectedId = static_cast<intptr_t>(activating->genId()) + 1;
    allocationStage = AllocationStage::outside;
    fault = desired;
}
static void terminated() noexcept {
    const bool valid = childSite && injected == 1 && closeCalls == 0
        && viewCreatedObservations == 0 && viewDestroyedObservations == 0 && !viewObservationOrderInvalid
        && engineEntries == 1 && shellAllocations == 1 && registryAdds == 1
        && handlerLive == 1 && wke::g_liveWebViews.empty()
        && net::ActivatingObjCheck::inst()->isActivating(expectedId);
    if (valid) {
        std::printf("TERMINATE_WITNESS site=%s injected=1 close=0 engine=1 liveViews=0\n", childSite);
        std::fflush(stdout);
        std::_Exit(86);
    }
    std::_Exit(87);
}
static int terminateChild(const char* site) {
    childSite = site;
    prepare(std::strcmp(site, "engine") == 0 ? Fault::engine : Fault::liveInsert);
    std::set_terminate(&terminated);
    try { delete wke::CWebView::createForCursorBroker(0); }
    catch (const std::bad_alloc&) { return 91; } // Escaped as recoverable failure: test must fail.
    catch (...) { return 92; }
    return 93; // std::terminate was never called.
}
static bool runChild(const char* site) {
    WCHAR executable[32768];
    const DWORD count = ::GetModuleFileNameW(nullptr, executable, 32768);
    if (!require(count > 0 && count < 32768, "current executable path")) return false;
    std::wstring command = L"\"" + std::wstring(executable, count) + L"\" --terminate-child ";
    command += std::strcmp(site, "engine") == 0 ? L"engine" : L"live";
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!require(::CreatePipe(&read, &write, &security, 0) != FALSE, "child pipe")) return false;
    ::SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW start{}; start.cb = sizeof(start);
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdOutput = write; start.hStdError = write;
    start.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL created = ::CreateProcessW(executable, &command[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &start, &process);
    ::CloseHandle(write);
    if (!created) { ::CloseHandle(read); return require(false, "child creation"); }
    const DWORD waited = ::WaitForSingleObject(process.hProcess, 5000);
    if (waited != WAIT_OBJECT_0) {
        // The outer test runner/Job owns timeout disposal; do not fake closure.
        ::CloseHandle(read); ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess);
        return require(false, "terminate child did not exit");
    }
    DWORD code = 0; const BOOL gotCode = ::GetExitCodeProcess(process.hProcess, &code);
    char buffer[512]{}; DWORD bytes = 0; ::ReadFile(read, buffer, sizeof(buffer)-1, &bytes, nullptr);
    ::CloseHandle(read); ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess);
    const char* expected = std::strcmp(site, "engine") == 0
        ? "TERMINATE_WITNESS site=engine injected=1 close=0 engine=1 liveViews=0\n"
        : "TERMINATE_WITNESS site=live injected=1 close=0 engine=1 liveViews=0\n";
    // CRT text output on Windows may use CRLF; compare both concrete byte forms.
    std::string output(buffer, bytes), wanted(expected);
    std::string wantedCrLf = wanted.substr(0, wanted.size()-1) + "\r\n";
    return require(gotCode && code == 86 && (output == wanted || output == wantedCrLf), "real terminate witness and exit 86");
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--terminate-child") == 0) return terminateChild(argv[2]);
    if (argc != 2) return 2;
    const char* name = argv[1]; bool ok = true;
    if (std::strcmp(name, "engine-terminate") == 0 || std::strcmp(name, "live-insert-terminate") == 0) {
        ok = runChild(std::strcmp(name, "engine-terminate") == 0 ? "engine" : "live");
        ok &= require(viewCreatedObservations == 0 && viewDestroyedObservations == 0 && !viewObservationOrderInvalid, "terminate parent has no completed view observations");
        if (ok) std::printf("{\"case\":\"%s\",\"actualTerminateCallObserved\":true,\"childExit\":86,\"partialCloseCalled\":false,\"fullEngineQualified\":false}\n", name);
        return ok ? 0 : 1;
    }
    prepare(Fault::none);
    if (std::strcmp(name, "shell-success") == 0) {
        auto* page = new content::WebPage(nullptr);
        ok &= require(registryAdds == 1 && handlerLive == 1 && engineEntries == 0 && page->m_pageImpl == nullptr, "complete uninitialized shell");
        delete page;
        ok &= require(registryRemoves == 1 && handlerFrees == 1 && handlerLive == 0 && pageDataLive == 0 && closeCalls == 0 && emptyPageRegistry(), "shell destructor owns handler without close");
    } else if (std::strcmp(name, "handler-before-publication") == 0) {
        fault = Fault::handler; bool caught = false;
        try { delete new content::WebPage(nullptr); } catch (const std::bad_alloc&) { caught = true; }
        ok &= require(caught && injected == 1 && registryAdds == 0 && handlerLive == 0 && pageDataLive == 0 && engineEntries == 0 && closeCalls == 0 && emptyPageRegistry(), "handler failure before actual constructor publication");
    } else if (std::strcmp(name, "view-success") == 0) {
        auto* view = wke::CWebView::createForCursorBroker(0); auto* page = view->m_webPage;
        ok &= require(net::ActivatingObjCheck::inst()->isActivating(view->m_id) && view->m_id == expectedId && wke::g_liveWebViews.count(view) == 1 && engineEntries == 1 && handlerLive == 1, "successful real constructor registrations");
        ok &= require(viewCreatedObservations == 1 && viewDestroyedObservations == 0 && !viewObservationOrderInvalid, "view creation observation follows live publication before close");
        delete view;
        ok &= require(!net::ActivatingObjCheck::inst()->isActivating(expectedId) && wke::g_liveWebViews.empty() && closeCalls == 1, "successful source destructor unregisters");
        ok &= require(viewCreatedObservations == 1 && viewDestroyedObservations == 1 && !viewObservationOrderInvalid, "view destruction observation follows close and unregister");
        delete page;
        ok &= require(emptyPageRegistry() && handlerLive == 0 && handlerFrees == 1, "fixture shell ownership cleanup");
    } else if (std::strcmp(name, "activation-failure") == 0 || std::strcmp(name, "view-handler-rollback") == 0) {
        fault = std::strcmp(name, "activation-failure") == 0 ? Fault::activation : Fault::handler;
        bool caught = false;
        try { delete wke::CWebView::createForCursorBroker(0); } catch (const std::bad_alloc&) { caught = true; }
        ok &= require(caught && injected == 1 && !net::ActivatingObjCheck::inst()->isActivating(expectedId) && wke::g_liveWebViews.empty() && engineEntries == 0 && closeCalls == 0 && handlerLive == 0 && pageDataLive == 0 && registryAdds == 0, "real activation state rolled back before engine");
        if (std::strcmp(name, "activation-failure") == 0) ok &= require(shellAllocations == 0, "failed actual std::set add never prepared shell");
        else ok &= require(shellAllocations == 1 && shellFrees == 1, "failed shell new frees shell allocation");
        auto* activating = net::ActivatingObjCheck::inst(); const int probe = activating->genId();
        activating->add(probe); ok &= require(activating->isActivating(probe), "actual critical section usable after throw");
        activating->remove(probe); ok &= require(!activating->isActivating(probe), "actual remove and lock reusable");
    } else return 2;
    if (std::strcmp(name, "view-success") != 0)
        ok &= require(viewCreatedObservations == 0 && viewDestroyedObservations == 0 && !viewObservationOrderInvalid, "shell and recoverable construction failures publish no view observations");
    ok &= require(shellAllocations == shellFrees, "complete/failed shell allocation pairing");
    cleanup();
    if (ok) std::printf("{\"case\":\"%s\",\"sourceBodyPassed\":true,\"injected\":%d,\"partialCloseCalled\":false,\"fullEngineQualified\":false}\n", name, injected);
    return ok ? 0 : 1;
}
