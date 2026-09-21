#include <soui/json_bridge.h>
#include "../../third_party/include/json/json.hpp"
#include <windows.h>
#include <objbase.h>
#include <stdexcept>

namespace soui::sdk {
using json = nlohmann::json;
namespace {
std::string error(const std::string& id, const char* code, const char* message) {
  return json{{"requestId",id},{"ok",false},{"error",{{"code",code},{"message",message}}}}.dump();
}
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
json_bridge::json_bridge(std::string origin) : origin_{std::move(origin)} {
  if (origin_.rfind("app://",0) != 0 || origin_.size() > 512)
    throw std::invalid_argument("Native origin must begin with app://");
  (void)register_method("bridge.probe.report", "bridge.probe", [](std::string_view params) {
    const auto value=json::parse(params);
    return value.is_object() && value.size()==1 && value.value("ready",false)
      ? std::string{"{\"ready\":true}"}
      : std::string{"{\"error\":\"E_INVALID_PARAMS\",\"message\":\"Expected ready=true.\"}"};
  });
}
bool json_bridge::register_method(std::string name, std::string capability,
                                 soui::runtime::capability_registry::handler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registration_closed_) return false;
  if (!registry_.register_method(std::move(name),capability,std::move(handler))) return false;
  capabilities_.push_back(std::move(capability));
  return true;
}
std::string json_bridge::handle_json(std::string_view request, bool) {
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
    if(packet.value("method",std::string{}) != "bridge.bootstrap") return transport_.handle_json(request);
    if(packet.size()!=6 || !packet.contains("requestId") || !packet.contains("sessionId") ||
       !packet.contains("origin") || !packet.contains("token") || !packet.contains("params") ||
       request_id.empty() || request_id.size()>512 ||
       packet.value("sessionId",std::string{})!="" || packet.value("origin",std::string{})!="" ||
       packet.value("token",std::string{})!="" || packet.at("params") != json{{"protocol","soui.bridge/1"}})
      return error(request_id,"E_UNAUTHORIZED","Invalid local bootstrap envelope.");
    std::lock_guard<std::mutex> lock(mutex_);
    if(!active_session_.empty())return error(request_id,"E_UNAUTHORIZED","The document already has an active session.");
    registration_closed_=true;
    active_session_=new_session();
    const auto token=permissions_.issue(active_session_,origin_,capabilities_);
    return json{{"requestId",request_id},{"ok",true},{"result",{{"sessionId",active_session_},{"origin",origin_},{"token",token}}}}.dump();
  } catch(const std::exception&) {
    return error(request_id,"E_INVALID_PARAMS","Malformed bridge request or method parameters.");
  }
}
void json_bridge::shutdown_session(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  permissions_.revoke_session(session_id);
  if(active_session_==session_id)active_session_.clear();
}
}  // namespace soui::sdk
