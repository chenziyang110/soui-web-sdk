
#include "content/ui/DragHandle.h"
#include "content/ui/WCDataObject.h"
#include "content/ui/WebDropSource.h"
#include "content/ui/ClipboardUtil.h"
#include "content/browser/CheckReEnter.h"
#include "wke/wkedefine.h"
#include "wke/wkeGlobalVar.h"
#include "wke/CursorDescriptorBroker.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>
#include <limits>
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/Source/web/WebViewImpl.h"
#include "third_party/WebKit/public/platform/WebImage.h"
#include "third_party/WebKit/public/platform/WebPoint.h"
#include "third_party/WebKit/public/platform/Platform.h"
#include "base/values.h"
#include "base/json/json_writer.h"

namespace content {

DragHandle::DragHandle(
    std::function<void(void)>&& notifOnEnterDrag,
    std::function<void(void)>&& notifOnLeaveDrag,
    std::function<void(void)>&& notifOnDragging
    )
    : m_notifOnEnterDrag(std::move(notifOnEnterDrag))
    , m_notifOnLeaveDrag(std::move(notifOnLeaveDrag))
    , m_notifOnDragging(std::move(notifOnDragging))
{
    m_refCount = 0;
    m_taskCount = 0;
    m_viewWindow = nullptr;
    m_webViewImpl = nullptr;
}

struct DragHandle::BrokerState {
    wkeWebView view;
    blink::WebViewImpl* engineView;
    std::function<bool()> closing;
    std::function<void()> leaveDrag;
    std::atomic<bool> sourceActive { false };
    std::atomic<bool> sourcePending { false };
    std::atomic<unsigned long long> targetRevision { 0 }; // UI admission / engine revocation.
    unsigned long long engineTargetSession { 0 }; // Engine owner only, actual Enter.
    bool targetCancelling { false }; // Engine-only reentrant Leave guard.

    bool cancelTarget(unsigned long long expected = 0)
    {
        if (expected && engineTargetSession != expected)
            return true; // An old terminal command cannot clear a new session.
        if (targetCancelling)
            return false; // The outer actual Leave still owns completion.
        targetCancelling = true;
        try {
            if (!engineView->dragTargetCancelForCursorBroker()) {
                targetCancelling = false;
                return false;
            }
            engineTargetSession = 0;
        } catch (...) {
            targetCancelling = false;
            throw; // Keep the session identity and actual data for retry.
        }
        targetCancelling = false;
        return true;
    }
};

DragHandle::~DragHandle()
{
    // Broker COM storage is released by the UI terminal task before deletion.
    if (m_brokerState && (m_dragData || m_dropTargetHelper || m_nativeSourceActive || m_nativeCallbackDepth))
        wke::brokerRecordFailure(m_brokerState->view);
}

void DragHandle::setViewWindow(HWND viewWindow, blink::WebViewImpl* webViewImpl, wkeWebView view)
{
    m_viewWindow = viewWindow;
    m_webViewImpl = webViewImpl;
    if (!view || !wke::brokerOwnsView(view) || m_brokerState)
        return;
    m_brokerState = std::make_shared<BrokerState>();
    m_brokerState->view = view;
    m_brokerState->engineView = webViewImpl;
    m_brokerState->closing = wke::brokerUiClosingPredicate(view);
    m_brokerState->leaveDrag = m_notifOnLeaveDrag;
    DragHandle* self = this;
    std::weak_ptr<BrokerState> weakState(m_brokerState);
    wke::brokerAddCloseAction(view, [self, weakState] {
        if (std::shared_ptr<BrokerState> state = weakState.lock()) {
            state->targetRevision = 0;
            self->requestBrokerClose(); // Admit UI cleanup before fallible engine Leave.
            if (state->sourceActive.exchange(false))
                state->leaveDrag();
            state->cancelTarget();
        }
    });
    wke::brokerAddIdentityAction(view, [weakState] {
        if (std::shared_ptr<BrokerState> state = weakState.lock()) {
            state->targetRevision = 0;
            if (state->sourceActive.exchange(false)) {
                state->leaveDrag();
                state->engineView->dragSourceSystemDragEnded();
            }
            // A counted owner timer wakes the native loop to observe its old
            // epoch predicate; it performs no synthetic input or new work.
            if (!wke::brokerPostUiCancellation(state->view, 1, [state](bool) { })
                && !state->closing())
                wke::brokerRecordFailure(state->view);
            state->cancelTarget(); // Actual old target data, not a synthetic Enter/Leave.
        }
    });
}

void DragHandle::requestBrokerClose()
{
    const std::shared_ptr<BrokerState> state = m_brokerState;
    if (!state)
        return;
    if (!wke::brokerPostUiTask(state->view, WKE_CURSOR_BROKER_NATIVE_TASK,
        [this, state](bool cancelled) {
            if (!cancelled) {
                m_brokerTargetCandidate = 0; // Revoke an in-flight Enter before deferred resource cleanup.
                m_clearBrokerResourcesPending = true;
                clearBrokerUiResources();
            }
        }, true))
        wke::brokerRecordFailure(state->view);
}

void DragHandle::cancelBrokerTargetSession(unsigned long long expectedSession)
{
    const std::shared_ptr<BrokerState> state = m_brokerState;
    if (!state || !expectedSession || m_brokerTargetSession != expectedSession)
        return;
    const unsigned long long session = expectedSession;
    m_brokerTargetSession = 0;
    m_brokerTargetCancelled = nullptr;
    unsigned long long expected = session;
    state->targetRevision.compare_exchange_strong(expected, 0);
    if (!wke::brokerPostEngineTask(state->view, [state, session] {
        state->cancelTarget(session);
    }) && !state->closing())
        wke::brokerRecordFailure(state->view);
}

void DragHandle::clearBrokerUiResources()
{
    if (!m_clearBrokerResourcesPending || m_nativeSourceActive || m_nativeCallbackDepth)
        return;
    m_dragData.clear();
    m_dropTargetHelper.clear();
    m_tempDataObjectForSimulate.clear();
    m_brokerTargetCandidate = 0;
    m_brokerTargetSession = 0;
    m_brokerTargetCancelled = nullptr;
    m_brokerState->targetRevision = 0;
    m_clearBrokerResourcesPending = false;
}

// Reserve the UI candidate before any hook/helper/provider can re-enter.
// A terminal callback revokes the slot even before engine admission exists.
// Each stack frame may retire only its own token, never a nested new Enter.
class DragHandle::BrokerTargetCandidateGuard {
public:
    explicit BrokerTargetCandidateGuard(DragHandle* owner) : m_owner(owner)
    {
        if (!owner->m_brokerState)
            return;
        owner->m_brokerTargetCandidate = 0;
        if (owner->m_nextBrokerTargetSession == (std::numeric_limits<unsigned long long>::max)())
            throw std::runtime_error("Controlled drag session sequence exhausted");
        m_session = ++owner->m_nextBrokerTargetSession;
        owner->m_brokerTargetCandidate = m_session;
    }
    ~BrokerTargetCandidateGuard() { retire(); }
    void retire()
    {
        if (current())
            m_owner->m_brokerTargetCandidate = 0;
        m_session = 0; // No owner access after the counted native scope is left.
    }
    bool current() const { return m_session && m_owner->m_brokerTargetCandidate == m_session; }
    unsigned long long session() const { return m_session; }
private:
    DragHandle* m_owner;
    unsigned long long m_session { 0 };
};

class DragHandle::BrokerCallbackGuard {
public:
    explicit BrokerCallbackGuard(DragHandle* owner, BrokerTargetCandidateGuard* candidate = nullptr)
        : m_owner(owner), m_candidate(candidate)
    {
        if (!owner->m_brokerState)
            return;
        m_cancelled = wke::brokerUiCancelPredicate(owner->m_brokerState->view);
        if (m_cancelled())
            return;
        m_scope.reset(new wke::BrokerNativeScope(owner->m_brokerState->view,
            WKE_CURSOR_BROKER_NATIVE_OLE_DRAG, true));
        if (m_scope->entered()) {
            ++owner->m_nativeCallbackDepth;
            m_entered = true;
        }
    }
    ~BrokerCallbackGuard()
    {
        if (m_candidate)
            m_candidate->retire();
        if (m_entered) {
            --m_owner->m_nativeCallbackDepth;
            m_owner->clearBrokerUiResources();
        }
    }
    bool entered() const { return m_entered; }
    bool cancelled() const { return m_cancelled && m_cancelled(); }
    const std::function<bool()>& cancelPredicate() const { return m_cancelled; }
private:
    DragHandle* m_owner;
    BrokerTargetCandidateGuard* m_candidate;
    std::function<bool()> m_cancelled;
    std::unique_ptr<wke::BrokerNativeScope> m_scope;
    bool m_entered { false };
};

class DragHandle::BrokerSourceGuard {
public:
    explicit BrokerSourceGuard(DragHandle* owner) : m_owner(owner) { owner->m_nativeSourceActive = true; }
    ~BrokerSourceGuard()
    {
        m_owner->m_nativeSourceActive = false;
        m_owner->clearBrokerUiResources();
    }
private:
    DragHandle* m_owner;
};
class DragHandle::BrokerPendingGuard {
public:
    explicit BrokerPendingGuard(std::shared_ptr<BrokerState> state) : m_state(std::move(state)) { }
    ~BrokerPendingGuard() { m_state->sourcePending = false; }
private:
    std::shared_ptr<BrokerState> m_state;
};
// Native drag-out currently consumes string items. Copy those bytes before
// handing control to the UI; no frame, WebImage or caller-stack pointer crosses.
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

DWORD DragHandle::draggingSourceOperationMaskToDragCursors(blink::WebDragOperationsMask op)
{
    DWORD result = DROPEFFECT_NONE;
    if (op == blink::WebDragOperationEvery)
        return DROPEFFECT_COPY | DROPEFFECT_LINK | DROPEFFECT_MOVE;
    if (op & blink::WebDragOperationCopy)
        result |= DROPEFFECT_COPY;
    if (op & blink::WebDragOperationLink)
        result |= DROPEFFECT_LINK;
    if (op & blink::WebDragOperationMove)
        result |= DROPEFFECT_MOVE;
    if (op & blink::WebDragOperationGeneric)
        result |= DROPEFFECT_MOVE;
    return result;
}

// blink::WebDragOperation DragHandle::keyStateToDragOperation(DWORD grfKeyState) const
// {
//     // Conforms to Microsoft's key combinations as documented for 
//     // IDropTarget::DragOver. Note, grfKeyState is the current 
//     // state of the keyboard modifier keys on the keyboard. See:
//     // <http://msdn.microsoft.com/en-us/library/ms680129(VS.85).aspx>.
//     blink::WebDragOperation operation = blink::WebDragOperationNone; // m_page->dragController().sourceDragOperation();
// 
//     if ((grfKeyState & (MK_CONTROL | MK_SHIFT)) == (MK_CONTROL | MK_SHIFT))
//         operation = blink::WebDragOperationLink;
//     else if ((grfKeyState & MK_CONTROL) == MK_CONTROL)
//         operation = blink::WebDragOperationCopy;
//     else if ((grfKeyState & MK_SHIFT) == MK_SHIFT)
//         operation = blink::WebDragOperationGeneric;
// 
//     return operation;
// }

// int ConvertKeyStateToAuraEventFlags(DWORD key_state)
// int ConvertAuraEventFlagsToWebInputEventModifiers(int aura_event_flags)
static int keyStateToWebInputEventModifiers(DWORD keyState)
{
    int flags = 0;

    if (keyState & MK_CONTROL)
        flags |= blink::WebInputEvent::ControlKey;
    if (keyState & MK_ALT)
        flags |= blink::WebInputEvent::AltKey;
    if (keyState & MK_SHIFT)
        flags |= blink::WebInputEvent::ShiftKey;
    if (keyState & MK_LBUTTON)
        flags |= blink::WebInputEvent::LeftButtonDown;
    if (keyState & MK_MBUTTON)
        flags |= blink::WebInputEvent::MiddleButtonDown;
    if (keyState & MK_RBUTTON)
        flags |= blink::WebInputEvent::RightButtonDown;
    return flags;
}

bool DragHandle::containsPlainText(IDataObject* pDataObject)
{
    if (pDataObject)
        return SUCCEEDED(pDataObject->QueryGetData(ClipboardUtil::getPlainTextWFormatType())) ||
        SUCCEEDED(pDataObject->QueryGetData(ClipboardUtil::getPlainTextFormatType()));
    return false;
}

class DropMediumOwner {
public:
    STGMEDIUM medium = {};
    bool accepted = false;
    ~DropMediumOwner() { if (accepted) ::ReleaseStgMedium(&medium); }
};
class DropGlobalLock {
public:
    explicit DropGlobalLock(HGLOBAL handle) : m_handle(handle), bytes(::GlobalLock(handle)) { }
    ~DropGlobalLock() { if (bytes) ::GlobalUnlock(m_handle); }
    void* bytes;
private:
    HGLOBAL m_handle;
};

static void dropFilesToWebDragData(std::vector<blink::WebDragData::Item>* itemList, IDataObject* pDataObject,
    size_t& remainingPayloadBytes, bool controlled)
{
    if (!pDataObject) return;
    const size_t kMaxPayloadBytes = 1024 * 1024;
    const UINT kMaxFileCount = 256;
    const UINT kMaxPathCharacters = 32767;
    FORMATETC format = {};
    format.cfFormat = CF_HDROP;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    DropMediumOwner owner;
    const HRESULT read = pDataObject->GetData(&format, &owner.medium);
    if (FAILED(read)) {
        if (!controlled || read == DV_E_FORMATETC) return;
        throw std::runtime_error("CF_HDROP provider failure");
    }
    owner.accepted = true; // Every success path, rejection and exception releases.
    if (controlled && read != S_OK)
        throw std::runtime_error("CF_HDROP provider returned noncanonical success");
    if (owner.medium.tymed != TYMED_HGLOBAL || !owner.medium.hGlobal)
        throw std::runtime_error("Invalid CF_HDROP medium");
    const SIZE_T bytes = ::GlobalSize(owner.medium.hGlobal);
    if (bytes < sizeof(DROPFILES) + 2 || bytes > kMaxPayloadBytes)
        throw std::runtime_error("CF_HDROP medium exceeds bounded payload");
    DropGlobalLock locked(owner.medium.hGlobal);
    if (!locked.bytes) throw std::runtime_error("Cannot lock CF_HDROP medium");
    const DROPFILES* header = static_cast<const DROPFILES*>(locked.bytes);
    if (header->pFiles < sizeof(DROPFILES) || header->pFiles > bytes - 2)
        throw std::runtime_error("Invalid CF_HDROP offset");
    const unsigned char* names = static_cast<const unsigned char*>(locked.bytes) + header->pFiles;
    const SIZE_T available = bytes - header->pFiles;
    bool terminated = false;
    if (header->fWide) {
        if (header->pFiles % sizeof(WCHAR) || available < 2 * sizeof(WCHAR)
            || (controlled && available % sizeof(WCHAR)))
            throw std::runtime_error("Invalid wide CF_HDROP storage");
        const WCHAR* wide = reinterpret_cast<const WCHAR*>(names);
        for (SIZE_T i = 1; i < available / sizeof(WCHAR); ++i) {
            if (!wide[i - 1] && !wide[i]) { terminated = true; break; }
        }
    } else {
        for (SIZE_T i = 1; i < available; ++i) {
            if (!names[i - 1] && !names[i]) { terminated = true; break; }
        }
    }
    if (!terminated) throw std::runtime_error("Unterminated CF_HDROP file list");
    // DragQueryFile takes the HGLOBAL handle, never the pointer from GlobalLock.
    const HDROP drop = reinterpret_cast<HDROP>(owner.medium.hGlobal);
    const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count > kMaxFileCount || itemList->size() > kMaxFileCount - count)
        throw std::runtime_error("Too many CF_HDROP files");
    size_t usedBytes = 0;
    for (UINT index = 0; index < count; ++index) {
        const UINT characters = ::DragQueryFileW(drop, index, nullptr, 0);
        if (!characters || characters > kMaxPathCharacters)
            throw std::runtime_error("CF_HDROP path exceeds character bound");
        const size_t pathBytes = (static_cast<size_t>(characters) + 1) * sizeof(WCHAR);
        if (pathBytes > kMaxPayloadBytes - usedBytes || pathBytes > remainingPayloadBytes)
            throw std::runtime_error("CF_HDROP paths exceed total payload bound");
        usedBytes += pathBytes;
        std::vector<WCHAR> filename(static_cast<size_t>(characters) + 1, 0);
        const UINT copied = ::DragQueryFileW(drop, index, filename.data(), characters + 1);
        if (copied != characters || filename[characters] != 0)
            throw std::runtime_error("CF_HDROP path query changed");
        blink::WebDragData::Item item;
        item.storageType = blink::WebDragData::Item::StorageTypeFilename;
        item.filenameData = blink::WebString(filename.data(), characters);
        itemList->push_back(item);
        remainingPayloadBytes -= pathBytes;
    }
}

blink::WebDragData DragHandle::dropDataToWebDragData(IDataObject* pDataObject)
{
    blink::WebDragData result;
    result.initialize();

    std::vector<blink::WebDragData::Item> itemList;

    size_t remainingPayloadBytes = 1024 * 1024;
    dropFilesToWebDragData(&itemList, pDataObject, remainingPayloadBytes, !!m_brokerState);

    if (m_brokerState) {
        if (!pDataObject)
            throw std::runtime_error("Missing controlled IDataObject");
        ClipboardUtil::BoundedTextItem plain;
        const ClipboardUtil::BoundedReadResult plainRead =
            ClipboardUtil::getPlainTextBounded(pDataObject, remainingPayloadBytes, plain);
        if (plainRead == ClipboardUtil::BoundedReadFailure)
            throw std::runtime_error("Invalid controlled plain text");
        if (plainRead == ClipboardUtil::BoundedReadSuccess) {
            if (itemList.size() >= 256)
                throw std::runtime_error("Too many controlled drag items");
            blink::WebDragData::Item item;
            item.storageType = blink::WebDragData::Item::StorageTypeString;
            item.stringType = blink::WebString::fromUTF8(plain.type.data(), plain.type.size());
            item.stringData = blink::WebString::fromUTF8(plain.data.data(), plain.data.size());
            itemList.push_back(item);
        }
        std::vector<ClipboardUtil::BoundedTextItem> custom;
        const ClipboardUtil::BoundedReadResult customRead =
            ClipboardUtil::getCustomPlainTextsBounded(pDataObject, remainingPayloadBytes, custom, 256 - itemList.size());
        if (customRead == ClipboardUtil::BoundedReadFailure)
            throw std::runtime_error("Invalid controlled custom text");
        for (const auto& text : custom) {
            blink::WebDragData::Item item;
            item.storageType = blink::WebDragData::Item::StorageTypeString;
            item.stringType = blink::WebString::fromUTF8(text.type.data(), text.type.size());
            item.stringData = blink::WebString::fromUTF8(text.data.data(), text.data.size());
            itemList.push_back(item);
        }
        result.setItems(itemList);
        return result;
    }

    if (containsPlainText(pDataObject)) {
        blink::WebDragData::Item item;
        item.storageType = blink::WebDragData::Item::StorageTypeString;
        item.stringType = blink::WebString::fromUTF8(kMimeTypeText);
        item.stringData = blink::WebString::fromUTF8(ClipboardUtil::getPlainText(pDataObject));
        itemList.push_back(item);
    }

    std::unique_ptr<base::DictionaryValue> dictVal(ClipboardUtil::getCustomPlainTexts(pDataObject));
    if (dictVal) {
        base::DictionaryValue::Iterator it(*dictVal);
        for (; !it.IsAtEnd(); it.Advance()) {
            std::string type = it.key();
            std::string text;

            const base::Value& val = it.value();
            if (!val.IsType(base::Value::TYPE_STRING))
                continue;
            if (!val.GetAsString(&text) || text.empty())
                continue;

            blink::WebDragData::Item item;
            item.storageType = blink::WebDragData::Item::StorageTypeString;
            item.stringType = blink::WebString::fromUTF8(type.c_str());
            item.stringData = blink::WebString::fromUTF8(text.c_str());
            itemList.push_back(item);
        }
    }
        
    result.setItems(itemList);
    //result.setFilesystemId(drop_data.filesystem_id);

    return result;
}

DWORD DragHandle::dragOperationToDragCursor(blink::WebDragOperation op)
{
    DWORD result = DROPEFFECT_NONE;
    if (op & blink::WebDragOperationCopy)
        result = DROPEFFECT_COPY;
    else if (op & blink::WebDragOperationLink)
        result = DROPEFFECT_LINK;
    else if (op & blink::WebDragOperationMove)
        result = DROPEFFECT_MOVE;
    else if (op & blink::WebDragOperationGeneric)
        result = DROPEFFECT_MOVE; //This appears to be the Firefox behaviour
    return result;
}

blink::WebDragOperation DragHandle::dragCursorTodragOperation(DWORD op)
{
    blink::WebDragOperation result = blink::WebDragOperationNone;
    if (op & DROPEFFECT_COPY)
        result = blink::WebDragOperationCopy;
    else if (op & DROPEFFECT_LINK)
        result = blink::WebDragOperationLink;
    else if (op & DROPEFFECT_MOVE)
        result = blink::WebDragOperationMove;
    else if (op & DROPEFFECT_MOVE)
        result = blink::WebDragOperationGeneric;
    return result;
}

blink::WebDragOperation DragHandle::startDraggingInUiThread(blink::WebLocalFrame* frame,
    const wkeWebDragData* data,
    const blink::WebDragOperationsMask mask,
    const blink::WebImage* image,
    const blink::WebPoint* dragImageOffset,
    std::function<bool()> cancelled)
{
    //blink::WebDragOperation op = doStartDragging(frame, *data, mask, *image, *dragImageOffset);

    blink::WebDragOperation operation = blink::WebDragOperationNone;

    if (!m_viewWindow && !wke::g_wkeUiThreadPostTaskCallback) // updataInOtherThreadEnabled下这个值为空，除非设置了ui post回调
        return operation;

    //FIXME: Allow UIDelegate to override behaviour <rdar://problem/5015953>

    //We liberally protect everything, to protect against a load occurring mid-drag
    COMPtr<IDragSourceHelper> helper;
    COMPtr<IDropSource> source;
    if (FAILED(WebDropSource::createInstance(&source, std::move(cancelled))))
        return operation;

    WCDataObject* dataObjectPtr = nullptr;
    if (FAILED(WCDataObject::createInstance(&dataObjectPtr)) || !dataObjectPtr)
        return operation;
    // The factory starts at refcount zero. This UI local owns construction,
    // including allocation failures, and the member takes a separate reference.
    COMPtr<WCDataObject> dataObject(dataObjectPtr);

    wkeWebDragData::Item* items = data->m_itemList;

    std::unique_ptr<base::DictionaryValue> dictVal;

    for (int i = 0; i < data->m_itemListLength; ++i) {
        const wkeWebDragData::Item& it = items[i];
        if (wkeWebDragData::Item::StorageTypeString == it.storageType) {
            std::string typeStr((const char*)(it.stringType->data ? it.stringType->data : ""), it.stringType->length != 0 ? it.stringType->length : 0);
            std::string dataStr((const char*)(it.stringData->data ? it.stringData->data : ""), it.stringData->length != 0 ? it.stringData->length : 0);
            
            WCDataObject::ClipboardDataType typeEnum = WCDataObject::clipboardTypeFromMIMEType(typeStr);
            if (WCDataObject::kClipboardDataTypeNone == typeEnum) {
                if (!dictVal)
                    dictVal.reset(new base::DictionaryValue());

                dictVal->SetString(typeStr, dataStr);
            } else
                dataObjectPtr->writeString(typeStr, dataStr);
        }
    }

    if (dictVal) {
        std::string json;
        base::JSONWriter::Write(*dictVal, &json);
        if (json.size() > 0)
            dataObjectPtr->writeCustomPlainText(json);

    }

    m_dragData = dataObject.get();
    m_mask = mask;

    if (!m_brokerState && !wke::g_isSetDragDropEnable) {
        simulateDrag();
        return blink::WebDragOperationCopy;
    }

    if (source && ((image && !image->isNull()) || m_dragData)) {
        if (image && !image->isNull()) {
            // if (SUCCEEDED(::CoCreateInstance(CLSID_DragDropHelper, 0, CLSCTX_INPROC_SERVER,
            //     IID_IDragSourceHelper, (LPVOID*)&helper))) {
            //     BITMAP b;
            //     GetObject(image, sizeof(BITMAP), &b);
            //     SHDRAGIMAGE sdi;
            //     sdi.sizeDragImage.cx = b.bmWidth;
            //     sdi.sizeDragImage.cy = b.bmHeight;
            //     sdi.crColorKey = 0xffffffff;
            //     sdi.hbmpDragImage = image;
            //     sdi.ptOffset.x = dragImageOffset.x - imageOrigin.x();
            //     sdi.ptOffset.y = dragImageOffset.y - imageOrigin.y();
            //     if (isLink)
            //         sdi.ptOffset.y = b.bmHeight - sdi.ptOffset.y;
            // 
            //     helper->InitializeFromBitmap(&sdi, m_dragData.get());
            // }
        }

        DWORD okEffect = draggingSourceOperationMaskToDragCursors(mask);
        DWORD effect = DROPEFFECT_NONE;
        HRESULT hr = E_NOTIMPL;

        //         m_notifOnEnterDrag();
        //         CheckReEnter::decrementEnterCount();
        COMPtr<IDataObject> dragData(m_dragData);
        hr = ::DoDragDrop(dragData.get(), source.get(), okEffect, &effect);
        //         CheckReEnter::incrementEnterCount();
        //         m_notifOnLeaveDrag();

        if (hr == DRAGDROP_S_DROP) {
            if (effect & DROPEFFECT_COPY)
                operation = blink::WebDragOperationCopy;
            else if (effect & DROPEFFECT_LINK)
                operation = blink::WebDragOperationLink;
            else if (effect & DROPEFFECT_MOVE)
                operation = blink::WebDragOperationMove;
        }
    }
    return operation;
}

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
    if (!m_webViewImpl || !wke::g_isSetDragDropEnable)
        return;

    if (m_brokerState) {
        const std::shared_ptr<BrokerState> state = m_brokerState;
        if (state->closing())
            return;
        if (state->sourcePending) {
            // A different document cannot nest DoDragDrop while the previous
            // UI call still owns its native lease.
            if (!state->sourceActive) {
                state->engineView->dragSourceEndedAt(blink::WebPoint(), blink::WebPoint(), blink::WebDragOperationNone);
                state->engineView->dragSourceSystemDragEnded();
            }
            return;
        }
        const std::function<bool()> cancelled = wke::brokerUiCancelPredicate(state->view);
        const std::shared_ptr<BrokerDragPayload> payload = std::make_shared<BrokerDragPayload>(dragDate);
        if (!payload->valid) {
            state->engineView->dragSourceEndedAt(blink::WebPoint(), blink::WebPoint(), blink::WebDragOperationNone);
            state->engineView->dragSourceSystemDragEnded();
            return;
        }
        // The engine returns without pumping or waiting. Only plain values are
        // captured; all Blink completion runs later through the counted owner.
        std::function<void(bool)> uiBody =
            [this, state, payload, mask, cancelled](bool stale) {
                BrokerPendingGuard pending(state);
                blink::WebDragOperation operation = blink::WebDragOperationNone;
                POINT screenPoint = { 0 };
                POINT clientPoint = { 0 };
                if (!stale && !cancelled()) {
                    wke::BrokerNativeScope scope(state->view, WKE_CURSOR_BROKER_NATIVE_OLE_DRAG);
                    {
                        BrokerSourceGuard sourceGuard(this);
                        if (scope.entered())
                            operation = startDraggingInUiThread(nullptr, &payload->data, mask, nullptr, nullptr, cancelled);
                        else
                            wke::brokerRecordFailure(state->view);
                        ::GetCursorPos(&screenPoint);
                        clientPoint = screenPoint;
                        ::ScreenToClient(m_viewWindow, &clientPoint);
                    }
                }
                if (!wke::brokerPostEngineTask(state->view, [state, operation, screenPoint, clientPoint, cancelled] {
                    if (cancelled() || !state->sourceActive.exchange(false))
                        return;
                    state->leaveDrag();
                    state->engineView->dragSourceEndedAt(blink::WebPoint(clientPoint.x, clientPoint.y),
                        blink::WebPoint(screenPoint.x, screenPoint.y), operation);
                    state->engineView->dragSourceSystemDragEnded();
                }) && !state->closing() && !cancelled())
                    wke::brokerRecordFailure(state->view);
            };
        // Complete every allocating capture before source admission. Moving
        // this function into the broker cannot allocate at the call boundary.
        state->sourcePending = true;
        state->sourceActive = true;
        m_notifOnEnterDrag();
        if (!wke::brokerPostUiTask(state->view, WKE_CURSOR_BROKER_NATIVE_OLE_DRAG, std::move(uiBody))) {
            state->sourcePending = false;
            if (state->sourceActive.exchange(false))
                state->leaveDrag();
            if (!cancelled()) {
                state->engineView->dragSourceEndedAt(blink::WebPoint(), blink::WebPoint(), blink::WebDragOperationNone);
                state->engineView->dragSourceSystemDragEnded();
            }
        }
        return;
    }

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

void DragHandle::simulateDrag()
{
    m_notifOnEnterDrag();

    MSG msg = { 0 };
    while (WM_QUIT != msg.message) {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);

            m_notifOnDragging();

            if (WM_LBUTTONUP == msg.message || WM_MOUSELEAVE == msg.message)
                break;
        }
        ::Sleep(10);
    }
    m_notifOnLeaveDrag();
}

struct DragEnterInfo {
    blink::WebViewImpl* webViewImpl;
    int* taskCount;
    blink::WebDragData data;
    blink::WebPoint clientPoint;
    blink::WebPoint screenPoint;
    blink::WebDragOperationsMask opAllowed;
    int modifiers;
};

static void dragTargetDragEnter(DragEnterInfo* info)
{
    info->webViewImpl->dragTargetDragEnter(info->data, info->clientPoint, info->screenPoint, info->opAllowed, info->modifiers);
    *(info->taskCount) -= 1;
    delete info;
}

static blink::WebDragOperationsMask dropEffectToDragOperation(DWORD effect)
{
    int op = blink::WebDragOperationNone;
    if (effect & DROPEFFECT_LINK)
        op |= blink::WebDragOperationLink;
    if (effect & DROPEFFECT_COPY)
        op |= blink::WebDragOperationCopy;
    if (effect & DROPEFFECT_MOVE)
        op |= blink::WebDragOperationMove;
    return (blink::WebDragOperationsMask)op;
}

// IDropTarget impl
HRESULT __stdcall DragHandle::DragEnter(IDataObject* pDataObject, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
try
{
    if (!pdwEffect)
        return E_INVALIDARG;
    const DWORD sourceAllowedEffects = *pdwEffect;
    const unsigned long long previousBrokerSession = m_brokerState ? m_brokerTargetSession : 0;
    BrokerTargetCandidateGuard candidateGuard(this);
    BrokerCallbackGuard brokerGuard(this, &candidateGuard);
    if (m_brokerState && (!candidateGuard.current() || !brokerGuard.entered() || brokerGuard.cancelled())) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }
    if (!m_webViewImpl)
        return S_OK;
    unsigned long long brokerSession = 0;
    std::function<bool()> brokerCancelled;
    if (m_brokerState) {
        *pdwEffect = DROPEFFECT_NONE;
        m_lastDropEffect = DROPEFFECT_NONE;
        cancelBrokerTargetSession(previousBrokerSession);
        if (!(sourceAllowedEffects & DROPEFFECT_COPY))
            return S_OK;
        brokerSession = candidateGuard.session();
        brokerCancelled = brokerGuard.cancelPredicate();
    }
    
    if (!m_dropTargetHelper)
        ::CoCreateInstance(CLSID_DragDropHelper, 0, CLSCTX_INPROC_SERVER, IID_IDropTargetHelper, (void**)&m_dropTargetHelper);

    if (m_brokerState && (!candidateGuard.current() || brokerCancelled())) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }
    if (m_dropTargetHelper)
        m_dropTargetHelper->DragEnter(m_viewWindow, pDataObject, (POINT*)&pt, *pdwEffect);
    if (m_brokerState && (!candidateGuard.current() || brokerCancelled())) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    POINT screenPoint = { 0 };
    ::GetCursorPos(&screenPoint);

    POINT clientPoint = screenPoint;
    ::ScreenToClient(m_viewWindow, &clientPoint);

    // The controlled output was reset to NONE before opaque calls. Keep the
    // source mask captured at entry instead of treating that output as input.
    DWORD effect = m_brokerState ? sourceAllowedEffects : *pdwEffect;

    m_mask = dropEffectToDragOperation(effect);
    int modifiers = keyStateToWebInputEventModifiers(grfKeyState);

    if (m_brokerState) {
        const std::shared_ptr<BrokerState> state = m_brokerState;
        if (!candidateGuard.current() || brokerCancelled()) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK; // A nested terminal callback or Enter revoked this candidate.
        }
        const unsigned long long session = brokerSession;
        const std::function<bool()> cancelled = brokerCancelled;
        const blink::WebDragData dragData = dropDataToWebDragData(pDataObject);
        // GetData can re-enter this owner. Leave/Drop revoke even an unadmitted candidate.
        if (cancelled() || !candidateGuard.current() || dragData.items().size() == 0)
            return S_OK;
        const blink::WebPoint client(clientPoint.x, clientPoint.y);
        const blink::WebPoint screen(screenPoint.x, screenPoint.y);
        const blink::WebDragOperationsMask mask = blink::WebDragOperationCopy;
        m_brokerTargetCancelled = cancelled;
        m_brokerTargetSession = session;
        state->targetRevision = session;
        const bool posted = wke::brokerPostEngineTask(state->view, [state, session, dragData, client, screen, mask, modifiers, cancelled] {
            if (cancelled() || state->targetRevision != session)
                return;
            if (!state->cancelTarget() || cancelled() || state->targetRevision != session)
                return;
            state->engineTargetSession = session;
            try {
                state->engineView->dragTargetDragEnter(dragData, client, screen, mask, modifiers);
                if (cancelled() || state->targetRevision != session)
                    state->cancelTarget(session);
            } catch (...) {
                try { state->cancelTarget(session); } catch (...) { }
                throw;
            }
        });
        if (!posted)
            cancelBrokerTargetSession(session);
        *pdwEffect = posted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    } else if (WTF::isMainThread()) {
        blink::WebDragOperation op = m_webViewImpl->dragTargetDragEnter(dropDataToWebDragData(pDataObject),
            blink::WebPoint(clientPoint.x, clientPoint.y),
            blink::WebPoint(screenPoint.x, screenPoint.y),
            m_mask, modifiers);

        *pdwEffect = dragOperationToDragCursor(op);
    } else {
        m_taskCount++;

        DragEnterInfo* info = new DragEnterInfo();
        info->webViewImpl = m_webViewImpl;
        info->taskCount = &m_taskCount;
        info->data = dropDataToWebDragData(pDataObject);
        info->clientPoint = blink::WebPoint(clientPoint.x, clientPoint.y);
        info->screenPoint = blink::WebPoint(screenPoint.x, screenPoint.y);
        info->opAllowed = m_mask;
        info->modifiers = modifiers;
        blink::Platform::current()->mainThread()->postTask(FROM_HERE, WTF::bind(&dragTargetDragEnter, info));
        *pdwEffect = DROPEFFECT_MOVE;
    }

    m_lastDropEffect = *pdwEffect;
    //ASSERT(m_dragData.get() == pDataObject); // 外部拖入会触发这个断言

    return S_OK;
}

catch (...) {
    if (pdwEffect)
        *pdwEffect = DROPEFFECT_NONE;
    try {
        if (m_brokerState)
            wke::brokerRecordFailure(m_brokerState->view);
    } catch (...) { }
    return E_UNEXPECTED;
}

static void dragTargetDragOver(blink::WebViewImpl* m_webViewImpl, int* taskCount, const blink::WebPoint& clientPoint, const blink::WebPoint& screenPoint, blink::WebDragOperationsMask operationsAllowed, int modifiers)
{
    m_webViewImpl->dragTargetDragOver(clientPoint, screenPoint, operationsAllowed, modifiers);
    *taskCount -= 1;
}

HRESULT __stdcall DragHandle::DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
try
{
    if (!pdwEffect)
        return E_INVALIDARG;
    const DWORD sourceAllowedEffects = *pdwEffect;
    BrokerCallbackGuard brokerGuard(this);
    if (m_brokerState && (!brokerGuard.entered() || brokerGuard.cancelled())) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }
    if (!m_webViewImpl)
        return S_OK;
    const unsigned long long observedSession = m_brokerState ? m_brokerTargetSession : 0;
    const std::function<bool()> observedCancelled = m_brokerState ? m_brokerTargetCancelled : std::function<bool()>();
    
    if (m_dropTargetHelper)
        m_dropTargetHelper->DragOver((POINT*)&pt, *pdwEffect);

    POINT screenPoint = { 0 };
    ::GetCursorPos(&screenPoint);

    POINT clientPoint = screenPoint;
    ::ScreenToClient(m_viewWindow, &clientPoint);

    blink::WebDragOperation op = blink::WebDragOperationNone;
    int modifiers = keyStateToWebInputEventModifiers(grfKeyState);

    if (m_brokerState) {
        const std::shared_ptr<BrokerState> state = m_brokerState;
        const unsigned long long session = observedSession;
        const std::function<bool()> cancelled = observedCancelled;
        if (!session || m_brokerTargetSession != session || !cancelled || cancelled() || state->targetRevision != session) {
            *pdwEffect = DROPEFFECT_NONE;
            if (m_brokerTargetSession == session) {
                m_lastDropEffect = DROPEFFECT_NONE;
                cancelBrokerTargetSession(session);
            }
            return S_OK;
        }
        const blink::WebPoint client(clientPoint.x, clientPoint.y);
        const blink::WebPoint screen(screenPoint.x, screenPoint.y);
        if (!(sourceAllowedEffects & DROPEFFECT_COPY) || !(m_mask & blink::WebDragOperationCopy)) {
            *pdwEffect = DROPEFFECT_NONE;
            m_lastDropEffect = DROPEFFECT_NONE;
            return S_OK;
        }
        const blink::WebDragOperationsMask mask = blink::WebDragOperationCopy;
        const bool posted = wke::brokerPostEngineTask(state->view, [state, session, client, screen, mask, modifiers, cancelled] {
            if (!cancelled() && state->targetRevision == session && state->engineTargetSession == session)
                state->engineView->dragTargetDragOver(client, screen, mask, modifiers);
        });
        *pdwEffect = posted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    } else if (WTF::isMainThread()) {
        op = m_webViewImpl->dragTargetDragOver(
            blink::WebPoint(clientPoint.x, clientPoint.y),
            blink::WebPoint(screenPoint.x, screenPoint.y),
            m_mask, modifiers);

        *pdwEffect = DROPEFFECT_NONE;
        if (m_dragData)
            *pdwEffect = dragOperationToDragCursor(op);
        else
            *pdwEffect = DROPEFFECT_MOVE;
    } else {
        m_taskCount++;
        blink::Platform::current()->mainThread()->postTask(FROM_HERE, WTF::bind(&dragTargetDragOver, m_webViewImpl, &m_taskCount,
            blink::WebPoint(clientPoint.x, clientPoint.y),
            blink::WebPoint(screenPoint.x, screenPoint.y),
            m_mask, modifiers
            ));
        *pdwEffect = DROPEFFECT_MOVE;
    }
    m_lastDropEffect = *pdwEffect;
    return S_OK;
}

catch (...) {
    if (pdwEffect)
        *pdwEffect = DROPEFFECT_NONE;
    try {
        if (m_brokerState)
            wke::brokerRecordFailure(m_brokerState->view);
    } catch (...) { }
    return E_UNEXPECTED;
}

static void dragTargetDragLeave(blink::WebViewImpl* webViewImpl, int* taskCount)
{
    webViewImpl->dragTargetDragLeave();
    *taskCount -= 1;
}

HRESULT __stdcall DragHandle::DragLeave() 
try
{
    const unsigned long long observedSession = m_brokerState ? m_brokerTargetSession : 0;
    if (m_brokerState)
        m_brokerTargetCandidate = 0; // Terminal before any native hook/helper reentry.
    BrokerCallbackGuard brokerGuard(this);
    if (m_brokerState && (!brokerGuard.entered() || brokerGuard.cancelled()))
        return S_OK;
    if (m_brokerState)
        cancelBrokerTargetSession(observedSession); // Only the captured old admission, never a nested new one.
    if (m_dropTargetHelper)
        m_dropTargetHelper->DragLeave();

    if (m_webViewImpl) {
        if (m_brokerState) {
            // The captured controlled session was terminated before helper reentry.
        } else if (WTF::isMainThread()) {
            m_webViewImpl->dragTargetDragLeave();
        } else {
            m_taskCount++;
            blink::Platform::current()->mainThread()->postTask(FROM_HERE, WTF::bind(&dragTargetDragLeave, m_webViewImpl, &m_taskCount));
        }
        m_dragData = nullptr;
    }
    return S_OK;
}

catch (...) {
    try {
        if (m_brokerState)
            wke::brokerRecordFailure(m_brokerState->view);
    } catch (...) { }
    return E_UNEXPECTED;
}

static void dragTargetDrop(blink::WebViewImpl* webViewImpl, int* taskCount, const blink::WebPoint& clientPoint, const blink::WebPoint& screenPoint, int modifiers)
{
    webViewImpl->dragTargetDrop(clientPoint, screenPoint, modifiers);
    *taskCount -= 1;
}

HRESULT __stdcall DragHandle::Drop(IDataObject* pDataObject, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
try
{
    const unsigned long long observedSession = m_brokerState ? m_brokerTargetSession : 0;
    if (m_brokerState)
        m_brokerTargetCandidate = 0; // Drop is terminal even when Enter has not reached admission.
    if (!pdwEffect)
        return E_INVALIDARG;
    const DWORD sourceAllowedEffects = *pdwEffect;
    const std::function<bool()> observedCancelled = m_brokerState ? m_brokerTargetCancelled : std::function<bool()>();
    BrokerCallbackGuard brokerGuard(this);
    if (m_brokerState && (!brokerGuard.entered() || brokerGuard.cancelled())) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }
    if (!m_webViewImpl)
        return S_OK;

    if (m_dropTargetHelper)
        m_dropTargetHelper->Drop(pDataObject, (POINT*)&pt, *pdwEffect);

    m_dragData = nullptr;
    *pdwEffect = m_lastDropEffect;

    POINT screenPoint = { 0 };
    ::GetCursorPos(&screenPoint);

    POINT clientPoint = screenPoint;
    ::ScreenToClient(m_viewWindow, &clientPoint);

    if (m_brokerState) {
        const std::shared_ptr<BrokerState> state = m_brokerState;
        const unsigned long long session = observedSession;
        const std::function<bool()> cancelled = observedCancelled;
        if (!session || m_brokerTargetSession != session || !cancelled || cancelled() || state->targetRevision != session
            || !(sourceAllowedEffects & DROPEFFECT_COPY) || !(m_mask & blink::WebDragOperationCopy)
            || m_lastDropEffect != DROPEFFECT_COPY) {
            *pdwEffect = DROPEFFECT_NONE;
            cancelBrokerTargetSession(session);
            return S_OK;
        }
        const blink::WebDragData ownedDropData = dropDataToWebDragData(pDataObject);
        if (cancelled() || m_brokerTargetSession != session || state->targetRevision != session
            || ownedDropData.items().size() == 0) {
            *pdwEffect = DROPEFFECT_NONE;
            if (m_brokerTargetSession == session)
                cancelBrokerTargetSession(session);
            return S_OK;
        }
        const blink::WebPoint client(clientPoint.x, clientPoint.y);
        const blink::WebPoint screen(screenPoint.x, screenPoint.y);
        const int modifiers = keyStateToWebInputEventModifiers(grfKeyState);
        // COPY acknowledges owned bounded data plus counted admission only.
        // Engine Drop must retain the actual Enter session and its negotiation.
        const bool posted = wke::brokerPostEngineTask(state->view, [state, session, ownedDropData, client, screen, modifiers, cancelled] {
            if (cancelled() || state->targetRevision != session) {
                state->cancelTarget(session);
                return;
            }
            if (state->engineTargetSession != session)
                return; // No real Enter: do not fabricate a Drop or new Enter.
            try {
                if (!state->engineView->dragTargetUpdateDataForCursorBroker(ownedDropData)) {
                    state->cancelTarget(session);
                    wke::brokerRecordFailure(state->view);
                    return;
                }
                if (cancelled() || state->targetRevision != session) {
                    state->cancelTarget(session);
                    return;
                }
                state->engineView->dragTargetDrop(client, screen, modifiers);
                state->engineTargetSession = 0;
            } catch (...) {
                try { state->cancelTarget(session); } catch (...) { }
                throw;
            }
        });
        if (posted) {
            m_brokerTargetSession = 0;
            m_brokerTargetCancelled = nullptr;
        } else {
            cancelBrokerTargetSession(session);
        }
        *pdwEffect = posted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    } else if (WTF::isMainThread()) {
        m_webViewImpl->dragTargetDrop(blink::WebPoint(clientPoint.x, clientPoint.y),
            blink::WebPoint(screenPoint.x, screenPoint.y), keyStateToWebInputEventModifiers(grfKeyState));
    } else {
        m_taskCount++;
        blink::Platform::current()->mainThread()->postTask(FROM_HERE, WTF::bind(&dragTargetDrop, m_webViewImpl, &m_taskCount,
            blink::WebPoint(clientPoint.x, clientPoint.y),
            blink::WebPoint(screenPoint.x, screenPoint.y), keyStateToWebInputEventModifiers(grfKeyState)));
    }
    
    return S_OK;
}

catch (...) {
    if (pdwEffect)
        *pdwEffect = DROPEFFECT_NONE;
    try {
        if (m_brokerState)
            wke::brokerRecordFailure(m_brokerState->view);
    } catch (...) { }
    return E_UNEXPECTED;
}

HRESULT __stdcall DragHandle::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject)
        return E_POINTER;
    *ppvObject = nullptr;

    if (IsEqualGUID(riid, IID_IDropTarget) || IsEqualGUID(riid, IID_IUnknown))
        *ppvObject = static_cast<IDropTarget*>(this);
    else
        return E_NOINTERFACE;

    AddRef();
    return S_OK;
}

ULONG __stdcall DragHandle::AddRef()
{
    const ULONG count = ::InterlockedIncrement(&m_refCount);
    if (m_brokerState)
        wke::brokerAddComReference(m_brokerState->view);
    return count;
}

ULONG __stdcall DragHandle::Release()
{
    const wkeWebView view = m_brokerState ? m_brokerState->view : nullptr;
    const ULONG count = ::InterlockedDecrement(&m_refCount);
    if (view)
        wke::brokerReleaseComReference(view);
    return count;
}

ULONG DragHandle::getRefCount() const
{
    return ::InterlockedCompareExchange(const_cast<long*>(&m_refCount), 0, 0);
}

}