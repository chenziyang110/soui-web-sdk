#include <soui/diagnostics/renderer_model_binding.h>
#include "../acceptance-app/native-owner-metrics.h"
#include <cstddef>
#include <iostream>
#include <stdexcept>

namespace {
using soui::sdk::diagnostics::detail::renderer_model_binding;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
wkeRendererModelSnapshot fixture() {
  wkeRendererModelSnapshot value{};
  value.structSize=sizeof(value); value.abiVersion=2; value.status=1; value.flags=19;
  value.providerId=1; value.backendId=1; value.factoryCoverageMask=127;
  value.backendInitializationQpc=90;
  auto& raw=value.compositor;
  raw.structSize=sizeof(raw); raw.abiVersion=1; raw.status=1; raw.flags=0x27; raw.lastDrawKind=1;
  raw.processId=9; raw.processCreationFileTime=123; raw.engineOwnerThreadId=10;
  raw.qpcFrequency=1000; raw.recordingEpoch=raw.initializedQpc=100;
  raw.renderersCreatedTotal=raw.renderersLive=1; raw.drawEventsTotal=raw.rasterDrawsTotal=1;
  raw.lastDrawQpc=110; raw.lastDrawThreadId=11; raw.lastDrawProcessId=9;
  return value;
}
unsigned int __cdecl fixture_read(wkeRendererModelSnapshot*) { return WKE_RENDER_DIAGNOSTICS_NOT_STARTED; }
void exact_api_and_envelope() {
  wkeRendererModelDiagnosticsApi api{sizeof(api),2,7,&fixture_read};
  require(soui::sdk::diagnostics::detail::renderer_model_api_supported(&api),"v2 table rejected");
  for(unsigned field=0;field!=4;++field) {
    auto bad=api; if(field==0)bad.structSize--; if(field==1)bad.abiVersion=1;
    if(field==2)bad.capabilities=3; if(field==3)bad.readProcessModel=nullptr;
    require(!soui::sdk::diagnostics::detail::renderer_model_api_supported(&bad),"incomplete API accepted");
  }
  renderer_model_binding binding; require(binding.observe(fixture(),9,123),"valid bound envelope rejected");
}
void identity_epoch_and_counter_faults_latch() {
  for(unsigned scenario=0;scenario!=7;++scenario) {
    renderer_model_binding binding; auto value=fixture(); require(binding.observe(value,9,123),"initial bind failed");
    if(scenario==0)value.compositor.processCreationFileTime++;
    if(scenario==1)value.backendInitializationQpc++;
    if(scenario==2)value.compositor.recordingEpoch++;
    if(scenario==3)value.flags|=WKE_RENDER_MODEL_COUNTER_FAULT;
    if(scenario==4)value.factoryCoverageMask=63;
    if(scenario==5)value.compiledGpuSupport=WKE_RENDER_MODEL_GPU_UNKNOWN;
    if(scenario==6)value.providerId=2;
    require(!binding.observe(value,9,123)&&binding.invalid(),"model mismatch did not latch");
    require(!binding.observe(fixture(),9,123),"mismatch was silently reset");
  }
}
void factory_history_remains_data_not_software() {
  renderer_model_binding binding; auto value=fixture();
  value.compiledGpuSupport=1; value.mediaFactoryAttempts=2;
  require(binding.observe(value,9,123),"real GPU support/factory diagnostic was hidden");
  value.mediaFactoryAttempts=1;
  require(!binding.observe(value,9,123),"factory history regression accepted");
}
void terminal_is_one_shot_and_requires_actual_retirement() {
  renderer_model_binding binding; auto value=fixture(); require(binding.observe(value,9,123),"bind failed");
  value.status=value.compositor.status=4; value.compositor.renderersClosedTotal=1; value.compositor.renderersLive=0;
  value.compositor.flags|=8; value.compositor.lastRendererCloseQpc=value.compositor.closedSnapshotQpc=120;
  value.compositor.engineFinalizedQpc=130;
  require(binding.observe(value,9,123),"terminal observation failed");
  require(!binding.retain_terminal(value,false),"unretired terminal retained");
  require(binding.retain_terminal(value,true),"real terminal not retained");
  value.compositor.engineFinalizedQpc=999;
  require(binding.retain_terminal(value,true)&&binding.terminal().compositor.engineFinalizedQpc==130,
      "repeated close/read replaced original terminal time");
}
void serialized_u64_is_exact_and_unknown_is_not_zero() {
  soui::sdk::diagnostics::renderer_model_observation observed;
  observed.native=fixture(); observed.native.mediaFactoryAttempts=18446744073709551615ULL;
  observed.state=soui::sdk::diagnostics::renderer_status::ready;
  const auto json=soui::acceptance::metrics::serialize(observed);
  require(json.at("mediaFactoryAttempts")=="18446744073709551615","u64 serialized imprecisely");
  observed.native.compiledGpuSupport=WKE_RENDER_MODEL_GPU_UNKNOWN;
  observed.state=soui::sdk::diagnostics::renderer_status::unknown;
  const auto unknown=soui::acceptance::metrics::serialize(observed);
  require(unknown.at("state")=="unknown"&&unknown.at("compiledGpuSupport")==4294967295U,
      "unknown support became known software zero");
}
} // namespace
static_assert(sizeof(wkeRendererProcessSnapshot)==168);
static_assert(sizeof(wkeRendererModelSnapshot)==264);
static_assert(offsetof(wkeRendererModelSnapshot,compositor)==16);
static_assert(offsetof(wkeRendererModelSnapshot,unknownRouteTotal)==256);
int main() {
  try { exact_api_and_envelope(); identity_epoch_and_counter_faults_latch();
    factory_history_remains_data_not_software(); terminal_is_one_shot_and_requires_actual_retirement();
    serialized_u64_is_exact_and_unknown_is_not_zero(); }
  catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
  return 0;
}
