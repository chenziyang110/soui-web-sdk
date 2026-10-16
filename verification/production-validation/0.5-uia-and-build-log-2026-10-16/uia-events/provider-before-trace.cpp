#include <soui/runtime/accessibility/windows_uia_provider.h>

#include <UIAutomation.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

namespace soui::runtime {
namespace {

template <class F> HRESULT boundary(F&& body) noexcept {
  try { return body(); }
  catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
  catch (...) { return E_FAIL; }
}

bool identifier(const std::string& value) noexcept {
  if (value.empty() || value.size() > 128) return false;
  for (const unsigned char c : value) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':')) return false;
  }
  return true;
}

bool text(const std::wstring& value, std::size_t limit) noexcept {
  if (value.size() > limit) return false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const auto c = static_cast<unsigned>(value[i]);
    if (c == 0) return false;
    if (c >= 0xd800 && c <= 0xdbff) {
      if (++i == value.size()) return false;
      const auto low = static_cast<unsigned>(value[i]);
      if (low < 0xdc00 || low > 0xdfff) return false;
    } else if (c >= 0xdc00 && c <= 0xdfff) return false;
  }
  return true;
}

bool rectangle(const RECT& value) noexcept {
  return value.right >= value.left && value.bottom >= value.top;
}

bool same_binding(const uia_action_binding& a, const uia_action_binding& b) {
  return a.method == b.method && a.parameters_json == b.parameters_json &&
         a.value_parameter == b.value_parameter && ((!a.contract && !b.contract) ||
         (a.contract && b.contract && a.contract->encoded() == b.contract->encoded()));
}

bool binding(const uia_action_binding& action, bool value) {
  if (!identifier(action.method) || !action.contract || action.parameters_json.size() > 32768) return false;
  return value ? identifier(action.value_parameter) :
                 (action.value_parameter.empty() && action.contract->validates_input(action.parameters_json));
}

std::string quoted(const std::string& value) {
  std::string result{"\""};
  const char hex[] = "0123456789abcdef";
  for (const unsigned char c : value) {
    if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
    else if (c < 0x20) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
    else result += static_cast<char>(c);
  }
  result += '"';
  return result;
}

std::string value_input(const uia_action_binding& action, const std::wstring& value) {
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (!value.empty() && count <= 0) throw std::invalid_argument("value");
  std::string utf8(static_cast<std::size_t>(count), '\0');
  if (count && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), utf8.data(), count, nullptr, nullptr)) throw std::invalid_argument("value");
  return "{" + quoted(action.value_parameter) + ":" + quoted(utf8) + "}";
}

// UIAutomationCore is loaded only by opt-in creation. No static API invocation
// or import library is used, including for the reserved unsupported value.
struct system_api final {
  HMODULE module{};
  std::wstring path;
  decltype(&UiaReturnRawElementProvider) return_raw{};
  decltype(&UiaHostProviderFromHwnd) host_from_window{};
  decltype(&UiaGetReservedNotSupportedValue) unsupported{};
  decltype(&UiaRaiseAutomationPropertyChangedEvent) raise_property{};
  decltype(&UiaRaiseStructureChangedEvent) raise_structure{};
  decltype(&UiaRaiseAutomationEvent) raise_event{};
  std::mutex lock;
  bool pinned{};

  ~system_api() { if (module && !pinned) FreeLibrary(module); }
  HRESULT pin_for_call() {
    std::lock_guard<std::mutex> held(lock);
    if (pinned) return S_OK;
    HMODULE retained{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(return_raw), &retained) || retained != module)
      return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    pinned = true;  // Explicit process lifetime. Never FreeLibrary on a COM stack.
    return S_OK;
  }
  bool used() { std::lock_guard<std::mutex> held(lock); return pinned; }
  static std::shared_ptr<system_api> load() {
    wchar_t directory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return {};
    auto result = std::make_shared<system_api>();
    result->path.assign(directory, length);
    result->path += L"\\UIAutomationCore.dll";
    result->module = LoadLibraryW(result->path.c_str());
    if (!result->module) return {};
    result->return_raw = reinterpret_cast<decltype(return_raw)>(GetProcAddress(result->module, "UiaReturnRawElementProvider"));
    result->host_from_window = reinterpret_cast<decltype(host_from_window)>(GetProcAddress(result->module, "UiaHostProviderFromHwnd"));
    result->unsupported = reinterpret_cast<decltype(unsupported)>(GetProcAddress(result->module, "UiaGetReservedNotSupportedValue"));
    result->raise_property = reinterpret_cast<decltype(raise_property)>(GetProcAddress(result->module, "UiaRaiseAutomationPropertyChangedEvent"));
    result->raise_structure = reinterpret_cast<decltype(raise_structure)>(GetProcAddress(result->module, "UiaRaiseStructureChangedEvent"));
    result->raise_event = reinterpret_cast<decltype(raise_event)>(GetProcAddress(result->module, "UiaRaiseAutomationEvent"));
    return result->return_raw && result->host_from_window && result->unsupported && result->raise_property &&
      result->raise_structure && result->raise_event ? result : nullptr;
  }
};

struct record { uia_node node; std::uint64_t incarnation{}; };
struct snapshot { uia_projection projection; std::vector<record> records; };
enum class command_phase { queued, validating, committed, done, cancelled };
struct command final {
  uia_action_request request;
  std::wstring desired_value;
  HANDLE completion{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  command_phase phase{command_phase::queued};
  HRESULT result{E_FAIL};
  ~command() { if (completion) CloseHandle(completion); }
};

class element;
struct shared_state final {
  std::mutex lock;
  HWND window{};
  HWND original_window{};  // Immutable cache fact; mutable window requires lock/borrow.
  UINT wake{};
  DWORD owner{};
  std::uint64_t instance{};
  std::uint64_t generation{};
  std::uint64_t next_incarnation{1};
  bool closing{};
  bool closed{};
  bool cleanup_in_progress{};
  bool notification_wake_pending{};
  std::size_t api_borrows{};
  std::size_t callback_borrows{};
  std::shared_ptr<system_api> api;
  std::shared_ptr<const snapshot> view;
  std::deque<std::shared_ptr<command>> pending;
  element* root{};  // Non-owning FTM identity. Controller owns one actual ref.
};

std::atomic<std::uint64_t> next_instance{1};
std::uint64_t instance_id() noexcept {
  auto value = next_instance.load();
  while (value != (std::numeric_limits<std::uint64_t>::max)()) {
    if (next_instance.compare_exchange_weak(value, value + 1)) return value;
  }
  return 0;
}

const record* find(const snapshot& view, const std::string& id) noexcept {
  for (const auto& item : view.records) if (item.node.id == id) return &item;
  return nullptr;
}

struct read_view {
  std::shared_ptr<const snapshot> view;
  const record* item{};
};

struct api_borrow final {
  std::shared_ptr<shared_state> state;
  HWND window{};
  explicit api_borrow(const std::shared_ptr<shared_state>& value) : state(value) {
    std::lock_guard<std::mutex> held(state->lock);
    if (!state->closing) { ++state->api_borrows; window = state->window; }
  }
  ~api_borrow() {
    if (window) { std::lock_guard<std::mutex> held(state->lock); --state->api_borrows; }
    // Ordinary owner pumping retries close; no stale HWND post after borrow ends.
  }
};

class element final : public IRawElementProviderSimple, public IRawElementProviderFragment,
                      public IRawElementProviderFragmentRoot, public IInvokeProvider,
                      public IValueProvider, public ISelectionProvider, public ISelectionItemProvider {
 public:
  static HRESULT make(const std::shared_ptr<shared_state>& state, const record* item, element** result) {
    *result = nullptr;
    ULONG_PTR context{};
    const HRESULT initialized = CoGetContextToken(&context);
    if (FAILED(initialized)) return initialized;
    struct release { void operator()(element* value) const noexcept { value->Release(); } };
    std::unique_ptr<element, release> value(new element(state, item));
    const HRESULT status = CoCreateFreeThreadedMarshaler(static_cast<IRawElementProviderSimple*>(value.get()), &value->marshaler_);
    if (FAILED(status)) return status;
    if (!value->marshaler_) return E_FAIL;
    *result = value.release();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) noexcept override {
    if (!result) return E_POINTER;
    *result = nullptr;
    return boundary([&]() -> HRESULT {
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
      *result = static_cast<IRawElementProviderSimple*>(this);
    else if (iid == __uuidof(IRawElementProviderFragment)) *result = static_cast<IRawElementProviderFragment*>(this);
    else if (iid == __uuidof(IRawElementProviderFragmentRoot) && root_) *result = static_cast<IRawElementProviderFragmentRoot*>(this);
    else if (iid == __uuidof(IInvokeProvider) && !root_ && role_ == uia_role::button) *result = static_cast<IInvokeProvider*>(this);
    else if (iid == __uuidof(IValueProvider) && !root_ && role_ == uia_role::input && !secret_) *result = static_cast<IValueProvider*>(this);
    else if (iid == __uuidof(ISelectionProvider) && !root_ && role_ == uia_role::selection_list) *result = static_cast<ISelectionProvider*>(this);
    else if (iid == __uuidof(ISelectionItemProvider) && !root_ && role_ == uia_role::selection_item) *result = static_cast<ISelectionItemProvider*>(this);
    else if (iid == __uuidof(IMarshal) && marshaler_) return marshaler_->QueryInterface(iid, result);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
    });
  }
  ULONG STDMETHODCALLTYPE AddRef() noexcept override { return references_.fetch_add(1) + 1; }
  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const ULONG count = references_.fetch_sub(1) - 1;
    if (!count) delete this;
    return count;
  }

  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) noexcept override {
    if (!result) return E_POINTER;
    *result = static_cast<ProviderOptions>(0);
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      *result = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading); return S_OK; });
  }
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** result) noexcept override {
    if (!result) return E_POINTER;
    *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (pattern == UIA_InvokePatternId && !root_ && role_ == uia_role::button) return QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void**>(result));
      if (pattern == UIA_ValuePatternId && !root_ && role_ == uia_role::input && !secret_) return QueryInterface(__uuidof(IValueProvider), reinterpret_cast<void**>(result));
      if (pattern == UIA_SelectionPatternId && !root_ && role_ == uia_role::selection_list) return QueryInterface(__uuidof(ISelectionProvider), reinterpret_cast<void**>(result));
      if (pattern == UIA_SelectionItemPatternId && !root_ && role_ == uia_role::selection_item) return QueryInterface(__uuidof(ISelectionItemProvider), reinterpret_cast<void**>(result));
      return S_OK; });
  }
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* result) noexcept override {
    if (!result) return E_POINTER;
    VariantInit(result);
    return boundary([&]() -> HRESULT {
      read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      const auto* node = current.item ? &current.item->node : nullptr;
      if (property == UIA_NamePropertyId) return string_value(node ? node->name : current.view->projection.name, result);
      if (property == UIA_AutomationIdPropertyId) return string_value(root_ ? L"soui-uia-root" : std::wstring(id_.begin(), id_.end()), result);
      if (property == UIA_ControlTypePropertyId) { result->vt = VT_I4; result->lVal = control_type(); return S_OK; }
      if (property == UIA_NativeWindowHandlePropertyId && root_) { result->vt = VT_I4; result->lVal = HandleToLong(window_); return S_OK; }
      if (property == UIA_IsEnabledPropertyId) return boolean(!node || node->enabled, result);
      if (property == UIA_IsPasswordPropertyId) return boolean(node && node->secret, result);
      if (property == UIA_IsOffscreenPropertyId) return boolean(node && node->offscreen, result);
      if (property == UIA_IsControlElementPropertyId || property == UIA_IsContentElementPropertyId) return boolean(true, result);
      if (property == UIA_HasKeyboardFocusPropertyId) return boolean(node && current.view->projection.focused_id == id_, result);
      if (property == UIA_IsKeyboardFocusablePropertyId) return boolean(node && !node->focus.method.empty(), result);
      if (property == UIA_ItemStatusPropertyId) return string_value(node ? (node->busy ? L"busy" : node->status) : L"", result);
      if (property == UIA_ValueValuePropertyId && node && role_ == uia_role::input && !secret_) return string_value(node->value, result);
      if (property == UIA_ValueIsReadOnlyPropertyId && node && role_ == uia_role::input && !secret_) return boolean(node->read_only, result);
      if (property == UIA_SelectionCanSelectMultiplePropertyId && role_ == uia_role::selection_list) return boolean(false, result);
      if (property == UIA_SelectionIsSelectionRequiredPropertyId && role_ == uia_role::selection_list) return boolean(true, result);
      if (property == UIA_SelectionItemIsSelectedPropertyId && node && role_ == uia_role::selection_item) return boolean(node->selected, result);
      if (property == UIA_ValueValuePropertyId && secret_) {
        api_borrow borrowed(state_); if (!borrowed.window) return UIA_E_ELEMENTNOTAVAILABLE;
        const HRESULT pinned = state_->api->pin_for_call(); if (FAILED(pinned)) return pinned;
        IUnknown* unsupported{}; const HRESULT status = state_->api->unsupported(&unsupported);
        if (SUCCEEDED(status)) { result->vt = VT_UNKNOWN; result->punkVal = unsupported; }
        else if (unsupported) unsupported->Release();
        return status;
      }
      return S_OK;  // VT_EMPTY: property is not supplied; no secret text exists in state.
    });
  }
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) noexcept override {
    if (!result) return E_POINTER;
    *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (!root_) return S_OK;
      api_borrow borrowed(state_); if (!borrowed.window) return UIA_E_ELEMENTNOTAVAILABLE;
      const HRESULT pinned = state_->api->pin_for_call(); if (FAILED(pinned)) return pinned;
      const HRESULT status = state_->api->host_from_window(borrowed.window, result);
      if (FAILED(status) && *result) { (*result)->Release(); *result = nullptr; }
      return status; });
  }

  HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** result) noexcept override {
    if (!result) return E_POINTER;
    *result = nullptr;
    return boundary([&]() -> HRESULT {
      read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      const record* target{};
      if (direction == NavigateDirection_Parent) {
        if (root_) return S_OK;
        if (current.item->node.parent_id.empty()) return root_fragment(result);
        target = find(*current.view, current.item->node.parent_id);
      } else if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
        for (const auto& item : current.view->records) {
          if (item.node.parent_id == (root_ ? std::string{} : id_)) {
            target = &item; if (direction == NavigateDirection_FirstChild) break;
          }
        }
      } else if (direction == NavigateDirection_NextSibling || direction == NavigateDirection_PreviousSibling) {
        if (root_) return S_OK;
        const record* previous{};
        bool after{};
        for (const auto& item : current.view->records) {
          if (item.node.parent_id != current.item->node.parent_id) continue;
          if (after) { target = &item; break; }
          if (item.node.id == id_) {
            if (direction == NavigateDirection_PreviousSibling) { target = previous; break; }
            after = true;
          }
          previous = &item;
        }
      } else return E_INVALIDARG;
      return fragment(target, result);
    });
  }
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) noexcept override {
    if (!result) return E_POINTER;
    *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr) || root_) return hr;
      LONG values[] = {UiaAppendRuntimeId, static_cast<LONG>(state_->instance >> 32), static_cast<LONG>(state_->instance),
                      static_cast<LONG>(state_->generation >> 32), static_cast<LONG>(state_->generation),
                      static_cast<LONG>(incarnation_ >> 32), static_cast<LONG>(incarnation_)};
      SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, 7); if (!array) return E_OUTOFMEMORY;
      for (LONG index = 0; index != 7; ++index) {
        const HRESULT status = SafeArrayPutElement(array, &index, &values[index]);
        if (FAILED(status)) { SafeArrayDestroy(array); return status; }
      }
      *result = array; return S_OK; });
  }
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) noexcept override {
    if (!result) return E_POINTER;
    *result = {};
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      const RECT& box = current.item ? current.item->node.screen_bounds : current.view->projection.screen_bounds;
      result->left = box.left; result->top = box.top;
      result->width = static_cast<double>(box.right) - box.left;
      result->height = static_cast<double>(box.bottom) - box.top; return S_OK; });
  }
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) noexcept override {
    if (!result) return E_POINTER; *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; return read(current); });
  }
  HRESULT STDMETHODCALLTYPE SetFocus() noexcept override { return submit(uia_operation::focus, nullptr); }
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) noexcept override {
    if (!result) return E_POINTER; *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      std::lock_guard<std::mutex> held(state_->lock);
      if (state_->closing || !state_->root) return UIA_E_ELEMENTNOTAVAILABLE;
      return state_->root->QueryInterface(__uuidof(IRawElementProviderFragmentRoot), reinterpret_cast<void**>(result)); });
  }
  HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** result) noexcept override {
    if (!result) return E_POINTER; *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (!root_ || !std::isfinite(x) || !std::isfinite(y)) return E_INVALIDARG;
      for (auto item = current.view->records.rbegin(); item != current.view->records.rend(); ++item) {
        const auto& box = item->node.screen_bounds;
        if (!item->node.offscreen && x >= box.left && x < box.right && y >= box.top && y < box.bottom) return fragment(&*item, result);
      }
      const auto& box = current.view->projection.screen_bounds;
      if (x >= box.left && x < box.right && y >= box.top && y < box.bottom) return root_fragment(result);
      return S_OK; });
  }
  HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) noexcept override {
    if (!result) return E_POINTER; *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (!root_) return UIA_E_NOTSUPPORTED;
      return fragment(find(*current.view, current.view->projection.focused_id), result); });
  }

  HRESULT STDMETHODCALLTYPE Invoke() noexcept override { return submit(uia_operation::invoke, nullptr); }
  HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) noexcept override { return submit(uia_operation::set_value, value); }
  HRESULT STDMETHODCALLTYPE get_Value(BSTR* result) noexcept override {
    if (!result) return E_POINTER; *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (root_ || role_ != uia_role::input || secret_) return UIA_E_NOTSUPPORTED;
      *result = SysAllocStringLen(current.item->node.value.data(), static_cast<UINT>(current.item->node.value.size()));
      return *result ? S_OK : E_OUTOFMEMORY; });
  }
  HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* result) noexcept override {
    if (!result) return E_POINTER; *result = TRUE;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (root_ || role_ != uia_role::input || secret_) return UIA_E_NOTSUPPORTED;
      *result = current.item->node.read_only; return S_OK; });
  }
  HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** result) noexcept override {
    if (!result) return E_POINTER; *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (root_ || role_ != uia_role::selection_list) return UIA_E_NOTSUPPORTED;
      for (const auto& item : current.view->records) if (item.node.parent_id == id_ && item.node.selected) {
        element* child{}; const HRESULT made = make(state_, &item, &child); if (FAILED(made)) return made;
        SAFEARRAY* array = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
        if (!array) { child->Release(); return E_OUTOFMEMORY; }
        LONG index{}; IUnknown* object = static_cast<IRawElementProviderSimple*>(child);
        const HRESULT added = SafeArrayPutElement(array, &index, object); child->Release();
        if (FAILED(added)) { SafeArrayDestroy(array); return added; }
        *result = array; return S_OK;
      }
      return E_FAIL;  // Published required single-selection lists always have one item.
    });
  }
  HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* result) noexcept override { return selection_flag(false, result); }
  HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* result) noexcept override { return selection_flag(true, result); }
  HRESULT STDMETHODCALLTYPE Select() noexcept override { return submit(uia_operation::select, nullptr); }
  HRESULT STDMETHODCALLTYPE AddToSelection() noexcept override {
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (root_ || role_ != uia_role::selection_item) return UIA_E_NOTSUPPORTED;
      return current.item->node.selected ? S_OK : UIA_E_INVALIDOPERATION; });
  }
  HRESULT STDMETHODCALLTYPE RemoveFromSelection() noexcept override {
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (root_ || role_ != uia_role::selection_item) return UIA_E_NOTSUPPORTED;
      return current.item->node.selected ? UIA_E_INVALIDOPERATION : S_OK; });
  }
  HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* result) noexcept override {
    if (!result) return E_POINTER; *result = FALSE;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (root_ || role_ != uia_role::selection_item) return UIA_E_NOTSUPPORTED;
      *result = current.item->node.selected; return S_OK; });
  }
  HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** result) noexcept override {
    if (!result) return E_POINTER; *result = nullptr;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (root_ || role_ != uia_role::selection_item) return UIA_E_NOTSUPPORTED;
      const auto* parent = find(*current.view, current.item->node.parent_id);
      element* value{}; const HRESULT made = make(state_, parent, &value);
      if (SUCCEEDED(made)) *result = static_cast<IRawElementProviderSimple*>(value); return made; });
  }

 private:
  element(std::shared_ptr<shared_state> state, const record* item)
      : state_(std::move(state)), root_(!item), role_(item ? item->node.role : uia_role::task_status),
        secret_(item && item->node.secret), id_(item ? item->node.id : std::string{}),
        incarnation_(item ? item->incarnation : 0), window_(state_->original_window) {}
  ~element() { if (marshaler_) marshaler_->Release(); }
  HRESULT read(read_view& result) const {
    std::lock_guard<std::mutex> held(state_->lock);
    if (state_->closing || !state_->view) return UIA_E_ELEMENTNOTAVAILABLE;
    result.view = state_->view;
    if (!root_) {
      result.item = find(*result.view, id_);
      if (!result.item || result.item->incarnation != incarnation_) return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return S_OK;
  }
  HRESULT fragment(const record* item, IRawElementProviderFragment** result) {
    if (!item) return S_OK;
    element* value{}; const HRESULT made = make(state_, item, &value);
    if (SUCCEEDED(made)) *result = static_cast<IRawElementProviderFragment*>(value);
    return made;
  }
  HRESULT root_fragment(IRawElementProviderFragment** result) {
    std::lock_guard<std::mutex> held(state_->lock);
    if (state_->closing || !state_->root) return UIA_E_ELEMENTNOTAVAILABLE;
    return state_->root->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(result));
  }
  static HRESULT boolean(bool value, VARIANT* result) noexcept {
    result->vt = VT_BOOL; result->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE; return S_OK;
  }
  static HRESULT string_value(const std::wstring& value, VARIANT* result) noexcept {
    BSTR text_value = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
    if (!text_value) return E_OUTOFMEMORY;
    result->vt = VT_BSTR; result->bstrVal = text_value; return S_OK;
  }
  LONG control_type() const noexcept {
    if (root_) return UIA_PaneControlTypeId;
    switch (role_) {
      case uia_role::button: return UIA_ButtonControlTypeId;
      case uia_role::input: return UIA_EditControlTypeId;
      case uia_role::selection_list: return UIA_ListControlTypeId;
      case uia_role::selection_item: return UIA_ListItemControlTypeId;
      case uia_role::task_status: return UIA_TextControlTypeId;
    }
    return UIA_CustomControlTypeId;
  }
  HRESULT selection_flag(bool value, BOOL* result) noexcept {
    if (!result) return E_POINTER; *result = FALSE;
    return boundary([&]() -> HRESULT { read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
      if (root_ || role_ != uia_role::selection_list) return UIA_E_NOTSUPPORTED;
      *result = value; return S_OK; });
  }
  HRESULT submit(uia_operation operation, LPCWSTR value) noexcept;
  std::atomic<ULONG> references_{1};
  std::shared_ptr<shared_state> state_;
  const bool root_;
  const uia_role role_;
  const bool secret_;
  const std::string id_;
  const std::uint64_t incarnation_;
  const HWND window_;
  IUnknown* marshaler_{};
};

HRESULT element::submit(uia_operation operation, LPCWSTR value) noexcept {
  return boundary([&]() -> HRESULT {
    read_view current; const HRESULT hr = read(current); if (FAILED(hr)) return hr;
    if (root_) return UIA_E_NOTSUPPORTED;
    const auto& node = current.item->node;
    if (!node.enabled) return UIA_E_ELEMENTNOTENABLED;
    if (node.busy) return UIA_E_INVALIDOPERATION;
    const bool synchronous = operation != uia_operation::invoke;
    if (synchronous && GetCurrentThreadId() == state_->owner) return UIA_E_INVALIDOPERATION;
    const auto& action = operation == uia_operation::focus ? node.focus : node.action;
    if (action.method.empty()) return UIA_E_NOTSUPPORTED;
    if ((operation == uia_operation::invoke && role_ != uia_role::button) ||
        (operation == uia_operation::set_value && (role_ != uia_role::input || secret_ || node.read_only)) ||
        (operation == uia_operation::select && role_ != uia_role::selection_item)) return UIA_E_NOTSUPPORTED;
    std::string input = action.parameters_json;
    std::wstring desired;
    if (operation == uia_operation::set_value) {
      if (!value) return E_INVALIDARG;
      std::size_t length{};
      while (length <= windows_uia_provider::max_value_units && value[length]) ++length;
      if (length > windows_uia_provider::max_value_units) return E_INVALIDARG;
      desired.assign(value, length);
      if (!text(desired, windows_uia_provider::max_value_units)) return E_INVALIDARG;
      input = value_input(action, desired);
    }
    if (input.size() > 32768 || !action.contract->validates_input(input)) return E_INVALIDARG;
    auto work = std::make_shared<command>(); if (!work->completion) return E_OUTOFMEMORY;
    work->request = {state_->generation, incarnation_, id_, action.method, std::move(input), operation};
    work->desired_value = std::move(desired);
    {
      std::lock_guard<std::mutex> held(state_->lock);
      const auto* present = state_->view ? find(*state_->view, id_) : nullptr;
      if (state_->closing || !present || present->incarnation != incarnation_) return UIA_E_ELEMENTNOTAVAILABLE;
      if (state_->pending.size() >= windows_uia_provider::max_pending) return UIA_E_TIMEOUT;
      state_->pending.push_back(work);
      // PostMessage cannot synchronously invoke UI/business code. Holding this
      // lock prevents close/HWND reuse between admission and the actual post.
      if (!PostMessageW(state_->window, state_->wake, static_cast<WPARAM>(state_->instance), 0)) {
        state_->pending.pop_back(); return E_FAIL;
      }
    }
    if (!synchronous) return S_OK;
    const DWORD waited = WaitForSingleObject(work->completion, windows_uia_provider::synchronous_wait_ms);
    std::lock_guard<std::mutex> held(state_->lock);
    if (work->phase == command_phase::done || work->phase == command_phase::cancelled) return work->result;
    if (work->phase == command_phase::queued || work->phase == command_phase::validating) {
      work->phase = command_phase::cancelled;
      work->result = waited == WAIT_FAILED ? E_FAIL : UIA_E_TIMEOUT;
    }
    // Committed work may already have side effects; timeout does not roll it back.
    return waited == WAIT_FAILED ? E_FAIL : UIA_E_TIMEOUT;
  });
}

enum class notification_kind { property, structure, focus };
struct notification final {
  notification_kind kind{notification_kind::property};
  const record* before{};
  const record* after{};
  PROPERTYID property{};
};
struct notification_batch final {
  std::shared_ptr<const snapshot> before, after;
  std::vector<notification> items;
  std::size_t cursor{};
};
bool equal_rect(const RECT& left, const RECT& right) noexcept {
  return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}
notification_batch changes(std::shared_ptr<const snapshot> before, std::shared_ptr<const snapshot> after) {
  notification_batch batch; batch.before = std::move(before); batch.after = std::move(after);
  const auto& old = *batch.before; const auto& current = *batch.after;
  auto property = [&](bool changed, const record* a, const record* b, PROPERTYID id) {
    if (changed) batch.items.push_back({notification_kind::property, a, b, id});
  };
  property(old.projection.name != current.projection.name, nullptr, nullptr, UIA_NamePropertyId);
  property(!equal_rect(old.projection.screen_bounds, current.projection.screen_bounds), nullptr, nullptr, UIA_BoundingRectanglePropertyId);
  bool structure = old.records.size() != current.records.size();
  for (std::size_t index = 0; index != current.records.size(); ++index) {
    const auto& b = current.records[index]; const auto* a = find(old, b.node.id);
    if (index >= old.records.size() || old.records[index].incarnation != b.incarnation) structure = true;
    if (!a || a->incarnation != b.incarnation) continue; // New identities are discovered through structure invalidation.
    property(a->node.name != b.node.name, a, &b, UIA_NamePropertyId);
    property(a->node.enabled != b.node.enabled, a, &b, UIA_IsEnabledPropertyId);
    property(a->node.offscreen != b.node.offscreen, a, &b, UIA_IsOffscreenPropertyId);
    property(!equal_rect(a->node.screen_bounds, b.node.screen_bounds), a, &b, UIA_BoundingRectanglePropertyId);
    property((old.projection.focused_id == b.node.id) != (current.projection.focused_id == b.node.id), a, &b, UIA_HasKeyboardFocusPropertyId);
    property((a->node.busy ? L"busy" : a->node.status) != (b.node.busy ? L"busy" : b.node.status), a, &b, UIA_ItemStatusPropertyId);
    if (b.node.role == uia_role::input && !b.node.secret)
      property(a->node.value != b.node.value, a, &b, UIA_ValueValuePropertyId);
    if (b.node.role == uia_role::selection_item)
      property(a->node.selected != b.node.selected, a, &b, UIA_SelectionItemIsSelectedPropertyId);
  }
  if (structure) batch.items.insert(batch.items.begin(), {notification_kind::structure, nullptr, nullptr, 0});
  if (!current.projection.focused_id.empty() && old.projection.focused_id != current.projection.focused_id)
    batch.items.push_back({notification_kind::focus, nullptr, find(current, current.projection.focused_id), 0});
  return batch;
}
class event_variant final {
 public:
  VARIANT value{};
  event_variant() noexcept { VariantInit(&value); }
  ~event_variant() { VariantClear(&value); }
  event_variant(const event_variant&) = delete;
  event_variant& operator=(const event_variant&) = delete;
  HRESULT set(PROPERTYID property, const snapshot& view, const record* item) {
    const auto* node = item ? &item->node : nullptr;
    if (property == UIA_BoundingRectanglePropertyId) {
      const auto& bounds = node ? node->screen_bounds : view.projection.screen_bounds;
      const double numbers[]{static_cast<double>(bounds.left), static_cast<double>(bounds.top),
        static_cast<double>(bounds.right) - bounds.left, static_cast<double>(bounds.bottom) - bounds.top};
      value.parray = SafeArrayCreateVector(VT_R8, 0, 4);
      if (!value.parray) return E_OUTOFMEMORY;
      value.vt = VT_ARRAY | VT_R8;
      for (LONG index = 0; index != 4; ++index) {
        double number = numbers[index]; const auto status = SafeArrayPutElement(value.parray, &index, &number);
        if (FAILED(status)) return status;
      }
      return S_OK;
    }
    if (property == UIA_NamePropertyId || property == UIA_ItemStatusPropertyId || property == UIA_ValueValuePropertyId) {
      const std::wstring data = property == UIA_NamePropertyId ? (node ? node->name : view.projection.name) :
        property == UIA_ItemStatusPropertyId ? (node->busy ? L"busy" : node->status) : node->value;
      value.bstrVal = SysAllocStringLen(data.data(), static_cast<UINT>(data.size()));
      if (!value.bstrVal) return E_OUTOFMEMORY;
      value.vt = VT_BSTR; return S_OK;
    }
    const bool flag = property == UIA_IsEnabledPropertyId ? node->enabled :
      property == UIA_IsOffscreenPropertyId ? node->offscreen :
      property == UIA_HasKeyboardFocusPropertyId ? view.projection.focused_id == node->id : node->selected;
    value.vt = VT_BOOL; value.boolVal = flag ? VARIANT_TRUE : VARIANT_FALSE; return S_OK;
  }
};
struct notification_borrow final {
  std::shared_ptr<shared_state> state;
  explicit notification_borrow(std::shared_ptr<shared_state> shared) : state(std::move(shared)) {
    std::lock_guard<std::mutex> held(state->lock); ++state->callback_borrows;
  }
  ~notification_borrow() { std::lock_guard<std::mutex> held(state->lock); --state->callback_borrows; }
};
HRESULT drain_notifications(const std::shared_ptr<shared_state>& shared, notification_batch& batch,
    std::shared_ptr<const snapshot>& delivered) {
  api_borrow api(shared); if (!api.window) return S_FALSE;
  notification_borrow notifying(shared); // Blocks recursive pump and retains owner captures through system calls.
  const auto pinned = shared->api->pin_for_call(); if (FAILED(pinned)) return pinned;
  if (!batch.after) {
    std::shared_ptr<const snapshot> latest;
    { std::lock_guard<std::mutex> held(shared->lock); latest = shared->view; }
    if (latest == delivered) return S_OK;
    batch = changes(delivered, std::move(latest));
  }
  for (unsigned emitted = 0; batch.cursor < batch.items.size() && emitted != 16; ++emitted) {
    { std::lock_guard<std::mutex> held(shared->lock); if (shared->closing) return S_FALSE; }
    const auto& work = batch.items[batch.cursor];
    element* target{};
    if (work.after) {
      const auto made = element::make(shared, work.after, &target); if (FAILED(made)) return made;
    } else { std::lock_guard<std::mutex> held(shared->lock); target = shared->root; target->AddRef(); }
    struct release { void operator()(element* value) const noexcept { value->Release(); } };
    std::unique_ptr<element, release> retained(target);
    HRESULT status{};
    if (work.kind == notification_kind::structure)
      status = shared->api->raise_structure(target, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    else if (work.kind == notification_kind::focus)
      status = shared->api->raise_event(target, UIA_AutomationFocusChangedEventId);
    else {
      event_variant old, current;
      status = old.set(work.property, *batch.before, work.before); if (FAILED(status)) return status;
      status = current.set(work.property, *batch.after, work.after); if (FAILED(status)) return status;
      status = shared->api->raise_property(target, work.property, old.value, current.value);
    }
    if (FAILED(status)) return status;
    ++batch.cursor;
  }
  if (batch.cursor == batch.items.size()) { delivered = batch.after; batch = {}; }
  std::lock_guard<std::mutex> held(shared->lock);
  if (!shared->closing && (batch.after || shared->view != delivered) && !shared->notification_wake_pending) {
    if (!PostMessageW(shared->window, shared->wake, static_cast<WPARAM>(shared->instance), 0))
      return HRESULT_FROM_WIN32(GetLastError());
    shared->notification_wake_pending = true;
  }
  return S_OK;
}

}  // namespace

struct windows_uia_provider::impl final {
  std::shared_ptr<shared_state> shared;
  policy allow;
  dispatcher dispatch;
  notification_batch notifications;
  std::shared_ptr<const snapshot> notified;
  element* root{};
  ~impl() { if (root) root->Release(); }
};

windows_uia_provider::windows_uia_provider(std::unique_ptr<impl> state) noexcept : state_(std::move(state)) {}
windows_uia_provider::~windows_uia_provider() {
  // Destroying an owner/its captured business objects while a callback/API still
  // borrows them cannot be repaired by leaking or pretending close completed.
  if (state_ && (GetCurrentThreadId() != state_->shared->owner || close() != S_OK)) std::terminate();
}

HRESULT windows_uia_provider::create(HWND window, UINT wake_message, std::uint64_t generation,
                                    policy allow, dispatcher dispatch,
                                    std::unique_ptr<windows_uia_provider>& result) noexcept {
  if (result) return E_INVALIDARG;
  return boundary([&]() -> HRESULT {
    DWORD process{};
    if (!window || !IsWindow(window) || GetWindowThreadProcessId(window, &process) != GetCurrentThreadId() ||
        process != GetCurrentProcessId() || !generation || wake_message < WM_APP || wake_message > 0xbfff) return E_INVALIDARG;
    ULONG_PTR context{};
    const HRESULT initialized = CoGetContextToken(&context);
    if (FAILED(initialized)) return initialized;
    auto state = std::make_unique<impl>();
    state->shared = std::make_shared<shared_state>();
    state->shared->window = window; state->shared->original_window = window;
    state->shared->owner = GetCurrentThreadId(); state->shared->wake = wake_message;
    state->shared->generation = generation; state->shared->instance = instance_id();
    if (!state->shared->instance) return E_OUTOFMEMORY;
    state->shared->api = system_api::load();
    if (!state->shared->api) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    auto initial = std::make_shared<snapshot>(); initial->projection.document_generation = generation;
    state->shared->view = std::move(initial);
    state->notified = state->shared->view;
    state->allow = std::move(allow); state->dispatch = std::move(dispatch);
    const HRESULT made = element::make(state->shared, nullptr, &state->root);
    // CO_E_NOTINITIALIZED is preserved: application must initialize COM first.
    if (FAILED(made)) return made;
    state->shared->root = state->root;
    result.reset(new windows_uia_provider(std::move(state)));
    return S_OK;
  });
}

HRESULT windows_uia_provider::publish(const uia_projection& projection) noexcept {
  return boundary([&]() -> HRESULT {
    const auto shared = state_->shared;
    if (GetCurrentThreadId() != shared->owner) return RPC_E_WRONG_THREAD;
    if (projection.document_generation != shared->generation) return UIA_E_ELEMENTNOTAVAILABLE;
    if (projection.nodes.size() > max_nodes || !text(projection.name, 1024) || !rectangle(projection.screen_bounds)) return E_INVALIDARG;
    std::size_t bytes = projection.name.size() * sizeof(wchar_t) + projection.focused_id.size();
    for (const auto& node : projection.nodes) {
      if (!identifier(node.id) || !text(node.name, 1024) || !text(node.status, 1024) ||
          !text(node.value, max_value_units) || !rectangle(node.screen_bounds)) return E_INVALIDARG;
      if (node.role != uia_role::button && node.role != uia_role::input && node.role != uia_role::selection_list &&
          node.role != uia_role::selection_item && node.role != uia_role::task_status) return E_INVALIDARG;
      if (node.secret && (node.role != uia_role::input || !node.value.empty() || !node.status.empty() || !node.action.method.empty())) return E_INVALIDARG;
      if (node.role != uia_role::input && (!node.value.empty() || node.secret || node.read_only)) return E_INVALIDARG;
      if (node.role != uia_role::selection_item && (!node.parent_id.empty() || node.selected)) return E_INVALIDARG;
      if (node.role == uia_role::selection_item && !identifier(node.parent_id)) return E_INVALIDARG;
      if (node.role == uia_role::button || node.role == uia_role::selection_item ||
          (node.role == uia_role::input && !node.secret && !node.read_only)) {
        if (!binding(node.action, node.role == uia_role::input)) return E_INVALIDARG;
      } else if (!node.action.method.empty()) return E_INVALIDARG;
      if (!node.focus.method.empty() && !binding(node.focus, false)) return E_INVALIDARG;
      bytes += node.id.size() + node.parent_id.size() + (node.name.size() + node.value.size() + node.status.size()) * sizeof(wchar_t);
      for (const auto* action : {&node.action, &node.focus}) {
        if (action->method.empty() && (action->contract || action->parameters_json != "{}" || !action->value_parameter.empty())) return E_INVALIDARG;
        bytes += action->method.size() + action->parameters_json.size() + action->value_parameter.size();
        if (action->contract) bytes += action->contract->encoded().size();
      }
      if (bytes > max_public_bytes) return E_INVALIDARG;
    }
    for (std::size_t index = 0; index != projection.nodes.size(); ++index) {
      const auto& node = projection.nodes[index];
      for (std::size_t other = index + 1; other != projection.nodes.size(); ++other)
        if (node.id == projection.nodes[other].id) return E_INVALIDARG;
      if (node.role == uia_role::selection_item) {
        const auto parent = std::find_if(projection.nodes.begin(), projection.nodes.end(), [&](const uia_node& candidate) { return candidate.id == node.parent_id; });
        if (parent == projection.nodes.end() || parent->role != uia_role::selection_list) return E_INVALIDARG;
      }
      if (node.role == uia_role::selection_list) {
        std::size_t selected{};
        for (const auto& child : projection.nodes) if (child.parent_id == node.id && child.selected) ++selected;
        if (selected != 1) return E_INVALIDARG;
      }
    }
    if (!projection.focused_id.empty()) {
      const auto focused = std::find_if(projection.nodes.begin(), projection.nodes.end(), [&](const uia_node& node) { return node.id == projection.focused_id; });
      if (focused == projection.nodes.end() || focused->focus.method.empty()) return E_INVALIDARG;
    }
    auto next = std::make_shared<snapshot>(); next->projection = projection;
    std::lock_guard<std::mutex> held(shared->lock);
    if (shared->closing) return UIA_E_ELEMENTNOTAVAILABLE;
    auto serial = shared->next_incarnation;
    for (const auto& node : projection.nodes) {
      const auto* previous = find(*shared->view, node.id);
      std::uint64_t incarnation{};
      if (previous && previous->node.role == node.role && previous->node.secret == node.secret &&
          previous->node.read_only == node.read_only && previous->node.parent_id == node.parent_id &&
          same_binding(previous->node.action, node.action) && same_binding(previous->node.focus, node.focus)) incarnation = previous->incarnation;
      else {
        if (serial == (std::numeric_limits<std::uint64_t>::max)()) return E_OUTOFMEMORY;
        incarnation = serial++;
      }
      next->records.push_back({node, incarnation});
    }
    if (!shared->notification_wake_pending) {
      if (!PostMessageW(shared->window, shared->wake, static_cast<WPARAM>(shared->instance), 0)) return HRESULT_FROM_WIN32(GetLastError());
      shared->notification_wake_pending = true;
    }
    shared->next_incarnation = serial; shared->view = std::move(next); return S_OK;
  });
}

HRESULT windows_uia_provider::pump() noexcept {
  return boundary([&]() -> HRESULT {
    const auto shared = state_->shared;
    if (GetCurrentThreadId() != shared->owner) return RPC_E_WRONG_THREAD;
    {
      std::lock_guard<std::mutex> held(shared->lock);
      if (shared->callback_borrows != 0) return S_FALSE;  // No recursive business pump.
    }
    for (unsigned batch = 0; batch != 8; ++batch) {
      std::shared_ptr<command> work;
      std::shared_ptr<const action_contract> contract;
      {
        std::lock_guard<std::mutex> held(shared->lock);
        if (shared->closing) break;
        if (shared->pending.empty()) break;
        work = shared->pending.front(); shared->pending.pop_front();
        if (work->phase == command_phase::cancelled) continue;
        const auto* item = find(*shared->view, work->request.node_id);
        if (!item || item->incarnation != work->request.incarnation) work->result = UIA_E_ELEMENTNOTAVAILABLE;
        else if (!item->node.enabled) work->result = UIA_E_ELEMENTNOTENABLED;
        else if (item->node.busy) work->result = UIA_E_INVALIDOPERATION;
        else {
          contract = (work->request.operation == uia_operation::focus ? item->node.focus : item->node.action).contract;
          work->phase = command_phase::validating; ++shared->callback_borrows;
        }
        if (!contract) work->phase = command_phase::done;
      }
      if (contract) {
        HRESULT status = boundary([&]() -> HRESULT {
          if (!contract->validates_input(work->request.parameters_json)) return E_INVALIDARG;
          if (!state_->allow || !state_->dispatch || !state_->allow(work->request)) return E_ACCESSDENIED;
          {
            std::lock_guard<std::mutex> held(shared->lock);
            const auto* current = find(*shared->view, work->request.node_id);
            if (shared->closing || !current || current->incarnation != work->request.incarnation) return UIA_E_ELEMENTNOTAVAILABLE;
            if (work->phase == command_phase::cancelled) return work->result;
            if (!current->node.enabled) return UIA_E_ELEMENTNOTENABLED;
            if (current->node.busy) return UIA_E_INVALIDOPERATION;
            work->phase = command_phase::committed;
          }
          const auto outcome = state_->dispatch(work->request);
          if (FAILED(outcome.result)) return outcome.result;
          if (!contract->validates_output(outcome.output_json)) return E_INVALIDARG;
          {
            std::lock_guard<std::mutex> held(shared->lock);
            const auto* current = find(*shared->view, work->request.node_id);
            if (shared->closing || !current || current->incarnation != work->request.incarnation) return UIA_E_ELEMENTNOTAVAILABLE;
            // A synchronous success must describe the observable owner result,
            // not merely acceptance by a dispatcher that will update later.
            if ((work->request.operation == uia_operation::set_value && current->node.value != work->desired_value) ||
                (work->request.operation == uia_operation::select && !current->node.selected) ||
                (work->request.operation == uia_operation::focus && shared->view->projection.focused_id != work->request.node_id))
              return UIA_E_INVALIDOPERATION;
          }
          return outcome.result;
        });
        {
          std::lock_guard<std::mutex> held(shared->lock);
          --shared->callback_borrows;
          if (shared->closing) status = UIA_E_ELEMENTNOTAVAILABLE;
          if (work->phase != command_phase::cancelled) { work->phase = command_phase::done; work->result = status; }
        }
      }
      SetEvent(work->completion);
    }
    bool closing{};
    { std::lock_guard<std::mutex> held(shared->lock); closing = shared->closing; }
    if (closing) return close();
    const auto notified = drain_notifications(shared, state_->notifications, state_->notified);
    { std::lock_guard<std::mutex> held(shared->lock); closing = shared->closing; }
    if (closing) return close();
    if (FAILED(notified)) return notified;
    // Additional batches are reached through actual queued wake messages or the
    // application's ordinary pump. No unbounded drain on the owner thread.
    return S_OK;
  });
}

bool windows_uia_provider::handle_message(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) noexcept {
  result = 0;
  if (GetCurrentThreadId() != state_->shared->owner) return false;
  if (message == state_->shared->wake && wparam == static_cast<WPARAM>(state_->shared->instance)) {
    { std::lock_guard<std::mutex> held(state_->shared->lock); state_->shared->notification_wake_pending = false; }
    result = pump(); return true;
  }
  if (message != WM_GETOBJECT || static_cast<DWORD>(lparam) != static_cast<DWORD>(UiaRootObjectId)) return false;
  const HRESULT status = boundary([&]() -> HRESULT {
    api_borrow borrowed(state_->shared); if (!borrowed.window) return UIA_E_ELEMENTNOTAVAILABLE;
    const HRESULT pinned = state_->shared->api->pin_for_call(); if (FAILED(pinned)) return pinned;
    result = state_->shared->api->return_raw(borrowed.window, wparam, lparam, static_cast<IRawElementProviderSimple*>(state_->root));
    return S_OK;
  });
  if (FAILED(status)) result = 0;
  return true;
}

HRESULT windows_uia_provider::root(IRawElementProviderSimple** result) noexcept {
  if (!result) return E_POINTER; *result = nullptr;
  return boundary([&]() -> HRESULT {
    if (GetCurrentThreadId() != state_->shared->owner) return RPC_E_WRONG_THREAD;
    std::lock_guard<std::mutex> held(state_->shared->lock);
    if (state_->shared->closing || !state_->root) return UIA_E_ELEMENTNOTAVAILABLE;
    return state_->root->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(result));
  });
}

uia_capability windows_uia_provider::capability() const noexcept {
  try {
    std::lock_guard<std::mutex> held(state_->shared->lock);
    return state_->shared->closing ? uia_capability::closed : uia_capability::available;
  } catch (...) { return uia_capability::closed; }
}

HRESULT windows_uia_provider::close() noexcept {
  return boundary([&]() -> HRESULT {
    const auto shared = state_->shared;
    if (GetCurrentThreadId() != shared->owner) return RPC_E_WRONG_THREAD;
    std::deque<std::shared_ptr<command>> cancelled;
    HWND window{};
    bool complete{};
    {
      std::lock_guard<std::mutex> held(shared->lock);
      if (shared->closed) return S_OK;
      shared->closing = true;
      cancelled.swap(shared->pending);
      for (auto& work : cancelled) { work->phase = command_phase::cancelled; work->result = UIA_E_ELEMENTNOTAVAILABLE; }
      complete = shared->api_borrows == 0 && shared->callback_borrows == 0 && !shared->cleanup_in_progress;
      if (complete) {
        shared->cleanup_in_progress = true;
        window = shared->window; shared->window = nullptr; shared->root = nullptr;
      }
    }
    for (const auto& work : cancelled) SetEvent(work->completion);
    if (!complete) return S_FALSE;
    if (shared->api->used()) shared->api->return_raw(window, 0, 0, nullptr);
    state_->root->Release(); state_->root = nullptr;
    state_->allow = {}; state_->dispatch = {};
    state_->notifications = {}; state_->notified.reset();
    {
      std::lock_guard<std::mutex> held(shared->lock);
      shared->closed = true; shared->cleanup_in_progress = false;
    }
    // The bounded inert snapshot contains only reviewed public data; external
    // COM refs do not retain business captures or HWND access after this point.
    return S_OK;
  });
}

}  // namespace soui::runtime
