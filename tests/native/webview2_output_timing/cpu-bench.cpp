#include <windows.h>
#include <json.hpp>
#include "../../../sdk/renderers/webview2/owner_output_policy.h"
#include "../../../sdk/renderers/webview2/output_timing_policy.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using json=nlohmann::json;
namespace detail=soui::sdk::detail;
constexpr std::size_t message_limit=64U*1024U;
struct state { struct output {
  std::string document_id{"fixed-document"},id{"fixed-request"},invocation_id{"1"},raw;
  bool event{false};std::wstring prepared_json;std::size_t charged_bytes{0};
};};
#include "serialization.inc" // Exact current production serializer/parser/UTF conversion functions.

namespace {
using clock_type=std::chrono::steady_clock;
double microseconds(clock_type::duration duration){return std::chrono::duration<double,std::micro>(duration).count();}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
enum class preparation {disabled,cached,limit_fallback,exception_fallback};
preparation prepare(state::output& output,bool enabled,bool guarded=false){
  output.charged_bytes=output.raw.size();
  if(!enabled)return preparation::disabled;
  if(guarded&&!detail::webview2_output_timing::should_prepare(enabled,output.event,output.raw.size(),message_limit))return preparation::limit_fallback;
  // Projection of push_output's candidate. No queue, trace or COM is simulated.
  try{
    const auto encoded=serialize_output(output);
    if(encoded.size()<=message_limit/sizeof(wchar_t)){
      auto prepared=wide(encoded);
      if(detail::owner_output_budget::may_cache(output.raw.size(),prepared.size(),message_limit)){
        output.charged_bytes+=prepared.size()*sizeof(wchar_t);output.prepared_json=std::move(prepared);
        return preparation::cached;
      }
    }
  }catch(...){output.prepared_json.clear();return preparation::exception_fallback;}
  return preparation::limit_fallback;
}
std::wstring deliver(state::output& output){
  std::wstring wire=std::move(output.prepared_json);
  if(wire.empty())wire=wide(serialize_output(output));
  return wire;
}
std::string raw_payload(unsigned bound,const std::string& pattern){
  const std::string token=pattern=="unicode"?"\xe7\x8c\xab":pattern=="escaped"?"\"":"x";
  const auto cost=pattern=="unicode"?3U:pattern=="escaped"?2U:1U;
  std::string value;for(unsigned i=0;i<(bound-12)/cost;++i)value+=token;
  const auto raw=json{{"value",value}}.dump();require(raw.size()<=bound,"payload byte budget differs");return raw;
}
void measure(const std::string& raw,bool event,bool enabled,const std::string& pattern,unsigned trial,bool guarded,bool compare_guard){
  state::output reference;reference.raw=raw;reference.event=event;const auto expected=wide(serialize_output(reference));
  constexpr unsigned iterations=128;
  double preparation_us=0,owner_us=0,total_us=0;unsigned cached=0,fallback=0;std::size_t peak_charge=0;
  for(unsigned i=0;i<iterations;++i){
    state::output output;output.raw=raw;output.event=event;
    const auto start=clock_type::now();const auto result=prepare(output,enabled,guarded);const auto prepared=clock_type::now();
    const auto wire=deliver(output);const auto delivered=clock_type::now();
    preparation_us+=microseconds(prepared-start);owner_us+=microseconds(delivered-prepared);total_us+=microseconds(delivered-start);
    require(wire==expected,"prepare altered actual serialized wire");require(output.charged_bytes<=message_limit,"raw+wire cache exceeds bound");
    cached+=result==preparation::cached;fallback+=result==preparation::limit_fallback||result==preparation::exception_fallback;
    if(output.charged_bytes>peak_charge)peak_charge=output.charged_bytes;
  }
  std::cout<<json{{"schema","soui.webview2-output-cpu-projection/v1"},{"trial",trial},{"kind",event?"event":"response"},
    {"pattern",pattern},{"raw_bytes",raw.size()},{"wire_utf16_bytes",expected.size()*sizeof(wchar_t)},
    {"prepare",enabled},{"guarded",guarded},{"comparison",compare_guard?"guard-only":"prepare-only"},
    {"iterations",iterations},{"cached",cached},{"fallback",fallback},{"peak_charged_bytes",peak_charge},
    {"preparation_us",preparation_us},{"simulated_owner_us",owner_us},{"total_us",total_us},
    {"wire_equal",true},{"single_cpu_thread",true},{"com_measured",false},{"input_measured",false},{"fps_measured",false}}.dump()<<'\n';
}
}
int main(int argc,char** argv){
  try{
    require(argc==1||(argc==2&&std::string(argv[1])=="--compare-guard"),"Usage: CPU bench [--compare-guard]");
    const bool compare_guard=argc==2;
    state::output invalid;invalid.event=true;invalid.raw="{";
    require(prepare(invalid,true)==preparation::exception_fallback,"malformed event did not retain fallback");
    bool failed=false;try{(void)deliver(invalid);}catch(...){failed=true;}require(failed,"malformed event silently succeeded");
    for(const auto size:{message_limit/sizeof(wchar_t)-1,message_limit/sizeof(wchar_t),message_limit/sizeof(wchar_t)+1}){
      state::output old;old.raw=raw_payload(static_cast<unsigned>(size),"ascii");state::output candidate=old;
      (void)prepare(old,true);(void)prepare(candidate,true,true);require(deliver(old)==deliver(candidate),"response boundary changed wire");
    }
    state::output unicode;unicode.raw=raw_payload(60000,"unicode");state::output unicode_guarded=unicode;
    (void)prepare(unicode,true);require(prepare(unicode_guarded,true,true)==preparation::limit_fallback,"unicode response should early skip");
    require(deliver(unicode)==deliver(unicode_guarded),"unicode early skip changed wire");
    state::output whitespace;whitespace.event=true;whitespace.raw=std::string(59993,' ')+"{\"v\":1}";
    state::output whitespace_guarded=whitespace;
    require(prepare(whitespace,true)==preparation::cached&&prepare(whitespace_guarded,true,true)==preparation::cached,"large compactable event incorrectly skipped");
    require(deliver(whitespace)==deliver(whitespace_guarded),"whitespace event changed wire");
    // ABBA limits a simple direction bias; these are CPU attribution probes,
    // not physical performance acceptance or WebView2 owner fairness results.
    for(const auto size:{64U,4096U,60000U})for(const auto& pattern:{std::string("ascii"),std::string("escaped"),std::string("unicode")}){
      const auto raw=raw_payload(size,pattern);
      for(const bool event:{false,true}){unsigned trial=0;for(const bool selected:{false,true,true,false})
        measure(raw,event,compare_guard?true:selected,pattern,++trial,compare_guard&&selected,compare_guard);}
    }
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
