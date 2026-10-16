'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const file=__dirname+'/proposal/sdk/accessibility/uia/windows_uia_provider.cpp';
let s=fs.readFileSync(file,'utf8');
function replace(a,b){assert(s.includes(a),a.slice(0,100));s=s.replace(a,b);}
replace('decltype(&UiaGetReservedNotSupportedValue) unsupported{};',`decltype(&UiaGetReservedNotSupportedValue) unsupported{};
  decltype(&UiaRaiseAutomationPropertyChangedEvent) raise_property{};
  decltype(&UiaRaiseStructureChangedEvent) raise_structure{};
  decltype(&UiaRaiseAutomationEvent) raise_event{};`);
replace('return result->return_raw && result->host_from_window && result->unsupported ? result : nullptr;',`result->raise_property = reinterpret_cast<decltype(raise_property)>(GetProcAddress(result->module, "UiaRaiseAutomationPropertyChangedEvent"));
    result->raise_structure = reinterpret_cast<decltype(raise_structure)>(GetProcAddress(result->module, "UiaRaiseStructureChangedEvent"));
    result->raise_event = reinterpret_cast<decltype(raise_event)>(GetProcAddress(result->module, "UiaRaiseAutomationEvent"));
    return result->return_raw && result->host_from_window && result->unsupported && result->raise_property &&
      result->raise_structure && result->raise_event ? result : nullptr;`);
replace('bool cleanup_in_progress{};','bool cleanup_in_progress{};\n  bool notification_wake_pending{};');
replace('}  // namespace\n\nstruct windows_uia_provider::impl final {',fs.readFileSync(__dirname+'/notifications.inc','utf8')+'\n}  // namespace\n\nstruct windows_uia_provider::impl final {');
replace('dispatcher dispatch;\n  element* root{};','dispatcher dispatch;\n  notification_batch notifications;\n  std::shared_ptr<const snapshot> notified;\n  element* root{};');
replace('state->shared->view = std::move(initial);','state->shared->view = std::move(initial);\n    state->notified = state->shared->view;');
replace('shared->next_incarnation = serial; shared->view = std::move(next); return S_OK;',`if (!shared->notification_wake_pending) {
      if (!PostMessageW(shared->window, shared->wake, static_cast<WPARAM>(shared->instance), 0)) return HRESULT_FROM_WIN32(GetLastError());
      shared->notification_wake_pending = true;
    }
    shared->next_incarnation = serial; shared->view = std::move(next); return S_OK;`);
replace('if (shared->pending.empty()) return S_OK;','if (shared->pending.empty()) break;');
replace('if (closing) return close();\n    // Additional batches',`if (closing) return close();
    const auto notified = drain_notifications(shared, state_->notifications, state_->notified);
    { std::lock_guard<std::mutex> held(shared->lock); closing = shared->closing; }
    if (closing) return close();
    if (FAILED(notified)) return notified;
    // Additional batches`);
replace('result = pump(); return true;',`{ std::lock_guard<std::mutex> held(state_->shared->lock); state_->shared->notification_wake_pending = false; }
    result = pump(); return true;`);
replace('state_->allow = {}; state_->dispatch = {};','state_->allow = {}; state_->dispatch = {};\n    state_->notifications = {}; state_->notified.reset();');
fs.writeFileSync(file,s);
