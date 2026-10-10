#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// Test-only allocator observations; the production bodies below are byte-exact.
static long long g_heapBlocks = 0;
static long long g_bufferBlocks = 0;
static unsigned g_bufferAllocations = 0;
static unsigned g_bufferFrees = 0;
static unsigned g_arrayAttempts = 0;
static unsigned g_utf8Calls = 0;
static unsigned g_throwUtf8At = 0;
static bool g_failNextArray = false;
static bool g_failNextNew = false;
static bool g_downstreamAllocationFailure = false;
static bool g_brokerOwned = false;
static bool g_frameIdFailure = false;

void* operator new(std::size_t bytes)
{
    if (g_failNextNew) { g_failNextNew = false; throw std::bad_alloc(); }
    void* result = std::malloc(bytes ? bytes : 1);
    if (!result) throw std::bad_alloc();
    ++g_heapBlocks;
    return result;
}
void operator delete(void* value) noexcept
{
    if (value) { --g_heapBlocks; std::free(value); }
}
void* operator new[](std::size_t bytes)
{
    ++g_arrayAttempts;
    if (g_failNextArray) { g_failNextArray = false; throw std::bad_alloc(); }
    return ::operator new(bytes);
}
void operator delete[](void* value) noexcept { ::operator delete(value); }
void operator delete(void* value, std::size_t) noexcept { ::operator delete(value); }
void operator delete[](void* value, std::size_t) noexcept { ::operator delete[](value); }
static void* observedBufferMalloc(std::size_t bytes)
{
    void* result = std::malloc(bytes ? bytes : 1);
    if (!result) std::abort(); // This suite does not qualify the legacy unchecked malloc failure.
    ++g_bufferBlocks;
    ++g_bufferAllocations;
    return result;
}
static void observedBufferFree(void* value)
{
    if (value) { --g_bufferBlocks; ++g_bufferFrees; std::free(value); }
}
using utf8 = char;
#define WKE_CALL_TYPE __cdecl
using wkeWebView = void*;
using wkeWebFrameHandle = void*;
struct wkePoint { int x; int y; };
// EXACT SOURCE BEGIN: actual WKE drag types
typedef struct _wkeMemBuf {
    int size;
    void* data;
    size_t length;
} wkeMemBuf;

typedef struct _wkeWebDragData {
    struct Item {
        enum wkeStorageType {
            // String data with an associated MIME type. Depending on the MIME type, there may be
            // optional metadata attributes as well.
            StorageTypeString,
            // Stores the name of one file being dragged into the renderer.
            StorageTypeFilename,
            // An image being dragged out of the renderer. Contains a buffer holding the image data
            // as well as the suggested name for saving the image to.
            StorageTypeBinaryData,
            // Stores the filesystem URL of one file being dragged into the renderer.
            StorageTypeFileSystemFile,
        } storageType;

        // Only valid when storageType == StorageTypeString.
        wkeMemBuf* stringType;
        wkeMemBuf* stringData;

        // Only valid when storageType == StorageTypeFilename.
        wkeMemBuf* filenameData;
        wkeMemBuf* displayNameData;

        // Only valid when storageType == StorageTypeBinaryData.
        wkeMemBuf* binaryData;

        // Title associated with a link when stringType == "text/uri-list".
        // Filename when storageType == StorageTypeBinaryData.
        wkeMemBuf* title;

        // Only valid when storageType == StorageTypeFileSystemFile.
        wkeMemBuf* fileSystemURL;
        __int64 fileSystemFileSize;

        // Only valid when stringType == "text/html".
        wkeMemBuf* baseURL;
    };

    struct Item* m_itemList;
    int m_itemListLength;

    int m_modifierKeyState; // State of Shift/Ctrl/Alt/Meta keys.
    wkeMemBuf* m_filesystemId;
} wkeWebDragData;

typedef enum _wkeWebDragOperation {
    wkeWebDragOperationNone = 0,
    wkeWebDragOperationCopy = 1,
    wkeWebDragOperationLink = 2,
    wkeWebDragOperationGeneric = 4,
    wkeWebDragOperationPrivate = 8,
    wkeWebDragOperationMove = 16,
    wkeWebDragOperationDelete = 32,
    wkeWebDragOperationEvery = 0xffffffff
} wkeWebDragOperation;

typedef wkeWebDragOperation wkeWebDragOperationsMask;


// EXACT SOURCE END: actual WKE drag types
#define malloc observedBufferMalloc
#define free observedBufferFree
// EXACT SOURCE BEGIN: actual WKE buffer creator and releaser
wkeMemBuf* WKE_CALL_TYPE wkeCreateMemBuf(wkeWebView webView, void* buf, size_t length)
{
    if (!buf || 0 == length)
        return nullptr;
    wkeMemBuf* result = (wkeMemBuf*)malloc(sizeof(wkeMemBuf));
    result->size = sizeof(wkeMemBuf);
    result->length = length;
    result->data = malloc(length);
    memcpy(result->data, buf, length);
    return result;
}

void WKE_CALL_TYPE wkeFreeMemBuf(wkeMemBuf* buf)
{
    if (!buf)
        return;
    if (buf->data)
        free(buf->data);
    free(buf);
}


// EXACT SOURCE END: actual WKE buffer creator and releaser
#undef malloc
#undef free
#define BLINK_ASSERT(condition) assert(condition)
namespace blink {
// EXACT SOURCE BEGIN: actual WebVector allocation and copy
template <typename T>
class WebVector {
public:
    using ValueType = T;
    using iterator = T*;
    using const_iterator = const T*;

    ~WebVector()
    {
        destroy();
    }

    explicit WebVector(size_t size = 0)
    {
        initialize(size);
    }

    template <typename U>
    WebVector(const U* values, size_t size)
    {
        initializeFrom(values, size);
    }

    WebVector(const WebVector<T>& other)
    {
        initializeFrom(other.m_ptr, other.m_size);
    }

    template <typename C>
    WebVector(const C& other)
    {
        initializeFrom(other.size() ? &other[0] : 0, other.size());
    }

    WebVector& operator=(const WebVector& other)
    {
        if (this != &other)
            assign(other);
        return *this;
    }

    template <typename C>
    WebVector<T>& operator=(const C& other)
    {
        if (this != reinterpret_cast<const WebVector<T>*>(&other))
            assign(other);
        return *this;
    }

    template <typename C>
    void assign(const C& other)
    {
        assign(other.size() ? &other[0] : 0, other.size());
    }

    template <typename U>
    void assign(const U* values, size_t size)
    {
        destroy();
        initializeFrom(values, size);
    }

    size_t size() const { return m_size; }
    bool isEmpty() const { return !m_size; }

    T& operator[](size_t i)
    {
        BLINK_ASSERT(i < m_size);
        return m_ptr[i];
    }
    const T& operator[](size_t i) const
    {
        BLINK_ASSERT(i < m_size);
        return m_ptr[i];
    }

    bool contains(const T& value) const
    {
        for (size_t i = 0; i < m_size; i++) {
            if (m_ptr[i] == value)
                return true;
        }
        return false;
    }

    T* data() { return m_ptr; }
    const T* data() const { return m_ptr; }

    iterator begin() { return data(); }
    iterator end() { return begin() + m_size; }
    const_iterator begin() const { return data(); }
    const_iterator end() const { return begin() + m_size; }

    void swap(WebVector<T>& other)
    {
        std::swap(m_ptr, other.m_ptr);
        std::swap(m_size, other.m_size);
    }

private:
    void initialize(size_t size)
    {
        validateSize(size);
        m_size = size;
        if (!m_size)
            m_ptr = 0;
        else {
            char* cptr = static_cast<char*>(::operator new(sizeof(T) * m_size));
            for (size_t i = 0; i < m_size; ++i)
                new (&cptr[sizeof(T) * i]) T();
            m_ptr = reinterpret_cast<T*>(cptr);
        }
    }

    template <typename U>
    void initializeFrom(const U* values, size_t size)
    {
        validateSize(size);
        m_size = size;
        if (!m_size)
            m_ptr = 0;
        else {
            char* cptr = static_cast<char*>(::operator new(sizeof(T) * m_size));
            for (size_t i = 0; i < m_size; ++i)
                new (&cptr[sizeof(T) * i]) T(values[i]);
            m_ptr = reinterpret_cast<T*>(cptr);
        }
    }

    void validateSize(size_t size)
    {
        if (std::numeric_limits<size_t>::max() / sizeof(T) < size)
            abort();
    }

    void destroy()
    {
        for (size_t i = 0; i < m_size; ++i)
            m_ptr[i].~T();
        ::operator delete(m_ptr);
    }

    T* m_ptr;
    size_t m_size;
};


// EXACT SOURCE END: actual WebVector allocation and copy
// Blink/V8 leaves are adapters: shared handles copy without throwing, and utf8
// creates a real std::string whose actual allocation can fail under the seam.
class WebString {
public:
    std::shared_ptr<std::string> text;
    std::string utf8() const
    {
        ++g_utf8Calls;
        if (g_throwUtf8At && g_utf8Calls == g_throwUtf8At) g_failNextNew = true;
        return text ? *text : std::string();
    }
};
struct WebData { };
struct WebURL { WebString value; };
class String {
public:
    std::shared_ptr<std::string> text;
    const unsigned char* characters8() const { return reinterpret_cast<const unsigned char*>(text ? text->data() : ""); }
    std::size_t length() const { return text ? text->size() : 0; }
};
class KURL {
public:
    KURL(const WebURL& url) : m_url(url) { }
    String getUTF8String() const { String result; result.text = m_url.value.text; return result; }
private:
    WebURL m_url;
};
class WebDragData {
public:
// EXACT SOURCE BEGIN: actual Blink drag Item declaration
    struct Item {
        enum StorageType {
            // String data with an associated MIME type. Depending on the MIME type, there may be
            // optional metadata attributes as well.
            StorageTypeString,
            // Stores the name of one file being dragged into the renderer.
            StorageTypeFilename,
            // An image being dragged out of the renderer. Contains a buffer holding the image data
            // as well as the suggested name for saving the image to.
            StorageTypeBinaryData,
            // Stores the filesystem URL of one file being dragged into the renderer.
            StorageTypeFileSystemFile,
        };

        StorageType storageType;

        // Only valid when storageType == StorageTypeString.
        WebString stringType;
        WebString stringData;

        // Only valid when storageType == StorageTypeFilename.
        WebString filenameData;
        WebString displayNameData;

        // Only valid when storageType == StorageTypeBinaryData.
        WebData binaryData;

        // Title associated with a link when stringType == "text/uri-list".
        // Filename when storageType == StorageTypeBinaryData.
        WebString title;

        // Only valid when storageType == StorageTypeFileSystemFile.
        WebURL fileSystemURL;
        long long fileSystemFileSize;

        // Only valid when stringType == "text/html".
        WebURL baseURL;
    };


// EXACT SOURCE END: actual Blink drag Item declaration
// EXACT SOURCE BEGIN: actual Blink drag items getter
    WebVector<Item> items() const
    {
        return m_itemList;
    }


// EXACT SOURCE END: actual Blink drag items getter
    WebVector<Item> m_itemList;
};
struct WebLocalFrame { };
struct WebImage { };
struct WebPoint { int x = 0; int y = 0; };
enum WebDragOperation { WebDragOperationNone = 0, WebDragOperationCopy = 1 };
using WebDragOperationsMask = unsigned;
} // namespace blink
using blink::String;
using wkeStartDraggingCallback = void(__cdecl*)(wkeWebView, void*, wkeWebFrameHandle, const wkeWebDragData*, wkeWebDragOperationsMask, void*, const wkePoint*);
namespace wke {
static bool brokerOwnsView(wkeWebView) { return g_brokerOwned; }
struct CWebView {
    template<class T> static wkeWebFrameHandle frameIdTowkeWebFrameHandle(T*, long long)
    { if (g_frameIdFailure) throw std::bad_alloc(); return nullptr; }
};
}
namespace content {
// EXACT SOURCE BEGIN: actual BrokerDragPayload constructor
struct BrokerDragPayload {
    struct StringItem {
        std::string type;
        std::string data;
        wkeMemBuf typeBuffer;
        wkeMemBuf dataBuffer;
    };
    std::vector<StringItem> strings;
    std::vector<wkeWebDragData::Item> items;
    wkeWebDragData data { };
    bool valid { false };

    explicit BrokerDragPayload(const wkeWebDragData* source)
    {
        const size_t maxItems = 256;
        const size_t maxBytes = 1024 * 1024;
        if (!source || source->m_itemListLength < 0
            || static_cast<size_t>(source->m_itemListLength) > maxItems
            || (source->m_itemListLength && !source->m_itemList))
            return;
        strings.reserve(source->m_itemListLength);
        items.reserve(source->m_itemListLength);
        size_t bytes = 0;
        for (int i = 0; i < source->m_itemListLength; ++i) {
            const wkeWebDragData::Item& sourceItem = source->m_itemList[i];
            if (sourceItem.storageType != wkeWebDragData::Item::StorageTypeString)
                continue;
            const wkeMemBuf* type = sourceItem.stringType;
            const wkeMemBuf* text = sourceItem.stringData;
            if (!type || !text || (type->length && !type->data) || (text->length && !text->data)
                || type->length > maxBytes - bytes)
                return;
            bytes += type->length;
            if (text->length > maxBytes - bytes)
                return;
            bytes += text->length;
            StringItem item;
            item.type.assign(type->data ? static_cast<const char*>(type->data) : "", type->length);
            item.data.assign(text->data ? static_cast<const char*>(text->data) : "", text->length);
            strings.push_back(std::move(item));
        }
        for (StringItem& string : strings) {
            string.typeBuffer = { sizeof(wkeMemBuf), const_cast<char*>(string.type.c_str()), string.type.size() };
            string.dataBuffer = { sizeof(wkeMemBuf), const_cast<char*>(string.data.c_str()), string.data.size() };
            wkeWebDragData::Item item = { };
            item.storageType = wkeWebDragData::Item::StorageTypeString;
            item.stringType = &string.typeBuffer;
            item.stringData = &string.dataBuffer;
            items.push_back(item);
        }
        data.m_itemList = items.data();
        data.m_itemListLength = static_cast<int>(items.size());
        data.m_modifierKeyState = 0;
        valid = true;
    }
};


// EXACT SOURCE END: actual BrokerDragPayload constructor
struct EngineCompletion {
    unsigned endedNone = 0;
    unsigned systemEnded = 0;
    void dragSourceEndedAt(blink::WebPoint, blink::WebPoint, blink::WebDragOperation operation)
    { if (operation == blink::WebDragOperationNone) ++endedNone; }
    void dragSourceSystemDragEnded() { ++systemEnded; }
};
class DragHandle {
public:
    EngineCompletion engine;
    int observedLength = -1;
    void startDragging(blink::WebLocalFrame*, const wkeWebDragData* dragDate,
        blink::WebDragOperationsMask, const blink::WebImage&, const blink::WebPoint&)
    {
        if (g_brokerOwned) {
            struct State { EngineCompletion* engineView; } stateValue { &engine };
            State* state = &stateValue;
            if (g_downstreamAllocationFailure) g_failNextNew = true;
// EXACT SOURCE BEGIN: actual default controlled payload allocation and invalid NONE completion
        const std::shared_ptr<BrokerDragPayload> payload = std::make_shared<BrokerDragPayload>(dragDate);
        if (!payload->valid) {
            state->engineView->dragSourceEndedAt(blink::WebPoint(), blink::WebPoint(), blink::WebDragOperationNone);
            state->engineView->dragSourceSystemDragEnded();
            return;
        }

// EXACT SOURCE END: actual default controlled payload allocation and invalid NONE completion
            observedLength = payload->data.m_itemListLength;
            return;
        }
        observedLength = dragDate ? dragDate->m_itemListLength : -1;
    }
};
class CheckReEnter {
public:
    static void incrementEnterCount();
    static void decrementEnterCount();
    static int getEnterCount();
    static int s_kEnterCount;
};
int CheckReEnter::s_kEnterCount = 0;
// EXACT SOURCE BEGIN: actual reentry counter functions
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


// EXACT SOURCE END: actual reentry counter functions
struct PageHandler { wkeStartDraggingCallback startDraggingCallback = nullptr; void* startDraggingCallbackParam = nullptr; };
struct Page {
    PageHandler handler;
    PageHandler& wkeHandler() { return handler; }
    ::wkeWebView wkeWebView() { return this; }
};
struct BlinkPlatformImpl {
    struct AutoDisableGC { }; // No GC/page-lifetime assertion is made by this seam.
};
class WebPageImpl {
public:
    Page* m_pagePtr;
    DragHandle* m_dragHandle;
    bool m_isDragging = false;
    bool m_isFirstEnterDrag = true;
    void onEnterDragSimulate();
    void onLeaveDragSimulate();
    long long getFrameIdByBlinkFrame(blink::WebLocalFrame*) { return 1; }
    void startDragging(blink::WebLocalFrame*, const blink::WebDragData&,
        blink::WebDragOperationsMask, const blink::WebImage&, const blink::WebPoint&);
};
// EXACT SOURCE BEGIN: actual drag flag enter and leave
void WebPageImpl::onEnterDragSimulate()
{
    m_isDragging = true;
}

void WebPageImpl::onLeaveDragSimulate()
{
    m_isFirstEnterDrag = false;
    m_isDragging = false;
}


// EXACT SOURCE END: actual drag flag enter and leave
// EXACT SOURCE BEGIN: actual initializer deleter converter scope and startDragging
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


// EXACT SOURCE END: actual initializer deleter converter scope and startDragging
} // namespace content

static int g_callbackLength = -1;
static bool g_callbackEntered = false;
static bool g_callbackThrow = false;
static void __cdecl observedCallback(wkeWebView, void* parameter, wkeWebFrameHandle,
    const wkeWebDragData* data, wkeWebDragOperationsMask, void*, const wkePoint*)
{
    content::WebPageImpl* page = static_cast<content::WebPageImpl*>(parameter);
    g_callbackLength = data ? data->m_itemListLength : -1;
    g_callbackEntered = page->m_isDragging && content::CheckReEnter::getEnterCount() == 0;
    if (g_callbackThrow) throw std::bad_alloc();
}
static blink::WebDragData makeData(std::size_t size, bool mixed = false)
{
    blink::WebDragData result;
    blink::WebVector<blink::WebDragData::Item> items(size);
    const std::shared_ptr<std::string> type = std::make_shared<std::string>(64, 't');
    const std::shared_ptr<std::string> text = std::make_shared<std::string>(64, 'x');
    for (std::size_t i = 0; i < size; ++i) {
        items[i].storageType = blink::WebDragData::Item::StorageTypeString;
        items[i].stringType.text = type;
        items[i].stringData.text = text;
        if (mixed && i == 1) {
            items[i].storageType = blink::WebDragData::Item::StorageTypeFilename;
            items[i].filenameData.text = text;
        } else if (mixed && i == 2) {
            items[i].storageType = blink::WebDragData::Item::StorageTypeFileSystemFile;
            items[i].fileSystemURL.value.text = text;
        } else if (mixed && i == 3) {
            items[i].storageType = blink::WebDragData::Item::StorageTypeBinaryData;
        }
    }
    result.m_itemList.swap(items);
    return result;
}
static bool runCase(const char* name)
{
    const bool arrayFailure = std::strcmp(name, "array-allocation-failure") == 0;
    const bool partialString = std::strcmp(name, "utf8-partial-string-failure") == 0;
    const bool laterItem = std::strcmp(name, "utf8-later-item-failure") == 0;
    const bool downstream = std::strcmp(name, "downstream-owned-copy-failure") == 0;
    const bool callbackFailure = std::strcmp(name, "callback-unwind-counts") == 0;
    const bool argumentFailure = std::strcmp(name, "callback-argument-unwind-counts") == 0;
    const bool controlledLarge = std::strcmp(name, "controlled-257-invalid") == 0;
    const bool legacyLarge = std::strcmp(name, "legacy-257-preserved") == 0;
    const bool customLarge = std::strcmp(name, "custom-257-preserved") == 0;
    const bool mixed = std::strcmp(name, "mixed-success-releases") == 0;
    const bool empty = std::strcmp(name, "empty-success-releases") == 0;
    if (!(arrayFailure || partialString || laterItem || downstream || callbackFailure || argumentFailure
        || controlledLarge || legacyLarge || customLarge || mixed || empty)) return false;
    const bool callback = callbackFailure || argumentFailure || customLarge;
    const std::size_t count = controlledLarge || legacyLarge || customLarge ? 257 : empty ? 0 : mixed ? 4 : 3;
    blink::WebDragData data = makeData(count, mixed);
    content::Page page;
    content::DragHandle drag;
    content::WebPageImpl view { &page, &drag };
    blink::WebLocalFrame frame;
    const long long heapBefore = g_heapBlocks;
    const long long buffersBefore = g_bufferBlocks;
    g_arrayAttempts = 0;
    g_bufferAllocations = 0;
    g_bufferFrees = 0;
    g_utf8Calls = 0;
    g_throwUtf8At = partialString ? 2 : laterItem ? 3 : 0;
    g_failNextArray = arrayFailure;
    g_downstreamAllocationFailure = downstream;
    g_brokerOwned = !legacyLarge;
    g_frameIdFailure = argumentFailure;
    g_callbackThrow = callbackFailure;
    if (callback) {
        page.handler.startDraggingCallback = observedCallback;
        page.handler.startDraggingCallbackParam = &view;
    }
    content::CheckReEnter::s_kEnterCount = 1;
    bool threw = false;
    try { view.startDragging(&frame, data, blink::WebDragOperationCopy, blink::WebImage(), blink::WebPoint()); }
    catch (const std::bad_alloc&) { threw = true; }
    const bool expectedThrow = arrayFailure || partialString || laterItem || downstream || callbackFailure || argumentFailure;
    bool passed = threw == expectedThrow && g_heapBlocks == heapBefore && g_bufferBlocks == buffersBefore
        && content::CheckReEnter::getEnterCount() == 1 && !view.m_isDragging;
    if (partialString) passed = passed && g_utf8Calls == 2 && g_bufferAllocations == 2 && g_bufferFrees == 2;
    if (laterItem) passed = passed && g_utf8Calls == 3 && g_bufferAllocations == 4 && g_bufferFrees == 4;
    if (arrayFailure) passed = passed && g_arrayAttempts == 1 && g_utf8Calls == 0;
    if (controlledLarge) passed = passed && g_arrayAttempts == 0 && g_utf8Calls == 0
        && drag.engine.endedNone == 1 && drag.engine.systemEnded == 1 && drag.observedLength == -1;
    if (legacyLarge) passed = passed && drag.observedLength == 257 && g_utf8Calls == 514;
    if (customLarge) passed = passed && g_callbackLength == 257 && g_utf8Calls == 514 && g_callbackEntered;
    if (callbackFailure) passed = passed && g_callbackEntered && !view.m_isFirstEnterDrag;
    if (argumentFailure) passed = passed && g_callbackLength == -1 && !view.m_isFirstEnterDrag;
    if (mixed) passed = passed && drag.observedLength == 1;
    if (empty) passed = passed && drag.observedLength == 0;
    FILETIME created { }, exited { }, kernel { }, user { };
    const bool timeRead = GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE;
    const unsigned long long filetime = (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    std::printf("dragdata-body-v1 case=%s passed=%u subjectPid=%lu subjectFiletime=%llu heapDelta=%lld bufferDelta=%lld utf8Calls=%u arrayAttempts=%u bufferAllocations=%u bufferFrees=%u",
        name, passed && timeRead ? 1 : 0, GetCurrentProcessId(), filetime,
        g_heapBlocks - heapBefore, g_bufferBlocks - buffersBefore, g_utf8Calls, g_arrayAttempts, g_bufferAllocations, g_bufferFrees);
    std::putchar(10);
    return passed && timeRead;
}
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try { return runCase(argv[1]) ? 0 : 3; }
    catch (...) { return 4; }
}
