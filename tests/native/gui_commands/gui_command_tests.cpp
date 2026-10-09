#include "../json_bridge_endpoint/public_endpoint_client.h"
#include <soui/json_bridge.h>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using namespace public_endpoint_tests;
using namespace soui::runtime;
using namespace soui::runtime::agent;
using clock_type = std::chrono::steady_clock;
json object(json properties=json::object(),json required=json::array()) {
  return {{"type","object"},{"properties",std::move(properties)},
      {"required",std::move(required)},{"additionalProperties",false}};
}
const json number{{"type","integer"},{"minimum",1},{"maximum",10}};
const json input_schema=object({{"value",number}},{"value"});
const json output_schema=object({{"safe",number}},{"safe"});
std::string contract() {
  return json{{"protocol","soui.action/1"},{"title","Native GUI effect"},{"description","Fixed native policy."},
      {"inputSchema",input_schema},{"outputSchema",output_schema}}.dump();
}
gui_command_definition row(std::string family="gui.reference",std::string effect="work.apply") {
  return {std::move(family),"reference.table","table",gui_operation::perform,"apply",
      "gui.start","gui.query","gui.cancel",input_schema.dump(),output_schema.dump(),std::move(effect),
      [](const task_outcome& result) {
        const auto raw=json::parse(result.result_json);
        return json{{"safe",raw.at("safe")}}.dump(); // Explicit whitelist, never the raw snapshot.
      }};
}
json bootstrap(soui::sdk::json_bridge& bridge) {
  const auto response=json::parse(bridge.handle_json(R"({"requestId":"bootstrap","sessionId":"","origin":"","token":"","method":"bridge.bootstrap","params":{"protocol":"soui.bridge/1"}})"));
  require(response.at("ok")==true,"Bootstrap failed"); return response.at("result");
}
json page(soui::sdk::json_bridge& bridge,const json& credentials,const char* method,json params) {
  return json::parse(bridge.handle_json(json{{"requestId","page"},{"sessionId",credentials.at("sessionId")},
      {"origin",credentials.at("origin")},{"token",credentials.at("token")},{"method",method},{"params",std::move(params)}}.dump()));
}
json remote(client& pipe,const std::string& method,json params,std::uint32_t timeout=1000) {
  require(pipe.write_raw(json{{"kind","request"},{"schemaVersion","soui.bridge/1"},{"requestId","gui-test"},
      {"method",method},{"params",std::move(params)},{"timeoutMs",timeout}}.dump()),"Client request failed");
  const auto raw=pipe.read_raw(); require(!raw.empty(),"Missing response"); const auto response=json::parse(raw);
  require(response.at("requestId")=="gui-test" && !response.contains("token") && !response.contains("sessionId") &&
      !response.contains("origin") && !response.contains("subject"),"Local credentials escaped"); return response;
}
void denied(const json& response,const char* code="E_UNAUTHORIZED") {
  require(response.at("ok")==false && response.at("error").at("code")==code && !response.contains("result"),"Denial changed");
}
std::string accepted(const json& response) {
  require(response.at("ok")==true && response.at("result").at("accepted")==true,"Task admission failed");
  return response.at("result").at("taskId").get<std::string>();
}
bool terminal(const json& response) {
  if (response.at("ok")!=true) return false;
  const auto state=response.at("result").at("state").get<std::string>();
  return state=="succeeded" || state=="failed" || state=="cancelled" || state=="partial";
}
std::uint64_t creation(HANDLE process) {
  FILETIME created{},exited{},kernel{},user{};
  require(::GetProcessTimes(process,&created,&exited,&kernel,&user)!=FALSE,"Process FILETIME unavailable");
  return (static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}

struct environment final {
  // Native callback/task storage precedes Bridge and outlives its actual join.
  const DWORD owner=::GetCurrentThreadId();
  const std::string name=component();
  const local_endpoint_scope key{51,52,53,54};
  std::atomic<unsigned> child_runs{0};
  unsigned effects{0},events{0},authorizations{0};
  bool current{true},effect_permission{true};
  DWORD selected_pid=::GetCurrentProcessId();
  std::uint64_t selected_creation=creation(::GetCurrentProcess());
  std::shared_ptr<const authorization_scope> effect_scope;
  std::vector<json> commands;
  std::function<void(const json&)> on_command;
  std::function<void()> on_effect;
  soui::sdk::json_bridge bridge{"app://gui-page"};
  windows_local_endpoint* endpoint{nullptr};
  json credentials;

  explicit environment(bool child_effect=false,bool bind_now=true) {
    require(bridge.register_action("work.apply","work.effect",contract(),[this](const bridge_request& request) {
      require(::GetCurrentThreadId()==owner && request.authorization && request.authorization->active() &&
          request.authorization->allows_local_method("work.apply","work.effect") &&
          request.session_id=="gui-native" && request.origin=="app://gui-native" &&
          !request.native_user_gesture,"Effect lost actual Agent scope/owner or acquired gesture");
      effect_scope=request.authorization; ++effects; if(on_effect)on_effect();
      return json{{"safe",json::parse(request.params_json).at("value")}}.dump();
    }),"Effect registration failed");
    require(bridge.allow_local_action("work.apply"),"Effect opt-in failed");
    require(bridge.register_task_handler("work-child",[this](const task_invocation& request,const task_control&) {
      require(request.session_id=="gui-native" && request.origin=="app://gui-native","Child used page scope");
      child_runs.fetch_add(1,std::memory_order_release);
      return task_outcome{result_kind::success,json{{"safe",json::parse(request.parameters_json).at("value")},
          {"secret","private-child-secret"}}.dump(),"Private message"};
    }),"Child handler registration failed");
    require(bridge.register_local_task_family({"work.child","work-child","child.start","child.query","child.cancel",
      input_schema.dump(),output_schema.dump(),[](const task_outcome& result) {
        return json{{"safe",json::parse(result.result_json).at("safe")}}.dump();
      }}),"Child family registration failed");
    auto snapshot=row("gui.snapshot",""); snapshot.operation=gui_operation::snapshot;
    snapshot.action.clear(); snapshot.input_schema_json=object().dump();
    require(bridge.enable_gui_commands({row("gui.reference",child_effect?"work.child.start":"work.apply"),
        std::move(snapshot)}),"GUI table registration failed");
    require(bridge.set_session_capabilities({"bridge.describe","bridge.probe","gui.bind","gui.effect","gui.ack",
        "task.start","task.query","task.cancel"}),"Page policy failed");
    endpoint=bridge.create_local_endpoint(); require(endpoint && endpoint->state()==local_endpoint_state::stopped,"Factory not stopped");
    bridge.set_event_sink([this](std::string_view raw) {
      require(::GetCurrentThreadId()==owner,"GUI callback was not owner");
      const auto event=json::parse(raw); require(event.at("eventName")=="gui.command" && !event.contains("taskId"),"Invalid GUI event");
      require(event.at("sessionId")==credentials.at("sessionId") && raw.size()<=65536,"GUI event lost document/bound");
      ++events; commands.push_back(event.at("payload")); if(on_command)on_command(commands.back());
    });
    credentials=bootstrap(bridge);
    require(page(bridge,credentials,"gui.bind",json::object()).at("ok")==true,"Page route bind failed");
    if(bind_now)bind();
  }
  ~environment() {
    bridge.close_gui_commands();
    if(bridge.poll_gui_commands()!=gui_owner_result::closed)native_pending_failure();
    endpoint->request_stop(); const auto until=::GetTickCount64()+5000;
    while(endpoint->poll_stop()!=local_endpoint_stop::stopped) {
      if(::GetTickCount64()>=until)native_pending_failure(); ::Sleep(1);
    }
    // Destruction then performs the actual two-worker task_service join.
  }
  void bind(std::function<bool()> proof={}) {
    if(!proof)proof=[this] { require(::GetCurrentThreadId()==owner,"Proof was not owner"); return current; };
    require(bridge.bind_gui_document("native-generation-1",std::move(proof)),"Trusted owner bind failed");
  }
  void start() {
    const auto error=endpoint->start({name,4000,4000},[this](const windows_peer_identity& peer)->std::optional<local_endpoint_policy> {
      require(::GetCurrentThreadId()==owner && peer.process_id()==selected_pid &&
          peer.creation_filetime()==selected_creation && peer.process_state()==held_process_state::alive,
          "Authorizer lost held native subject identity"); ++authorizations;
      std::vector<std::string> caps{"bridge.describe","gui.start","gui.query","gui.cancel","child.start","child.query","child.cancel"};
      if(effect_permission)caps.push_back("work.effect");
      return local_endpoint_policy{key,"gui-native","app://gui-native",std::move(caps),
        {"bridge.describe","gui.reference.start","gui.reference.query","gui.reference.cancel",
         "gui.snapshot.start","gui.snapshot.query","gui.snapshot.cancel","work.apply",
         "work.child.start","work.child.query","work.child.cancel","gui.bind","task.start"}};
    },[this](const local_endpoint_scope& scope) { return current && scope.application==key.application &&
        scope.window==key.window && scope.document==key.document && scope.generation==key.generation; });
    require(error==local_endpoint_error::none,"Endpoint start failed");
  }
  json effect(const json& command,json input={{"value",2}}) {
    return page(bridge,credentials,"gui.effect",{{"requestId",command.at("requestId")},{"documentId",command.at("documentId")},
        {"effectId","native"},{"inputJson",input.dump()}});
  }
  json ack(const json& command,json result={{"safe",2}},bool ok=true) {
    return page(bridge,credentials,"gui.ack",{{"requestId",command.at("requestId")},{"documentId",command.at("documentId")},
        {"ok",ok},{"resultJson",result.dump()}});
  }
  void respond(const json& command) {
    if(command.at("effectId")=="native")require(effect(command).at("ok")==true,"Actual effect rejected");
    require(ack(command).at("ok")==true,"Actual ACK rejected");
  }
  void tick(bool gui=true) { (void)endpoint->pump_owner(); if(gui)(void)bridge.poll_gui_commands(); }
  void connect(const std::function<void(client&)>& work,const std::function<void()>& tick_override={}) {
    std::atomic<bool> done{false}; std::exception_ptr failure;
    std::thread remote_thread([&] { try { client pipe(name); pipe.hello(); work(pipe); }
      catch(...) { failure=std::current_exception(); } done.store(true,std::memory_order_release); });
    const auto until=::GetTickCount64()+10000;
    while(!done.load(std::memory_order_acquire)) {
      if(::GetTickCount64()>=until)native_pending_failure();
      try { if(tick_override)tick_override(); else tick(); } catch(...) { native_pending_failure(); }
      ::Sleep(1);
    }
    remote_thread.join(); if(failure)std::rethrow_exception(failure);
  }
};
json query_until(client& pipe,const std::string& family,const std::string& id) {
  const auto until=::GetTickCount64()+5000; json result;
  do { result=remote(pipe,family+".query",{{"taskId",id}}); require(result.at("ok")==true,"Actual scope query failed");
    if(terminal(result))return result; require(::GetTickCount64()<until,"Actual task never completed"); ::Sleep(1);
  } while(true);
}
std::string begin(client& pipe,const std::string& operation,const std::string& family="gui.reference",unsigned timeout=1000) {
  return accepted(remote(pipe,family+".start",{{"operationId",operation},
      {"input",family=="gui.snapshot"?json::object():json{{"value",2}}}},timeout));
}

void registration() {
  soui::sdk::json_bridge bridge{"app://registration"};
  require(!bridge.enable_gui_commands({}),"Empty table accepted");
  auto invalid=row("gui.invalid",""); invalid.project_result={};
  require(!bridge.enable_gui_commands({row("gui.partial",""),invalid}),"Invalid table accepted");
  auto mutation=row("gui.not-readonly", "work.apply"); mutation.operation=gui_operation::snapshot; mutation.action.clear();
  require(!bridge.enable_gui_commands({mutation}),"Readonly row acquired an effect");
  require(bridge.register_task_handler("soui.gui.gui.blocked",[](const auto&,const auto&) { return task_outcome{}; }),"Collision fixture registration failed");
  require(!bridge.enable_gui_commands({row("gui.partial",""),row("gui.blocked","")}),"Partial family transaction accepted");
  require(bridge.enable_gui_commands({row("gui.partial","")}),"Failed transaction left table/family entries");
  require(!bridge.enable_gui_commands({row("gui.second","")}),"Second coordinator accepted");
  require(!bridge.set_session_capabilities({"gui.start"}),"GUI local grant entered page policy");
  const auto credentials=bootstrap(bridge);
  denied(page(bridge,credentials,"gui.partial.start",{{"operationId","page"},{"input",{{"value",2}}}}));
  denied(page(bridge,credentials,"task.start",{{"kind","soui.gui.gui.partial"},{"operationId","public"},{"input",{{"value",2}}}}),"E_UNAUTHORIZED");
  require(!bridge.enable_gui_commands({row("late","")}) && bridge.poll_gui_commands()==gui_owner_result::idle,"Bootstrap did not freeze registration");
}
void scope_matrix() {
  environment f; f.effect_permission=false; f.start();
  denied(page(f.bridge,f.credentials,"gui.reference.start",{{"operationId","page"},{"input",{{"value",2}}}}));
  f.connect([&](client& pipe) {
    denied(remote(pipe,"gui.bind",json::object())); denied(remote(pipe,"task.start",json::object()));
    denied(remote(pipe,"gui.reference.start",{{"operationId","no-effect-grant"},{"input",{{"value",2}}}}),"E_ACTION_REJECTED");
  });
  require(f.effects==0 && f.events==0,"Page grant/missing effect permission reached native effect");
}
void one_use_effect() {
  environment f; f.start(); f.on_command=[&](const json& command) {
    json foreign;
    std::thread wrong([&] { foreign=f.effect(command); }); wrong.join();
    denied(foreign,"E_GUI_WRONG_OWNER");
    denied(f.effect(command,{{"value",3}}),"E_GUI_EFFECT_REJECTED");
    require(f.effect(command).at("ok")==true,"Correct once-only effect rejected");
    denied(f.effect(command),"E_GUI_EFFECT_USED"); require(f.ack(command).at("ok")==true,"ACK failed");
    denied(f.ack(command),"E_GUI_ACK_REJECTED");
  };
  f.connect([&](client& pipe) {
    const auto id=begin(pipe,"once"); const auto result=query_until(pipe,"gui.reference",id).at("result");
    require(result.at("state")=="succeeded" && result.at("result").at("effectState")=="committed" &&
        json::parse(result.at("result").at("effectResultJson").get<std::string>())==json{{"safe",2}},"Actual effect receipt lost");
    const auto duplicate=remote(pipe,"gui.reference.start",{{"operationId","once"},{"input",{{"value",2}}}});
    require(accepted(duplicate)==id && duplicate.at("result").at("duplicate")==true,"Duplicate created another GUI context/task");
    denied(remote(pipe,"gui.reference.start",{{"operationId","once"},{"input",{{"value",3}}}}),"E_ACTION_REJECTED");
  });
  const auto until=::GetTickCount64()+4000;
  while(f.effect_scope && f.effect_scope->active()) {
    require(::GetTickCount64()<until,"Actual disconnected scope cleanup did not complete"); f.tick(); ::Sleep(1);
  }
  require(f.effects==1 && f.events==1 && f.effect_scope && !f.effect_scope->active(),"Once-only/scope cleanup failed");
}
void actual_child() {
  environment f(true); f.start();
  f.on_command=[&](const json& command) {
    const auto response=f.effect(command); require(response.at("ok")==true,"Child admission rejected");
    const auto receipt=json::parse(response.at("result").at("resultJson").get<std::string>());
    require(receipt.at("taskId").is_string() && receipt.at("accepted")==true,"Child was not admitted");
    require(f.ack(command,{{"safe",2},{"taskId","forged-child"},{"secret","must not escape"}}).at("ok")==true,"ACK failed");
  };
  f.connect([&](client& pipe) {
    const auto id=begin(pipe,"child"); const auto parent=query_until(pipe,"gui.reference",id).at("result").at("result");
    const auto child_id=json::parse(parent.at("effectResultJson").get<std::string>()).at("taskId").get<std::string>();
    require(child_id!="forged-child" &&
        json::parse(parent.at("guiResultJson").get<std::string>())==json{{"safe",2}},"ACK forged receipt or leaked secret");
    const auto child=query_until(pipe,"work.child",child_id);
    require(child.at("result").at("state")=="succeeded" && child.at("result").at("result")==json{{"safe",2}},"Actual same-service child failed");
    denied(page(f.bridge,f.credentials,"task.query",{{"taskId",child_id}}),"E_TASK_NOT_FOUND");
  }); require(f.child_runs.load()==1,"Child handler did not execute exactly once");
}
void owner_reentrancy() {
  environment f(false,false); bool entered=false; gui_owner_result nested=gui_owner_result::idle;
  f.bind([&] { entered=true; nested=f.bridge.poll_gui_commands(); return true; });
  require(entered && nested==gui_owner_result::busy,"Binding callback was not a protected owner borrow");
  std::thread wrong([&] { nested=f.bridge.poll_gui_commands(); }); wrong.join();
  require(nested==gui_owner_result::wrong_owner,"Foreign thread executed owner callback");
  f.start(); f.on_command=[&](const json&) {
    f.bridge.close_gui_commands(); require(f.bridge.poll_gui_commands()==gui_owner_result::pending,"Close ignored actual owner callback borrow");
  };
  f.connect([&](client& pipe) { const auto id=begin(pipe,"reentrant"); const auto result=query_until(pipe,"gui.reference",id);
    require(result.at("result").at("state")!="succeeded","Closed callback published success"); });
  require(f.bridge.poll_gui_commands()==gui_owner_result::closed && f.effects==0,"Owner close did not become inert");
  environment g; g.start(); g.on_effect=[&] {
    g.bridge.close_gui_commands();
    require(g.bridge.poll_gui_commands()==gui_owner_result::pending,"Actual native effect borrow was not retained during close");
  };
  g.on_command=[&](const json& command) { denied(g.effect(command),"E_GUI_EFFECT_REJECTED"); };
  g.connect([&](client& pipe) { const auto id=begin(pipe,"effect-close"); const auto result=query_until(pipe,"gui.reference",id).at("result");
    require(result.at("state")=="failed" && result.at("result").at("effectState")=="unknown", "Close during actual effect claimed rollback/completion"); });
  require(g.effects==1 && g.bridge.poll_gui_commands()==gui_owner_result::closed,"Actual effect borrow never closed");
  environment stale(false,false); bool proof_current=true;
  stale.bind([&] { require(::GetCurrentThreadId()==stale.owner,"Effect proof was not owner"); return proof_current; });
  stale.start(); stale.on_command=[&](const json& command) {
    proof_current=false; // Actual dispatch already passed; scopes remain active.
    denied(stale.effect(command),"E_GUI_EFFECT_REJECTED");
  };
  stale.connect([&](client& pipe) { const auto id=begin(pipe,"proof-stale");
    const auto result=query_until(pipe,"gui.reference",id).at("result");
    require(result.at("state")=="failed" && result.at("result").at("effectState")=="unused",
        "Stale proof consumed an effect or published success"); });
  require(stale.events==1 && stale.effects==0 && stale.bridge.poll_gui_commands()==gui_owner_result::closed,
      "Dispatch-time proof was reused for an actual effect");
  environment proof_close(false,false); bool close_in_proof=false; unsigned effect_proofs=0;
  proof_close.bind([&] {
    require(::GetCurrentThreadId()==proof_close.owner,"Closing proof was not owner");
    if(close_in_proof) { ++effect_proofs; proof_close.bridge.close_gui_commands();
      require(proof_close.bridge.poll_gui_commands()==gui_owner_result::pending,
          "Close inside effect proof lost the owner borrow"); }
    return true;
  });
  proof_close.start(); proof_close.on_command=[&](const json& command) {
    close_in_proof=true; denied(proof_close.effect(command),"E_GUI_EFFECT_REJECTED");
  };
  proof_close.connect([&](client& pipe) { const auto id=begin(pipe,"proof-close");
    require(query_until(pipe,"gui.reference",id).at("result").at("state")=="failed",
        "Close inside proof published success"); });
  require(effect_proofs==1 && proof_close.events==1 && proof_close.effects==0 &&
      proof_close.bridge.poll_gui_commands()==gui_owner_result::closed,"Effect proof close did not become inert");
}
void revoke_prebind() {
  environment f(false,false); const auto old=f.credentials;
  std::thread shutdown([&] { f.bridge.shutdown_session(old.at("sessionId").get<std::string>()); }); shutdown.join();
  f.credentials=bootstrap(f.bridge); require(page(f.bridge,f.credentials,"gui.bind",json::object()).at("ok")==true,"Prebind cleanup prevented new page route");
  f.bind(); f.start(); std::string old_id; f.on_command=[&](const json& command) { f.respond(command); };
  f.connect([&](client& pipe) { old_id=begin(pipe,"old-scope"); (void)query_until(pipe,"gui.reference",old_id); });
  f.connect([&](client& pipe) { denied(remote(pipe,"gui.reference.query",{{"taskId",old_id}}),"E_ACTION_REJECTED"); });
  std::thread bound_shutdown([&] { f.bridge.shutdown_session(f.credentials.at("sessionId").get<std::string>()); }); bound_shutdown.join();
  require(f.bridge.poll_gui_commands()==gui_owner_result::closed,"Bound document revocation did not close coordinator");
  f.credentials=bootstrap(f.bridge); denied(page(f.bridge,f.credentials,"gui.bind",json::object()),"E_GUI_NOT_READY");
  require(!f.bridge.bind_gui_document("native-generation-2",[] { return true; }),"Revoked binding reopened");
}
void timeout_consumption() {
  environment f; f.start(); f.on_command=[&](const json& command) {
    require(f.effect(command).at("ok")==true,"Actual effect failed before timeout"); // Deliberately no ACK.
  };
  f.connect([&](client& pipe) {
    const auto id=begin(pipe,"timeout","gui.reference",300); const auto result=query_until(pipe,"gui.reference",id).at("result");
    require(result.at("state")=="failed" && result.at("result").at("confirmed")==false &&
        result.at("result").at("effectState")=="committed","Timeout fabricated rollback or completion");
  });
  require(f.effects==1 && f.commands.size()==1,"Timeout replayed effect");
  denied(f.effect(f.commands.front()),"E_GUI_EFFECT_REJECTED"); denied(f.ack(f.commands.front()),"E_GUI_ACK_REJECTED");
}
void capacity_secret() {
  environment f; f.start(); std::atomic<bool> process{false}; std::vector<std::string> ids;
  f.on_command=[&](const json& command) { require(f.ack(command,{{"safe",2},{"secret","hidden-state"}}).at("ok")==true,"Projected ACK failed"); };
  f.connect([&](client& pipe) {
    for(unsigned i=0;i<16;++i)ids.push_back(begin(pipe,"capacity:"+std::to_string(i),"gui.snapshot",5000));
    denied(remote(pipe,"gui.snapshot.start",{{"operationId","overflow"},{"input",json::object()}},5000),"E_ACTION_REJECTED");
    for(const auto& id:ids)require(remote(pipe,"gui.snapshot.cancel",{{"taskId",id}}).at("ok")==true,"Cancel failed");
    for(const auto& id:ids)(void)query_until(pipe,"gui.snapshot",id);
    process.store(true,std::memory_order_release);
    const auto id=begin(pipe,"after-release","gui.snapshot"); const auto result=query_until(pipe,"gui.snapshot",id);
    require(result.dump().find("hidden-state")==std::string::npos &&
        json::parse(result.at("result").at("result").at("guiResultJson").get<std::string>())==json{{"safe",2}},
        "Native projection leaked secret/lost safe value");
  },[&] { f.tick(process.load(std::memory_order_acquire)); });
}
void two_parent_pool() {
  environment f(true); f.start(); std::atomic<bool> process{false}; std::vector<std::string> children; std::mutex child_mutex;
  f.on_command=[&](const json& command) {
    const auto response=f.effect(command); require(response.at("ok")==true,"Child admission failed");
    { std::lock_guard<std::mutex> lock(child_mutex);
      children.push_back(json::parse(response.at("result").at("resultJson").get<std::string>()).at("taskId").get<std::string>()); }
    require(f.ack(command).at("ok")==true,"Parent admission ACK failed");
  };
  f.connect([&](client& pipe) {
    const auto first=begin(pipe,"pool:1", "gui.reference",5000),second=begin(pipe,"pool:2","gui.reference",5000);
    const auto until=::GetTickCount64()+4000;
    while(remote(pipe,"gui.reference.query",{{"taskId",first}}).at("result").at("state")!="running" ||
        remote(pipe,"gui.reference.query",{{"taskId",second}}).at("result").at("state")!="running")
      require(::GetTickCount64()<until,"Two actual GUI parents never occupied both workers");
    process.store(true,std::memory_order_release);
    require(query_until(pipe,"gui.reference",first).at("result").at("state")=="succeeded" &&
        query_until(pipe,"gui.reference",second).at("result").at("state")=="succeeded","Parents failed to release workers after admission");
    std::vector<std::string> observed;
    { std::lock_guard<std::mutex> lock(child_mutex); observed=children; }
    require(observed.size()==2 && observed[0]!=observed[1],"Child admission identities are not actual unique tasks");
    for(const auto& id:observed)require(query_until(pipe,"work.child",id).at("result").at("state")=="succeeded","Queued child never ran");
  },[&] { f.tick(process.load(std::memory_order_acquire)); });
  require(f.child_runs.load()==2,"Shared two-worker service did not actually finish both children");
}
int external_child(const std::string& name) {
  client pipe(name); pipe.hello(); const auto id=begin(pipe,"external");
  require(query_until(pipe,"gui.reference",id).at("result").at("state")=="succeeded","External parent task failed"); return 0;
}
void external_peer() {
  environment f; wchar_t executable[32768]{};
  const auto count=::GetModuleFileNameW(nullptr,executable,32768); require(count && count<32768,"Executable path unavailable");
  std::wstring command=L"\""+std::wstring(executable)+L"\" --peer "+std::wstring(f.name.begin(),f.name.end());
  STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
  require(::CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&startup,&process)!=FALSE,"Actual child creation failed");
  handle held(process.hProcess),thread(process.hThread); f.selected_pid=process.dwProcessId; f.selected_creation=creation(held.get());
  try {
    f.start(); f.on_command=[&](const json& packet) { f.respond(packet); };
    require(::ResumeThread(thread.get())!=static_cast<DWORD>(-1),"Child resume failed");
    const auto until=::GetTickCount64()+9000;
    while(::WaitForSingleObject(held.get(),0)==WAIT_TIMEOUT) {
      if(::GetTickCount64()>=until)native_pending_failure(); f.tick(); ::Sleep(1);
    }
    DWORD exit=0; require(::GetExitCodeProcess(held.get(),&exit)!=FALSE && exit==0,"Actual external child failed");
    require(f.authorizations==1 && f.effects==1 && f.events==1,"External native peer did not exercise actual GUI effect");
    std::cout << json{{"case","external-peer"},{"rootPid",::GetCurrentProcessId()},
        {"rootCreationFileTime",std::to_string(creation(::GetCurrentProcess()))},{"childPid",f.selected_pid},
        {"childCreationFileTime",std::to_string(f.selected_creation)},{"childExitCode",exit},{"protocolFixtureOnly",true}}.dump()<<'\n';
  } catch(...) { native_pending_failure(); } // Do not release a live/suspended child on failure.
}
struct case_entry { const char* name; void (*run)(); };
const case_entry cases[]={{"registration",registration},{"scope-matrix",scope_matrix},{"one-use-effect",one_use_effect},
    {"actual-child",actual_child},{"owner-reentrancy",owner_reentrancy},{"revoke-prebind",revoke_prebind},
    {"timeout-consumption",timeout_consumption},{"capacity-secret",capacity_secret},{"two-parent-pool",two_parent_pool},{"external-peer",external_peer}};
} // namespace
int main(int argc,char** argv) {
  try {
    if(argc==3 && std::string(argv[1])=="--peer")return external_child(argv[2]);
    require(argc==2,"Expected one exact case");
    for(const auto& item:cases)if(item.name==std::string(argv[1])) { item.run();
      std::cout<<json{{"case",item.name},{"rootPid",::GetCurrentProcessId()},
          {"rootCreationFileTime",std::to_string(creation(::GetCurrentProcess()))},{"protocolFixtureOnly",true},{"passed",true}}.dump()<<'\n'; return 0; }
  } catch(...) { std::cerr<<"E_GUI_COMMAND_TEST\n"; return 1; }
  return 2;
}
