#pragma once
#include "support.h"
#include <limits>
#include <iterator>
#include <string_view>

namespace perf {
inline constexpr const char* native_phase_environment="SOUI_DIAGNOSTIC_BRIDGE_PHASES";
inline constexpr std::size_t native_phase_limit=8192;
inline constexpr const char* native_phase_names[]={"native_first_parse","resolver_create_js","reply_run_js"};
inline constexpr const char* native_phase_fields[]={"schema","requested","enabled","status","usable","coherent",
  "observerIncarnation","qpcFrequency","captureBeginQpc","captureEndQpc","publicationSequence","snapshotAttempts",
  "faultMask","counterSaturated","clockInvalid","ownerViolation","sequenceExhausted","closing","inclusiveWallTime",
  "wholeHostAtomic","quiescent","phases"};
inline bool native_phase_u64(const json& value,std::uint64_t& result){
  if(!value.is_string())return false;const auto& text=value.get_ref<const std::string&>();
  if(text.empty()||text.size()>20||(text.size()>1&&text.front()=='0'))return false;
  result=0;for(const auto c:text){if(c<'0'||c>'9')return false;const auto digit=static_cast<unsigned>(c-'0');
    if(result>((std::numeric_limits<std::uint64_t>::max)()-digit)/10)return false;result=result*10+digit;}return true;
}
inline bool native_phase_number(const json& value,std::uint64_t maximum){
  return value.is_number_unsigned()&&value.get<std::uint64_t>()<=maximum;
}
inline bool native_phase_off(const json& value){return value.is_null()||(value.is_string()&&(value==""||value=="0"));}
inline json native_phase_env(const json& environment,const char* name){
  const auto found=environment.find(name);return found==environment.end()?json(nullptr):*found;
}
struct native_phase_extension final{std::size_t begin{std::string::npos},end{};std::string_view raw;bool unique{};};
inline native_phase_extension native_phase_slice(const std::string& text){
  constexpr std::string_view marker=",native_phases=";native_phase_extension part;
  const auto script=text.find(",script=");const auto start=text.find(marker);
  if(start==std::string::npos)return part;
  part.begin=start;part.end=script==std::string::npos?text.size():script;
  if(part.end<start){part.end=text.size();return part;}
  const auto content=start+marker.size();part.raw=std::string_view(text).substr(content,part.end-content);
  part.unique=text.find(marker,content)==std::string::npos;return part;
}
inline bool native_phase_shape(const json& value){
  if(!value.is_object()||value.size()!=std::size(native_phase_fields))return false;
  for(const auto* key:native_phase_fields)if(!value.contains(key))return false;
  if(value["schema"]!="soui.miniblink-native-phases/v1"||!value["status"].is_string())return false;
  const auto status=value["status"].get<std::string>();
  if(status!="configuration_invalid"&&status!="allocation_failed"&&status!="observer_unavailable"&&
     status!="not_started"&&status!="busy"&&status!="unavailable"&&status!="ready")return false;
  for(const auto* key:{"requested","enabled","usable","coherent","counterSaturated","clockInvalid","ownerViolation",
      "sequenceExhausted","closing","inclusiveWallTime","wholeHostAtomic","quiescent"})if(!value[key].is_boolean())return false;
  if(value["inclusiveWallTime"]!=true||value["wholeHostAtomic"]!=false||value["quiescent"]!=false||
     !native_phase_number(value["snapshotAttempts"],3)||!native_phase_number(value["faultMask"],UINT32_MAX))return false;
  const auto faults=value["faultMask"].get<std::uint32_t>();
  unsigned bit=0;for(const auto* key:{"counterSaturated","clockInvalid","ownerViolation","sequenceExhausted"})
    if(value[key].get<bool>()!=bool(faults&(1U<<bit++)))return false;
  std::uint64_t number{};
  for(const auto* key:{"observerIncarnation","qpcFrequency","captureBeginQpc","captureEndQpc","publicationSequence"})
    if(!value[key].is_null()&&!native_phase_u64(value[key],number))return false;
  if(value["coherent"]==false&&!value["publicationSequence"].is_null())return false;
  if(value["coherent"]==true&&(!native_phase_u64(value["publicationSequence"],number)||(number&1)))return false;
  if(value["usable"]==false)return value["phases"].is_null();
  if(status!="ready"||value["requested"]!=true||value["enabled"]!=true||value["coherent"]!=true||faults||value["snapshotAttempts"]==0)return false;
  std::uint64_t begin{},end{};
  if(!native_phase_u64(value["observerIncarnation"],number)||!number||!native_phase_u64(value["qpcFrequency"],number)||!number||
     !native_phase_u64(value["captureBeginQpc"],begin)||!begin||!native_phase_u64(value["captureEndQpc"],end)||end<begin)return false;
  const auto& phases=value["phases"];if(!phases.is_array()||phases.size()!=3)return false;
  for(std::size_t i=0;i<3;++i){const auto& phase=phases[i];
    if(!phase.is_object()||phase.size()!=9||phase.value("phase",std::string{})!=native_phase_names[i])return false;
    for(const auto* key:{"started","completed","returned","unwound","inFlight","nestedStarts","sumElapsedQpc"})
      if(!phase.contains(key)||!native_phase_u64(phase[key],number))return false;
    if(!phase.contains("buckets")||!phase["buckets"].is_array()||phase["buckets"].size()!=64)return false;
    std::uint64_t total=0;for(const auto& bucket:phase["buckets"]){
      if(!native_phase_u64(bucket,number)||number>(std::numeric_limits<std::uint64_t>::max)()-total)return false;total+=number;}
    std::uint64_t started{},completed{},returned{},unwound{},flight{},nested{};
    native_phase_u64(phase["started"],started);native_phase_u64(phase["completed"],completed);native_phase_u64(phase["returned"],returned);
    native_phase_u64(phase["unwound"],unwound);native_phase_u64(phase["inFlight"],flight);native_phase_u64(phase["nestedStarts"],nested);
    if(started<completed||started-completed!=flight||returned>completed||completed-returned!=unwound||nested>started||total!=completed)return false;
  }return true;
}
// Called only at the existing UI cache publication. Raw text stays in the
// native snapshot once; this small summary never contains histogram buckets.
inline json native_phase_summary(const std::string& text,const json& environment){
  const auto requested=native_phase_env(environment,native_phase_environment);
  const auto script=native_phase_env(environment,"SOUI_DIAGNOSTIC_SCRIPT");const auto part=native_phase_slice(text);
  if(native_phase_off(requested)&&part.begin==std::string::npos)return nullptr;
  json result={{"schema","soui.sdk-performance-native-phase-cache/v1"},{"source","cached-ui-application-diagnostics"},
    {"requestedEnvironment",requested},{"scriptEnvironment",script},{"extensionPresent",part.begin!=std::string::npos},
    {"parseStatus","missing"},{"usable",false},{"rawUtf8Bytes",part.raw.size()},{"rawIncluded",false},{"reasons",json::array()}};
  auto& reasons=result["reasons"];
  if(requested!="1")reasons.push_back("profile-not-enabled");
  if(!native_phase_off(script))reasons.push_back("script-profile-enabled-or-invalid");
  if(text.find(",script=")!=std::string::npos)reasons.push_back("unexpected-script-observation");
  if(part.begin==std::string::npos){reasons.push_back("extension-missing");return result;}
  try{
    require(part.unique&&part.raw.size()<=native_phase_limit,"Invalid native phase extent");
    auto value=json::parse(part.raw.begin(),part.raw.end(),[](int depth,json::parse_event_t,json&){
      if(depth>6)throw std::invalid_argument("Native phase nesting");return true;});
    const auto normalized=value.dump();
    require(std::string_view(normalized)==part.raw&&native_phase_shape(value),"Invalid native phase schema");
    result["parseStatus"]="valid";
    for(const auto* key:native_phase_fields)if(std::string_view(key)!="schema"&&std::string_view(key)!="phases"&&std::string_view(key)!="usable")result[key]=value[key];
    result["nativeUsable"]=value["usable"];
    if(value["usable"]!=true)reasons.push_back("native-snapshot-unusable");
    if(value["closing"]==true)reasons.push_back("native-closing");
    result["usable"]=reasons.empty();
  }catch(const std::exception&){result["parseStatus"]="invalid";reasons.push_back("extension-invalid");}
  return result;
}
inline json native_phase_wire_snapshot(json value){
  if(!value.contains("nativePhaseCache"))return value;
  const auto found=value.find("applicationDiagnostics");
  if(found!=value.end()&&found->is_string()){
    const auto text=found->get<std::string>();const auto part=native_phase_slice(text);
    const auto script=text.find(",script=");
    if(part.begin!=std::string::npos&&(script==std::string::npos||part.begin<script))(*found)=text.substr(0,part.begin)+text.substr(part.end);
  }return value;
}
// The caller has already taken its existing scalar boundary timestamps. This
// consumes a retained JSON cache, never a Host/getter/invoke or a new QPC.
inline json native_phase_boundary(const json& source,std::uint64_t begin,std::uint64_t end,std::uint64_t frequency,std::uint64_t session){
  const auto host=source.find("nativeHost");if(host==source.end()||!host->is_object()||!host->contains("nativePhaseCache"))return nullptr;
  const auto& cache=(*host)["nativePhaseCache"];
  json result={{"schema","soui.sdk-performance-native-phase-boundary/v1"},{"cache",cache},
    {"boundaryBeginQpc",std::to_string(begin)},{"boundaryEndQpc",std::to_string(end)},{"qpcFrequency",std::to_string(frequency)},
    {"expectedSessionGeneration",std::to_string(session)},{"cacheAgeAtBoundaryBeginQpc",nullptr},{"qualified",false},
    {"rawSource","sourceIdentity.nativeHost.applicationDiagnostics"},{"reasons",json::array()}};
  auto& reasons=result["reasons"];std::uint64_t captured{},capture_begin{},observed_frequency{};
  if(!cache.is_object()||cache.value("usable",false)!=true)reasons.push_back("cache-unusable");
  else if(!native_phase_u64(cache["qpcFrequency"],observed_frequency)||!frequency||observed_frequency!=frequency)reasons.push_back("frequency-mismatch");
  else if(!native_phase_u64(cache["captureBeginQpc"],capture_begin)||!native_phase_u64(cache["captureEndQpc"],captured)||
          !capture_begin||captured<capture_begin||!begin||end<begin||captured>begin)reasons.push_back("capture-order-invalid");
  else if(!host->contains("sessionGeneration")||!native_phase_number((*host)["sessionGeneration"],(std::numeric_limits<std::uint64_t>::max)())||
          (*host)["sessionGeneration"].get<std::uint64_t>()!=session)reasons.push_back("cached-session-mismatch");
  else{result["cacheAgeAtBoundaryBeginQpc"]=std::to_string(begin-captured);result["qualified"]=true;}
  return result;
}
}
