#include <soui/json_bridge.h>
#include <soui/runtime/services/task/task_service.h>
#include <soui/runtime/core/task/task_store.h>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace rt=soui::runtime;
namespace dm=soui::sdk::diagnostics;
namespace {
constexpr std::uint64_t proposed_limit=128ull*1024*1024;
static_assert(rt::default_task_store_request_quota.max_live_requested_bytes==proposed_limit,
    "Published default quota must match the calibrated policy");
unsigned checks=0,failures=0;
void check(const char* name,rt::task_store_request_quota_status status,
    const rt::task_store_request_quota_snapshot& sample,std::optional<std::uint64_t> expected) {
  const bool valid=status==rt::task_store_request_quota_status::available&&sample.owner_epoch_valid&&
      sample.credit_valid&&sample.denied_requests_exact&&sample.denied_requests==0&&sample.max_live_requested_bytes==expected;
  ++checks;if(!valid)++failures;
  std::cout<<"{\"case\":\""<<name<<"\",\"matchedProposedPolicy\":"<<(valid?"true":"false")<<",\"limit\":";
  if(sample.max_live_requested_bytes)std::cout<<'"'<<*sample.max_live_requested_bytes<<'"';else std::cout<<"null";
  std::cout<<"}\n";
}
void bridge(const char* name,soui::sdk::json_bridge& value,std::optional<std::uint64_t> expected){
  rt::task_store_request_quota_snapshot sample;const auto status=value.read_task_store_request_quota(sample);check(name,status,sample,expected);
}
template<class T>void store(const char* name,T& value,std::optional<std::uint64_t> expected){
  rt::task_store_request_quota_snapshot sample;const auto status=value.read_request_quota(sample);check(name,status,sample,expected);
}
template<class Verify>
void explicit_policy(const char* name, std::optional<std::uint64_t> expected, Verify&& verify) {
  try { verify(); }
  catch (const rt::task_store_request_quota_exceeded&) {
    if (expected != 0) throw;
    // Empty map construction may already request positive bytes. A failed
    // constructor has no live object from which a quota snapshot can be read.
    ++checks;
    std::cout << "{\"case\":\"" << name
              << "\",\"matchedProposedPolicy\":true,\"explicitLimit\":\"0\","
                 "\"snapshotAvailable\":false,\"constructorRefusedPositiveRequest\":true}\n";
  }
}
}
int main(){
 try {
  {soui::sdk::json_bridge value("app://default-quota-probe");bridge("bridge-default",value,proposed_limit);}
  {rt::task_service value;store("service-default",value,proposed_limit);}
  {rt::task_store value;store("store-default",value,proposed_limit);}
  for(const auto mode:{dm::mode::disabled,dm::mode::scalar}){
   {soui::sdk::json_bridge value("app://default-quota-probe",mode);bridge("bridge-mode",value,proposed_limit);}
   {rt::task_service value(mode);store("service-mode",value,proposed_limit);}
   {rt::task_store value("probe-",mode);store("store-mode",value,proposed_limit);}
   for(const auto policy:{std::optional<rt::task_store_request_quota_policy>{},std::optional<rt::task_store_request_quota_policy>{rt::task_store_request_quota_policy{0}},std::optional<rt::task_store_request_quota_policy>{rt::task_store_request_quota_policy{4096}}}){
    const auto expected=policy?std::optional<std::uint64_t>{policy->max_live_requested_bytes}:std::nullopt;
    explicit_policy("bridge-explicit", expected, [&] {
      soui::sdk::json_bridge value("app://explicit-quota-probe",mode,policy);
      bridge("bridge-explicit",value,expected);
    });
    explicit_policy("service-explicit", expected, [&] {
      rt::task_service value(mode,policy);
      store("service-explicit",value,expected);
    });
    explicit_policy("store-explicit", expected, [&] {
      rt::task_store value("probe-",mode,policy);
      store("store-explicit",value,expected);
    });
   }
  }
  {
   rt::task_store first,second;rt::task_store_request_quota_snapshot a,b;
   if(first.read_request_quota(a)!=rt::task_store_request_quota_status::available||
      second.read_request_quota(b)!=rt::task_store_request_quota_status::available||
      !a.owner_epoch_valid||!b.owner_epoch_valid||a.owner_epoch==b.owner_epoch)
     throw std::runtime_error("Default stores must retain independent quota owners");
   bool denied=false;
   try{rt::task_store rejected(std::string(64,'p'),dm::mode::disabled,rt::task_store_request_quota_policy{0});}
   catch(const rt::task_store_request_quota_exceeded&){denied=true;}
   if(!denied)throw std::runtime_error("Explicit zero must refuse an allocated Core prefix");
  }
  {
   rt::task_store bounded,control;
   rt::task_store_request_quota_snapshot before,after;
   if(bounded.read_request_quota(before)!=rt::task_store_request_quota_status::available||
      before.max_live_requested_bytes!=proposed_limit)
     throw std::runtime_error("Default limit must be present before the oversized request test");
   bool denied=false;
   try{
    std::string oversized(static_cast<std::size_t>(proposed_limit+1),'x');
    (void)bounded.start_scoped(std::move(oversized),"default-quota-scope");
   }catch(const rt::task_store_request_quota_exceeded&){denied=true;}
   if(!denied||bounded.read_request_quota(after)!=rt::task_store_request_quota_status::available||
      !after.credit_valid||!after.denied_requests_exact||after.owner_epoch!=before.owner_epoch||
      after.denied_requests!=before.denied_requests+1||
      after.charged_live_requested_bytes!=before.charged_live_requested_bytes||
      after.successful_live_requested_bytes!=before.successful_live_requested_bytes||
      after.charged_peak_requested_bytes>proposed_limit)
     throw std::runtime_error("Default quota must reject, roll back and retain valid accounting");
   const auto actual=bounded.start_scoped("small-operation","default-quota-scope");
   const auto expected=control.start_scoped("small-operation","default-quota-scope");
   if(actual.task_id!=expected.task_id||actual.sequence!=expected.sequence||
      !bounded.mark_running(actual.task_id)||!control.mark_running(expected.task_id)||
      !bounded.complete(actual.task_id,rt::result_kind::success)||
      !control.complete(expected.task_id,rt::result_kind::success)||
      !bounded.erase(actual.task_id)||!control.erase(expected.task_id))
     throw std::runtime_error("Default quota rejection must not consume identity or prevent later lawful work");
  }
  if (checks != 27) throw std::runtime_error("Complete default and explicit-policy case roster required");
  std::cout<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"productionDefaultQualified\":false}\n";
  return failures?1:0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
