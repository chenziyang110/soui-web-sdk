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
    std::cout<<"Native SDK contract: 13 checks passed\n";
    return 0;
  } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
