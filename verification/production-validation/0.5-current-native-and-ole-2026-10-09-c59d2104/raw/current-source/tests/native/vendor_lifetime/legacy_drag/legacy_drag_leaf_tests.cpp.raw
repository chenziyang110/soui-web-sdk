// Support declarations for the actual captured legacy task/body only.
// The UI consumer is an ordinary-throw seam; no COM, Blink GC or async claim.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <functional>
#include <memory>
#include <new>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#define WKE_CALL_TYPE __cdecl
namespace probe {
bool armTaskAllocation = false;
bool failTaskAllocation = false;
bool failConsumer = false;
size_t taskBytes = 0;
void* taskPointer = nullptr;
int taskAttempts = 0;
int taskAllocs = 0;
int taskFrees = 0;
int enterCalls = 0;
int leaveCalls = 0;
int uiCalls = 0;
int completionCalls = 0;
bool dragging = false;
bool borrowOkay = true;
bool admittedOkay = true;
const void* expectedFrame = nullptr;
const void* expectedData = nullptr;
const void* expectedImage = nullptr;
const void* expectedOffset = nullptr;
int* expectedCount = nullptr;
}
void* operator new(size_t n)
{
    bool task = probe::armTaskAllocation && n == probe::taskBytes;
    if (task) {
        probe::armTaskAllocation = false;
        ++probe::taskAttempts;
        if (probe::failTaskAllocation)
            throw std::bad_alloc();
    }
    void* p = std::malloc(n ? n : 1);
    if (!p)
        throw std::bad_alloc();
    if (task) {
        probe::taskPointer = p;
        ++probe::taskAllocs;
    }
    return p;
}
void operator delete(void* p) noexcept
{
    if (p && p == probe::taskPointer) {
        ++probe::taskFrees;
        probe::taskPointer = nullptr;
    }
    std::free(p);
}
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
namespace blink {
struct WebLocalFrame { int marker = 31; };
struct WebImage { int marker = 41; };
struct WebPoint {
    int x;
    int y;
    WebPoint(int a = 0, int b = 0) : x(a), y(b) { }
};
using WebDragOperationsMask = unsigned int;
enum WebDragOperation { WebDragOperationNone = 0, WebDragOperationCopy = 1 };
struct WebViewImpl {
    void dragSourceEndedAt(const WebPoint&, const WebPoint&, WebDragOperation op)
    {
        probe::borrowOkay = probe::borrowOkay && op == WebDragOperationCopy;
        probe::admittedOkay = probe::admittedOkay && !probe::dragging && *probe::expectedCount == 0;
        ++probe::completionCalls;
    }
    void dragSourceSystemDragEnded() { ++probe::completionCalls; }
};
}
struct wkeWebDragData { int marker = 51; };
typedef void(WKE_CALL_TYPE*wkeUiThreadRunCallback)(HWND hWnd, void* param);
typedef int(WKE_CALL_TYPE*wkeUiThreadPostTaskCallback)(HWND hWnd, wkeUiThreadRunCallback callback, void* param);
namespace wke { wkeUiThreadPostTaskCallback g_wkeUiThreadPostTaskCallback = nullptr; }
namespace content {
class CheckReEnter {
public:
    static void incrementEnterCount();
    static void decrementEnterCount();
    static int getEnterCount();
private:
    static int s_kEnterCount;
};
int CheckReEnter::s_kEnterCount = 0;
void CheckReEnter::incrementEnterCount()
{
    ++s_kEnterCount;
}

void CheckReEnter::decrementEnterCount()
{
    --s_kEnterCount;
}

int CheckReEnter::getEnterCount()
{
    return s_kEnterCount;
}

class DragHandle {
public:
    int m_taskCount = 0;
    HWND m_viewWindow = nullptr;
    blink::WebViewImpl* m_webViewImpl = nullptr;
    std::function<void(void)> m_notifOnEnterDrag;
    std::function<void(void)> m_notifOnLeaveDrag;
    blink::WebDragOperation startDraggingInUiThread(blink::WebLocalFrame* frame,
        const wkeWebDragData* data, blink::WebDragOperationsMask mask,
        const blink::WebImage* image, const blink::WebPoint* offset)
    {
        ++probe::uiCalls;
        probe::admittedOkay = probe::admittedOkay && probe::dragging && m_taskCount == 1 && CheckReEnter::getEnterCount() == 0;
        probe::borrowOkay = probe::borrowOkay && frame == probe::expectedFrame && data == probe::expectedData && image == probe::expectedImage && offset == probe::expectedOffset;
        probe::borrowOkay = probe::borrowOkay && frame->marker == 31 && data->marker == 51 && image->marker == 41 && offset->x == 7 && offset->y == 9 && mask == 1;
        if (probe::failConsumer)
            throw std::bad_alloc();
        return blink::WebDragOperationCopy;
    }
    void startDragging(blink::WebLocalFrame* frame, const wkeWebDragData* dragDate,
        blink::WebDragOperationsMask mask, const blink::WebImage& image,
        const blink::WebPoint& dragImageOffset);
};
class StartDraggingTask {
public:
    StartDraggingTask(
        DragHandle* dragHandle,
        int* count,
        blink::WebLocalFrame* frame,
        const wkeWebDragData* data,
        const blink::WebDragOperationsMask mask,
        const blink::WebImage& image,
        const blink::WebPoint& dragImageOffset)
    {
        m_dragHandle = dragHandle;
        m_count = count;
        m_frame = frame;
        m_data = data;
        m_mask = mask;
        m_image = &image;
        m_dragImageOffset = &dragImageOffset;
        m_op = blink::WebDragOperationNone;

        *m_count += 1;
    }

    ~StartDraggingTask() { complete(); }

    static void WKE_CALL_TYPE runInUiThread(HWND hWnd, void* param)
    {
        StartDraggingTask* self = (StartDraggingTask*)param;
        self->runInUiThreadImpl();
    }

    void runInUiThreadImpl()
    {
        m_op = m_dragHandle->startDraggingInUiThread(m_frame, m_data, m_mask, m_image, m_dragImageOffset);
        complete();
    }

    blink::WebDragOperation getOp() const { return m_op; }

private:
    void complete() noexcept
    {
        if (!m_count)
            return;
        int* count = m_count;
        m_count = nullptr;
        *count -= 1;
    }

    blink::WebLocalFrame* m_frame;
    const wkeWebDragData* m_data;
    blink::WebDragOperationsMask m_mask;
    const blink::WebImage* m_image;
    const blink::WebPoint* m_dragImageOffset;

    blink::WebDragOperation m_op;

    DragHandle* m_dragHandle;
    int* m_count;
};

class LegacyDragScope {
public:
    explicit LegacyDragScope(const std::function<void(void)>& leave) noexcept
        : m_leave(leave)
    {
    }

    ~LegacyDragScope() noexcept
    {
        CheckReEnter::incrementEnterCount();
        m_leave();
    }

private:
    const std::function<void(void)>& m_leave;
};

void DragHandle::startDragging(blink::WebLocalFrame* frame,
    const wkeWebDragData* dragDate,
    const blink::WebDragOperationsMask mask,
    const blink::WebImage& image,
    const blink::WebPoint& dragImageOffset)
{
    // Allocate before admitting legacy drag bookkeeping.
    std::unique_ptr<StartDraggingTask> task(new StartDraggingTask(this, &m_taskCount, frame, dragDate, mask, image, dragImageOffset));
    m_notifOnEnterDrag();
    CheckReEnter::decrementEnterCount();
    {
        LegacyDragScope dragScope(m_notifOnLeaveDrag);
        if (wke::g_wkeUiThreadPostTaskCallback) {
            // Keep the existing external callback handoff and wait protocol.
            StartDraggingTask* postedTask = task.release();
            wke::g_wkeUiThreadPostTaskCallback(m_viewWindow, StartDraggingTask::runInUiThread, postedTask);

            MSG msg = { 0 };
            while (0 != m_taskCount) {
                if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                    ::TranslateMessage(&msg);
                    ::DispatchMessageW(&msg);
                }
                ::Sleep(10);
            }
            task.reset(postedTask);
        } else {
            StartDraggingTask::runInUiThread(m_viewWindow, task.get());
        }
    }

    POINT screenPoint = { 0 };
    ::GetCursorPos(&screenPoint);

    POINT clientPoint = screenPoint;
    ::ScreenToClient(m_viewWindow, &clientPoint);

    m_webViewImpl->dragSourceEndedAt(blink::WebPoint(clientPoint.x, clientPoint.y), blink::WebPoint(screenPoint.x, screenPoint.y), task->getOp());
    m_webViewImpl->dragSourceSystemDragEnded();
}
} // content
static int fail(const char* text) { std::fprintf(stderr, "%s\n", text); return 1; }
int main(int argc, char** argv)
{
    if (argc != 2)
        return fail("one case ID is required");
    const bool allocFault = std::strcmp(argv[1], "task-allocation-fails") == 0;
    const bool consumerFault = std::strcmp(argv[1], "synchronous-consumer-fails") == 0;
    const bool success = std::strcmp(argv[1], "synchronous-success") == 0;
    if (!allocFault && !consumerFault && !success)
        return fail("unknown case ID");
    content::DragHandle handle;
    blink::WebViewImpl view;
    blink::WebLocalFrame frame;
    wkeWebDragData data;
    blink::WebImage image;
    blink::WebPoint offset(7, 9);
    handle.m_webViewImpl = &view;
    handle.m_notifOnEnterDrag = [] { ++probe::enterCalls; probe::dragging = true; };
    handle.m_notifOnLeaveDrag = [] { ++probe::leaveCalls; probe::dragging = false; };
    content::CheckReEnter::incrementEnterCount();
    probe::expectedFrame = &frame;
    probe::expectedData = &data;
    probe::expectedImage = &image;
    probe::expectedOffset = &offset;
    probe::expectedCount = &handle.m_taskCount;
    probe::taskBytes = sizeof(content::StartDraggingTask);
    probe::failTaskAllocation = allocFault;
    probe::failConsumer = consumerFault;
    probe::armTaskAllocation = true;
    bool caught = false;
    try { handle.startDragging(&frame, &data, 1, image, offset); }
    catch (const std::bad_alloc&) { caught = true; }
    catch (...) { return fail("unexpected exception type"); }
    if (caught != (allocFault || consumerFault))
        return fail("ordinary bad_alloc boundary differs");
    if (probe::taskAttempts != 1 || probe::armTaskAllocation || handle.m_taskCount != 0 || content::CheckReEnter::getEnterCount() != 1 || probe::dragging)
        return fail("task count or admission bookkeeping did not restore");
    if (!probe::borrowOkay || !probe::admittedOkay || probe::taskPointer)
        return fail("borrowed inputs, admission order or owner cleanup differs");
    if (allocFault) {
        if (probe::enterCalls || probe::leaveCalls || probe::uiCalls || probe::completionCalls || probe::taskAllocs || probe::taskFrees)
            return fail("failed allocation entered or retained a task");
    } else {
        if (probe::enterCalls != 1 || probe::leaveCalls != 1 || probe::uiCalls != 1 || probe::taskAllocs != 1 || probe::taskFrees != 1)
            return fail("synchronous task was not consumed and destroyed once");
        if (probe::completionCalls != (success ? 2 : 0))
            return fail("completion ran after throwing consumer or was lost on success");
    }
    std::printf("case=%s taskAttempts=%d allocated=%d freed=%d count=%d enter=%d leave=%d ui=%d completion=%d reenter=%d caught=%d\n",
        argv[1], probe::taskAttempts, probe::taskAllocs, probe::taskFrees, handle.m_taskCount,
        probe::enterCalls, probe::leaveCalls, probe::uiCalls, probe::completionCalls,
        content::CheckReEnter::getEnterCount(), caught ? 1 : 0);
    return 0;
}
