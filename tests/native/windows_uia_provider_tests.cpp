#include <soui/runtime/accessibility/windows_uia_provider.h>
#include <UIAutomation.h>
#include <objbase.h>
#include <json.hpp>

#include <algorithm>
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
void ok(HRESULT status, const char* message) {
  if (status != S_OK) std::fprintf(stderr, "UIA native HRESULT: %s hr=0x%08lx\n", message, static_cast<unsigned long>(status));
  require(status == S_OK, message);
}

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

// Diagnostics only: individual atomic observations, not a provider queue trace.
// No output occurs unless the original focus assertion or request fails.
struct focus_diagnostics final {
  enum point : unsigned { client_call, owner_callback, native_call, native_return,
                          publish_return, owner_return, client_return, point_count };
  struct snapshot final {
    LONGLONG ticks[point_count]{};
    HWND previous{}, actual{};
    DWORD native_error{}, owner_thread{};
    unsigned policies{}, dispatches{};
    bool running{};
  };
  std::atomic<bool> enabled{};
  std::atomic<LONGLONG> ticks[point_count]{};
  std::atomic<HWND> previous{}, actual{};
  std::atomic<DWORD> native_error{}, owner_thread{};
  snapshot at_client_return;
  DWORD client_thread{};
  HRESULT result{S_OK};

  static LONGLONG now() noexcept {
    LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart;
  }
  void mark(point index) noexcept { ticks[index] = now(); }
  snapshot capture(unsigned policies, unsigned dispatches, bool running) const noexcept {
    snapshot value;
    for (unsigned index = 0; index != point_count; ++index) value.ticks[index] = ticks[index].load();
    value.previous = previous.load(); value.actual = actual.load();
    value.native_error = native_error.load(); value.owner_thread = owner_thread.load();
    value.policies = policies; value.dispatches = dispatches; value.running = running;
    return value;
  }
  void print(const char* phase, const snapshot& value, HWND expected) const noexcept {
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    std::fprintf(stderr,
      "UIA focus diagnostic: phase=%s hr=0x%08lx qpcFrequency=%lld clientThread=%lu ownerThread=%lu "
      "clientCall=%lld ownerCallback=%lld nativeCall=%lld nativeReturn=%lld publishReturn=%lld ownerReturn=%lld clientReturn=%lld "
      "previous=%p actual=%p expected=%p win32error=%lu policy=%u dispatch=%u running=%u\n",
      phase, static_cast<unsigned long>(result), static_cast<long long>(frequency.QuadPart), client_thread, value.owner_thread,
      static_cast<long long>(value.ticks[client_call]), static_cast<long long>(value.ticks[owner_callback]),
      static_cast<long long>(value.ticks[native_call]), static_cast<long long>(value.ticks[native_return]),
      static_cast<long long>(value.ticks[publish_return]), static_cast<long long>(value.ticks[owner_return]),
      static_cast<long long>(value.ticks[client_return]), static_cast<void*>(value.previous), static_cast<void*>(value.actual),
      static_cast<void*>(expected), value.native_error, value.policies, value.dispatches, value.running ? 1u : 0u);
  }
};

struct fixture final {
  HWND window{};
  std::unique_ptr<windows_uia_provider> provider;
  uia_projection projection;
  bool allow{true};
  std::function<HRESULT()> event_mutation;
  std::function<void()> inside_dispatch;
  bool drop_wake{};
  bool close_inside{};
  bool mark_invoked{};
  HRESULT dispatch_result{S_OK};
  HRESULT callback_close{E_FAIL};
  std::atomic<unsigned> policy_count{};
  std::atomic<unsigned> dispatch_count{};
  std::vector<uia_operation> dispatched;
  std::atomic<unsigned> getobject_count{};
  std::atomic<DWORD> dispatch_thread{};
  std::atomic<bool> running{};
  std::atomic<bool> destroyed{};
  std::atomic<bool> destroyed_in_callback{};
  focus_diagnostics focus_diagnostic;

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
        [this](const uia_action_request& request) { ++policy_count; require(!request.native_user_gesture(), "no gesture"); return allow; },
        [this, lifetime](const uia_action_request& request) {
          const bool trace_focus = request.operation == uia_operation::focus && focus_diagnostic.enabled.load();
          if (trace_focus) {
            focus_diagnostic.owner_thread = GetCurrentThreadId();
            focus_diagnostic.mark(focus_diagnostics::owner_callback);
          }
          running = true;
          dispatch_thread = GetCurrentThreadId(); ++dispatch_count;
          dispatched.push_back(request.operation);
          if (inside_dispatch) inside_dispatch();
          if (close_inside) { callback_close = provider->close(); running = false; return uia_action_result{S_OK, "{}"}; }
          if (FAILED(dispatch_result)) { running = false; return uia_action_result{dispatch_result, "{}"}; }
          if (mark_invoked && request.operation == uia_operation::invoke)
            for (auto& node : projection.nodes) if (node.id == request.node_id) node.status = L"invoked";
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
            if (trace_focus) focus_diagnostic.mark(focus_diagnostics::native_call);
            const HWND previous = SetFocus(window);
            const DWORD native_error = GetLastError();
            const LONGLONG returned = trace_focus ? focus_diagnostics::now() : 0;
            const HWND actual = GetFocus();
            if (trace_focus) {
              focus_diagnostic.previous = previous; focus_diagnostic.actual = actual;
              focus_diagnostic.native_error = native_error;
              focus_diagnostic.ticks[focus_diagnostics::native_return] = returned;
            }
            if (actual != window) std::fprintf(stderr,
              "UIA owner focus assertion: previous=%p actual=%p expected=%p win32error=%lu ownerThread=%lu\n",
              static_cast<void*>(previous), static_cast<void*>(actual), static_cast<void*>(window), native_error, GetCurrentThreadId());
            require(actual == window, "actual owner keyboard focus");
            projection.focused_id = request.node_id;
          }
          const HRESULT published = provider->publish(projection);
          if (trace_focus) focus_diagnostic.mark(focus_diagnostics::publish_return);
          ok(published, "owner publishes actual result");
          InvalidateRect(window, nullptr, FALSE);
          running = false;
          if (trace_focus) focus_diagnostic.mark(focus_diagnostics::owner_return);
          return uia_action_result{S_OK, "{}"};
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
  if (host && message == WM_APP + 0x478 && host->event_mutation) return host->event_mutation();
  if (host && message == wake_message && host->drop_wake) return 0;
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
  require(host.dispatch_count == 0, "properties never execute business");
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
  host.focus_diagnostic.enabled = true;
  try {
    run_mta(host, [&] {
      auto& diagnostic = host.focus_diagnostic;
      diagnostic.client_thread = GetCurrentThreadId();
      diagnostic.mark(focus_diagnostics::client_call);
      diagnostic.result = input->SetFocus();
      diagnostic.mark(focus_diagnostics::client_return);
      diagnostic.at_client_return = diagnostic.capture(host.policy_count.load(), host.dispatch_count.load(), host.running.load());
      ok(diagnostic.result, "typed owner focus");
    });
    require(host.projection.focused_id == "title", "native focus entry committed");
  } catch (...) {
    // run_mta joined its worker before throwing: plain client fields are now safe.
    // The return snapshot may precede an already committed owner's completion.
    host.focus_diagnostic.enabled = false;
    host.focus_diagnostic.print("client-return", host.focus_diagnostic.at_client_return, host.window);
    host.focus_diagnostic.print("owner-after-join", host.focus_diagnostic.capture(
      host.policy_count.load(), host.dispatch_count.load(), host.running.load()), host.window);
    throw;
  }
  host.focus_diagnostic.enabled = false;
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

void owner_focus_case() {
  {
    fixture host;
    auto input = host.node("title");
    ok(input->SetFocus(), "owner focus uses the configured bounded pump");
    require(host.dispatch_count == 1 && host.projection.focused_id == "title" && GetFocus() == host.window,
            "owner focus commits actual window and projection");
    require(host.policy_count == 1, "owner focus passes application authorization once");
  }
  {
    fixture host(false);
    auto input = host.node("title");
    require(input->SetFocus() == E_ACCESSDENIED && host.policy_count == 1 && host.dispatch_count == 0,
            "owner focus retains application authorization");
  }
  {
    fixture host;
    auto input = host.node("title");
    host.inside_dispatch = [&] {
      require(input->SetFocus() == UIA_E_INVALIDOPERATION, "nested owner focus rejected before queueing");
    };
    ok(input->SetFocus(), "outer owner focus remains valid");
    ok(host.provider->pump(), "no latent nested focus");
    require(host.dispatch_count == 1 && host.policy_count == 1, "nested focus cannot enter policy or business");
  }
  {
    fixture host;
    auto input = host.node("title");
    host.drop_wake = true;
    require(input->SetFocus() == UIA_E_TIMEOUT && host.dispatch_count == 0,
            "missing wake forwarding fails without waiting for owner");
    host.drop_wake = false;
    ok(host.provider->pump(), "drain cancelled owner focus");
    require(host.dispatch_count == 0, "failed owner focus cannot execute later");
  }
  {
    fixture host;
    auto input = host.node("title"), button = host.node("commit");
    auto invoke = query<IInvokeProvider>(button.get());
    for (unsigned i = 0; i != 9; ++i) ok(invoke->Invoke(), "queue preceding actions");
    require(input->SetFocus() == UIA_E_TIMEOUT && host.dispatch_count == 8,
            "owner focus preserves eight-command batch and FIFO order");
    ok(host.provider->pump(), "drain remaining preceding action");
    require(host.dispatch_count == 9 && host.projection.focused_id.empty(),
            "budget-rejected focus is cancelled without reordering");
  }
  {
    fixture host(true, true);
    host.close_inside = true;
    auto input = host.node("title");
    require(input->SetFocus() == UIA_E_ELEMENTNOTAVAILABLE, "close revokes synchronous owner focus");
    require(host.callback_close == S_FALSE && !host.destroyed && !host.destroyed_in_callback,
            "focus HWND borrow retains callbacks through synchronous wake");
    ok(host.provider->close(), "owner completes close after focus borrow");
    require(host.destroyed && !host.destroyed_in_callback, "callback capture released after borrow");
    require(input->SetFocus() == UIA_E_ELEMENTNOTAVAILABLE, "closed focus reference remains inert");
  }
}

void owner_sync_case() {
  for (bool select : {false, true}) {
    auto mutate = [select](fixture& host) {
      if (select) return query<ISelectionItemProvider>(host.node("choice.second").get())->Select();
      return query<IValueProvider>(host.node("title").get())->SetValue(L"owner mutation");
    };
    {
      fixture host;
      ok(mutate(host), "owner mutation uses the configured bounded pump");
      require(host.policy_count == 1 && host.dispatch_count == 1 &&
              host.dispatch_thread == GetCurrentThreadId(), "owner mutation commits exactly once");
      if (select) {
        BOOL selected{};
        ok(query<ISelectionItemProvider>(host.node("choice.second").get())->get_IsSelected(&selected), "owner selection read");
        require(selected, "owner selection committed before success");
      } else {
        BSTR value{};
        ok(query<IValueProvider>(host.node("title").get())->get_Value(&value), "owner value read");
        const bool committed = value && std::wstring(value, SysStringLen(value)) == L"owner mutation";
        SysFreeString(value); require(committed, "owner value committed before success");
      }
    }
    {
      fixture host(false);
      require(mutate(host) == E_ACCESSDENIED && host.policy_count == 1 && host.dispatch_count == 0,
              "owner mutation retains application authorization");
    }
    {
      fixture host;
      host.inside_dispatch = [&] { require(mutate(host) == UIA_E_INVALIDOPERATION,
                                          "nested owner mutation rejected before admission"); };
      ok(mutate(host), "outer owner mutation");
      ok(host.provider->pump(), "drain without nested mutation");
      require(host.policy_count == 1 && host.dispatch_count == 1, "no latent nested mutation");
    }
    {
      fixture host; host.drop_wake = true;
      require(mutate(host) == UIA_E_TIMEOUT && host.dispatch_count == 0, "unhandled owner mutation cancelled");
      host.drop_wake = false; ok(host.provider->pump(), "drain cancelled mutation");
      require(host.dispatch_count == 0, "timed out mutation cannot execute later");
    }
  }
}

void client_mutations_case() {
  fixture host;
  run_mta(host, [&] {
    com_ref<IUIAutomation> client;
    ok(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
       __uuidof(IUIAutomation), reinterpret_cast<void**>(client.put())), "actual mutation client");
    com_ref<IUIAutomationElement> root;
    ok(client->ElementFromHandle(host.window, root.put()), "actual mutation root");
    auto find_element = [&](const wchar_t* name) {
      VARIANT id; VariantInit(&id); id.vt = VT_BSTR; id.bstrVal = SysAllocString(name);
      require(id.bstrVal != nullptr, "mutation AutomationId");
      com_ref<IUIAutomationCondition> condition;
      const auto result = client->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, condition.put());
      VariantClear(&id); ok(result, "mutation condition");
      com_ref<IUIAutomationElement> element;
      ok(root->FindFirst(TreeScope_Descendants, condition.get(), element.put()), "actual mutation element");
      require(element.get() != nullptr, "actual mutation control present");
      return element;
    };
    auto input = find_element(L"title");
    com_ref<IUIAutomationValuePattern> value;
    ok(input->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern),
       reinterpret_cast<void**>(value.put())), "actual Value pattern");
    BSTR text = SysAllocString(L"UIA committed value"); require(text != nullptr, "actual value input");
    const auto set_result = value->SetValue(text); SysFreeString(text);
    ok(set_result, "Windows UIA SetValue dispatch");
    BSTR observed{}; ok(value->get_CurrentValue(&observed), "actual committed value");
    const bool committed = observed && std::wstring(observed, SysStringLen(observed)) == L"UIA committed value";
    SysFreeString(observed); require(committed, "Windows UIA reads committed input");
    auto item = find_element(L"choice.second");
    com_ref<IUIAutomationSelectionItemPattern> selection;
    ok(item->GetCurrentPatternAs(UIA_SelectionItemPatternId, __uuidof(IUIAutomationSelectionItemPattern),
       reinterpret_cast<void**>(selection.put())), "actual SelectionItem pattern");
    ok(selection->Select(), "Windows UIA Select dispatch");
    BOOL selected{}; ok(selection->get_CurrentIsSelected(&selected), "actual committed selection");
    require(selected, "Windows UIA reads committed selection");
  });
  // UIAutomationCore can request focus before a pattern mutation. Count the
  // actual mutations separately, retaining authorization for every dispatch.
  const auto values = std::count(host.dispatched.begin(), host.dispatched.end(), uia_operation::set_value);
  const auto selections = std::count(host.dispatched.begin(), host.dispatched.end(), uia_operation::select);
  const auto focuses = std::count(host.dispatched.begin(), host.dispatched.end(), uia_operation::focus);
  std::fprintf(stderr, "actual UIA values=%td selections=%td focuses=%td policy=%u dispatch=%u\n",
               values, selections, focuses, host.policy_count.load(), host.dispatch_count.load());
  require(host.getobject_count > 0 && values == 1 && selections == 1 &&
          host.dispatched.size() == static_cast<std::size_t>(values + selections + focuses) &&
          host.policy_count == host.dispatch_count && host.dispatched.size() == host.dispatch_count &&
          host.dispatch_thread == GetCurrentThreadId(), "actual UIA mutations each commit once through owner policy");
}

void client_focus_case() {
  fixture host;
  run_mta(host, [&] {
    com_ref<IUIAutomation> client;
    ok(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
       __uuidof(IUIAutomation), reinterpret_cast<void**>(client.put())), "real focus client");
    com_ref<IUIAutomationElement> root;
    ok(client->ElementFromHandle(host.window, root.put()), "real focus root");
    VARIANT id; VariantInit(&id); id.vt = VT_BSTR; id.bstrVal = SysAllocString(L"title");
    require(id.bstrVal != nullptr, "focus AutomationId");
    com_ref<IUIAutomationCondition> condition;
    const auto result = client->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, condition.put());
    VariantClear(&id); ok(result, "focus condition");
    com_ref<IUIAutomationElement> input;
    ok(root->FindFirst(TreeScope_Children, condition.get(), input.put()), "real focus input");
    require(input.get() != nullptr, "real focus input found");
    ok(input->SetFocus(), "Windows UIA focus dispatch");
    BOOL focused{}; ok(input->get_CurrentHasKeyboardFocus(&focused), "focus projection read");
    require(focused, "Windows UIA observes committed focus");
  });
  require(host.getobject_count > 0 && host.dispatch_count == 1 &&
          host.dispatch_thread == GetCurrentThreadId() && GetFocus() == host.window,
          "real Windows UIA committed one owner focus");
}

// Real UIAutomationCore subscription on a separate MTA. The owner publishes
// the same typed business result exercised by the existing mutation case.
class property_events final : public IUIAutomationPropertyChangedEventHandler {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) noexcept override {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationPropertyChangedEventHandler)) return E_NOINTERFACE;
    *result = static_cast<IUIAutomationPropertyChangedEventHandler*>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const auto remaining = --references_; if (!remaining) delete this; return remaining;
  }
  HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement* sender,
      PROPERTYID property, VARIANT value) noexcept override {
    if (property != UIA_ValueValuePropertyId) return S_OK;
    BSTR id{};
    const auto identity_status = sender ? sender->get_CurrentAutomationId(&id) : E_POINTER;
    std::fprintf(stderr,"event property=%ld identity=%08lx type=%u\n",property,static_cast<unsigned long>(identity_status),value.vt);
    if (FAILED(identity_status)) { failed = true; SetEvent(received.get()); return S_OK; }
    std::fprintf(stderr,"event id=%ls payload=%ls\n",id?id:L"(null)",value.vt==VT_BSTR&&value.bstrVal?value.bstrVal:L"(nontext)");
    const bool expected = id && std::wstring(id, SysStringLen(id)) == L"title";
    SysFreeString(id);
    if (!expected) { failed = true; SetEvent(received.get()); return S_OK; }
    if (value.vt != VT_BSTR || !value.bstrVal ||
        std::wstring(value.bstrVal, SysStringLen(value.bstrVal)) != L"visible update") failed = true;
    // This real reentrant query must observe the committed snapshot, without
    // requiring the owner to reacquire a lock held across event publication.
    VARIANT current; VariantInit(&current);
    const auto read_status=sender->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &current);
    std::fprintf(stderr,"event current=%08lx type=%u value=%ls\n",static_cast<unsigned long>(read_status),current.vt,current.vt==VT_BSTR&&current.bstrVal?current.bstrVal:L"(nontext)");
    if (FAILED(read_status) ||
        current.vt != VT_BSTR || !current.bstrVal ||
        std::wstring(current.bstrVal, SysStringLen(current.bstrVal)) != L"visible update") failed = true;
    VariantClear(&current); last_event = GetTickCount64(); ++count; SetEvent(received.get()); return S_OK;
  }
  event received;
  std::atomic<unsigned> count{};
  std::atomic<ULONGLONG> last_event{};
  std::atomic<bool> failed{};
 private:
  std::atomic<ULONG> references_{1};
};

void property_events_case() {
  fixture host;
  unsigned publications{};
  host.event_mutation = [&] {
    require(GetCurrentThreadId() == GetWindowThreadProcessId(host.window, nullptr), "actual publication owner");
    host.projection.nodes[1].value = L"visible update"; ++publications;
    return host.provider->publish(host.projection);
  };
  run_mta(host, [&] {
    com_ref<IUIAutomation> client;
    ok(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IUIAutomation), reinterpret_cast<void**>(client.put())), "event client activation");
    com_ref<IUIAutomationElement> root;
    ok(client->ElementFromHandle(host.window, root.put()), "event client HWND");
    VARIANT id; VariantInit(&id); id.vt = VT_BSTR; id.bstrVal = SysAllocString(L"title");
    require(id.bstrVal != nullptr, "event client id");
    com_ref<IUIAutomationCondition> condition;
    const auto condition_result = client->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, condition.put());
    VariantClear(&id); ok(condition_result, "event client condition");
    com_ref<IUIAutomationElement> input;
    ok(root->FindFirst(TreeScope_Children, condition.get(), input.put()), "event client finds input");
    require(input.get() != nullptr, "event client actual input");
    com_ref<IUIAutomationValuePattern> pattern;
    ok(input->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern),
      reinterpret_cast<void**>(pattern.put())), "event client actual Value pattern");
    com_ref<property_events> sink; sink.reset(new property_events);
    PROPERTYID properties[]{UIA_ValueValuePropertyId};
    ok(client->AddPropertyChangedEventHandlerNativeArray(root.get(), TreeScope_Subtree,
      nullptr, sink.get(), properties, 1), "real value event subscription");
    DWORD_PTR published{};
    const bool sent = SendMessageTimeoutW(host.window, WM_APP + 0x478, 0, 0, SMTO_ABORTIFHUNG, 2000, &published) != 0;
    const HRESULT set_result = sent ? static_cast<HRESULT>(published) : E_FAIL;
    const auto received = WaitForSingleObject(sink->received.get(), 1500);
    const auto deadline = GetTickCount64() + 1500;
    while (sink->count && GetTickCount64() - sink->last_event.load() < 150 && GetTickCount64() < deadline) Sleep(5);
    const auto delivered = sink->count.load();
    DWORD_PTR unchanged{};
    const bool republished = SendMessageTimeoutW(host.window, WM_APP + 0x478, 0, 0, SMTO_ABORTIFHUNG, 2000, &unchanged) != 0;
    Sleep(200); // Owner keeps pumping; a no-op publication must not emit another value change.
    const auto final_count = sink->count.load();
    const auto removal = client->RemovePropertyChangedEventHandler(root.get(), sink.get());
    require(republished && static_cast<HRESULT>(unchanged) == S_OK && final_count == delivered, "unchanged publication emits no new property event");
    ok(removal, "event subscription removed on registering MTA");
    ok(set_result, "native owner projection committed");
    std::fprintf(stderr,"event wait=%lu count=%u failed=%d\n",received,sink->count.load(),sink->failed.load());
    require(received == WAIT_OBJECT_0 && sink->count >= 1 && !sink->failed,
      "real subscribed client must receive committed nonsecret Value change");
  });
  require(publications == 2, "changed and unchanged native publications completed");
}

class tree_events final : public IUIAutomationStructureChangedEventHandler,
                          public IUIAutomationFocusChangedEventHandler {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
    if (!out) return E_POINTER; *out = nullptr;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IUIAutomationStructureChangedEventHandler))
      *out = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
    else if (iid == __uuidof(IUIAutomationFocusChangedEventHandler))
      *out = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const auto remaining = --references_; if (!remaining) delete this; return remaining;
  }
  HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement* sender,
      StructureChangeType change, SAFEARRAY*) noexcept override {
    BSTR id{};
    if (!sender || FAILED(sender->get_CurrentAutomationId(&id))) { failed = true; SetEvent(structure.get()); return S_OK; }
    if (!id || std::wstring(id, SysStringLen(id)) != L"soui-uia-root" ||
        change != StructureChangeType_ChildrenInvalidated) failed = true;
    SysFreeString(id); ++structures; SetEvent(structure.get()); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) noexcept override {
    BSTR id{}; int pid{};
    if (!sender || FAILED(sender->get_CurrentProcessId(&pid)) || pid != static_cast<int>(GetCurrentProcessId()) ||
        FAILED(sender->get_CurrentAutomationId(&id))) return S_OK;
    const bool expected = id && std::wstring(id, SysStringLen(id)) == L"title";
    SysFreeString(id); if (!expected) return S_OK; // Other desktop focus is outside this registration's assertion.
    BOOL focused{}; if (FAILED(sender->get_CurrentHasKeyboardFocus(&focused)) || !focused) failed = true;
    ++focuses; SetEvent(focus.get()); return S_OK;
  }
  event structure, focus;
  std::atomic<unsigned> structures{}, focuses{};
  std::atomic<bool> failed{};
 private: std::atomic<ULONG> references_{1};
};

void tree_events_case() {
  fixture host;
  host.event_mutation = [&] {
    auto extra = host.projection.nodes[5]; extra.id = "new-task"; extra.name = L"New task";
    host.projection.nodes.push_back(std::move(extra));
    SetFocus(host.window); require(GetFocus() == host.window, "actual owner focus before projection");
    host.projection.focused_id = "title";
    return host.provider->publish(host.projection);
  };
  run_mta(host, [&] {
    com_ref<IUIAutomation> client;
    ok(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IUIAutomation), reinterpret_cast<void**>(client.put())), "tree event client");
    com_ref<IUIAutomationElement> root;
    ok(client->ElementFromHandle(host.window, root.put()), "tree event HWND");
    com_ref<tree_events> sink; sink.reset(new tree_events);
    ok(client->AddStructureChangedEventHandler(root.get(), TreeScope_Element, nullptr, sink.get()), "structure subscription");
    const auto focus_registered = client->AddFocusChangedEventHandler(nullptr, sink.get());
    if (FAILED(focus_registered)) {
      client->RemoveStructureChangedEventHandler(root.get(), sink.get()); ok(focus_registered, "focus subscription");
    }
    DWORD_PTR published{};
    const bool sent = SendMessageTimeoutW(host.window, WM_APP + 0x478, 0, 0, SMTO_ABORTIFHUNG, 2000, &published) != 0;
    HANDLE signals[]{sink->structure.get(), sink->focus.get()};
    const auto delivered = WaitForMultipleObjects(2, signals, TRUE, 1500);
    const auto removed_focus = client->RemoveFocusChangedEventHandler(sink.get());
    const auto removed_structure = client->RemoveStructureChangedEventHandler(root.get(), sink.get());
    ok(removed_focus, "focus handler removal"); ok(removed_structure, "structure handler removal");
    require(sent && static_cast<HRESULT>(published) == S_OK, "owner tree mutation published");
    require(delivered == WAIT_OBJECT_0 && sink->structures > 0 && sink->focuses > 0 && !sink->failed,
      "real subscribed client receives structure and actual focus changes");
  });
}

void revoked_events_case() {
  fixture host;
  host.event_mutation = [&] {
    auto invalid = host.projection; invalid.nodes.back().value = L"not-public";
    require(host.provider->publish(invalid) == E_INVALIDARG, "secret rejected before notification admission");
    host.projection.nodes[1].value = L"revoked-before-pump";
    ok(host.provider->publish(host.projection), "public change queued before close");
    return host.provider->close(); // No event/API borrow has started, so revocation discards the pending batch.
  };
  run_mta(host, [&] {
    com_ref<IUIAutomation> client;
    ok(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IUIAutomation), reinterpret_cast<void**>(client.put())), "revocation event client");
    com_ref<IUIAutomationElement> root;
    ok(client->ElementFromHandle(host.window, root.put()), "revocation event HWND");
    com_ref<property_events> sink; sink.reset(new property_events);
    PROPERTYID properties[]{UIA_ValueValuePropertyId};
    ok(client->AddPropertyChangedEventHandlerNativeArray(root.get(), TreeScope_Subtree,
      nullptr, sink.get(), properties, 1), "revocation subscription");
    DWORD_PTR closed{};
    const bool sent = SendMessageTimeoutW(host.window, WM_APP + 0x478, 0, 0, SMTO_ABORTIFHUNG, 2000, &closed) != 0;
    const auto received = WaitForSingleObject(sink->received.get(), 250);
    const auto removed = client->RemovePropertyChangedEventHandler(root.get(), sink.get());
    ok(removed, "revoked handler removal");
    require(sent && static_cast<HRESULT>(closed) == S_OK, "close completes before HWND destruction");
    require(received == WAIT_TIMEOUT && sink->count == 0 && !sink->failed,
      "neither secret rejection nor revoked pending publication sends Value event");
  });
}

class invoke_events final : public IUIAutomationEventHandler {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
    if (!out) return E_POINTER; *out = nullptr;
    if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationEventHandler)) return E_NOINTERFACE;
    *out = static_cast<IUIAutomationEventHandler*>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const auto remaining = --references_; if (!remaining) delete this; return remaining;
  }
  HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement* sender, EVENTID id) noexcept override {
    BSTR name{}; VARIANT status; VariantInit(&status);
    const bool matched = sender && id == UIA_Invoke_InvokedEventId &&
      SUCCEEDED(sender->get_CurrentAutomationId(&name)) && name && std::wstring(name, SysStringLen(name)) == L"commit" &&
      SUCCEEDED(sender->GetCurrentPropertyValue(UIA_ItemStatusPropertyId, &status)) && status.vt == VT_BSTR &&
      status.bstrVal && std::wstring(status.bstrVal, SysStringLen(status.bstrVal)) == L"invoked";
    SysFreeString(name); VariantClear(&status);
    if (!matched) failed = true;
    ++count; SetEvent(received.get()); return S_OK;
  }
  event received;
  std::atomic<unsigned> count{};
  std::atomic<bool> failed{};
 private:
  std::atomic<ULONG> references_{1};
};

void invoke_event_scenario(unsigned mode) {
  fixture host(mode != 1); host.mark_invoked = true;
  if (mode == 2) host.dispatch_result = E_FAIL;
  if (mode == 3) host.close_inside = true;
  run_mta(host, [&] {
    com_ref<IUIAutomation> client;
    ok(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IUIAutomation), reinterpret_cast<void**>(client.put())), "invoke event client activation");
    com_ref<IUIAutomationElement> root;
    ok(client->ElementFromHandle(host.window, root.put()), "invoke event real HWND");
    VARIANT id; VariantInit(&id); id.vt = VT_BSTR; id.bstrVal = SysAllocString(L"commit");
    require(id.bstrVal != nullptr, "invoke event identity");
    com_ref<IUIAutomationCondition> condition;
    const auto condition_result = client->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, condition.put());
    VariantClear(&id); ok(condition_result, "invoke event condition");
    com_ref<IUIAutomationElement> button;
    ok(root->FindFirst(TreeScope_Children, condition.get(), button.put()), "invoke event finds button");
    require(button.get() != nullptr, "invoke event actual button");
    com_ref<IUIAutomationInvokePattern> pattern;
    ok(button->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern),
      reinterpret_cast<void**>(pattern.put())), "invoke event actual pattern");
    com_ref<invoke_events> sink; sink.reset(new invoke_events);
    ok(client->AddAutomationEventHandler(UIA_Invoke_InvokedEventId, button.get(), TreeScope_Element,
      nullptr, sink.get()), "real Invoked event subscription");
    const auto admitted = pattern->Invoke();
    const auto received = WaitForSingleObject(sink->received.get(), mode == 0 ? 1500 : 300);
    const auto removed = client->RemoveAutomationEventHandler(UIA_Invoke_InvokedEventId, button.get(), sink.get());
    ok(removed, "Invoked handler removed on registering MTA");
    ok(admitted, "Invoke admission is asynchronous");
    require(host.policy_count == 1 && host.dispatch_count == (mode == 1 ? 0U : 1U),
      "queued request actually reached expected policy/dispatch boundary");
    if (mode == 0)
      require(received == WAIT_OBJECT_0 && sink->count >= 1 && !sink->failed,
        "real client receives Invoked only after committed observable business result");
    else require(received == WAIT_TIMEOUT && sink->count == 0 && !sink->failed,
      "denied, failed or revoked Invoke never emits success notification");
  });
  if (mode == 3) require(host.callback_close == S_FALSE, "close retains in-flight dispatch borrow");
}
void invoke_events_case() { invoke_event_scenario(0); }
void denied_invoke_events_case() { invoke_event_scenario(1); }
void failed_invoke_events_case() { invoke_event_scenario(2); }
void closed_invoke_events_case() { invoke_event_scenario(3); }

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
      {"owner-focus", owner_focus_case}, {"uia-focus", client_focus_case},
      {"owner-sync", owner_sync_case}, {"uia-mutations", client_mutations_case},
      {"property-events", property_events_case}, {"tree-events", tree_events_case}, {"revoked-events", revoked_events_case},
      {"invoke-events", invoke_events_case}, {"denied-invoke-events", denied_invoke_events_case},
      {"failed-invoke-events", failed_invoke_events_case}, {"closed-invoke-events", closed_invoke_events_case},
      {"com-initialization", com_initialization_case}, {"api-close", api_close_case}};
    bool found = selected == "all";
    for (const auto& item : cases) if (selected == "all" || selected == item.first) { found = true; item.second(); std::cout << "case=" << item.first << " passed\n"; }
    require(found, "unknown case"); require(UnregisterClassW(class_name, GetModuleHandleW(nullptr)) != FALSE, "native class cleanup");
    return 0;
  } catch (const std::exception& failure) { std::cerr << "UIA native test failed: " << failure.what() << '\n'; return 1; }
}
