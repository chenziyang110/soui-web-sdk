// Dedicated measurement TU: the SDK's actual implementation and its existing
// private, native grant boundary. The normal library never compiles this file.
#include "../../sdk/renderers/webview2/webview2_host.cpp"
#include "../scratch-webview2-baseline/diagnostic-capture-policy.h"
#include "diagnostic_recipe.h"

namespace soui::sdk {
namespace {
std::string profile_utf8(LPCWSTR raw) {
  constexpr std::size_t maximum = 16777216;
  if (!raw) return "{}";
  const auto length = wcsnlen_s(raw, maximum + 1);
  if (length > maximum) throw std::runtime_error("Diagnostic UTF-16 response exceeds limit");
  if (!length) return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
  if (bytes <= 0 || static_cast<std::size_t>(bytes) > maximum) throw std::runtime_error("Diagnostic UTF-8 response exceeds limit");
  std::string output(static_cast<std::size_t>(bytes), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw, static_cast<int>(length), output.data(), bytes, nullptr, nullptr) != bytes)
    throw std::runtime_error("Diagnostic UTF-8 conversion failed");
  return output;
}
bool profile_current(const std::shared_ptr<state>& value, std::uint64_t generation, std::uint64_t epoch) {
  return live(value) && value->current == state::phase::mounted && value->native_grant &&
      value->generation == generation && value->grant_epoch == epoch && !value->stop_requested;
}
auto profile_callback(const std::shared_ptr<state>& value, detail::stage0_profile_completion callback) {
  const auto receipt = begin_async(value);
  const auto generation = value->generation, epoch = value->grant_epoch;
  const std::weak_ptr<state> weak(value);
  return std::make_pair(receipt, [weak, receipt, generation, epoch, callback = std::move(callback)](HRESULT result, LPCWSTR raw) noexcept -> HRESULT {
    const auto current = weak.lock();
    if (!current) return S_OK;
    async_completion completion(current, receipt);
    if (!completion) return S_OK;
    try {
      if (!profile_current(current, generation, epoch)) callback(error("E_CANCELLED", "Diagnostic target retired"), {});
      else if (FAILED(result)) callback(error("E_WEBVIEW2", hresult_text(result)), {});
      else {
        std::string converted;
        try { converted=profile_utf8(raw); }
        catch (const std::exception& exception) { callback(error("E_DIAGNOSTIC_RESPONSE",exception.what()),{}); return S_OK; }
        callback(ok(),std::move(converted));
      }
      return S_OK;
    } catch (...) { current->protocol_failed = true; return E_FAIL; }
  });
}
struct profile_admission final {
  std::shared_ptr<state> owner;
  std::shared_ptr<async_receipt> receipt;
  bool submitted{};
  ~profile_admission() { if (!submitted) reject_async(owner,receipt); }
};
} // namespace

class detail::stage0_profile_subscription final {
 public:
  std::weak_ptr<state> owner;
  ComPtr<ICoreWebView2DevToolsProtocolEventReceiver> receiver;
  EventRegistrationToken token{};
  std::uint64_t generation{}, epoch{};
  std::wstring event;
  stage0_profile_event sink;
  unsigned active{};
  bool registered{};
  ~stage0_profile_subscription() { if (registered || active) std::terminate(); }
};

host_result detail::stage0_webview2_access::profile_command(webview2_adapter& adapter,
    const stage0_webview2_grant& grant, const std::wstring& method, stage0_profile_completion callback) {
  if (!callback) return error("E_INVALID_PARAMS", "Diagnostic completion is required");
  const auto authorized = validate(adapter, grant);
  if (!authorized.ok) return authorized;
  const auto value = adapter.state_;
  try {
    const auto protocol = json::parse(soui::scratch_diagnostic::recipe_input::protocol);
    const auto parameters = soui::scratch_diagnostic::command_parameters(method, protocol).dump();
    const auto native_parameters = wide(parameters);
    auto completion = profile_callback(value, std::move(callback));
    profile_admission admission{value,completion.first};
    const auto handler = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(std::move(completion.second));
    if (!handler) throw std::bad_alloc();
    const auto submitted = value->core->CallDevToolsProtocolMethod(method.c_str(), native_parameters.c_str(),handler.Get());
    if (FAILED(submitted)) return error("E_WEBVIEW2", hresult_text(submitted));
    admission.submitted = true;
    return ok();
  } catch (const std::exception& exception) { return error("E_DIAGNOSTIC_COMMAND", exception.what()); }
}

host_result detail::stage0_webview2_access::profile_page(webview2_adapter& adapter,
    const stage0_webview2_grant& grant, profile_page_action action, stage0_profile_completion callback) {
  if (!callback) return error("E_INVALID_PARAMS", "Diagnostic completion is required");
  const auto authorized = validate(adapter, grant);
  if (!authorized.ok) return authorized;
  const auto value = adapter.state_;
  try {
    using fixed = soui::scratch_diagnostic::page_script;
    fixed operation;
    switch (action) {
      case profile_page_action::setup: operation = fixed::setup; break;
      case profile_page_action::query: operation = fixed::query; break;
      case profile_page_action::install: operation = fixed::install; break;
      case profile_page_action::start: operation = fixed::start; break;
      case profile_page_action::finish: operation = fixed::finish; break;
      case profile_page_action::abort: operation = fixed::abort; break;
      case profile_page_action::stop_workload: operation = fixed::stop_workload; break;
      default: return error("E_INVALID_PARAMS", "Unknown diagnostic page operation");
    }
    const auto script = soui::scratch_diagnostic::fixed_page_script(operation,
        json::parse(soui::scratch_diagnostic::recipe_input::scripts), soui::scratch_diagnostic::recipe_input::probe);
    const auto native_script = wide(script);
    auto completion = profile_callback(value, std::move(callback));
    profile_admission admission{value,completion.first};
    const auto handler = Callback<ICoreWebView2ExecuteScriptCompletedHandler>(std::move(completion.second));
    if (!handler) throw std::bad_alloc();
    const auto submitted = value->core->ExecuteScript(native_script.c_str(),handler.Get());
    if (FAILED(submitted)) return error("E_WEBVIEW2", hresult_text(submitted));
    admission.submitted = true;
    return ok();
  } catch (const std::exception& exception) { return error("E_DIAGNOSTIC_SCRIPT", exception.what()); }
}

host_result detail::stage0_webview2_access::profile_subscribe(webview2_adapter& adapter,
    const stage0_webview2_grant& grant, const std::wstring& event, stage0_profile_event sink,
    std::shared_ptr<stage0_profile_subscription>& output) {
  if (output || !sink || (event != L"Tracing.dataCollected" && event != L"Tracing.bufferUsage" && event != L"Tracing.tracingComplete"))
    return error("E_INVALID_PARAMS", "Diagnostic subscription is not in the fixed recipe");
  const auto authorized = validate(adapter, grant);
  if (!authorized.ok) return authorized;
  const auto value = adapter.state_;
  auto subscription = std::make_shared<stage0_profile_subscription>();
  subscription->owner = value; subscription->generation = value->generation; subscription->epoch = value->grant_epoch;
  subscription->event = event; subscription->sink = std::move(sink);
  auto result = value->core->GetDevToolsProtocolEventReceiver(event.c_str(), &subscription->receiver);
  if (FAILED(result)) return error("E_WEBVIEW2", hresult_text(result));
  const std::weak_ptr<stage0_profile_subscription> weak(subscription);
  result = subscription->receiver->add_DevToolsProtocolEventReceived(
      Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler>(
          [weak](ICoreWebView2* sender, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args) noexcept -> HRESULT {
            const auto lease = weak.lock(); if (!lease) return S_OK;
            const auto current = lease->owner.lock(); if (!current) return S_OK;
            stage0_event_borrow borrow(current);
            struct active_borrow { unsigned& count; explicit active_borrow(unsigned& count) : count(count) { ++count; } ~active_borrow() { --count; } } active(lease->active);
            if (!lease->registered || !profile_current(current, lease->generation, lease->epoch)) return S_OK;
            try {
              ComPtr<IUnknown> actual, incoming;
              if (!sender || FAILED(current->core.As(&actual)) || FAILED(sender->QueryInterface(IID_PPV_ARGS(&incoming))) || actual.Get() != incoming.Get())
                throw std::runtime_error("Diagnostic event target differs");
              task_string raw; check(args->get_ParameterObjectAsJson(&raw.value), "Diagnostic event payload");
              lease->sink(lease->event, profile_utf8(raw.value)); return S_OK;
            } catch (...) { current->protocol_failed = true; return E_FAIL; }
          }).Get(), &subscription->token);
  if (FAILED(result)) return error("E_WEBVIEW2", hresult_text(result));
  subscription->registered = true; output = std::move(subscription); return ok();
}

host_result detail::stage0_webview2_access::profile_unsubscribe(std::shared_ptr<stage0_profile_subscription>& value) {
  if (!value) return ok();
  const auto owner = value->owner.lock();
  if (!owner || GetCurrentThreadId() != owner->ui_thread) return error("E_THREAD", "Diagnostic subscription cleanup requires its owner STA");
  if (value->active) return error("E_BUSY", "Diagnostic event is still borrowed");
  if (value->registered) {
    const auto result = value->receiver->remove_DevToolsProtocolEventReceived(value->token);
    if (FAILED(result)) return error("E_WEBVIEW2", hresult_text(result));
    value->registered = false;
  }
  value->receiver.Reset(); value.reset(); return ok();
}
} // namespace soui::sdk
