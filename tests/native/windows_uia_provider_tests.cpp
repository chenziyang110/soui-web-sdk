#include <soui/runtime/accessibility/windows_uia_provider.h>
#include <UIAutomation.h>
#include <objbase.h>
#include <json.hpp>

#include <atomic>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Actual native tests, not executed by the candidate author. Root owns both
// architecture builds and the desktop lease. No UIAutomationCore import library.
namespace {
using namespace soui::runtime;
constexpr UINT wake_message = WM_APP + 0x477;
constexpr wchar_t class_name[] = L"SouiFiniteUiaProviderTests";

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void ok(HRESULT status, const char* message) { require(status == S_OK, message); }

template<class T> class com_ref final {
 public:
  com_ref() = default;
  ~com_ref() { reset(); }
  com_ref(const com_ref&) = delete;
  com_ref& operator=(const com_ref&) = delete;
  com_ref(com_ref&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
  com_ref& operator=(com_ref&& other) noexcept { if (this != &other) { reset(); value_ = std::exchange(other.value_, nullptr); } return *this; }
  void reset(T* value = nullptr) noexcept { if (value_) value_->Release(); value_ = value; }
  T* get() const noexcept { return value_; }
  T* operator->() const noexcept { return value_; }
  T** put() noexcept { reset(); return &value_; }
 private: T* value_{};
};
template<class T, class U> com_ref<T> query(U* object) {
  com_ref<T> result;
  ok(object->QueryInterface(__uuidof(T), reinterpret_cast<void**>(result.put())), "COM QI");
  return result;
}
class event final {
 public:
  event() : value_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) { require(value_ != nullptr, "CreateEvent"); }
  ~event() { CloseHandle(value_); }
  HANDLE get() const noexcept { return value_; }
 private: HANDLE value_{};
};
class apartment final {
 public:
  explicit apartment(DWORD kind) : result_(CoInitializeEx(nullptr, kind)) { require(SUCCEEDED(result_), "COM initialize"); }
  ~apartment() { CoUninitialize(); }
 private: HRESULT result_{};
};

std::shared_ptr<const action_contract> contract(const char* properties, const char* required) {
  const std::string encoded = std::string("{\"protocol\":\"soui.action/1\",\"title\":\"UIA native test\",\"description\":\"\",\"inputSchema\":{\"type\":\"object\",\"properties\":") +
    properties + ",\"required\":" + required + ",\"additionalProperties\":false},\"outputSchema\":{\"type\":\"object\",\"properties\":{},\"required\":[],\"additionalProperties\":false}}";
  auto result = action_contract::parse(encoded);
  require(result != nullptr, "real action_contract parse"); return result;
}
uia_action_binding empty_action(const char* method) { return {method, contract("{}", "[]"), "{}", {}}; }

struct fixture;
LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);

struct callback_lifetime final {
  std::atomic<bool>* running{};
  std::atomic<bool>* destroyed{};
  std::atomic<bool>* destroyed_in_callback{};
  ~callback_lifetime() { *destroyed_in_callback = running->load(); *destroyed = true; }
};

struct fixture final {
  HWND window{};
  std::unique_ptr<windows_uia_provider> provider;
  uia_projection projection;
  bool allow{true};
  bool close_inside{};
  HRESULT callback_close{E_FAIL};
  std::atomic<unsigned> dispatch_count{};
  std::atomic<unsigned> getobject_count{};
  std::atomic<DWORD> dispatch_thread{};
  std::atomic<bool> running{};
  std::atomic<bool> destroyed{};
  std::atomic<bool> destroyed_in_callback{};

  explicit fixture(bool permitted = true, bool observe_lifetime = false) : allow(permitted) {
    window = CreateWindowExW(0, class_name, L"Finite native accessibility test", WS_OVERLAPPEDWINDOW,
                             100, 100, 360, 280, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    require(window != nullptr, "real HWND");
    try {
      std::shared_ptr<callback_lifetime> lifetime;
      if (observe_lifetime) {
        lifetime = std::make_shared<callback_lifetime>();
        lifetime->running = &running; lifetime->destroyed = &destroyed; lifetime->destroyed_in_callback = &destroyed_in_callback;
      }
      const HRESULT created = windows_uia_provider::create(window, wake_message, 7,
        [this](const uia_action_request& request) { require(!request.native_user_gesture(), "no gesture"); return allow; },
        [this, lifetime](const uia_action_request& request) {
          running = true;
          dispatch_thread = GetCurrentThreadId(); ++dispatch_count;
          if (close_inside) { callback_close = provider->close(); running = false; return uia_action_result{S_OK, "{}"}; }
          if (request.operation == uia_operation::set_value) {
            // The business method consumes the real validated existing-schema input.
            const auto parsed = nlohmann::json::parse(request.parameters_json);
            const auto utf8 = parsed.at("value").get<std::string>();
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            require(utf8.empty() || count > 0, "actual UTF-8 business input");
            std::wstring accepted(static_cast<std::size_t>(count), L'\0');
            if (count) require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), accepted.data(), count) == count, "native business conversion");
            for (auto& node : projection.nodes) if (node.id == request.node_id) node.value = accepted;
          } else if (request.operation == uia_operation::select) {
            for (auto& node : projection.nodes) if (node.parent_id == "choices") node.selected = node.id == request.node_id;
          } else if (request.operation == uia_operation::focus) {
            SetFocus(window); require(GetFocus() == window, "actual owner keyboard focus");
            projection.focused_id = request.node_id;
          }
          ok(provider->publish(projection), "owner publishes actual result");
          InvalidateRect(window, nullptr, FALSE);
          running = false; return uia_action_result{S_OK, "{}"};
        }, provider);
      ok(created, "opt-in provider create: missing UIA is a real failure, never skipped");
      projection.document_generation = 7; projection.name = L"Finite native document";
      GetWindowRect(window, &projection.screen_bounds);
      const RECT box{110, 130, 340, 155};
      uia_node button; button.id = "commit"; button.role = uia_role::button; button.name = L"Commit"; button.screen_bounds = box; button.action = empty_action("test.commit");
      uia_node input; input.id = "title"; input.role = uia_role::input; input.name = L"Title"; input.value = L"initial"; input.screen_bounds = {110, 160, 340, 185};
      input.action = {"test.title", contract("{\"value\":{\"type\":\"string\",\"maxLength\":4096}}", "[\"value\"]"), "{}", "value"};
      input.focus = empty_action("test.focus");
      uia_node list; list.id = "choices"; list.role = uia_role::selection_list; list.name = L"Mode"; list.screen_bounds = {110, 190, 340, 245};
      uia_node first; first.id = "choice.first"; first.parent_id = "choices"; first.role = uia_role::selection_item; first.name = L"First"; first.selected = true; first.screen_bounds = {110, 190, 340, 215}; first.action = empty_action("test.first");
      uia_node second = first; second.id = "choice.second"; second.name = L"Second"; second.selected = false; second.screen_bounds = {110, 215, 340, 245}; second.action = empty_action("test.second");
      uia_node task; task.id = "task"; task.role = uia_role::task_status; task.name = L"Task"; task.status = L"ready"; task.screen_bounds = {110, 250, 340, 275};
      uia_node secret; secret.id = "secret"; secret.role = uia_role::input; secret.name = L"Password"; secret.secret = true; secret.screen_bounds = {110, 280, 340, 305};
      projection.nodes = {button, input, list, first, second, task, secret};
      POINT origin{}; require(ClientToScreen(window, &origin) != FALSE, "actual client screen origin");
      RECT client{}; require(GetClientRect(window, &client) != FALSE, "actual client bounds");
      for (std::size_t index = 0; index != projection.nodes.size(); ++index) {
        const LONG y = origin.y + 8 + static_cast<LONG>(index) * 24;
        projection.nodes[index].screen_bounds = {origin.x + 8, y, origin.x + client.right - 8, y + 24};
      }
      projection.nodes[2].screen_bounds.bottom = projection.nodes[4].screen_bounds.bottom;
      ok(provider->publish(projection), "native projection");
      // Real owner window and paint path, with no injected input or user gesture.
      ShowWindow(window, SW_SHOWNOACTIVATE); UpdateWindow(window);
    } catch (...) {
      if (provider) { require(provider->close() == S_OK, "failed factory close"); provider.reset(); }
      DestroyWindow(window); window = nullptr; throw;
    }
  }
  ~fixture() {
    if (provider) {
      if (provider->close() != S_OK) ExitProcess(93);
      provider.reset();
    }
    if (window) DestroyWindow(window);
  }
  com_ref<IRawElementProviderSimple> root() { com_ref<IRawElementProviderSimple> value; ok(provider->root(value.put()), "owned root ref"); return value; }
  com_ref<IRawElementProviderFragment> node(const char* id) {
    auto root_value = root(); auto fragment = query<IRawElementProviderFragment>(root_value.get());
    com_ref<IRawElementProviderFragment> child;
    ok(fragment->Navigate(NavigateDirection_FirstChild, child.put()), "first child");
    while (child.get()) {
      auto simple = query<IRawElementProviderSimple>(child.get());
      VARIANT name; VariantInit(&name);
      ok(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &name), "AutomationId");
      const std::wstring expected(id, id + std::char_traits<char>::length(id));
      const bool match = name.vt == VT_BSTR && std::wstring(name.bstrVal, SysStringLen(name.bstrVal)) == expected;
      VariantClear(&name); if (match) return child;
      if (std::string(id).find("choice.") == 0) {
        com_ref<IRawElementProviderFragment> item;
        ok(child->Navigate(NavigateDirection_FirstChild, item.put()), "list item");
        while (item.get()) {
          auto item_simple = query<IRawElementProviderSimple>(item.get());
          VariantInit(&name); ok(item_simple->GetPropertyValue(UIA_AutomationIdPropertyId, &name), "item id");
          const bool found = name.vt == VT_BSTR && std::wstring(name.bstrVal, SysStringLen(name.bstrVal)) == expected;
          VariantClear(&name); if (found) return item;
          com_ref<IRawElementProviderFragment> next; ok(item->Navigate(NavigateDirection_NextSibling, next.put()), "next item"); item = std::move(next);
        }
      }
      com_ref<IRawElementProviderFragment> next; ok(child->Navigate(NavigateDirection_NextSibling, next.put()), "next child"); child = std::move(next);
    }
    throw std::runtime_error("registered node absent");
  }
};

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
  auto* host = reinterpret_cast<fixture*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (host && host->provider) {
    if (message == WM_GETOBJECT && static_cast<DWORD>(lparam) == static_cast<DWORD>(UiaRootObjectId)) ++host->getobject_count;
    LRESULT result{}; if (host->provider->handle_message(message, wparam, lparam, result)) return result;
  }
  if (message == WM_PAINT && host) {
    PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
    int y = 8;
    for (const auto& node : host->projection.nodes) {
      const std::wstring label = node.name + (node.secret ? L"" : L" " + node.value + L" " + node.status);
      TextOutW(dc, 8, y, label.data(), static_cast<int>(label.size())); y += 24;
    }
    EndPaint(window, &paint); return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void run_mta(fixture& host, const std::function<void()>& body, bool pump_owner = true) {
  event completed;
  std::string error;
  std::thread worker([&] {
    try { apartment com(COINIT_MULTITHREADED); body(); }
    catch (const std::exception& exception) { error = exception.what(); }
    catch (...) { error = "MTA failure"; }
    SetEvent(completed.get());
  });
  const ULONGLONG deadline = GetTickCount64() + 5000;
  while (WaitForSingleObject(completed.get(), 0) != WAIT_OBJECT_0) {
    if (GetTickCount64() >= deadline) {
      std::fputs("UIA test owner/client deadline: failure; Owned Job must dispose remaining threads\n", stderr);
      ExitProcess(92);  // A live thread cannot safely outlive fixture/stack buffers.
    }
    if (pump_owner) {
      MSG message{};
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
      const HRESULT status = host.provider->pump();
      if (status != S_OK && status != S_FALSE) { std::fputs("Owner pump failed with live client\n", stderr); ExitProcess(92); }
      HANDLE wait = completed.get(); MsgWaitForMultipleObjectsEx(1, &wait, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } else WaitForSingleObject(completed.get(), 20);
  }
  worker.join(); require(error.empty(), error.c_str());
}

std::vector<LONG> runtime_id(IRawElementProviderFragment* item) {
  SAFEARRAY* value{}; ok(item->GetRuntimeId(&value), "actual SAFEARRAY runtime id"); require(value != nullptr, "child runtime id");
  LONG low{}, high{}; ok(SafeArrayGetLBound(value, 1, &low), "runtime lower"); ok(SafeArrayGetUBound(value, 1, &high), "runtime upper");
  std::vector<LONG> result;
  for (LONG index = low; index <= high; ++index) { LONG number{}; ok(SafeArrayGetElement(value, &index, &number), "runtime element"); result.push_back(number); }
  SafeArrayDestroy(value); return result;
}

void properties_case() {
  fixture host; auto root = host.root(); auto fragment = query<IRawElementProviderFragment>(root.get());
  auto unknown = query<IUnknown>(root.get()); auto same = query<IUnknown>(fragment.get()); require(unknown.get() == same.get(), "controlling IUnknown identity");
  auto marshaler = query<IMarshal>(root.get()); require(marshaler.get() != nullptr, "actual FTM");
  IUnknown* missing = reinterpret_cast<IUnknown*>(1);
  require(root->GetPatternProvider(UIA_TextPatternId, &missing) == S_OK && !missing, "unsupported pattern initialized null");
  SAFEARRAY* root_id = reinterpret_cast<SAFEARRAY*>(1); ok(fragment->GetRuntimeId(&root_id), "HWND runtime id"); require(!root_id, "HWND root id null");
  auto button = host.node("commit"); auto invoke = query<IInvokeProvider>(button.get());
  auto input = host.node("title"); auto value = query<IValueProvider>(input.get());
  BSTR contents{}; ok(value->get_Value(&contents), "BSTR value"); require(std::wstring(contents, SysStringLen(contents)) == L"initial", "published actual value"); SysFreeString(contents);
  auto list = host.node("choices"); auto selection = query<ISelectionProvider>(list.get());
  SAFEARRAY* selected{}; ok(selection->GetSelection(&selected), "real selected SAFEARRAY"); require(selected != nullptr, "required selection"); SafeArrayDestroy(selected);
  auto item = host.node("choice.second"); auto item_pattern = query<ISelectionItemProvider>(item.get());
  BOOL selected_flag = TRUE; ok(item_pattern->get_IsSelected(&selected_flag), "selected property"); require(!selected_flag, "actual second unselected");
  require(item_pattern->AddToSelection() == UIA_E_INVALIDOPERATION, "single selection cannot add second");
  require(item_pattern->RemoveFromSelection() == S_OK, "unselected remove no-op");
  auto first = host.node("choice.first"); require(query<ISelectionItemProvider>(first.get())->RemoveFromSelection() == UIA_E_INVALIDOPERATION, "required selected cannot remove");
  require(value->SetValue(L"owner") == UIA_E_INVALIDOPERATION, "owner synchronous call rejects");
  require(host.dispatch_count == 0, "properties/owner reject never execute business");
}

void mutation_case() {
  fixture host; auto input = host.node("title"); auto value = query<IValueProvider>(input.get());
  run_mta(host, [&] { require(host.provider->publish(host.projection) == RPC_E_WRONG_THREAD, "foreign controller publish rejected"); require(host.provider->pump() == RPC_E_WRONG_THREAD, "foreign controller pump rejected"); });
  com_ref<IStream> stream; ok(CoMarshalInterThreadInterfaceInStream(__uuidof(IValueProvider), value.get(), stream.put()), "real cross-apartment marshal");
  IStream* transferred = stream.get(); transferred->AddRef();
  const std::wstring accepted_value = L"actual \"owner\" value 中\\\n";
  run_mta(host, [&] { com_ref<IValueProvider> proxy; ok(CoGetInterfaceAndReleaseStream(transferred, __uuidof(IValueProvider), reinterpret_cast<void**>(proxy.put())), "MTA unmarshals FTM"); ok(proxy->SetValue(accepted_value.c_str()), "sync actual SetValue"); });
  require(host.dispatch_thread == GetCurrentThreadId() && host.dispatch_count == 1, "business only owner");
  BSTR text_value{}; ok(value->get_Value(&text_value), "result value"); require(std::wstring(text_value, SysStringLen(text_value)) == accepted_value, "sync real-input result is published"); SysFreeString(text_value);
  auto item = host.node("choice.second"); auto selection = query<ISelectionItemProvider>(item.get());
  run_mta(host, [&] { ok(selection->Select(), "sync Select"); });
  BOOL selected{}; ok(selection->get_IsSelected(&selected), "selection after return"); require(selected, "actual owner selected before S_OK");
  run_mta(host, [&] { ok(input->SetFocus(), "typed owner focus"); });
  require(host.projection.focused_id == "title", "native focus entry committed");
  host.allow = false;
  run_mta(host, [&] { require(value->SetValue(L"denied") == E_ACCESSDENIED, "native policy denies"); });
  require(host.dispatch_count == 3, "denied policy never business");
}

void stale_secret_case() {
  fixture host;
  auto secret = host.node("secret"); auto secret_simple = query<IRawElementProviderSimple>(secret.get());
  IUnknown* pattern = reinterpret_cast<IUnknown*>(1); ok(secret_simple->GetPatternProvider(UIA_ValuePatternId, &pattern), "secret pattern"); require(!pattern, "secret no Value pattern");
  VARIANT value; VariantInit(&value); ok(secret_simple->GetPropertyValue(UIA_ValueValuePropertyId, &value), "secret unsupported value"); require(value.vt == VT_UNKNOWN && value.punkVal, "reserved unsupported, no empty-value fallback"); VariantClear(&value);
  auto invalid = host.projection; invalid.nodes.back().value = L"private secret"; require(host.provider->publish(invalid) == E_INVALIDARG, "secret never stored");
  auto title = host.node("title"); const auto old_id = runtime_id(title.get()); auto old_value = query<IValueProvider>(title.get());
  uia_node removed = host.projection.nodes[1]; host.projection.nodes.erase(host.projection.nodes.begin() + 1); ok(host.provider->publish(host.projection), "remove node");
  BSTR text_value = reinterpret_cast<BSTR>(1); require(old_value->get_Value(&text_value) == UIA_E_ELEMENTNOTAVAILABLE && !text_value, "removed old provider inert");
  host.projection.nodes.insert(host.projection.nodes.begin() + 1, removed); ok(host.provider->publish(host.projection), "readd id");
  require(runtime_id(host.node("title").get()) != old_id, "native incarnation never reused");
  require(old_value->get_Value(&text_value) == UIA_E_ELEMENTNOTAVAILABLE && !text_value, "old provider cannot revive");
  auto wrong_document = host.projection; ++wrong_document.document_generation; require(host.provider->publish(wrong_document) == UIA_E_ELEMENTNOTAVAILABLE, "page generation cannot replace native document");
  auto root = host.root(); ok(host.provider->close(), "revoke root"); VariantInit(&value); require(root->GetPropertyValue(UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE && value.vt == VT_EMPTY, "closed root inert");
  require(host.provider->capability() == uia_capability::closed, "fixed closed diagnostic");
  host.provider.reset();
  ok(windows_uia_provider::create(host.window, wake_message, 8, {}, {}, host.provider), "same HWND fresh native instance");
  host.projection.document_generation = 8; ok(host.provider->publish(host.projection), "fresh native document");
  VariantInit(&value); require(root->GetPropertyValue(UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE, "old root cannot revive on HWND reattach");
  require(runtime_id(host.node("title").get()) != old_id, "fresh instance/doc runtime identity");
}

void bounded_case() {
  fixture host(false); auto button = host.node("commit"); auto invoke = query<IInvokeProvider>(button.get());
  for (std::size_t index = 0; index != windows_uia_provider::max_pending; ++index) ok(invoke->Invoke(), "bounded Invoke admission only");
  require(invoke->Invoke() == UIA_E_TIMEOUT, "queue full"); require(host.dispatch_count == 0, "no direct Invoke callback");
  for (unsigned batch = 0; batch != 8; ++batch) ok(host.provider->pump(), "bounded real owner batches");
  require(host.dispatch_count == 0, "default/native deny remains authoritative");
  auto title = host.node("title"); auto value = query<IValueProvider>(title.get()); host.allow = true;
  run_mta(host, [&] { require(value->SetValue(L"timeout") == UIA_E_TIMEOUT, "real owner not pumping timeout"); }, false);
  ok(host.provider->pump(), "later owner pump skips cancelled input"); require(host.dispatch_count == 0, "timeout precommit cancelled");
  const std::wstring overlong(windows_uia_provider::max_value_units + 1, L'x');
  run_mta(host, [&] { require(value->SetValue(overlong.c_str()) == E_INVALIDARG, "input bound"); });
}

void callback_close_case() {
  fixture host(true, true); host.destroyed = false; host.destroyed_in_callback = false; host.close_inside = true;
  auto button = host.node("commit"); auto invoke = query<IInvokeProvider>(button.get());
  ok(invoke->Invoke(), "asynchronous admission"); ok(host.provider->pump(), "owner closes after callback");
  require(host.callback_close == S_FALSE, "actual callback borrow retains controller");
  require(host.destroyed && !host.destroyed_in_callback, "business capture freed only after callback returns");
  require(invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE, "external COM ref becomes inert");
}

void client_case() {
  fixture host;
  run_mta(host, [&] {
    com_ref<IUIAutomation> client; ok(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IUIAutomation), reinterpret_cast<void**>(client.put())), "actual UIA client");
    com_ref<IUIAutomationElement> root; ok(client->ElementFromHandle(host.window, root.put()), "UIA ElementFromHandle");
    VARIANT name; VariantInit(&name); ok(root->GetCurrentPropertyValue(UIA_NamePropertyId, &name), "client sees provider name");
    require(name.vt == VT_BSTR && std::wstring(name.bstrVal, SysStringLen(name.bstrVal)) == L"Finite native document", "actual native provider selected"); VariantClear(&name);
    VARIANT id; VariantInit(&id); id.vt = VT_BSTR; id.bstrVal = SysAllocString(L"commit"); require(id.bstrVal != nullptr, "client id");
    com_ref<IUIAutomationCondition> condition; ok(client->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, condition.put()), "client condition"); VariantClear(&id);
    com_ref<IUIAutomationElement> button; ok(root->FindFirst(TreeScope_Children, condition.get(), button.put()), "client finds projected button"); require(button.get() != nullptr, "native child visible");
    com_ref<IUIAutomationInvokePattern> invoke; ok(button->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), reinterpret_cast<void**>(invoke.put())), "actual client Invoke pattern"); ok(invoke->Invoke(), "actual UIA client admission");
  });
  ok(host.provider->pump(), "owner commits client request"); require(host.getobject_count > 0, "client triggered real WM_GETOBJECT");
  require(host.dispatch_count == 1 && host.dispatch_thread == GetCurrentThreadId(), "real UIA routed owner business");
}

void com_initialization_case() {
  fixture host;
  event completed; HRESULT observed = E_FAIL;
  std::thread owner([&] {
    HWND window = CreateWindowExW(0, class_name, L"COM not initialized", WS_OVERLAPPED, 0, 0, 100, 100,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window) { std::unique_ptr<windows_uia_provider> provider; observed = windows_uia_provider::create(window, wake_message, 1, {}, {}, provider); DestroyWindow(window); }
    SetEvent(completed.get());
  });
  if (WaitForSingleObject(completed.get(), 5000) != WAIT_OBJECT_0) ExitProcess(92);
  owner.join(); require(observed == CO_E_NOTINITIALIZED, "FTM factory requires actual COM initialization");
}

void api_close_case() {
  fixture host; auto root = host.root(); std::atomic<unsigned> queries{};
  run_mta(host, [&] {
    for (unsigned index = 0; index != 100; ++index) {
      com_ref<IRawElementProviderSimple> native;
      const HRESULT status = root->get_HostRawElementProvider(native.put());
      require(status == S_OK || status == UIA_E_ELEMENTNOTAVAILABLE, "actual cross-thread Host API call"); ++queries;
    }
  });
  require(queries == 100, "actual cross-thread API calls completed");
  ok(host.provider->close(), "close after actual API calls");
  run_mta(host, [&] { VARIANT result; VariantInit(&result); require(root->GetPropertyValue(UIA_NamePropertyId, &result) == UIA_E_ELEMENTNOTAVAILABLE, "foreign provider after revoke"); });
  // This case observes actual calls and post-revoke safety; it does not claim
  // that the scheduler necessarily overlapped an API borrow with close.
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (!IsProcessDPIAware()) require(SetProcessDPIAware() != FALSE, "actual physical screen coordinates");
    apartment owner(COINIT_APARTMENTTHREADED);
    WNDCLASSW window_class{}; window_class.lpfnWndProc = window_proc; window_class.hInstance = GetModuleHandleW(nullptr); window_class.lpszClassName = class_name;
    require(RegisterClassW(&window_class) != 0, "register actual native window class");
    const std::string selected = argc == 2 ? argv[1] : "all";
    const std::pair<const char*, void(*)()> cases[] = {
      {"properties", properties_case}, {"mutations", mutation_case}, {"stale-secret", stale_secret_case},
      {"bounded", bounded_case}, {"callback-close", callback_close_case}, {"uia-client", client_case},
      {"com-initialization", com_initialization_case}, {"api-close", api_close_case}};
    bool found = selected == "all";
    for (const auto& item : cases) if (selected == "all" || selected == item.first) { found = true; item.second(); std::cout << "case=" << item.first << " passed\n"; }
    require(found, "unknown case"); require(UnregisterClassW(class_name, GetModuleHandleW(nullptr)) != FALSE, "native class cleanup");
    return 0;
  } catch (const std::exception& failure) { std::cerr << "UIA native test failed: " << failure.what() << '\n'; return 1; }
}
