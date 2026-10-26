#pragma once

#include <soui/diagnostics/input_frame_trace.h>

// Called cold on the engine owner before initialization can register producers.
// An unavailable requested trace invalidates diagnostics, not product behavior.
inline int prepare_input_ui_trace(HMODULE module) noexcept {
  namespace trace = soui::sdk::diagnostics::input_frame_trace;
  wkeGetInputUiApi = reinterpret_cast<FN_wkeGetInputUiApi>(
      GetProcAddress(module, "wkeGetInputUiApi"));
  const auto source = trace::process_recorder().snapshot();
  if (!(source.session.run_id_high || source.session.run_id_low))
    return WKE_INPUT_UI_OK; // No detailed/correlated capture was requested.
  const auto* api = wkeGetInputUiApi ? wkeGetInputUiApi(WKE_INPUT_UI_ABI_VERSION) : nullptr;
  if (!source.available || !api || api->abiVersion != WKE_INPUT_UI_ABI_VERSION
      || api->structSize < offsetof(wkeInputUiApi, serviceOwnerOpportunity)
      || !(api->capabilities & WKE_INPUT_UI_CAP_TRACE) || !api->traceConfigure) {
    trace::process_recorder().note_causal_loss();
    return WKE_INPUT_UI_UNAVAILABLE;
  }
  const wkeInputUiTraceConfig config = {sizeof(wkeInputUiTraceConfig), WKE_INPUT_UI_ABI_VERSION,
      static_cast<unsigned>(source.recording_mode), source.session.process_id,
      source.session.run_id_high, source.session.run_id_low,
      source.session.process_incarnation, source.session.qpc_frequency};
  const auto result = api->traceConfigure(&config);
  if (result != WKE_INPUT_UI_OK)
    trace::process_recorder().note_causal_loss();
  return result;
}
