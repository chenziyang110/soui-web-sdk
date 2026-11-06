#pragma once
#include "support.h"
#include "native_phase_capture.h"
#include "native_clock_anchor.h"
#include <soui/diagnostics/input_frame_trace.h>
#include <soui/diagnostics/miniblink_trace_source.h>
#include <cstring>
#include <sstream>

namespace perf {
namespace input_trace=soui::sdk::diagnostics::input_frame_trace;
inline json scalar_source_status(const input_trace::scalar_snapshot& value,const input_trace::identity& expected,bool requested,bool live=true){
  json reasons=json::array();
  if(!requested)return {{"requested",false},{"available",value.available},{"complete",nullptr},{"reasons",reasons}};
  if(!value.available)reasons.push_back("unavailable");
  else{
    const auto& session=value.session;
    if(session.version!=expected.version||session.process_id!=expected.process_id||session.run_id_high!=expected.run_id_high||
       session.run_id_low!=expected.run_id_low||session.process_incarnation!=expected.process_incarnation||session.qpc_frequency!=expected.qpc_frequency)
      reasons.push_back("identity-mismatch");
    if(!session.qpc_frequency)reasons.push_back("qpc-frequency-unavailable");
    if(value.recording_mode!=input_trace::mode::scalar)reasons.push_back("not-scalar");
    if(live&&value.stopped)reasons.push_back("stopped-before-boundary");
    if(value.counter_saturated)reasons.push_back("counter-saturated");
    if(value.owner_violation)reasons.push_back("owner-violation");
    if(value.invalid_event)reasons.push_back("invalid-event");
    if(value.clock_invalid)reasons.push_back("clock-invalid");
    if(value.dropped)reasons.push_back("dropped");
    if(value.unregistered_writes)reasons.push_back("unregistered-writes");
    if(value.registration_rejections)reasons.push_back("registration-rejections");
    if(value.causal_lost)reasons.push_back("causal-lost");
    if(value.counters[static_cast<std::size_t>(input_trace::counter::causal_ranges_dropped)]||
       value.counters[static_cast<std::size_t>(input_trace::counter::causal_context_dropped)])reasons.push_back("causal-counter-loss");
    // Scalar record() increments attempted/stages but intentionally records no
    // detailed events. attempted!=recorded is normal, not evidence of loss.
    if(value.recorded||value.queued)reasons.push_back("unexpected-detailed-events");
  }
  return {{"requested",true},{"available",value.available},{"complete",reasons.empty()},{"reasons",std::move(reasons)}};
}
inline json scalar_json(const input_trace::scalar_snapshot& value){
  using namespace input_trace;
  constexpr const char* counters[]={"copy_operations","copy_bytes","raster_bytes","lock_contentions","scene_pending","scene_activated",
    "resource_notifications","pacing_wakes","client_callbacks","buffer_leases","admission_accepted","admission_rejected",
    "admission_coalesced","input_recoveries","causal_ranges_dropped","causal_context_dropped"};
  constexpr const char* durations[]={"lock_wait","lock_hold","client_callback","input_queue","task_queue","layout","record","resource_wait",
    "composite","engine_native","engine_provider","engine_input","engine_fifo","webview2_owner","serialization"};
  static_assert(sizeof(counters)/sizeof(counters[0])==counter_count,"Review changed public counter layout");
  static_assert(sizeof(durations)/sizeof(durations[0])==duration_metric_count,"Review changed public histogram layout");
  json result={{"available",value.available},{"mode",value.recording_mode==mode::scalar?"scalar":"detailed"},
    {"processId",value.session.process_id},{"runIdHigh",std::to_string(value.session.run_id_high)},{"runIdLow",std::to_string(value.session.run_id_low)},
    {"processIncarnation",std::to_string(value.session.process_incarnation)},{"qpcFrequency",std::to_string(value.session.qpc_frequency)},
    {"registeredThreads",value.registered_threads},{"activeProducers",value.active_producers},{"pendingRegistrations",value.pending_registrations},
    {"stopped",value.stopped},{"quiescent",value.quiescent},{"counterSaturated",value.counter_saturated},{"ownerViolation",value.owner_violation},
    {"invalidEvent",value.invalid_event},{"clockInvalid",value.clock_invalid},{"attempted",std::to_string(value.attempted)},
    {"recorded",std::to_string(value.recorded)},{"dropped",std::to_string(value.dropped)},{"queued",std::to_string(value.queued)},
    {"unregisteredWrites",std::to_string(value.unregistered_writes)},{"registrationRejections",std::to_string(value.registration_rejections)},
    {"causalLost",std::to_string(value.causal_lost)},{"durationBucketContract","0 for 0/1 QPC ticks; floor(log2(ticks)) otherwise; 64 buckets"},
    {"liveReadNotAtomicAcrossCounters",!value.quiescent},{"stages",nullptr},{"counters",nullptr},{"durations",nullptr}};
  if(!value.available){result["missingReason"]="source snapshot unavailable; zero-filled storage is not measurement";return result;}
  result["stages"]=json::array();for(auto count:value.stages)result["stages"].push_back(std::to_string(count));
  result["counters"]=json::object();for(std::size_t i=0;i<counter_count;++i)result["counters"][counters[i]]=std::to_string(value.counters[i]);
  result["durations"]=json::object();for(std::size_t i=0;i<duration_metric_count;++i){auto& histogram=result["durations"][durations[i]];histogram=json::array();for(auto count:value.durations[i])histogram.push_back(std::to_string(count));}
  return result;
}
class scalar_capture final {
 public:
  ~scalar_capture(){if(configured_&&!finished_){input_trace::detach_current_thread();input_trace::process_recorder().stop();if(kernel_)(void)kernel_->stop();}}
  void configure(bool enabled,const fs::path& miniblink){
    if(!enabled)return;
    GUID guid{};require(SUCCEEDED(CoCreateGuid(&guid)),"Trace run identity unavailable");std::uint64_t high{},low{};
    std::memcpy(&high,&guid,8);std::memcpy(&low,reinterpret_cast<const char*>(&guid)+8,8);
    identity_=input_trace::current_identity(high,low);
    require(identity_.process_id&&identity_.process_incarnation&&identity_.qpc_frequency&&(high||low),"Trace process/QPC identity unavailable");
    require(input_trace::process_recorder().configure(identity_,input_trace::mode::scalar),"Cold SDK scalar trace configuration failed");configured_=true;
    require(input_trace::register_current_thread(),"Cold UI scalar registration failed");
    if(!miniblink.empty()){kernel_requested_=true;kernel_=input_trace::miniblink_trace_source::load(miniblink.c_str(),identity_,input_trace::mode::scalar,kernel_status_);}
  }
  bool enabled()const noexcept{return configured_;}
  bool all_requested_available()const noexcept{return configured_&&(!kernel_requested_||bool(kernel_));}
  bool final_usable()const noexcept{return finished_&&final_usable_;}
  json descriptor()const{
    if(!configured_)return {{"mode","off"},{"configured",false}};
    return {{"mode","scalar"},{"configured",true},{"processId",identity_.process_id},{"runIdHigh",std::to_string(identity_.run_id_high)},
      {"runIdLow",std::to_string(identity_.run_id_low)},{"processIncarnation",std::to_string(identity_.process_incarnation)},
      {"qpcFrequency",std::to_string(identity_.qpc_frequency)},{"kernelRequested",kernel_requested_},{"kernelAvailable",bool(kernel_)},
      {"kernelStatus",kernel_status_name()},{"scope","process cumulative; includes all Hosts and snapshot Bridge work"},{"detailed",false}};
  }
  json measurement(bool begin,unsigned host,dispatch_gate& gate,const dispatch_gate::receipt& original,unsigned id,report_writer& writer,const json& source_identity){
    const auto epoch=original.epoch;const auto& session=original.session;
    require(configured_,"E_INPUT_TRACE_DISABLED");require(host<active_.size()&&!session.empty(),"E_MEASUREMENT_CONTEXT");
    require(gate.claim(original),"E_MEASUREMENT_STALE");
    std::lock_guard<std::mutex> lock(mutex_);auto& current=active_[host];
    if(begin){
      require(next_<32,"E_MEASUREMENT_LIMIT");
      require(!current.id||current.epoch!=epoch||current.session!=session,"E_MEASUREMENT_ALREADY_ACTIVE");
      id=next_+1;
    }else require(current.id==id&&current.epoch==epoch&&current.session==session,"E_MEASUREMENT_STALE");
    active_measurement replacement;if(begin)replacement={id,epoch,session}; // Allocate before publication to the writer.
    const auto captured_begin=input_trace::now_qpc();
    const auto sdk_snapshot=input_trace::process_recorder().snapshot();
    const auto kernel_snapshot=kernel_?kernel_->snapshot():input_trace::scalar_snapshot{};
    const auto captured_end=input_trace::now_qpc();
    require(captured_begin&&captured_end>=captured_begin,"E_MEASUREMENT_CLOCK");
    const auto clock_anchor=capture_native_clock_anchor();
    const auto phase_cache=native_phase_boundary(source_identity,captured_begin,captured_end,identity_.qpc_frequency,epoch);
    const auto sources=source_status(sdk_snapshot,kernel_snapshot);const bool usable=sources_usable(sources);
    auto kernel=kernel_?scalar_json(kernel_snapshot):json{{"available",false},{"missingReason",kernel_status_name()},{"counters",nullptr},{"durations",nullptr},{"stages",nullptr}};
    json report={{"schema","soui.sdk-performance-scalar-boundary/v1"},{"phase",begin?"begin":"end"},{"measurementId",id},
      {"hostIndex",host},{"sessionGeneration",std::to_string(epoch)},{"scope","process-cumulative-live"},{"sourceIdentity",source_identity},
      {"captureBeginQpc",std::to_string(captured_begin)},{"captureEndQpc",std::to_string(captured_end)},{"qpcFrequency",std::to_string(identity_.qpc_frequency)},
      {"clockAnchor",clock_anchor},
      {"sdk",scalar_json(sdk_snapshot)},{"kernel",std::move(kernel)},{"physicalInputLatencyMeasured",false},
      {"snapshotUsable",usable},{"sourceStatus",sources},
      {"clockBoundary","Native snapshot executes after Bridge dispatch; never subtract performance.now from QPC"}};
    if(!phase_cache.is_null())report["nativePhaseCache"]=phase_cache;
    auto prepared=report_writer::prepare(std::move(report));
    json receipt;
    // Snapshot and large JSON serialization finish outside the revocation gate.
    // Only bounded in-memory writer admission and active-id publication share
    // its linearization point with shutdown_session. No filesystem/Host call.
    require(gate.publish(original,[&]{
      receipt=writer.enqueue_prepared(std::move(prepared));require(receipt.value("accepted",false),"E_MEASUREMENT_WRITE_CAPACITY");
      if(begin){
        if(current.id)cancelled_[cancelled_count_++]={current.id,host,current.epoch};
        next_=id;current=std::move(replacement);
      }else current={};
    }),"E_MEASUREMENT_STALE");
    json result={{"id",id},{"phase",begin?"begin":"end"},{"status","snapshot_queued"},{"scope","process-cumulative-live"},
      {"writeId",receipt["id"]},{"path",receipt["path"]},{"captureEndQpc",std::to_string(captured_end)},
      {"qpcFrequency",std::to_string(identity_.qpc_frequency)},{"clockAnchor",clock_anchor},{"snapshotUsable",usable},{"sourceStatus",sources}};
    result[begin?"beginNativeQpc":"endNativeQpc"]=std::to_string(captured_begin);return result;
  }
  bool before_runtime_shutdown(){return !configured_||input_trace::register_current_thread();}
  bool finish(const fs::path& output){
    if(!configured_||finished_)return true;
    input_trace::detach_current_thread();input_trace::process_recorder().stop();const bool kernel_stopped=!kernel_||kernel_->stop();
    const auto export_one=[&](input_trace::trace_source& source,const wchar_t* name){
      std::ostringstream stream;input_trace::exporter exporter(stream,source);
      bool complete=exporter.begin();if(complete){const auto drained=exporter.drain(1);complete=!drained.busy&&drained.count==0&&exporter.finish();}
      write_new(output/name,stream.str());return complete;
    };
    bool sdk_complete=export_one(input_trace::process_recorder(),L"scalar-sdk-final.ndjson");
    bool kernel_complete=!kernel_;if(kernel_)kernel_complete=export_one(*kernel_,L"scalar-kernel-final.ndjson");
    json orphaned=json::array();
    for(unsigned i=0;i<cancelled_count_;++i)orphaned.push_back({{"id",cancelled_[i].id},{"hostIndex",cancelled_[i].host},{"sessionGeneration",std::to_string(cancelled_[i].epoch)},{"state","superseded-by-session-change-without-end"}});
    for(std::size_t i=0;i<active_.size();++i)if(active_[i].id)orphaned.push_back({{"id",active_[i].id},{"hostIndex",i},{"state","missing-end-or-navigation-cancelled"}});
    const auto sdk_final=input_trace::process_recorder().snapshot();const auto kernel_final=kernel_?kernel_->snapshot():input_trace::scalar_snapshot{};
    const auto sources=source_status(sdk_final,kernel_final,false);final_usable_=sources_usable(sources);
    write_new(output/L"scalar-final-status.json",json{{"descriptor",descriptor()},{"allRequestedSourcesAvailable",all_requested_available()},{"sdkComplete",sdk_complete},{"kernelComplete",kernel_?json(kernel_complete):json(nullptr)},
      {"snapshotUsable",final_usable_},{"sourceStatus",sources},
      {"kernelStopSucceeded",kernel_stopped},{"unclosedMeasurements",orphaned},{"sdk",scalar_json(input_trace::process_recorder().snapshot())},
      {"kernel",kernel_?scalar_json(kernel_->snapshot()):json{{"available",false},{"missingReason",kernel_status_name()}}},{"physicalPresentationMeasured",false}}.dump(2)+"\n");
    finished_=true;return sdk_complete&&kernel_complete&&kernel_stopped;
  }
 private:
  json source_status(const input_trace::scalar_snapshot& sdk,const input_trace::scalar_snapshot& kernel,bool live=true)const{
    return {{"sdk",scalar_source_status(sdk,identity_,true,live)},{"kernel",scalar_source_status(kernel,identity_,kernel_requested_,live)},
      {"qualification","Observed scalar source identity/flags only; not atomic, quiescent, full causal coverage or physical latency"}};
  }
  bool sources_usable(const json& sources)const{return sources["sdk"]["complete"]==true&&(!kernel_requested_||sources["kernel"]["complete"]==true);}
  const char* kernel_status_name()const noexcept{
    if(!kernel_requested_)return "not-applicable-to-selected-backend";
    switch(kernel_status_){case input_trace::backend_status::ready:return "ready";case input_trace::backend_status::module_unavailable:return "module-unavailable";
      case input_trace::backend_status::api_unavailable:return "api-unavailable";case input_trace::backend_status::incompatible:return "incompatible";
      case input_trace::backend_status::configure_rejected:return "configure-rejected";case input_trace::backend_status::no_memory:return "no-memory";}
    return "unknown";
  }
  struct active_measurement {unsigned id{};std::uint64_t epoch{};std::string session;};
  struct cancelled_measurement {unsigned id{},host{};std::uint64_t epoch{};};
  bool configured_{},finished_{},kernel_requested_{},final_usable_{};input_trace::identity identity_;
  input_trace::backend_status kernel_status_{input_trace::backend_status::api_unavailable};std::unique_ptr<input_trace::miniblink_trace_source> kernel_;
  std::mutex mutex_;std::array<active_measurement,2> active_;unsigned next_{};
  std::array<cancelled_measurement,32> cancelled_;unsigned cancelled_count_{};
};
}
