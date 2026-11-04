#include "profile_options.h"
#include "trace_capture.h"
#include "manual_report.h"
#include <soui/json_bridge.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

namespace trace=soui::sdk::diagnostics::input_frame_trace;
namespace bench=soui::benchmark;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Call> void rejects(Call&& call,const char* message) {
  bool rejected=false;try{call();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
std::string read(const std::filesystem::path& file) {
  std::ifstream input(file,std::ios::binary);check(input.good(),"Expected persisted evidence missing");
  return std::string(std::istreambuf_iterator<char>(input),{});
}
std::filesystem::path directory(const std::filesystem::path& root,const wchar_t* name) {
  const auto result=root/name;check(std::filesystem::create_directory(result),"Evidence directory already exists");return result;
}
// Fault injection is confined to the trace_source contract. Success and exact
// exporter-byte checks below use two real public recorders, not a mock exporter.
class fault_source final:public trace::trace_source {
 public:
  trace::scalar_snapshot value;
  bool mutate_on_drain{false};
  trace::scalar_snapshot snapshot()const noexcept override{return value;}
  trace::drain_result drain(trace::event*,std::size_t)noexcept override {
    if(mutate_on_drain)value.clock_invalid=true;return {};
  }
  void note_causal_loss(std::uint64_t amount)noexcept override{value.causal_lost+=amount;}
};
void options_contracts() {
  bench::profile_options normal;unsigned calls=0;
  normal.validate(false);normal.cursor_setup([&]{++calls;});check(calls==1,"Default cursor setup changed");
  check(normal.input_json().find("\"deterministic_replay\":true")!=std::string::npos,"Default input marker changed");
  for(const auto* budget:{L"0",L"1000",L"2000"}) {
    bench::profile_options manual;check(manual.consume(L"--manual-input"),"Manual option rejected");
    check(manual.consume(std::wstring(L"--engine-budget=")+budget),"Supported budget rejected");
    manual.validate(false);manual.cursor_setup([&]{++calls;});check(calls==1,"Manual mode executed cursor setup");
    check(manual.input_json().find("\"deterministic_replay\":false")!=std::string::npos,"Manual mode claims replay");
    rejects([&]{manual.validate(true);},"Manual mode accepted foreground control");
    rejects([&]{manual.consume(L"--manual-input");},"Duplicate manual switch accepted");
    rejects([&]{manual.consume(L"--engine-budget=0");},"Duplicate budget accepted");
  }
  bench::profile_options isolated;isolated.consume(L"--engine-budget=0");
  rejects([&]{isolated.validate(false);},"Budget accepted without manual mode");
  for(const auto* value:{L"",L"00",L"-1",L"1",L"1001",L"2000x",L" 1000"}) {
    bench::profile_options invalid;rejects([&]{invalid.consume(std::wstring(L"--engine-budget=")+value);},"Invalid budget accepted");
  }
  bench::profile_options unknown;check(!unknown.consume(L"--manual-input=true"),"Unknown option consumed");
}
void scalar_contracts(const std::filesystem::path& root) {
  const auto identity=trace::current_identity(0x13579bdfULL,0x2468ace0ULL);
  check(identity.process_id&&identity.process_incarnation&&identity.qpc_frequency,"Real process identity unavailable");
  trace::recorder sdk,kernel;check(sdk.configure(identity,trace::mode::scalar)&&kernel.configure(identity,trace::mode::scalar),"Recorder configure failed");
  const auto initial=bench::capture_scalar_boundary(identity,sdk,&kernel);
  const auto good=directory(root,L"real-recorders");bench::write_scalar_initial(good,initial,"{}");
  {
    auto writer=sdk.attach_current_thread();auto kernel_writer=kernel.attach_current_thread();
    check(static_cast<bool>(writer)&&static_cast<bool>(kernel_writer),"Real writer registration failed");
    trace::event event;event.point=trace::stage::wake_requested;event.qpc=trace::now_qpc();
    check(writer.record(event)==trace::record_result::scalar_only,"Scalar event unexpectedly became detailed");
    writer.add(trace::counter::copy_bytes,9007199254740993ULL);
    writer.observe_duration(trace::duration_metric::input_queue,1ULL<<63);
    kernel_writer.add(trace::counter::copy_operations,3);
  }
  sdk.stop();kernel.stop();
  const auto result=bench::finish_scalar_capture(good,initial,sdk,&kernel,std::chrono::seconds(2));
  check(result.ok&&result.io_complete&&result.usable,"Healthy real scalar sources rejected");
  std::ostringstream exported;trace::exporter expected(exported,sdk);
  check(expected.begin()&&expected.drain(1).count==0&&expected.finish(),"Independent public exporter failed");
  check(read(good/L"scalar-sdk-final.ndjson")==exported.str(),"Standard exporter bytes were rewritten");
  const auto status=read(good/L"scalar-final-status.json");
  check(status.find("\"9007199254740993\"")!=std::string::npos,"64-bit value rounded or narrowed");
  check(status.find("\"physical_latency_measured\":false")!=std::string::npos,"Scalar file claims physical latency");
  check(read(good/L"scalar-initial.json").find("\"snapshot\":")!=std::string::npos,"Initial snapshot missing");

  // These real-source failures are not exporter mock results.
  const auto legacy=directory(root,L"legacy-api-unavailable");
  auto no_kernel=initial;no_kernel.kernel_present=false;no_kernel.kernel={};
  const auto legacy_result=bench::finish_scalar_capture(legacy,no_kernel,sdk,nullptr,std::chrono::seconds(2));
  check(legacy_result.ok&&legacy_result.io_complete&&!legacy_result.usable,"Legacy absence was fabricated as usable or lifecycle failure");
  check(read(legacy/L"scalar-final-status.json").find("\"final_file\":null")!=std::string::npos,"Absent kernel fabricated a file");
  const auto missing=bench::finish_scalar_capture(directory(root,L"kernel-disappeared"),initial,sdk,nullptr,std::chrono::seconds(2));
  check(!missing.ok&&!missing.io_complete&&!missing.usable,"Disappearing required kernel was accepted");
  const auto expired=bench::finish_scalar_capture(directory(root,L"expired-budget"),initial,sdk,&kernel,std::chrono::milliseconds(0));
  check(!expired.ok&&!expired.io_complete&&!expired.usable,"Expired close budget accepted");
  const auto collision=directory(root,L"existing-output");
  {std::ofstream file(collision/L"scalar-sdk-final.ndjson",std::ios::binary);file<<"original evidence\n";}
  const auto failed=bench::finish_scalar_capture(collision,initial,sdk,&kernel,std::chrono::seconds(2));
  check(!failed.ok&&!failed.io_complete&&!failed.usable,"Writer collision was accepted");
  check(read(collision/L"scalar-sdk-final.ndjson")=="original evidence\n","Existing evidence overwritten");

  const auto base=sdk.snapshot();
  for(unsigned fault=0;fault<10;++fault) {
    fault_source source;source.value=base;
    switch(fault) {
      case 0:source.value.counter_saturated=true;break;
      case 1:source.value.clock_invalid=true;break;
      case 2:source.value.owner_violation=true;break;
      case 3:source.value.invalid_event=true;break;
      case 4:source.value.causal_lost=1;break;
      case 5:source.value.session.run_id_low++;break;
      case 6:source.value.available=false;break;
      case 7:source.value.active_producers=1;source.value.quiescent=false;break;
      case 8:source.value.stopped=false;break;
      case 9:source.mutate_on_drain=true;break;
    }
    const auto name=L"fault-"+std::to_wstring(fault);
    const auto rejected=bench::finish_scalar_capture(directory(root,name.c_str()),initial,source,&kernel,std::chrono::seconds(2));
    check(!rejected.ok&&!rejected.usable&&!rejected.sdk_usable,"Source fault accepted as usable");
  }
  auto reversed=initial;reversed.sdk.counters[0]=base.counters[0]+1;
  const auto reverse=bench::finish_scalar_capture(directory(root,L"counter-reversed"),reversed,sdk,&kernel,std::chrono::seconds(2));
  check(!reverse.ok&&!reverse.usable,"Reversed cumulative scalar accepted");
  auto wrong_clock=initial;wrong_clock.capture_begin_qpc=0;
  const auto clock=bench::finish_scalar_capture(directory(root,L"boundary-clock"),wrong_clock,sdk,&kernel,std::chrono::seconds(2));
  check(!clock.ok&&!clock.usable,"Invalid boundary QPC accepted");
}
void manual_report_contracts(const std::filesystem::path& root) {
  using json=nlohmann::json;
  const std::string run(32,'a');
  const json snapshot={{"schema","soui.manual-input-page/v1"},{"run_id",run},{"requested_kind","pointer_move"},
    {"counts",{{"pointer",3},{"wheel",0},{"renders",1},{"invalid",1}}},{"position",{{"x",20},{"y",30}}},
    {"wheel",{{"deltaX",nullptr},{"deltaY",nullptr},{"deltaMode",nullptr},{"legacyWheelDelta",nullptr}}},
    {"lifecycle",{{"blur",1},{"hidden",1},{"resize",1},{"focus_at_start",false}}},{"overflow",false},{"geometry_invalid",true},
    {"render_pending",true},{"device_pixel_ratio",1},{"viewport",{{"width",1100},{"height",720}}},
    {"deterministic_replay",false},{"native_input_ids_available",false},{"physical_latency_measured",false},
    {"foreground_validity","unknown"},{"workload_comparable","unknown"}};
  const auto params=[&](const json& page){return json{{"snapshot_json",page.dump()}}.dump();};
  const auto body=params(snapshot);const auto identity=trace::current_identity(0xaaaaaaaaaaaaaaaaULL,0xaaaaaaaaaaaaaaaaULL);
  unsigned cases=0;
  for(unsigned failure=0;failure<8;++failure) {
    auto page=snapshot;
    switch(failure) {
      case 0:page["run_id"]=std::string(32,'b');break;
      case 1:page["counts"]["pointer"]=-1;break;
      case 2:page["counts"]["renders"]=2147483648ULL;break;
      case 3:page["render_pending"]=1;break;
      case 4:page["token"]="never-store";break;
      case 5:page["wheel"]["deltaMode"]=3;break;
      case 6:page["counts"]["invalid"]=true;break;
      case 7:page["position"]["x"]=9007199254740992.0;break;
    }
    bench::manual_report_collector invalid(run,"pointer_move");
    rejects([&]{invalid.submit(params(page));},"Malformed manual snapshot accepted");++cases;
  }
  const auto raw=snapshot.dump();const auto duplicate=std::string("{\"run_id\":\"")+run+"\","+raw.substr(1);
  bench::manual_report_collector invalid(run,"pointer_move");
  rejects([&]{invalid.submit(json{{"snapshot_json",duplicate}}.dump());},"Duplicate snapshot key was folded");++cases;
  rejects([&]{invalid.submit(std::string(8193,' '));},"Oversized snapshot accepted");++cases;
  rejects([&]{invalid.submit(json{{"snapshot_json",std::string("{\"counts\":")+std::string(32,'[')+"0"+std::string(32,']')+"}"}}.dump());},"Deep snapshot accepted");++cases;
  bench::manual_report_collector closed(run,"pointer_move");closed.close();
  rejects([&]{closed.submit(body);},"Report admitted after close");++cases;
  bench::manual_report_collector accepted(run,"pointer_move");
  const auto receipt=json::parse(accepted.submit(body));check(receipt.at("accepted")==true&&receipt.at("persisted")==false,"Admission claimed persistence");
  rejects([&]{accepted.submit(body);},"Repeated report replaced the first");++cases;
  accepted.close();const auto file=root/L"manual-input-result.json";
  const auto writer=[&](const std::string& bytes) {
    const auto handle=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(handle!=INVALID_HANDLE_VALUE,"Manual report CREATE_NEW failed");DWORD count{};
    const bool saved=WriteFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(handle);
    CloseHandle(handle);check(saved,"Manual report write/flush failed");
  };
  const auto remaining=[] {return std::chrono::seconds(2);};
  const auto saved=bench::persist_manual_report(accepted,identity,run,remaining,writer);
  check(saved.at("persisted")==true&&saved.at("page_snapshot")==snapshot,"Invalid lifecycle observations were lost");
  const auto original=read(file);check(original.size()<=8192&&original.find("never-store")==std::string::npos,"Manual report limit/secret boundary changed");++cases;
  bench::manual_report_collector collision(run,"pointer_move");collision.submit(body);
  rejects([&]{bench::persist_manual_report(collision,identity,run,remaining,writer);},"Persistence collision reported success");
  check(read(file)==original,"Persistence failure overwrote evidence");++cases;
  bench::manual_report_collector expired(run,"pointer_move");expired.submit(body);bool wrote=false;
  rejects([&]{bench::persist_manual_report(expired,identity,run,[]{return std::chrono::milliseconds(0);},[&](const std::string&){wrote=true;});},"Expired persistence budget accepted");
  check(!wrote,"Expired budget wrote evidence");++cases;
  bench::manual_report_collector unsubmitted(run,"pointer_move");std::string empty;
  const auto absent=bench::persist_manual_report(unsubmitted,identity,run,remaining,[&](const std::string& bytes){empty=bytes;});
  check(absent.at("submission")=="unsubmitted"&&absent.at("persisted")==false&&absent.at("page_snapshot").is_null()&&!empty.empty(),"Missing page was fabricated");++cases;
  // Actual public authorization/transport must preserve the named rejection;
  // uncaught handler exceptions would be collapsed by json_bridge instead.
  bench::manual_report_collector wired(run,"pointer_move");soui::sdk::json_bridge bridge("app://manual-report-contract");
  check(bridge.register_contextual_method("benchmark.manualInputReport","benchmark.manualInputReport",
    [&](const soui::runtime::bridge_request& request){return wired.submit_request(request.params_json);})&&
    bridge.set_session_capabilities({"benchmark.manualInputReport"}),"Manual method/grant registration failed");
  const auto bootstrap=json::parse(bridge.handle_json(json{{"requestId","manual:bootstrap"},{"sessionId",""},{"origin",""},{"token",""},
    {"method","bridge.bootstrap"},{"params",{{"protocol","soui.bridge/1"}}}}.dump()));
  check(bootstrap.at("ok")==true,"Manual contract bootstrap failed");const auto& auth=bootstrap.at("result");
  const auto call=[&]{return json::parse(bridge.handle_json(json{{"requestId","manual:submit"},{"sessionId",auth.at("sessionId")},
    {"origin",auth.at("origin")},{"token",auth.at("token")},{"method","benchmark.manualInputReport"},{"params",json::parse(body)},{"timeoutMs",1000}}.dump()));};
  check(call().at("ok")==true,"Authorized manual snapshot rejected");
  const auto repeated=call();check(repeated.at("ok")==false&&repeated.at("error").at("code")=="E_MANUAL_REPORT_ALREADY_SUBMITTED","Actual transport lost duplicate rejection");++cases;
  wired.close();const auto closing=call();check(closing.at("ok")==false&&closing.at("error").at("code")=="E_MANUAL_REPORT_CLOSING","Actual transport lost close rejection");++cases;
  std::cout<<"manual report contracts: "<<cases<<"; no Host/GUI/input\n";
}
}
int wmain(int argc,wchar_t** argv) {
  try {
    check(argc==2,"Pass one new absolute evidence directory; no windows or DLL are used");
    const std::filesystem::path root(argv[1]);check(root.is_absolute()&&std::filesystem::create_directory(root),"Evidence directory must be new and absolute");
    options_contracts();scalar_contracts(root);manual_report_contracts(root);
    std::cout<<"profile/scalar contracts passed; no Host, DLL, desktop or input execution\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
