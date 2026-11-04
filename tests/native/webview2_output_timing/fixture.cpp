#include "../../../sdk/renderers/webview2/output_timing_policy.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <fstream>

namespace timing = soui::sdk::detail::webview2_output_timing;
namespace trace = soui::sdk::diagnostics::input_frame_trace;
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
#include "observation_cases.h"
int main(int argc, char** argv) {
  try {
    observation_fixture::run_initialization();
    observation_fixture::run();
    unsigned checked = 0;
    for(unsigned source=1;source<=3;++source)for(unsigned executor=1;executor<=3;++executor)
      for(unsigned prepared=0;prepared<=4;++prepared)for(const auto bytes:{0U,64U,4096U,65536U}) {
        timing::metadata before{static_cast<timing::origin>(source),static_cast<timing::executor>(executor),
          static_cast<timing::preparation>(prepared),bytes},after;
        require(timing::decode(timing::encode(before),after),"valid metadata decode");
        require(before.source==after.source&&before.thread==after.thread&&before.prepared==after.prepared&&before.raw_bytes==after.raw_bytes,"metadata roundtrip");
        ++checked;
      }
    timing::metadata details{timing::origin::event_sink,timing::executor::owner,timing::preparation::cached,4096},invalid;
    constexpr std::size_t limit=65536,boundary=limit/sizeof(wchar_t);
    require(!timing::should_prepare(false,true,64,limit)&&!timing::should_prepare(true,false,1,0),"disabled/invalid budget prepared");
    require(timing::should_prepare(true,false,boundary-1,limit)&&!timing::should_prepare(true,false,boundary,limit)&&
      !timing::should_prepare(true,false,boundary+1,limit),"response early-skip boundary differs");
    require(timing::should_prepare(true,true,boundary,limit)&&timing::should_prepare(true,true,60000,limit),"compressible event skipped");
    require(!timing::should_prepare(true,true,limit+1,limit),"raw over byte budget prepared");
    invalid.raw_bytes=65537;require(!timing::encode(invalid),"oversized metadata accepted");
    require(!timing::decode(timing::signature|(1ULL<<26),invalid),"reserved bits accepted");
    require(!timing::decode(timing::signature,invalid),"unknown origin/executor accepted");
    trace::event event;event.input_first=event.input_last=event.frame_id=123;
    require(timing::make_event(event,trace::stage::task_begin,17,2,details),"begin rejected");
    require(!event.input_first&&!event.input_last&&!event.action_first&&!event.action_last&&!event.frame_id&&
      !event.view_id&&!event.view_generation&&!event.geometry_generation&&!event.related_id&&!event.flags,"invented input/frame relation");
    require(!timing::make_event(event,trace::stage::task_begin,0,2,details),"zero task accepted");
    require(!timing::make_event(event,trace::stage::submit,17,2,details),"unrelated stage accepted");
    require(!timing::make_event(event,trace::stage::task_end,17,2,details,trace::reason::capacity),"successful task has failure reason");
    unsigned called=0;require(!timing::emit(false,[&](const trace::event&){++called;},trace::stage::task_begin,17,2,details)&&!called,"disabled timing emitted work");

    trace::recorder recorder;require(recorder.configure(trace::current_identity(0x57564f01,1),trace::mode::detailed),"real recorder configure");
    {
      auto writer=recorder.attach_current_thread();require(static_cast<bool>(writer),"real producer registration");
      for(const auto point:{trace::stage::task_posted,trace::stage::task_begin,trace::stage::task_end}){
        require(timing::make_event(event,point,17,2,details),"valid event creation");
        require(writer.record(event)==trace::record_result::recorded,"existing recorder rejected output point");
      }
      event={};event.point=trace::stage::bridge_response;event.task_id=17;event.document_generation=2;event.value=details.raw_bytes;
      require(writer.record(event)==trace::record_result::recorded,"legacy raw-byte bridge_response rejected");
      require(timing::make_event(event,trace::stage::task_cancelled,17,2,details,trace::reason::post_failed),"post failure after owner observation rejected");
      require(writer.record(event)==trace::record_result::recorded,"post failure record rejected");
      for(const auto reason:{trace::reason::capacity,trace::reason::stale_generation,trace::reason::stopped,trace::reason::invalid}){
        require(timing::make_event(event,trace::stage::task_cancelled,18+static_cast<unsigned>(reason),2,details,reason),"failure event rejected");
        require(writer.record(event)==trace::record_result::recorded,"existing recorder rejected failure");
      }
    }
    recorder.stop();std::ostringstream bytes;trace::exporter exporter(bytes,recorder);
    require(exporter.begin(),"export begin");require(exporter.drain(32).count==9,"real event count differs");require(exporter.finish(),"real export end");
    if(argc==2){std::ifstream existing(argv[1],std::ios::binary);require(!existing.good(),"export destination must be new");
      std::ofstream output(argv[1],std::ios::binary);output<<bytes.str();require(output.good(),"cannot write test trace");}
    else require(argc==1,"usage: fixture [NEW_TRACE]");
    std::cout<<checked<<" metadata cases; recorder/export/disabled/invalid boundaries passed\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
