'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const childProcess=require('node:child_process');
const profile=require('../../../benchmarks/protocol/input-ui-profile.cjs');
const measurement=require('../../../tools/internal/measurement-validation.cjs');
const repo=path.resolve(__dirname,'../../..');
const clone=value=>structuredClone(value);
const BUDGET='SOUI_ENGINE_SHARED_TURN_BUDGET_US';
const gpu={SOUI_GPU_COMPOSITOR:null,SOUI_GPU_DIRECT_PRESENT:null};
function request() {
  return {schema:'soui.input-ui-profile-request/v1',experiment_id:'manual-budget',architecture:'x64',
    profile:{engine_budget_us:1000},input_mode:'manual',trace_mode:'e2e',host_executable:'',sdk_library:'',
    sdk_header:'',sdk_raw_version:'0.7.0-preview.1',sdk_package_version:'0.7.0',miniblink_binary:'',
    host_build_evidence:'',runtime_root:'',page_root:'',measurement_owner:'',owned_runner:'',source_commit:'a'.repeat(40),
    measurement_source_commit:'a'.repeat(40),source_manifest:'',environment:{...gpu},
    configuration:{window_width:1100,window_height:720,device_pixel_ratio:1,instruction:'Activate manually; move pointer for 30 seconds; close.'},
    workload:{kind:'pointer_move',requested_window_ms:30000,deterministic_replay:false},required_sources:['sdk','kernel']};
}
const id={version:1,run_id:'a'.repeat(32),process_id:42,process_incarnation:'133000000000000001',qpc_frequency:'10000000'};
function scalar(final=false) {
  return {session:clone(id),mode:'scalar',registered_threads:1,active_producers:final?0:1,pending_registrations:0,
    attempted:final?'500':'2',recorded:'0',dropped:'0',queued:'0',unregistered_writes:'0',registration_rejections:'0',causal_lost:'0',
    stopped:final,quiescent:final,available:true,counter_saturated:false,owner_violation:false,invalid_event:false,clock_invalid:false,
    stages:Array(31).fill(final?'3':'0'),counters:Array(16).fill(final?'9007199254740993':'0'),
    durations:Array.from({length:15},()=>Array(64).fill('0'))};
}
function standard(snapshot) {
  const end={event:'trace.end',complete:true};
  for(const key of ['registered_threads','active_producers','attempted','recorded','dropped','queued','unregistered_writes',
    'registration_rejections','causal_lost','counter_saturated','owner_violation','invalid_event','clock_invalid','stages','counters','durations'])end[key]=clone(snapshot[key]);
  return [{event:'trace.begin',schema:'soui.input-frame-trace/v1',...clone(id),mode:'scalar',maximum_threads:32,events_per_thread:1024},end];
}
function artifacts() {
  const diagnostics={suppress_focus:false,borderless:false,foreground:false,logging:false};
  const mode={input_mode:'manual',deterministic_replay:false,measurement_path:'public-sdk',diagnostics,
    compiled_sdk_version:'0.7.0-preview.1',compiled_sdk_package_version:'0.7.0'};
  const cold={schema:'soui.input-ui-cold-profile/v1',requested_budget_us:1000,observed_env:'1000',engine_effective_observed:false,
    environment:{[BUDGET]:'1000',...gpu}};
  const a=scalar(),b=scalar(true);b.durations[0][63]='2';b.durations[1][2]='3';
  return {subject:{pid:42,creationFileTime:id.process_incarnation},
    ready:{schema:'soui.input-ui-host-ready/v1',run_id:id.run_id,pid:42,creationFileTime:id.process_incarnation,hwnd:'123',qpc_frequency:id.qpc_frequency,...clone(mode)},
    result:{schema:'soui.input-ui-host-result/v1',run_id:id.run_id,process_id:42,process_incarnation:id.process_incarnation,started_qpc:'1',ended_qpc:'7',
      normal_runtime_close:true,scalar_io_complete:true,scalar_usable:true,performanceGate:false,releaseReady:false,...clone(mode)},cold,
    initial:{schema:'soui.input-ui-scalar-boundary/v1',phase:'initial',scope:'whole-process-lifetime',identity:clone(id),
      capture_begin_qpc:'2',capture_end_qpc:'3',cold_profile:clone(cold),sources:{sdk:{available:true,snapshot:clone(a)},kernel:{available:true,backend_status:'loaded',snapshot:clone(a)}}},
    status:{schema:'soui.input-ui-scalar-status/v1',mode:'scalar',scope:'whole-process-lifetime',identity:clone(id),initial_file:'scalar-initial.json',
      capture_begin_qpc:'5',capture_end_qpc:'6',sources:Object.fromEntries(['sdk','kernel'].map(name=>[name,{available:true,persisted:true,usable:true,reasons:[],final_file:'scalar-'+name+'-final.ndjson',final_snapshot:clone(b)}])),
      io_complete:true,all_requested_sources_usable:true,full_causal_eligible:false,physical_latency_measured:false},
    streams:{sdk:standard(b),kernel:standard(b)}};
}
test('manual scalar delta preserves uint64 and does not pretend attempted equals recorded or physical latency',()=>{
  const result=profile.normalizeArtifacts(request(),artifacts());
  assert.equal(result.sources.sdk.counters[0],'9007199254740993');
  assert.equal(result.sources.sdk.attempted,'498');assert.equal(result.foreground_validity,'unknown');
  assert.equal(result.performanceGate,false);assert.equal(result.physical_latency_measured,false);
  assert.deepEqual(result.sources.sdk.duration_bounds[0].p95,{bucket:63,upper_ticks:null,unbounded:true,upper_ms:null});
  assert.equal(result.sources.sdk.duration_bounds[1].p95.upper_ticks,'7');
});
function manualArtifacts() {
  const value=artifacts(),capability=profile.MANUAL_REPORT_CAPABILITY;
  value.ready.manual_report_capability=capability;
  Object.assign(value.result,{manual_report_capability:capability,manual_report_submission:'accepted',manual_report_persisted:true});
  value.manual={schema:'soui.input-ui-manual-result/v1',capability,run_id:id.run_id,process_id:id.process_id,
    process_incarnation:id.process_incarnation,qpc_frequency:id.qpc_frequency,submission:'accepted',accepted_begin_qpc:'4',accepted_end_qpc:'4',
    snapshot_scope:'user-requested-page-snapshot',foreground_validity:'unknown',workload_comparable:'unknown',unique_frames:null,
    physical_latency_measured:false,performanceGate:false,releaseReady:false,
    page_snapshot:{schema:'soui.manual-input-page/v1',run_id:id.run_id,requested_kind:'pointer_move',
      counts:{pointer:3,wheel:0,renders:1,invalid:1},position:{x:20,y:30},wheel:{deltaX:null,deltaY:null,deltaMode:null,legacyWheelDelta:null},
      lifecycle:{blur:1,hidden:1,resize:1,focus_at_start:false},overflow:false,geometry_invalid:true,render_pending:true,
      device_pixel_ratio:1,viewport:{width:1100,height:720},deterministic_replay:false,native_input_ids_available:false,
      physical_latency_measured:false,foreground_validity:'unknown',workload_comparable:'unknown'}};
  return value;
}
test('new manual report retains actual invalid lifecycle and pending render observations; legacy stays unknown',()=>{
  const value=profile.normalizeArtifacts(request(),manualArtifacts());
  assert.equal(value.manual_page.persisted,true);assert.equal(value.manual_page.page_snapshot.lifecycle.blur,1);
  assert.equal(value.manual_page.page_snapshot.geometry_invalid,true);assert.equal(value.manual_page.page_snapshot.render_pending,true);
  assert.equal(value.foreground_validity,'unknown');assert.equal(value.performanceGate,false);assert.equal(value.manual_page.unique_frames,null);
  assert.equal(profile.normalizeArtifacts(request(),artifacts()).manual_page.submission,'unknown');
  const absent=manualArtifacts();Object.assign(absent.result,{manual_report_submission:'unsubmitted',manual_report_persisted:false});
  Object.assign(absent.manual,{submission:'unsubmitted',page_snapshot:null,accepted_begin_qpc:null,accepted_end_qpc:null});
  assert.equal(profile.normalizeArtifacts(request(),absent).manual_page.page_snapshot,null);
  assert.equal(profile.artifactFiles(absent.ready).manual,'manual-input-result.json');
});
for(const [name,mutate] of [
  ['native run',a=>{a.manual.run_id='b'.repeat(32);}],['native PID',a=>{a.manual.process_id=43;}],
  ['native creation',a=>{a.manual.process_incarnation='133000000000000002';}],['native frequency',a=>{a.manual.qpc_frequency='1';}],
  ['accepted but not persisted',a=>{a.result.manual_report_persisted=false;}],['late acceptance clock',a=>{a.manual.accepted_end_qpc='8';}],
  ['partial capability downgrade',a=>{delete a.ready.manual_report_capability;}],['unknown capability',a=>{a.result.manual_report_capability='future';}],
  ['missing new artifact',a=>{delete a.manual;}],['page chose another run',a=>{a.manual.page_snapshot.run_id='b'.repeat(32);}],
  ['extra secret',a=>{a.manual.token='secret';}],['counter overflow',a=>{a.manual.page_snapshot.counts.pointer=2147483648;}],
  ['nonfinite position',a=>{a.manual.page_snapshot.position.x=Infinity;}],['missing pending flag',a=>{delete a.manual.page_snapshot.render_pending;}],
  ['forged foreground',a=>{a.manual.page_snapshot.foreground_validity='valid';}],['unsubmitted fake page',a=>{a.manual.submission='unsubmitted';a.result.manual_report_submission='unsubmitted';a.result.manual_report_persisted=false;}]
])test('manual report rejects '+name,()=>{const a=manualArtifacts();mutate(a);assert.throws(()=>profile.normalizeArtifacts(request(),a));});
test('legacy artifacts cannot acquire a page report by dropping the native capability',()=>{
  const a=manualArtifacts();delete a.ready.manual_report_capability;delete a.result.manual_report_capability;
  delete a.result.manual_report_submission;delete a.result.manual_report_persisted;
  assert.throws(()=>profile.normalizeArtifacts(request(),a));assert.throws(()=>profile.artifactFiles({manual_report_capability:'future'}));
});
for(const [name,mutate] of [
  ['wrong process incarnation',a=>{a.initial.identity.process_incarnation='133000000000000002';}],
  ['QPC frequency mismatch',a=>{a.streams.sdk[0].qpc_frequency='1000';}],
  ['unknown kernel source',a=>{a.initial.sources.kernel={available:false,backend_status:'api_unavailable',snapshot:null};}],
  ['missing bucket',a=>{a.initial.sources.sdk.snapshot.durations[0].pop();}],
  ['uint64 Number',a=>{a.status.sources.sdk.final_snapshot.attempted=500;}],
  ['counter reversal',a=>{a.initial.sources.sdk.snapshot.counters[0]='9007199254740994';}],
  ['fault flag despite exporter complete',a=>{a.status.sources.sdk.final_snapshot.owner_violation=true;}],
  ['source not quiescent',a=>{a.status.sources.sdk.final_snapshot.quiescent=false;}],
  ['pending registration',a=>{a.status.sources.sdk.final_snapshot.pending_registrations=1;}],
  ['held snapshot disagrees with final trace',a=>{a.streams.sdk[1].counters[0]='1';}],
  ['initial source identity wrong',a=>{a.initial.sources.kernel.snapshot.session.run_id='b'.repeat(32);}],
  ['write failure',a=>{a.status.sources.sdk.persisted=false;}],
  ['failed final Host status',a=>{a.result.scalar_io_complete=false;}],
  ['nonmanual mode',a=>{a.ready.input_mode='burst-protocol';}],
  ['old EXE ready raw version',a=>{a.ready.compiled_sdk_version='0.6.0';}],
  ['old EXE result raw version',a=>{a.result.compiled_sdk_version='0.6.0';}],
  ['compiled numeric version mismatch',a=>{a.result.compiled_sdk_package_version='0.6.0';}],
  ['missing compiled EXE version',a=>{delete a.ready.compiled_sdk_version;}],
  ['foreground intervention',a=>{a.ready.diagnostics.foreground=true;}],
  ['wrong cold readback',a=>{a.cold.observed_env='2000';}],
  ['invented effective getter',a=>{a.cold.engine_effective_observed=true;}],
  ['boundary outside process lifetime',a=>{a.result.ended_qpc='4';}],
  ['unknown cold field',a=>{a.cold.token='unrelated';}]
])test('reject '+name,()=>{const value=artifacts();mutate(value);assert.throws(()=>profile.normalizeArtifacts(request(),value));});

test('child environment is a whitelist and only budget changes across profiles',()=>{
  const input=request(),parent={SystemRoot:'C:\\Windows',TEMP:'C:\\Temp',Path:'uncontrolled',SOUI_GPU_COMPOSITOR:'1',SOUI_OTHER_SWITCH:'1',TOKEN:'secret',NODE_OPTIONS:'--require bad'};
  const first=profile.childEnvironment(input,parent); input.profile.engine_budget_us=0;const second=profile.childEnvironment(input,parent);
  assert.deepEqual(Object.keys(first).sort(),['Path','SystemRoot','TEMP',BUDGET].sort());
  assert.equal(first[BUDGET],'1000'); assert.equal(second[BUDGET],'0'); delete first[BUDGET];delete second[BUDGET];assert.deepEqual(first,second);
});
test('formal request rejects unknown settings, full trace, invalid budget and GPU configuration cleared by Host',()=>{
  for(const mutate of [r=>r.extra=true,r=>r.profile.engine_budget_us=500,r=>r.trace_mode='full',r=>r.environment.SOUI_GPU_COMPOSITOR='1',r=>r.workload.deterministic_replay=true,r=>r.sdk_package_version='0.6.0']) {
    const value=request();mutate(value);assert.throws(()=>profile.template(value));
  }
});
test('arguments retain public manual path without diagnostic or injector flags',()=>{
  const input=request();input.miniblink_binary=path.resolve('kernel.dll');input.page_root=path.resolve('page');
  const argv=profile.hostArguments(input,path.resolve('output'),'a'.repeat(32));
  assert.equal(argv.length,8);assert.ok(argv.includes('--manual-input'));assert.ok(argv.includes('--engine-budget=1000'));
  assert.ok(argv.every(arg=>!arg.includes('diagnostic')));assert.ok(argv.includes('--trace=e2e'));
});
test('startup deadline uses one monotonic clock even when wall-clock access is unavailable',async t=>{
  const {performance}=require('node:perf_hooks');let calls=0;
  t.mock.method(performance,'now',()=>calls++===0?100:30101);
  t.mock.method(Date,'now',()=>{throw new Error('Wall clock must not control startup deadline');});
  const {waitReady}=require('../../../benchmarks/protocol/launch-input-ui-profile.cjs');
  await assert.rejects(waitReady(path.resolve('unused'),{}, {finished:new Promise(()=>{})},request()),/startup deadline/);
  assert.equal(calls,2);
});
test('exclusive native ready publication is retried without accepting bad JSON/identity',async t=>{
  const {waitReady}=require('../../../benchmarks/protocol/launch-input-ui-profile.cjs');
  const owned=require('../../../benchmarks/protocol/owned-trace-subject.cjs');let reads=0;
  const value=artifacts();value.ready.manual_report_capability=profile.MANUAL_REPORT_CAPABILITY;
  t.mock.method(owned,'readRunJson',()=>{if(reads++===0)throw Object.assign(new Error('Writer still owns the file'),{code:'EACCES'});return value.ready;});
  assert.deepEqual(await waitReady(path.resolve('unused'),value.subject,{finished:new Promise(()=>{})},request()),value.ready);
  assert.equal(reads,2);
  t.mock.method(owned,'readRunJson',()=>{throw new SyntaxError('Partial or malformed ready JSON');});
  await assert.rejects(waitReady(path.resolve('unused'),value.subject,{finished:Promise.resolve()},request()),/malformed/);
  t.mock.method(owned,'readRunJson',()=>({...value.ready,pid:999}));
  await assert.rejects(waitReady(path.resolve('unused'),value.subject,{finished:Promise.resolve()},request()),/identity/);
  t.mock.method(owned,'readRunJson',()=>({...value.ready,compiled_sdk_version:'0.6.0'}));
  await assert.rejects(waitReady(path.resolve('unused'),value.subject,{finished:Promise.resolve()},request()),/compiled SDK version/);
});
test('valid native ready returned by a slow synchronous read cannot pass after the same deadline',async t=>{
  const {performance}=require('node:perf_hooks'),owned=require('../../../benchmarks/protocol/owned-trace-subject.cjs');
  const {waitReady}=require('../../../benchmarks/protocol/launch-input-ui-profile.cjs');let now=100;
  const value=artifacts();value.ready.manual_report_capability=profile.MANUAL_REPORT_CAPABILITY;t.mock.method(performance,'now',()=>now);
  t.mock.method(owned,'readRunJson',()=>{now=30101;return value.ready;});
  await assert.rejects(waitReady(path.resolve('unused'),value.subject,{finished:new Promise(()=>{})},request()),/original startup deadline/);
});
test('same EXE/source profiles compare only explicit budget, and manual work never gains performance qualification',()=>{
  const make=budget=>{
    const input=request();input.schema='soui.input-ui-profile-request/v2';input.profile.engine_budget_us=budget;
    const pin={path:path.resolve('fixture-only'),bytes:1,sha256:'a'.repeat(64)};
    input.frozen_inputs={files:Object.fromEntries(['host_executable','sdk_library','sdk_header','miniblink_binary','host_build_evidence','owned_runner','source_manifest'].map(name=>[name,clone(pin)])),
      child_environment:{[BUDGET]:String(budget)},runtime:[{...pin,path:'host.exe'}],page:[{...pin,path:'manual-input-page.html'}],
      sources:{schema:'soui.input-ui-profile-source/v1',source_commit:input.source_commit,measurement_source_commit:input.measurement_source_commit,dirty:true,files:[clone(pin)]},
      measurement_sources:[clone(pin)],measurement_preparation:clone(pin),machine:{hostname:'fixture',platform:'win32',release:'fixture',architecture:'x64',cpu_model:'fixture'},
      node:{executable:path.resolve('node.exe'),version:'fixture',versions:{}}};
    return {request:input,run_id:'a'.repeat(32),...profile.normalizeArtifacts(request(),artifacts())};
  };
  const a=make(0),b=make(1000);let value=profile.compareRuns([a,b]);
  assert.equal(value.same_common_inputs,true);assert.equal(value.performanceGate,false);assert.equal(value.workload_comparable,'unknown');assert.equal(value.speedup,null);
  b.request.frozen_inputs.page[0].sha256='c'.repeat(64);value=profile.compareRuns([a,b]);assert.equal(value.same_common_inputs,false);
  assert.ok(value.differences.some(item=>item.path==='frozen_inputs.page'));
  b.request=clone(a.request);assert.equal(profile.compareRuns([a,b]).repeated_same_profile,true);
  b.request.frozen_inputs.unknownSetting='changed';assert.throws(()=>profile.compareRuns([a,b]),/unknown schema field/);
});
test('file pins are bounded and source mutation is rejected; freeze/import performs no spawn',t=>{
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'soui-profile-contract-'));
  t.after(()=>fs.rmSync(temporary,{recursive:true,force:true}));
  const put=(name,text)=>{const target=path.join(temporary,name);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,text);return target;};
  const input=request();input.runtime_root=path.join(temporary,'runtime');input.page_root=path.join(temporary,'page');input.measurement_owner=path.join(temporary,'owner');
  input.host_executable=put('runtime/host.exe','fixture only');input.miniblink_binary=put('runtime/kernel.dll','fixture only');
  input.sdk_library=put('sdk/sdk.lib','fixture only');input.sdk_header=put('sdk/version.h','#define SOUI_SDK_VERSION_STRING "0.7.0-preview.1"\n');
  input.host_build_evidence=put('build.json','{"fixture":true}\n');input.owned_runner=put('owner/runner.exe','fixture only');
  put('page/manual-input-page.html','fixture');put('page/manual-input-page.js','fixture');
  const sourceFiles=['main.cpp','trace_capture.cpp','trace_capture.h','harness_options.h','profile_options.h','manual_report.h','manual-observation-gate.h','CMakeLists.txt','sdk_version.cmake']
    .map(name=>profile.pin(path.join(repo,'benchmarks/input-ui-host',name)));
  sourceFiles.push(...['manual-input-page.html','manual-input-page.js'].map(name=>profile.pin(path.join(repo,'benchmarks/protocol',name))));
  input.source_manifest=put('source.json',JSON.stringify({schema:'soui.input-ui-profile-source/v1',source_commit:input.source_commit,
    measurement_source_commit:input.measurement_source_commit,dirty:true,files:sourceFiles}));
  const receipt=path.join(measurement.buildDirectory('x64',input.measurement_owner),'measurement-tools.json');fs.mkdirSync(path.dirname(receipt),{recursive:true});fs.writeFileSync(receipt,'{"fixture":true}\n');
  const ownerSource=profile.pin(path.join(repo,'benchmarks/protocol/owned-process-runner.cpp'));
  const ownerReport={source:{files:[{...ownerSource,path:'benchmarks/protocol/owned-process-runner.cpp'}]}};
  t.mock.method(measurement,'preparedAt',()=>({tools:{OwnedProcessRunner:input.owned_runner},report:ownerReport}));
  for(const name of ['spawn','spawnSync','exec','execFile','execSync','execFileSync'])t.mock.method(childProcess,name,()=>{throw new Error('Preparation attempted a process launch');});
  const frozen=profile.freezeRequest(input);assert.equal(frozen.schema,'soui.input-ui-profile-request/v2');
  assert.equal(frozen.frozen_inputs.sources.dirty,true);assert.equal(frozen.frozen_inputs.runtime.length,2);
  const sourceManifestBytes=fs.readFileSync(input.source_manifest),missingGate=JSON.parse(sourceManifestBytes);
  missingGate.files=missingGate.files.filter(item=>path.basename(item.path)!=='manual-observation-gate.h');
  assert.equal(missingGate.files.length,sourceFiles.length-1);
  fs.writeFileSync(input.source_manifest,JSON.stringify(missingGate));
  assert.throws(()=>profile.sourceInventory(input),/Source manifest omits current native consumer: manual-observation-gate\.h/);
  fs.writeFileSync(input.source_manifest,sourceManifestBytes);
  assert.deepEqual(profile.sourceInventory(input),frozen.frozen_inputs.sources);
  require('../../../benchmarks/protocol/launch-input-ui-profile.cjs');
  const originalSourceSha=ownerReport.source.files[0].sha256;ownerReport.source.files[0].sha256='f'.repeat(64);
  assert.throws(()=>profile.freezeRequest(input),/Prepared native tool source differs/);
  ownerReport.source.files[0].sha256=originalSourceSha;
  fs.appendFileSync(input.host_executable,'changed');assert.throws(()=>profile.verifyRequest(frozen),/Frozen profile inputs changed/);
  assert.throws(()=>profile.readRun(temporary));
});
