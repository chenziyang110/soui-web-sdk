#pragma once
#include <soui/diagnostics/input_frame_trace.h>
#include <json.hpp>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace soui::benchmark {
namespace manual_report_detail {
using json=nlohmann::json;
inline constexpr std::size_t byte_limit=8192;
inline constexpr const char* capability="manual-page-snapshot-v1";
inline void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
inline void keys(const json& value,std::initializer_list<const char*> expected) {
  require(value.is_object()&&value.size()==expected.size(),"E_MANUAL_REPORT_KEYS");
  for(const auto* key:expected)require(value.contains(key),"E_MANUAL_REPORT_KEYS");
}
inline bool integer(const json& value,std::uint64_t maximum=2147483647ULL) {
  return value.is_number_integer()&&(value.is_number_unsigned()||value.get<std::int64_t>()>=0)&&value.get<std::uint64_t>()<=maximum;
}
inline bool number(const json& value) {
  return value.is_number()&&std::isfinite(value.get<double>())&&std::abs(value.get<double>())<=9007199254740991.0;
}
inline void nullable_number(const json& value){require(value.is_null()||number(value),"E_MANUAL_REPORT_NUMBER");}
inline json parse(std::string_view params,const std::string& run,const std::string& kind) {
  require(params.size()<=byte_limit,"E_MANUAL_REPORT_SIZE");
  // The public transport has already parsed/dumped params. Preserve the actual
  // snapshot JSON as a string so its duplicate keys cannot be silently folded.
  std::set<std::string> envelope_keys;
  const auto envelope=json::parse(params.begin(),params.end(),[&](int depth,json::parse_event_t event,json& item) {
    require(depth<=1&&event!=json::parse_event_t::array_start&&!(depth&&event==json::parse_event_t::object_start),"E_MANUAL_REPORT_DEPTH");
    if(event==json::parse_event_t::key)require(envelope_keys.insert(item.get<std::string>()).second,"E_MANUAL_REPORT_DUPLICATE_KEY");
    return true;
  });keys(envelope,{"snapshot_json"});
  require(envelope.at("snapshot_json").is_string(),"E_MANUAL_REPORT_TYPE");
  const auto& text=envelope.at("snapshot_json").get_ref<const std::string&>();
  require(text.size()<=byte_limit,"E_MANUAL_REPORT_SIZE");
  std::vector<std::set<std::string>> objects;
  auto report=json::parse(text,[&](int depth,json::parse_event_t event,json& item) {
    require(depth<=3&&event!=json::parse_event_t::array_start,"E_MANUAL_REPORT_DEPTH");
    if(event==json::parse_event_t::object_start)objects.emplace_back();
    else if(event==json::parse_event_t::key)require(!objects.empty()&&objects.back().insert(item.get<std::string>()).second,"E_MANUAL_REPORT_DUPLICATE_KEY");
    else if(event==json::parse_event_t::object_end)objects.pop_back();
    return true;
  });
  keys(report,{"schema","run_id","requested_kind","counts","position","wheel","lifecycle","overflow","geometry_invalid",
    "device_pixel_ratio","viewport","deterministic_replay","native_input_ids_available","physical_latency_measured",
    "foreground_validity","workload_comparable","render_pending"});
  require(report.at("schema")=="soui.manual-input-page/v1"&&report.at("run_id")==run&&report.at("requested_kind")==kind,"E_MANUAL_REPORT_IDENTITY");
  const auto& counts=report.at("counts");keys(counts,{"pointer","wheel","renders","invalid"});
  for(const auto* key:{"pointer","wheel","renders","invalid"})require(integer(counts.at(key)),"E_MANUAL_REPORT_COUNTER");
  const auto& position=report.at("position");keys(position,{"x","y"});nullable_number(position.at("x"));nullable_number(position.at("y"));
  const auto& wheel=report.at("wheel");keys(wheel,{"deltaX","deltaY","deltaMode","legacyWheelDelta"});
  for(const auto* key:{"deltaX","deltaY","legacyWheelDelta"})nullable_number(wheel.at(key));
  require(wheel.at("deltaMode").is_null()||integer(wheel.at("deltaMode"),2),"E_MANUAL_REPORT_WHEEL_MODE");
  const auto& lifecycle=report.at("lifecycle");keys(lifecycle,{"blur","hidden","resize","focus_at_start"});
  for(const auto* key:{"blur","hidden","resize"})require(integer(lifecycle.at(key)),"E_MANUAL_REPORT_COUNTER");
  require(lifecycle.at("focus_at_start").is_null()||lifecycle.at("focus_at_start").is_boolean(),"E_MANUAL_REPORT_FOCUS");
  for(const auto* key:{"overflow","geometry_invalid","render_pending"})require(report.at(key).is_boolean(),"E_MANUAL_REPORT_BOOLEAN");
  const auto& viewport=report.at("viewport");keys(viewport,{"width","height"});
  require(integer(viewport.at("width"))&&integer(viewport.at("height")),"E_MANUAL_REPORT_VIEWPORT");
  require(number(report.at("device_pixel_ratio"))&&report.at("device_pixel_ratio").get<double>()>0,"E_MANUAL_REPORT_DPR");
  require(report.at("deterministic_replay")==false&&report.at("native_input_ids_available")==false&&report.at("physical_latency_measured")==false&&
    report.at("foreground_validity")=="unknown"&&report.at("workload_comparable")=="unknown","E_MANUAL_REPORT_CLAIM");
  return report;
}
}

// One immutable, authorized page observation. The mutex only publishes/moves
// bounded memory; parsing, encoding, Host calls and persistence stay outside it.
class manual_report_collector final {
 public:
  manual_report_collector(std::string run,std::string kind):run_(std::move(run)),kind_(std::move(kind)){}
  std::string submit(std::string_view params) {
    const auto begin=sdk::diagnostics::input_frame_trace::now_qpc();
    auto report=manual_report_detail::parse(params,run_,kind_).dump();
    const auto end=sdk::diagnostics::input_frame_trace::now_qpc();
    manual_report_detail::require(begin&&end>=begin,"E_MANUAL_REPORT_CLOCK");
    {
      std::lock_guard<std::mutex> lock(mutex_);
      manual_report_detail::require(!closing_,"E_MANUAL_REPORT_CLOSING");
      manual_report_detail::require(report_.empty(),"E_MANUAL_REPORT_ALREADY_SUBMITTED");
      report_=std::move(report);begin_=begin;end_=end;
    }
    return "{\"accepted\":true,\"persisted\":false,\"submission\":\"accepted\"}";
  }
  std::string submit_request(std::string_view params) {
    try{return submit(params);}
    catch(const std::exception& failure) {
      const std::string code=failure.what();
      const bool controlled=code.rfind("E_MANUAL_REPORT_",0)==0&&code.size()<80;
      return manual_report_detail::json{{"error",controlled?code:"E_MANUAL_REPORT_INVALID"},{"message","Manual page snapshot rejected."}}.dump();
    }
  }
  void close(){std::lock_guard<std::mutex> lock(mutex_);closing_=true;}
  manual_report_detail::json take(const sdk::diagnostics::input_frame_trace::identity& native,const std::string& run) {
    std::string report;std::uint64_t begin{},end{};
    {std::lock_guard<std::mutex> lock(mutex_);closing_=true;report=std::move(report_);begin=begin_;end=end_;}
    return {{"schema","soui.input-ui-manual-result/v1"},{"capability",manual_report_detail::capability},{"run_id",run},
      {"process_id",native.process_id},{"process_incarnation",std::to_string(native.process_incarnation)},
      {"qpc_frequency",std::to_string(native.qpc_frequency)},{"submission",report.empty()?"unsubmitted":"accepted"},
      {"accepted_begin_qpc",report.empty()?manual_report_detail::json(nullptr):manual_report_detail::json(std::to_string(begin))},
      {"accepted_end_qpc",report.empty()?manual_report_detail::json(nullptr):manual_report_detail::json(std::to_string(end))},
      {"page_snapshot",report.empty()?manual_report_detail::json(nullptr):manual_report_detail::json::parse(report)},
      {"snapshot_scope","user-requested-page-snapshot"},{"foreground_validity","unknown"},{"workload_comparable","unknown"},
      {"unique_frames",nullptr},{"physical_latency_measured",false},{"performanceGate",false},{"releaseReady",false}};
  }
 private:
  const std::string run_,kind_;std::mutex mutex_;bool closing_{};std::string report_;std::uint64_t begin_{},end_{};
};

template<class Remaining,class Write>
manual_report_detail::json persist_manual_report(manual_report_collector& collector,
    const sdk::diagnostics::input_frame_trace::identity& identity,const std::string& run,Remaining&& remaining,Write&& write) {
  auto value=collector.take(identity,run);
  manual_report_detail::require(remaining().count()>0,"E_MANUAL_REPORT_CLOSE_DEADLINE");
  const auto bytes=value.dump()+"\n";
  manual_report_detail::require(bytes.size()<=manual_report_detail::byte_limit,"E_MANUAL_REPORT_SIZE");
  write(bytes); // CREATE_NEW + full WriteFile + FlushFileBuffers, after quiescence.
  manual_report_detail::require(remaining().count()>0,"E_MANUAL_REPORT_CLOSE_DEADLINE");
  value["persisted"]=value.at("submission")=="accepted"; // Status only after the actual write/flush returns.
  return value;
}
}
