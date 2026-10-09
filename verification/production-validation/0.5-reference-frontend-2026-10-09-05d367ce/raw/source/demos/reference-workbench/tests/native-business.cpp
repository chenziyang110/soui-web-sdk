#include "../app/reference-business.h"
#include <json.hpp>
#include <windows.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string error(const json& response){return response.contains("error")?response.at("error").value("code",""):std::string{};}
struct actual_bridge final {
  std::shared_ptr<reference::business> data;
  soui::sdk::json_bridge bridge{"app://reference-workbench"};
  unsigned sequence{};
  explicit actual_bridge(const char* path):data{std::make_shared<reference::business>(reference::read_fixture(path))}{
    require(reference::register_business(bridge,data),"registration failed");
  }
  json call(const json& auth,const char* method,json input=json::object()) {
    return json::parse(bridge.handle_json(json{{"requestId","reference-"+std::to_string(++sequence)},
      {"sessionId",auth.value("sessionId","")},{"origin",auth.value("origin","")},{"token",auth.value("token","")},
      {"method",method},{"params",std::move(input)}}.dump()));
  }
  json bootstrap(){const auto value=call(json::object(),"bridge.bootstrap",{{"protocol","soui.bridge/1"}});
    require(value.value("ok",false),"real bootstrap failed");return value.at("result");}
  std::string start(const json& auth,const char* operation,const char* category="all",unsigned duration=500) {
    const auto value=call(auth,"task.start",{{"kind","reference.summary"},{"operationId",operation},
      {"input",{{"fixtureId",data->data->id},{"category",category},{"durationMs",duration}}}});
    require(value.value("ok",false)&&value.at("result").value("accepted",false),"real start failed");
    return value.at("result").at("taskId").get<std::string>();
  }
  json terminal(const json& auth,const std::string& task) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{8};
    while(std::chrono::steady_clock::now()<deadline) {
      const auto value=call(auth,"task.query",{{"taskId",task}});
      require(value.value("ok",false),"real query failed");const auto result=value.at("result");
      const auto state=result.value("state","");
      if(state=="succeeded"||state=="failed"||state=="cancelled"||state=="partial")return result;
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    throw std::runtime_error("real task terminal deadline exceeded");
  }
};
void fixed_summary(actual_bridge& app) {
  const auto auth=app.bootstrap();
  const auto description=app.call(auth,"reference.fixture.describe");
  require(description.value("ok",false)&&description.at("result").at("rowCount")==10000,"typed native fixture descriptor failed");
  const auto task=app.start(auth,"reference-summary-complete");
  const auto duplicate=app.call(auth,"task.start",{{"kind","reference.summary"},{"operationId","reference-summary-complete"},
    {"input",{{"fixtureId",app.data->data->id},{"category","all"},{"durationMs",500}}}});
  require(duplicate.value("ok",false)&&duplicate.at("result").value("duplicate",false)&&
    duplicate.at("result").at("taskId")==task,"same operation was restarted rather than retained");
  const auto value=app.terminal(auth,task);
  require(value.at("state")=="succeeded"&&value.at("progress")==100,"actual worker did not succeed");
  const auto result=value.at("result");
  require(result.at("fixtureId")==app.data->data->id&&result.at("processed")==10000&&result.at("matched")==10000,
    "actual fixed-fixture coverage differs");
  require(result.at("totalSizeKb").get<std::uint64_t>()>=160000&&result.at("totalSizeKb").get<std::uint64_t>()<=41110000,
    "summary is outside real input bounds");
  std::uint32_t count=0;
  require(result.at("groups").size()==5,"group projection count differs");
  for(std::size_t index=0;index<5;++index) {
    require(result.at("groups").at(index).at("group")==app.data->data->groups[index],"group projection identity differs");
    count+=result.at("groups").at(index).at("count").get<std::uint32_t>();
  }
  require(count==10000,"actual group counts lost rows");
  const auto by_operation=app.call(auth,"task.query",{{"operationId","reference-summary-complete"}});
  require(by_operation.value("ok",false)&&by_operation.at("result").at("taskId")==task,"public operation lookup lost actual identity");
}
void cancel(actual_bridge& app) {
  const auto auth=app.bootstrap();
  const auto task=app.start(auth,"reference-cancel","all",60000);
  const auto receipt=app.call(auth,"task.cancel",{{"taskId",task}});
  require(receipt.value("ok",false)&&receipt.at("result").value("cancelRequested",false),"native cancel request not accepted");
  const auto result=app.terminal(auth,task);
  require(result.at("state")=="cancelled"&&result.at("result").is_object()&&result.at("result").empty(),
    "request was mistaken for actual cancellation");
}
void invalid_form(actual_bridge& app) {
  const auto auth=app.bootstrap();
  const auto accepted=app.call(auth,"reference.form.submit",{{"name","中文业务"},{"notes","有界草稿"},{"category","素材"}});
  require(accepted.value("ok",false)&&accepted.at("result").value("accepted",false)&&
    accepted.at("result").at("receiptId")=="reference-form-1","typed actual form failed");
  const auto blank=app.call(auth,"reference.form.submit",{{"name"," "},{"notes",""},{"category","素材"}});
  require(blank.value("ok",false)&&!blank.at("result").value("accepted",true),"native form policy accepted blank name");
  const auto forged=app.call(auth,"reference.form.submit",{{"name","中文业务"},{"notes",""},{"category","素材"},{"userGesture",true}});
  require(!forged.value("ok",true)&&error(forged)=="E_INVALID_PARAMS","typed action accepted extra authority field");
  const auto invalid=app.start(auth,"reference-invalid-duration","all",0);
  require(app.terminal(auth,invalid).at("state")=="failed","real worker accepted invalid duration");
}
void revoked_document(actual_bridge& app) {
  const auto old=app.bootstrap();
  const auto task=app.start(old,"old-document-task","all",60000);
  app.bridge.shutdown_session(old.at("sessionId").get<std::string>());
  const auto revoked=app.call(old,"task.query",{{"taskId",task}});
  require(!revoked.value("ok",true)&&error(revoked)=="E_UNAUTHORIZED","revoked document kept task authorization");
  const auto current=app.bootstrap();
  for(const auto& selector:{json{{"taskId",task}},json{{"operationId","old-document-task"}}}) {
    const auto unknown=app.call(current,"task.query",selector);
    require(!unknown.value("ok",true)&&error(unknown)=="E_TASK_NOT_FOUND","new document recovered old task identity");
  }
  // No restart or fabricated terminal; actual Bridge destructor joins its workers.
}
std::string creation_time() {
  FILETIME created{},exited{},kernel{},user{};
  if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))throw std::runtime_error("process identity unavailable");
  ULARGE_INTEGER value{};value.LowPart=created.dwLowDateTime;value.HighPart=created.dwHighDateTime;return std::to_string(value.QuadPart);
}
}
int main(int argc,char** argv) {
  try {
    require(argc==3,"exact case and fixture path required");const std::string name=argv[1];
    {actual_bridge app{argv[2]};
      if(name=="fixed-summary")fixed_summary(app);else if(name=="cancel")cancel(app);
      else if(name=="invalid-form")invalid_form(app);else if(name=="revoked-document")revoked_document(app);
      else throw std::runtime_error("unknown case");
    } // Actual SDK service joins before the success receipt.
    std::cout<<json{{"schema","soui.reference-native/1"},{"case",name},{"passed",true},
      {"pid",std::to_string(GetCurrentProcessId())},{"creationFileTime",creation_time()},
      {"qualificationPassed",false},{"releaseReady",false}}.dump()<<'\n';return 0;
  }catch(const std::exception& failure){std::cerr<<"REFERENCE-FAILURE "<<failure.what()<<'\n';return 1;}
}
