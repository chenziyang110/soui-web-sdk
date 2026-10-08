#include <soui/json_bridge.h>
#include <soui/version.h>
#include <json.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <iostream>
#include <utility>
#include <vector>
using json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

namespace {
json bootstrap_session(soui::sdk::json_bridge& bridge) {
  const auto response = json::parse(bridge.handle_json(json{{"requestId", "discovery:boot"},
      {"sessionId", ""}, {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
      {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
  require(response.at("ok").get<bool>(), "discovery bootstrap");
  return response.at("result");
}

json discovery_request(const json& auth, json params = json::object()) {
  return {{"requestId", "discovery:1"}, {"sessionId", auth.at("sessionId")},
          {"origin", auth.at("origin")}, {"token", auth.at("token")},
          {"method", "bridge.describe"}, {"params", std::move(params)}};
}

json discovery_page(soui::sdk::json_bridge& bridge, const json& request) {
  const auto raw = bridge.handle_json(request.dump());
  require(raw.size() <= soui::runtime::bridge_transport::max_message_bytes,
          "discovery response obeys the complete wire message limit");
  const auto response = json::parse(raw);
  require(response.at("ok").get<bool>(), "authorized discovery succeeds");
  const auto page = response.at("result");
  require(page.size() == 4 && page.at("protocol") == "soui.capabilities/1" &&
          page.at("sdkVersion") == SOUI_SDK_VERSION_STRING && page.at("methods").is_array(),
          "discovery has an exact versioned public envelope");
  require(page.dump().find(request.at("token").get<std::string>()) == std::string::npos,
          "discovery never returns session credentials");
  for (const auto& method : page.at("methods"))
    require(method.size() == 2 && method.at("name").is_string() && method.at("capability").is_string(),
            "discovery exposes only method names and required capabilities");
  return page;
}

void expect_discovery_error(soui::sdk::json_bridge& bridge, const json& request, const char* code) {
  const auto raw = bridge.handle_json(request.dump());
  require(raw.size() <= soui::runtime::bridge_transport::max_message_bytes,
          "discovery errors remain within the wire message limit");
  const auto response = json::parse(raw);
  require(!response.at("ok").get<bool>() && response.at("error").at("code") == code,
          "discovery rejects the request with its explicit error code");
}

void discovery_contract_checks() {
  soui::sdk::json_bridge bridge("app://discovery");
  unsigned business_calls = 0;
  const auto business = [&](std::string_view) { ++business_calls; return std::string{"{}"}; };
  require(bridge.register_method("zeta.call", "zeta", business) &&
          bridge.register_method("alpha.call", "alpha", business), "custom discovery registration");
  const auto auth = bootstrap_session(bridge);
  auto request = discovery_request(auth);
  const auto full = discovery_page(bridge, request);
  require(full.at("nextCursor").is_null(), "small discovery completes in one page");
  std::vector<std::string> expected;
  for (const auto& method : full.at("methods")) expected.push_back(method.at("name").get<std::string>());
  require(std::is_sorted(expected.begin(), expected.end()) && expected.front() == "alpha.call" &&
          expected.back() == "zeta.call", "discovery sorts registration names, not registration order");
  require(std::find(expected.begin(), expected.end(), "bridge.describe") != expected.end() &&
          std::none_of(expected.begin(), expected.end(), [](const std::string& name) {
            return name.rfind("desktop.", 0) == 0;
          }), "core discovery includes itself and excludes optional desktop methods");
  std::vector<std::string> observed;
  std::uint64_t cursor = 0;
  for (;;) {
    request["params"] = {{"cursor", cursor}, {"limit", 2}};
    const auto page = discovery_page(bridge, request);
    require(page == discovery_page(bridge, request), "frozen discovery pages are repeatable");
    for (const auto& method : page.at("methods")) observed.push_back(method.at("name").get<std::string>());
    require(page.at("methods").size() <= 2, "discovery respects requested page length");
    if (page.at("nextCursor").is_null()) break;
    const auto next = page.at("nextCursor").get<std::uint64_t>();
    require(next > cursor && next == observed.size(), "discovery cursor always advances over emitted methods");
    cursor = next;
  }
  require(observed == expected && business_calls == 0, "pagination loses no methods and never calls business code");
  require(!bridge.register_method("late.call", "late", business), "discovery uses the frozen registry");
  request["params"] = {{"cursor", expected.size()}};
  const auto end = discovery_page(bridge, request);
  require(end.at("methods").empty() && end.at("nextCursor").is_null(), "end cursor returns an empty terminal page");
  for (const auto& params : std::vector<json>{
      {{"cursor", expected.size() + 1}}, {{"cursor", std::numeric_limits<std::uint64_t>::max()}},
      {{"cursor", -1}}, {{"cursor", 1.0}}, {{"cursor", true}}, {{"cursor", "0"}}, {{"cursor", nullptr}},
      {{"limit", 0}}, {{"limit", -1}}, {{"limit", 65}}, {{"limit", 1.5}}, {{"limit", false}},
      {{"limit", "1"}}, {{"limit", nullptr}}, {{"unknown", 1}}}) {
    request["params"] = params;
    expect_discovery_error(bridge, request, "E_INVALID_PARAMS");
  }
  request["params"] = json::object();
  for (const char* field : {"token", "origin", "sessionId"}) {
    auto forged = request;
    forged[field] = std::string(field) == "origin" ? "app://other" : "forged";
    expect_discovery_error(bridge, forged, "E_UNAUTHORIZED");
  }
  bridge.shutdown_session(auth.at("sessionId").get<std::string>());
  expect_discovery_error(bridge, request, "E_UNAUTHORIZED");
  require(discovery_page(bridge, discovery_request(bootstrap_session(bridge))) == full,
          "replacement session discovers the same frozen registrations");

  soui::sdk::json_bridge desktop("app://discovery");
  require(desktop.enable_directory_service() && desktop.enable_clipboard_service() && desktop.enable_window_service(),
          "optional services register before discovery bootstrap");
  const auto optional = discovery_page(desktop, discovery_request(bootstrap_session(desktop)));
  std::set<std::string> optional_names;
  for (const auto& method : optional.at("methods")) optional_names.insert(method.at("name").get<std::string>());
  for (const char* name : {"desktop.dialog.pickDirectory", "desktop.clipboard.writeText", "desktop.window.setTitle",
                           "desktop.window.beginDrag", "desktop.window.minimize", "desktop.window.toggleMaximize",
                           "desktop.window.close"})
    require(optional_names.count(name) == 1, "enabled optional methods appear without invoking native UI");

  soui::runtime::capability_registry registry;
  soui::runtime::permission_store permissions;
  soui::runtime::bridge_transport transport(registry, permissions);
  require(registry.register_contextual_method("bridge.describe", "bridge.describe",
      [&](const soui::runtime::bridge_request& call) { return transport.describe(call); }) &&
      registry.register_method("app.visible", "visible", business) &&
      registry.register_method("app.hidden", "hidden", business), "selective discovery registration");
  const auto token = permissions.issue("selective-session", "app://discovery", {"bridge.describe", "visible"});
  const auto selective_request = discovery_request({{"sessionId", "selective-session"},
      {"origin", "app://discovery"}, {"token", token}});
  const auto selective = json::parse(transport.handle_json(selective_request.dump())).at("result");
  require(selective.at("methods") == json::array({{{"name", "app.visible"}, {"capability", "visible"}},
      {{"name", "bridge.describe"}, {"capability", "bridge.describe"}}}) && business_calls == 0,
      "discovery filters every entry by the actual token capability grant");
  auto unauthorized_page = selective_request;
  unauthorized_page["params"] = {{"cursor", 3}};
  const auto outside = json::parse(transport.handle_json(unauthorized_page.dump()));
  require(!outside.at("ok").get<bool>() && outside.at("error").at("code") == "E_INVALID_PARAMS",
          "discovery cursors count only authorized entries");
  const auto no_discovery_token = permissions.issue("limited-session", "app://discovery", {"visible"});
  const auto forbidden_request = discovery_request({{"sessionId", "limited-session"},
      {"origin", "app://discovery"}, {"token", no_discovery_token}});
  const auto forbidden = json::parse(transport.handle_json(forbidden_request.dump()));
  require(!forbidden.at("ok").get<bool>() && forbidden.at("error").at("code") == "E_UNAUTHORIZED",
          "discovery itself requires its registered capability");
  require(registry.register_method("app.huge", "visible", [](std::string_view) {
    return json{{"data", std::string(70000, 'x')}}.dump();
  }), "oversized response test registration");
  auto huge_request = selective_request;
  huge_request["method"] = "app.huge";
  const auto huge_wire = transport.handle_json(huge_request.dump());
  const auto huge_response = json::parse(huge_wire);
  require(huge_wire.size() <= soui::runtime::bridge_transport::max_message_bytes &&
          !huge_response.at("ok").get<bool>() && huge_response.at("error").at("code") == "E_LIMIT_MESSAGE_SIZE" &&
          huge_response.at("requestId") == huge_request.at("requestId"),
          "oversized responses fail explicitly while preserving bounded request correlation");

  require(!registry.register_method(std::string(129, 'm'), "valid", business) &&
          !registry.register_method("invalid name", "valid", business) &&
          !registry.register_method("", "valid", business) &&
          !registry.register_method("bad.cap", std::string(513, 'c'), business) &&
          !registry.register_method("bad.cap", std::string(1, static_cast<char>(0xff)), business) &&
          !registry.register_method("bad.cap", "", business), "registration rejects uncallable names and unbounded or invalid UTF-8 capabilities");
  std::string unicode_capability;
  for (unsigned i = 0; i < 256; ++i) unicode_capability += "\xc3\xa9";
  require(registry.register_method(std::string(128, 'm'), unicode_capability, business) &&
          !registry.register_method("too.long.utf8", unicode_capability + "\xc3\xa9", business),
          "registration bounds are UTF-8 bytes rather than Unicode character count");

  soui::sdk::json_bridge many("app://discovery");
  for (unsigned i = 0; i < 80; ++i)
    require(many.register_method("many." + std::to_string(i), "many", business), "many-method registration");
  const auto many_auth = bootstrap_session(many);
  require(discovery_page(many, discovery_request(many_auth)).at("methods").size() == 32,
          "default discovery limit is 32");
  require(discovery_page(many, discovery_request(many_auth, {{"limit", 64}})).at("methods").size() == 64,
          "maximum discovery limit is 64");
  auto exact_request = discovery_request(many_auth, {{"cursor", 86}, {"limit", 1}});
  exact_request["operationId"] = "x";
  const auto baseline_bytes = many.handle_json(exact_request.dump()).size();
  exact_request["operationId"] = std::string(soui::runtime::bridge_transport::max_message_bytes - baseline_bytes + 1, 'x');
  const auto exact_wire = many.handle_json(exact_request.dump());
  require(exact_wire.size() == soui::runtime::bridge_transport::max_message_bytes &&
          json::parse(exact_wire).at("ok").get<bool>() &&
          json::parse(exact_wire).at("result").at("nextCursor").is_null(),
          "exact response budget includes terminal null and all operation metadata");

  soui::sdk::json_bridge escaped("app://discovery");
  for (unsigned i = 0; i < 80; ++i) {
    const auto prefix = "long." + std::to_string(i) + ".";
    require(escaped.register_method(prefix + std::string(128 - prefix.size(), 'm'), std::string(512, '\x01'), business),
            "JSON-escaped capability registration remains bounded");
  }
  const auto escaped_auth = bootstrap_session(escaped);
  cursor = 0;
  std::set<std::string> escaped_names;
  bool shortened = false;
  for (;;) {
    const auto page = discovery_page(escaped, discovery_request(escaped_auth, {{"cursor", cursor}, {"limit", 64}}));
    for (const auto& method : page.at("methods"))
      require(escaped_names.insert(method.at("name").get<std::string>()).second, "size-limited pages do not duplicate methods");
    if (page.at("nextCursor").is_null()) break;
    shortened = shortened || page.at("methods").size() < 64;
    const auto next = page.at("nextCursor").get<std::uint64_t>();
    require(next > cursor && next == escaped_names.size(), "size-limited pagination retains forward progress");
    cursor = next;
  }
  require(shortened && escaped_names.size() == 87 && business_calls == 0,
          "worst-case JSON escaping shrinks pages without losing any registered method");
}
}  // namespace

int main(){
  try {
    discovery_contract_checks();
    soui::sdk::json_bridge bridge("app://test");
    require(bridge.register_method("app.echo","app.echo",[](std::string_view value){return std::string(value);}),"method registration");
    unsigned contextual_calls=0;
    require(bridge.register_contextual_method("app.context","app.context",
        [&](const soui::runtime::bridge_request& request){
          ++contextual_calls;
          return json{{"session",request.session_id},{"origin",request.origin},
                      {"operationId",request.operation_id},{"params",json::parse(request.params_json)}}.dump();
        }),"contextual method registration");
    json bootstrap{{"requestId","boot:1"},{"sessionId",""},{"origin",""},{"token",""},{"method","bridge.bootstrap"},{"params",{{"protocol","soui.bridge/1"}}}};
    auto invalid=bootstrap;invalid.erase("origin");invalid["extra"]="";
    require(!json::parse(bridge.handle_json(invalid.dump())).at("ok").get<bool>(),"missing field must not become anonymous bootstrap");
    require(!json::parse(bridge.handle_json("null")).at("ok").get<bool>(),"reject non-object");
    require(!json::parse(bridge.handle_json(std::string(70000,'x'))).at("ok").get<bool>(),"message bound");
    const auto response=json::parse(bridge.handle_json(bootstrap.dump()));
    require(response.at("ok").get<bool>(),"bootstrap");
    const auto auth=response.at("result");
    require(!bridge.register_method("late","late",[](std::string_view){return "{}";}),"registration freezes after bootstrap");
    require(!json::parse(bridge.handle_json(bootstrap.dump())).at("ok").get<bool>(),"one session per document");
    json request{{"requestId","call:1"},{"sessionId",auth.at("sessionId")},{"origin",auth.at("origin")},{"token",auth.at("token")},{"method","app.echo"},{"params",{{"message","hello"}}},{"timeoutMs",10000}};
    auto echo=json::parse(bridge.handle_json(request.dump()));
    require(echo.at("ok").get<bool>() && echo.at("result").at("message")=="hello","native dispatch");
    auto context_request=request;
    context_request["method"]="app.context";
    context_request["operationId"]="op-1";
    context_request["params"]={{"sessionId","forged-param"}};
    const auto contextual=json::parse(bridge.handle_json(context_request.dump()));
    require(contextual.at("ok").get<bool>() && contextual.at("result").at("session")==auth.at("sessionId") &&
            contextual.at("result").at("origin")==auth.at("origin") &&
            contextual.at("result").at("operationId")=="op-1" &&
            contextual.at("result").at("params").at("sessionId")=="forged-param" &&
            contextual_calls==1,"context is bound to the authorized envelope, not untrusted params");
    invalid=context_request;invalid["token"]="forged";
    require(!json::parse(bridge.handle_json(invalid.dump())).at("ok").get<bool>() &&
            contextual_calls==1,"unauthorized contextual method must not run");
    invalid=request;invalid["token"]="forged";
    require(!json::parse(bridge.handle_json(invalid.dump())).at("ok").get<bool>(),"reject forged grant");
    invalid=request;invalid["origin"]="app://other";
    require(!json::parse(bridge.handle_json(invalid.dump())).at("ok").get<bool>(),"origin binding");
    invalid=request;invalid["method"]="app.unregistered";
    require(!json::parse(bridge.handle_json(invalid.dump())).at("ok").get<bool>(),"registered method allowlist");
    bridge.shutdown_session(auth.at("sessionId").get<std::string>());
    require(!json::parse(bridge.handle_json(request.dump())).at("ok").get<bool>(),"revoked session");
    const auto replacement=json::parse(bridge.handle_json(bootstrap.dump()));
    require(replacement.at("ok").get<bool>() && replacement.at("result").at("sessionId")!=auth.at("sessionId"),"navigation creates new session");
    soui::sdk::json_bridge desktop("app://test");
    require(desktop.enable_clipboard_service() && !desktop.enable_clipboard_service(),
            "public clipboard service must opt in exactly once");
    const auto desktop_boot=json::parse(desktop.handle_json(bootstrap.dump()));
    require(desktop_boot.at("ok").get<bool>() && !desktop.enable_clipboard_service(),
            "desktop registration must freeze after bootstrap");
    const auto desktop_auth=desktop_boot.at("result");
    auto copy=request;
    copy["requestId"]="desktop:1";
    copy["sessionId"]=desktop_auth.at("sessionId");
    copy["origin"]=desktop_auth.at("origin");
    copy["token"]=desktop_auth.at("token");
    copy["method"]="desktop.clipboard.writeText";
    copy["params"]={{"text","native-only gesture"}};
    const auto no_gesture=json::parse(desktop.handle_json(copy.dump(),false));
    require(!no_gesture.at("ok").get<bool>() && no_gesture.at("error").at("code")=="E_GESTURE_REQUIRED",
            "Web JSON cannot claim a native user gesture");
    copy["params"]["userGesture"]=true;
    const auto forged=json::parse(desktop.handle_json(copy.dump(),true));
    require(!forged.at("ok").get<bool>() && forged.at("error").at("code")=="E_INVALID_PARAMS",
            "untrusted userGesture parameter must be rejected");
    copy["params"]={{"text","native-only gesture"}};
    const auto no_owner=json::parse(desktop.handle_json(copy.dump(),true));
    require(!no_owner.at("ok").get<bool>() && no_owner.at("error").at("code")=="E_INVALID_ARGUMENT",
            "native gesture must reach the public service without inventing an owner window");
    soui::sdk::json_bridge directory_bridge("app://test");
    require(directory_bridge.enable_directory_service() && !directory_bridge.enable_directory_service(),
            "public directory service must opt in exactly once");
    const auto directory_boot=json::parse(directory_bridge.handle_json(bootstrap.dump()));
    require(directory_boot.at("ok").get<bool>() && !directory_bridge.enable_directory_service(),
            "directory registration must freeze after bootstrap");
    const auto directory_auth=directory_boot.at("result");
    auto directory_request=request;
    directory_request["requestId"]="directory:1";
    directory_request["sessionId"]=directory_auth.at("sessionId");
    directory_request["origin"]=directory_auth.at("origin");
    directory_request["token"]=directory_auth.at("token");
    directory_request["method"]="desktop.dialog.pickDirectory";
    directory_request["params"]=json::object();
    auto directory_result=json::parse(directory_bridge.handle_json(directory_request.dump(),false));
    require(!directory_result.at("ok").get<bool>() &&
            directory_result.at("error").at("code")=="E_GESTURE_REQUIRED",
            "directory picker requires a native user gesture");
    directory_request["params"]={{"userGesture",true}};
    directory_result=json::parse(directory_bridge.handle_json(directory_request.dump(),true));
    require(!directory_result.at("ok").get<bool>() &&
            directory_result.at("error").at("code")=="E_INVALID_PARAMS",
            "directory picker rejects a forged JSON gesture");
    directory_request["params"]=json::object();
    directory_result=json::parse(directory_bridge.handle_json(directory_request.dump(),true));
    require(!directory_result.at("ok").get<bool>() &&
            directory_result.at("error").at("code")=="E_INVALID_ARGUMENT",
            "directory picker rejects a missing owner before opening native UI");
    soui::sdk::json_bridge window_bridge("app://test");
    require(window_bridge.enable_window_service() && !window_bridge.enable_window_service(),
            "public window service must opt in exactly once");
    const auto window_boot=json::parse(window_bridge.handle_json(bootstrap.dump()));
    require(window_boot.at("ok").get<bool>() && !window_bridge.enable_window_service(),
            "window service registration must freeze after bootstrap");
    const auto window_auth=window_boot.at("result");
    auto window_request=request;
    window_request["requestId"]="window:1";
    window_request["sessionId"]=window_auth.at("sessionId");
    window_request["origin"]=window_auth.at("origin");
    window_request["token"]=window_auth.at("token");
    window_request["method"]="desktop.window.setTitle";
    window_request["params"]={{"title",u8"窗口标题"},{"userGesture",true}};
    auto window_result=json::parse(window_bridge.handle_json(window_request.dump(),true));
    require(!window_result.at("ok").get<bool>() && window_result.at("error").at("code")=="E_INVALID_PARAMS",
            "window title rejects untrusted gesture fields");
    window_request["params"]={{"title",u8"窗口标题"}};
    window_result=json::parse(window_bridge.handle_json(window_request.dump(),false));
    require(!window_result.at("ok").get<bool>() && window_result.at("error").at("code")=="E_INVALID_ARGUMENT",
            "window title reaches the owner check without a gesture requirement");
    window_request["method"]="desktop.window.minimize";
    window_request["params"]=json::object();
    window_result=json::parse(window_bridge.handle_json(window_request.dump(),false));
    require(!window_result.at("ok").get<bool>() && window_result.at("error").at("code")=="E_GESTURE_REQUIRED",
            "window actions require native gesture evidence");
    window_request["params"]={{"userGesture",true}};
    window_result=json::parse(window_bridge.handle_json(window_request.dump(),true));
    require(!window_result.at("ok").get<bool>() && window_result.at("error").at("code")=="E_INVALID_PARAMS",
            "window actions reject a forged JSON gesture");
    window_request["params"]=json::object();
    for(const char* method : {"desktop.window.beginDrag","desktop.window.minimize",
                              "desktop.window.toggleMaximize","desktop.window.close"}){
      window_request["method"]=method;
      window_result=json::parse(window_bridge.handle_json(window_request.dump(),true));
      require(!window_result.at("ok").get<bool>() && window_result.at("error").at("code")=="E_INVALID_ARGUMENT",
              "public window action must reject a missing owner");
    }
    std::cout<<"Native SDK contract checks passed\n";
    return 0;
  } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
