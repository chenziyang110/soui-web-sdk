'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const file=__dirname+'/proposal/tests/native/windows_uia_provider_tests.cpp';
let body=fs.readFileSync(file,'utf8');
assert(body.includes('bool allow{true};'));
body=body.replace('bool allow{true};','bool allow{true};\n  std::function<HRESULT()> event_mutation;');
body=body.replace('if (host && host->provider) {','if (host && message == WM_APP + 0x478 && host->event_mutation) return host->event_mutation();\n  if (host && host->provider) {');
body=body.replace('void property_events_case() {\n  fixture host;','void property_events_case() {\n  fixture host;\n  unsigned publications{};\n  host.event_mutation = [&] {\n    require(GetCurrentThreadId() == GetWindowThreadProcessId(host.window, nullptr), "actual publication owner");\n    host.projection.nodes[1].value = L"visible update"; ++publications;\n    return host.provider->publish(host.projection);\n  };');
body=body.replace('HRESULT set_result = pattern->SetValue(L"visible update");',
 'DWORD_PTR published{};\n    const bool sent = SendMessageTimeoutW(host.window, WM_APP + 0x478, 0, 0, SMTO_ABORTIFHUNG, 2000, &published) != 0;\n    const HRESULT set_result = sent ? static_cast<HRESULT>(published) : E_FAIL;');
body=body.replace('"typed native SetValue committed"','"native owner projection committed"');
body=body.replace('require(host.dispatch_count == 1 && host.dispatch_thread == GetCurrentThreadId(), "events preserve owner dispatch");','require(publications == 1, "actual native owner publication completed");');
fs.writeFileSync(file,body);
