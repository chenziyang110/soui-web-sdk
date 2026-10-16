#include <soui/json_bridge.h>
#include <soui/runtime/agent/windows_local_endpoint.h>
#include "runtime/services/gui/gui_commands_internal.h"
#include "runtime/diagnostics/runtime_metrics_internal.h"
#include "../../third_party/include/json/json.hpp"
#include <windows.h>
#include <objbase.h>
#include <array>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace soui::sdk {
using json = nlohmann::json;
namespace {
std::string error(const std::string& id, const char* code, const char* message) {
  return json{{"requestId",id},{"ok",false},{"error",{{"code",code},{"message",message}}}}.dump();
}
std::string payload_error(const std::string& code, const std::string& message) {
  return json{{"error",code},{"message",message}}.dump();
}
enum class window_action { set_title, begin_drag, minimize, toggle_maximize, close };
std::string new_session() {
  GUID id{};
  if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("Session identity unavailable");
  wchar_t wide[40]{};
  if (!StringFromGUID2(id, wide, 40)) throw std::runtime_error("Session identity unavailable");
  std::string result="soui:";
  for (const auto c : wide) if(c && c != L'{' && c != L'}') result += static_cast<char>(c);
  return result;
}
}
json_bridge::json_bridge(std::string origin) : json_bridge(std::move(origin), diagnostics::mode::disabled) {}
json_bridge::json_bridge(std::string origin, diagnostics::mode metrics)
    : json_bridge(std::move(origin), metrics, std::nullopt) {}
json_bridge::json_bridge(std::string origin, diagnostics::mode metrics,
    std::optional<soui::runtime::task_store_request_quota_policy> quota)
    : origin_{std::move(origin)}, metrics_mode_{metrics}, permissions_{metrics}, tasks_{metrics, quota} {
  if (origin_.rfind("app://",0) != 0 || origin_.size() > 512)
    throw std::invalid_argument("Native origin must begin with app://");
  (void)register_method("bridge.probe.report", "bridge.probe", [](std::string_view params) {
    const auto value=json::parse(params);
    return value.is_object() && value.size()==1 && value.value("ready",false)
      ? std::string{"{\"ready\":true}"}
      : std::string{"{\"error\":\"E_INVALID_PARAMS\",\"message\":\"Expected ready=true.\"}"};
  });
  (void)register_contextual_method("bridge.describe", "bridge.describe",
      [this](const soui::runtime::bridge_request& request) {
        return transport_.describe(request);
      });
  (void)register_contextual_method("task.start", "task.start", [this](const soui::runtime::bridge_request& request) {
    return tasks_.start(request);
  });
  (void)register_contextual_method("task.query", "task.query", [this](const soui::runtime::bridge_request& request) {
    return tasks_.query(request);
  });
  (void)register_contextual_method("task.cancel", "task.cancel", [this](const soui::runtime::bridge_request& request) {
    return tasks_.cancel(request);
  });
  (void)register_contextual_method("task.subscribe", "task.subscribe", [this](const soui::runtime::bridge_request& request) {
    return tasks_.subscribe(request);
  });
  (void)register_contextual_method("task.unsubscribe", "task.subscribe", [this](const soui::runtime::bridge_request& request) {
    return tasks_.unsubscribe(request);
  });
}
json_bridge::~json_bridge() { close_gui_commands(); }
soui::runtime::task_store_request_quota_status json_bridge::read_task_store_request_quota(
    soui::runtime::task_store_request_quota_snapshot& out) const noexcept {
  return tasks_.read_request_quota(out);
}

diagnostics::status json_bridge::read_task_store_storage_metrics(diagnostics::task_store_storage& out) const noexcept {
  return tasks_.read_storage_metrics(out);
}
diagnostics::status json_bridge::read_task_store_private_storage_metrics(diagnostics::task_store_private_storage& out) const noexcept {
  return tasks_.read_private_storage_metrics(out);
}
diagnostics::status json_bridge::read_runtime_metrics(diagnostics::bridge_runtime& out) const noexcept {
  out = {};
  if (metrics_mode_ != diagnostics::mode::scalar) return diagnostics::status::disabled;
  auto state = tasks_.read_runtime_metrics(out.task_service);
  state = diagnostics::detail::combine(state, permissions_.read_runtime_metrics(out.permission_store));
  try {
    std::shared_ptr<soui::runtime::detail::gui_commands_state> gui;
    {
      std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
      if (!lock.owns_lock()) {
        out.gui_commands.observation.state = diagnostics::status::busy;
        return diagnostics::detail::combine(state, diagnostics::status::busy);
      }
      gui = gui_;
    } // No bridge lock while sampling another owner.
    if (!gui) { out.gui_commands.observation.state = diagnostics::status::not_started; return state; }
    return diagnostics::detail::combine(state, gui->read_runtime_metrics(out.gui_commands));
  } catch (...) { out.gui_commands = {}; out.gui_commands.observation.state = diagnostics::status::unavailable;
    return diagnostics::detail::combine(state, diagnostics::status::unavailable); }
}
soui::runtime::agent::windows_local_endpoint* json_bridge::create_local_endpoint() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registration_closed_) return nullptr;
  auto endpoint = std::make_unique<soui::runtime::agent::windows_local_endpoint>(
      transport_, permissions_, &tasks_);
  local_endpoint_ = std::move(endpoint);
  registration_closed_ = true;
  return local_endpoint_.get();
}
bool json_bridge::register_method(std::string name, std::string capability,
                                 soui::runtime::capability_registry::handler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registration_closed_) return false;
  if (!registry_.register_method(std::move(name),capability,std::move(handler))) return false;
  capabilities_.push_back(std::move(capability));
  return true;
}
bool json_bridge::register_contextual_method(std::string name, std::string capability,
                                             soui::runtime::capability_registry::contextual_handler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registration_closed_) return false;
  if (!registry_.register_contextual_method(std::move(name), capability, std::move(handler))) return false;
  capabilities_.push_back(std::move(capability));
  return true;
}
bool json_bridge::register_action(std::string name, std::string capability, std::string contract_json,
                                  soui::runtime::capability_registry::contextual_handler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registration_closed_ || !registry_.register_action(std::move(name), capability,
                                                       std::move(contract_json), std::move(handler))) return false;
  capabilities_.push_back(std::move(capability));
  return true;
}
bool json_bridge::allow_local_action(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return !registration_closed_ && registry_.allow_local_action(name);
}
bool json_bridge::set_session_capabilities(std::vector<std::string> capabilities) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registration_closed_) return false;
  std::set<std::string> seen;
  for (const auto& capability : capabilities)
    if (std::find(capabilities_.begin(), capabilities_.end(), capability) == capabilities_.end() ||
        !seen.insert(capability).second) return false;
  session_capabilities_ = std::move(capabilities);
  session_capabilities_configured_ = true;
  return true;
}
bool json_bridge::register_task_handler(std::string kind, soui::runtime::task_service::handler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  return !registration_closed_ && tasks_.register_handler(std::move(kind), std::move(handler));
}
bool json_bridge::register_local_task_family(soui::runtime::local_task_family_definition definition) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Local capabilities must never enter the default or explicit page grants.
  return !registration_closed_ && static_cast<bool>(tasks_.register_local_family(registry_, std::move(definition)));
}
bool json_bridge::enable_gui_commands(std::vector<soui::runtime::gui_command_definition> rows) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registration_closed_ || gui_) return false;
  auto capabilities=capabilities_;
  capabilities.insert(capabilities.end(),{"gui.bind","gui.effect","gui.ack"});
  auto prepared=soui::runtime::detail::gui_commands_state::create(registry_,transport_,tasks_,std::move(rows),metrics_mode_);
  if (!prepared) return false;
  prepared->set_sink(event_sink_);
  capabilities_.swap(capabilities); gui_=std::move(prepared);
  return true;
}
bool json_bridge::bind_gui_document(std::string generation,std::function<bool()> current) {
  std::shared_ptr<soui::runtime::detail::gui_commands_state> gui;
  { std::lock_guard<std::mutex> lock(mutex_); gui=gui_; }
  return gui && gui->bind(std::move(generation),std::move(current));
}
soui::runtime::gui_owner_result json_bridge::poll_gui_commands() {
  std::shared_ptr<soui::runtime::detail::gui_commands_state> gui;
  { std::lock_guard<std::mutex> lock(mutex_); gui=gui_; }
  return gui?gui->poll():soui::runtime::gui_owner_result::closed;
}
void json_bridge::close_gui_commands() noexcept {
  // Registration is native-only, before use. During destruction callers have
  // stopped; during use the shared object is immutable after publication.
  std::shared_ptr<soui::runtime::detail::gui_commands_state> gui;
  { std::lock_guard<std::mutex> lock(mutex_); gui=gui_; }
  if (gui) gui->close();
}
bool json_bridge::enable_session_renewal() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registration_closed_ || !transport_.enable_session_renewal()) return false;
  capabilities_.emplace_back("bridge.session.renew");
  return true;
}
bool json_bridge::enable_directory_service() {
  std::lock_guard<std::mutex> lock(mutex_);
  constexpr const char* method = "desktop.dialog.pickDirectory";
  if (registration_closed_ || registry_.contains(method)) return false;
  const bool registered = registry_.register_contextual_method(method, method,
      [this](const soui::runtime::bridge_request& request) -> std::string {
        const auto params = json::parse(request.params_json, nullptr, false);
        if (!params.is_object() || !params.empty())
          return payload_error("E_INVALID_PARAMS", "This dialog method takes no parameters.");
        const soui::runtime::desktop_request_context context{
            request.session_id, request.origin, request.token, request.native_user_gesture};
        const auto response = directory_.open(owner_window_.load(), context);
        if (response.kind == soui::runtime::result_kind::user_cancelled)
          return R"({"cancelled":true,"path":""})";
        if (!response.ok()) return payload_error(response.code, response.message);
        return json{{"cancelled", false}, {"path", response.value}}.dump();
      });
  if (registered) capabilities_.emplace_back(method);
  return registered;
}
bool json_bridge::enable_clipboard_service() {
  std::lock_guard<std::mutex> lock(mutex_);
  constexpr const char* method = "desktop.clipboard.writeText";
  if (registration_closed_ || registry_.contains(method)) return false;
  const bool registered = registry_.register_contextual_method(method, method,
      [this](const soui::runtime::bridge_request& request) -> std::string {
        const auto params = json::parse(request.params_json, nullptr, false);
        if (!params.is_object() || params.size() != 1U || !params.contains("text") ||
            !params["text"].is_string())
          return json{{"error", "E_INVALID_PARAMS"},
                      {"message", "Expected only a text string."}}.dump();
        const soui::runtime::desktop_request_context context{
            request.session_id, request.origin, request.token, request.native_user_gesture};
        const auto response = clipboard_.write_text(owner_window_.load(), context,
                                                     params["text"].get_ref<const std::string&>());
        if (!response.ok())
          return json{{"error", response.code}, {"message", response.message}}.dump();
        return R"({"applied":true})";
      });
  if (registered) capabilities_.emplace_back(method);
  return registered;
}
bool json_bridge::enable_window_service() {
  std::lock_guard<std::mutex> lock(mutex_);
  constexpr std::array<std::pair<const char*, window_action>, 5> methods{{
      {"desktop.window.setTitle", window_action::set_title},
      {"desktop.window.beginDrag", window_action::begin_drag},
      {"desktop.window.minimize", window_action::minimize},
      {"desktop.window.toggleMaximize", window_action::toggle_maximize},
      {"desktop.window.close", window_action::close},
  }};
  if (registration_closed_) return false;
  for (const auto& method : methods)
    if (registry_.contains(method.first)) return false;
  for (const auto& method : methods) {
    const bool registered = registry_.register_contextual_method(method.first, method.first,
        [this, action = method.second](const soui::runtime::bridge_request& request) -> std::string {
          const auto params = json::parse(request.params_json, nullptr, false);
          if (!params.is_object())
            return payload_error("E_INVALID_PARAMS", "Expected window method parameters.");
          if (action == window_action::set_title) {
            if (params.size() != 1U || !params.contains("title") || !params["title"].is_string() ||
                params["title"].get_ref<const std::string&>().size() > 512U)
              return payload_error("E_INVALID_PARAMS", "Expected only a title of at most 512 bytes.");
          } else if (!params.empty()) {
            return payload_error("E_INVALID_PARAMS", "This window method takes no parameters.");
          }
          const soui::runtime::desktop_request_context context{
              request.session_id, request.origin, request.token, request.native_user_gesture};
          const auto owner = owner_window_.load();
          soui::runtime::desktop_response response;
          switch (action) {
            case window_action::set_title:
              response = windows_.set_title(owner, context, params["title"].get_ref<const std::string&>());
              break;
            case window_action::begin_drag: response = windows_.begin_drag(owner, context); break;
            case window_action::minimize: response = windows_.minimize(owner, context); break;
            case window_action::toggle_maximize: response = windows_.toggle_maximize(owner, context); break;
            case window_action::close: response = windows_.close(owner, context); break;
          }
          if (!response.ok()) return payload_error(response.code, response.message);
          if (action == window_action::set_title) return R"({"applied":true})";
          if (action == window_action::begin_drag) return R"({"started":true})";
          return response.value;
        });
    if (!registered) return false;
    capabilities_.emplace_back(method.first);
  }
  return true;
}
void json_bridge::set_event_sink(event_sink sink) {
  auto shared=sink?std::make_shared<const event_sink>(std::move(sink)):nullptr;
  std::shared_ptr<soui::runtime::detail::gui_commands_state> gui;
  std::shared_ptr<const event_sink> previous;
  { std::lock_guard<std::mutex> lock(mutex_); gui=gui_; previous=event_sink_; event_sink_=shared; }
  tasks_.set_event_sink(shared?event_sink{[shared](std::string_view event){(*shared)(event);}}:event_sink{});
  if (gui) gui->set_sink(std::move(shared));
}
std::string json_bridge::handle_json(std::string_view request, bool native_user_gesture) {
  if (request.empty() || request.size() > soui::runtime::bridge_transport::max_message_bytes)
    return error({},"E_LIMIT_MESSAGE_SIZE","Bridge message is too large.");
  std::string request_id;
  try {
    auto packet=json::parse(request.begin(),request.end(),[](int depth,json::parse_event_t,json&) {
      if(depth>32)throw std::invalid_argument("JSON nesting exceeds limit");
      return true;
    });
    if(!packet.is_object())return error({},"E_INVALID_PARAMS","Expected a JSON object.");
    request_id=packet.value("requestId",std::string{});
    if(packet.value("method",std::string{}) != "bridge.bootstrap")
      return transport_.handle_json(request, native_user_gesture);
    if(packet.size()!=6 || !packet.contains("requestId") || !packet.contains("sessionId") ||
       !packet.contains("origin") || !packet.contains("token") || !packet.contains("params") ||
       request_id.empty() || request_id.size()>512 ||
       packet.value("sessionId",std::string{})!="" || packet.value("origin",std::string{})!="" ||
       packet.value("token",std::string{})!="" || packet.at("params") != json{{"protocol","soui.bridge/1"}})
      return error(request_id,"E_UNAUTHORIZED","Invalid local bootstrap envelope.");
    std::lock_guard<std::mutex> lock(mutex_);
    if(!active_session_.empty())return error(request_id,"E_UNAUTHORIZED","The document already has an active session.");
    registration_closed_=true;
    const auto session=new_session();
    const auto& granted_capabilities = session_capabilities_configured_ ? session_capabilities_ : capabilities_;
    const auto token=permissions_.issue(session,origin_,granted_capabilities);
    active_session_=session;
    try {
      if (gui_) gui_->set_page(permissions_.resolve(session,origin_,token,"gui.bind").scope,session,origin_);
    } catch (...) {
      permissions_.revoke_session(session); active_session_.clear(); throw;
    }
    json result{{"sessionId",session},{"origin",origin_},{"token",token}};
    if (std::find(granted_capabilities.begin(), granted_capabilities.end(), "bridge.session.renew") !=
        granted_capabilities.end()) {
      const auto lease=permissions_.lease(session,origin_,token);
      if (!lease.ok()) { permissions_.revoke_session(session); active_session_.clear();
        return error(request_id,lease.error_code.c_str(),"Session lease unavailable."); }
      result=json::parse(soui::runtime::bridge_transport::serialize_lease(lease));
    }
    return json{{"requestId",request_id},{"ok",true},{"result",std::move(result)}}.dump();
  } catch(const soui::runtime::scope_capacity_error&) {
    return error(request_id,"E_LIMIT_SCOPE_CAPACITY","Authorization scope capacity is exhausted.");
  } catch(const std::exception&) {
    return error(request_id,"E_INVALID_PARAMS","Malformed bridge request or method parameters.");
  }
}
void json_bridge::shutdown_session(const std::string& session_id) {
  std::shared_ptr<soui::runtime::detail::gui_commands_state> gui;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    permissions_.revoke_session(session_id);
    if(active_session_==session_id)active_session_.clear();
    gui=gui_;
  }
  if (gui) gui->shutdown_session(session_id);
  tasks_.shutdown_session(session_id);
}
}  // namespace soui::sdk
