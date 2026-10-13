#include <soui/diagnostics/cache_observation.h>
#include <soui/diagnostics/renderer_model_abi.h>
#include "../acceptance-app/native-owner-metrics.h"
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace soui::sdk::diagnostics;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
wkeCacheSnapshot fixture() {
  wkeCacheSnapshot value{};
  value.structSize=sizeof(value); value.abiVersion=1; value.status=WKE_CACHE_READY;
  value.processId=9; value.processCreationFileTime=123; value.engineOwnerThreadId=10;
  value.recordingEpoch=value.initializedQpc=100; value.sampleQpc=110; value.qpcFrequency=1000;
  value.blinkStatus=value.glyphStatus=value.resourceStatus=WKE_CACHE_READY;
  value.resourceByteLimit=value.resourceEffectiveSingleLimit=1000; return value;
}
unsigned int WKE_CACHE_CALL fake_sample(wkeCacheSnapshot*) { return WKE_CACHE_UNAVAILABLE; }
void exact_optional_api() {
  wkeCacheDiagnosticsApi api{sizeof(api),1,7,&fake_sample};
  require(detail::cache_api_supported(&api),"valid optional table rejected");
  for(unsigned i=0;i!=4;++i) {
    auto bad=api; if(i==0)--bad.structSize; if(i==1)++bad.abiVersion;
    if(i==2)bad.capabilities=3; if(i==3)bad.sampleExistingCaches=nullptr;
    require(!detail::cache_api_supported(&bad),"invalid optional table accepted");
  }
  require(!detail::cache_api_supported(nullptr),"absent table accepted");
}
void single_flight_reject_retry_and_late_completion() {
  detail::cache_sample_slot slot;
  const auto first=slot.begin(); require(first!=0&&slot.inflight(),"begin failed");
  require(slot.begin()==0,"duplicate admitted");
  require(slot.finish(first)&&!slot.inflight(),"rejected/failed post did not release token");
  const auto retry=slot.begin(); require(retry>first,"retry reused token");
  require(!slot.finish(first)&&slot.inflight(),"old completion consumed new admission");
  slot.retire(); require(!slot.inflight()&&!slot.finish(retry)&&slot.begin()==0,"close admitted late write");
}
void bound_identity_and_clock_failures_latch() {
  for(unsigned i=0;i!=8;++i) {
    detail::cache_identity_binding binding; auto value=fixture();
    require(binding.observe(value,9,123),"initial identity bind failed");
    if(i==0)value.processCreationFileTime++; if(i==1)value.engineOwnerThreadId++;
    if(i==2)value.recordingEpoch++; if(i==3)value.qpcFrequency++;
    if(i==4)value.sampleQpc--; if(i==5)value.structSize--;
    if(i==6)value.blinkStatus=4; if(i==7)value.resourceEffectiveSingleLimit++;
    require(!binding.observe(value,9,123)&&binding.invalid(),"malformed sample did not latch");
    require(!binding.observe(fixture(),9,123),"invalid binding silently recovered");
  }
}
void partial_usage_and_soft_policy_changes_are_data() {
  detail::cache_identity_binding binding; auto value=fixture();
  value.blinkLiveBytes=10000; value.blinkCapacity=1; value.glyphAttachedBytes=10000;
  require(binding.observe(value,9,123),"soft target overage became invalid data");
  value.blinkLiveBytes=1; value.glyphAttachedBytes=1; value.glyphByteLimit=1;
  value.glyphStatus=WKE_CACHE_BUSY; value.resourceStatus=WKE_CACHE_NOT_STARTED;
  require(binding.observe(value,9,123),"partial/decreasing sample rejected");
  value.resourceStatus=WKE_CACHE_READY; value.resourceDiscardable=1;
  value.resourceByteLimit=value.resourceRawSingleLimit=value.resourceEffectiveSingleLimit=0;
  require(binding.observe(value,9,123),"discardable default was interpreted as zero-byte cap");
}
void completed_copy_preserves_u64_and_retired_nonzero_data() {
  cache_observation value; value.state=cache_status::unavailable; value.native=fixture();
  value.has_completed_sample=true; value.runtime_retired=true;
  value.native.glyphAttachedBytes=UINT64_MAX;
  const auto serialized=soui::acceptance::metrics::serialize(value);
  require(serialized.at("glyphAttachedBytes")=="18446744073709551615","u64 lost precision");
  require(serialized.at("sampleQpc")=="110"&&serialized.at("runtimeRetired")==true,
      "retired copy changed original sampling time");
  require(serialized.at("state")=="unavailable"&&serialized.at("hasCompletedSample")==true,
      "stale completed copy was falsely classified fresh");
}
} // namespace
static_assert(sizeof(wkeCacheSnapshot)==216);
static_assert(std::is_nothrow_move_assignable<cache_observation>::value);
static_assert(sizeof(wkeCacheDiagnosticsApi)==24);
static_assert(offsetof(wkeCacheSnapshot,sampleQpc)==56);
static_assert(offsetof(wkeCacheSnapshot,blinkStatus)==72);
static_assert(offsetof(wkeCacheSnapshot,glyphStatus)==120);
static_assert(offsetof(wkeCacheSnapshot,resourceStatus)==160);
static_assert(offsetof(wkeCacheSnapshot,resourceEffectiveSingleLimit)==208);
static_assert(sizeof(wkeRendererProcessSnapshot)==168);
static_assert(sizeof(wkeRendererModelSnapshot)==264);
int main() {
  try {
    exact_optional_api(); single_flight_reject_retry_and_late_completion();
    bound_identity_and_clock_failures_latch(); partial_usage_and_soft_policy_changes_are_data();
    completed_copy_preserves_u64_and_retired_nonzero_data();
    std::cout<<"5 cache consumer data groups passed (fixtures only)\n";return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
