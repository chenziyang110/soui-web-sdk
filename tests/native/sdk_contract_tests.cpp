#include <soui/json_bridge.h>
#include <json.hpp>
#include <stdexcept>
#include <iostream>
using json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
  try {
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
