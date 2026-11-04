'use strict';

// Manual, whole-process scalar diagnostics. This is deliberately separate from
// the deterministic BurstInput protocol: no injection/calibration is fabricated.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const os = require('node:os');
const {TextDecoder, isDeepStrictEqual: equal} = require('node:util');
const trace = require('./input-frame-trace.cjs');
const owned = require('./owned-trace-subject.cjs');
const measurement = require('../../tools/internal/measurement-validation.cjs');
const {isVersion} = require('../../tools/internal/sdk-version.cjs');
const {insist, exact, whole} = trace;
const root = path.resolve(__dirname, '../..');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const digest = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const BUDGET = 'SOUI_ENGINE_SHARED_TURN_BUDGET_US';
const GPU = ['SOUI_GPU_COMPOSITOR', 'SOUI_GPU_DIRECT_PRESENT'];
const SYSTEM = ['SystemRoot', 'WINDIR', 'ComSpec', 'TEMP', 'TMP', 'USERPROFILE', 'LOCALAPPDATA', 'APPDATA', 'ProgramData'];
const WIDE = ['attempted', 'recorded', 'dropped', 'queued', 'unregistered_writes', 'registration_rejections', 'causal_lost'];
const FAULTS = ['counter_saturated', 'owner_violation', 'invalid_event', 'clock_invalid'];
const REQUEST_KEYS = ['schema', 'experiment_id', 'architecture', 'profile', 'input_mode', 'trace_mode',
  'host_executable', 'sdk_library', 'sdk_header', 'sdk_raw_version', 'sdk_package_version', 'miniblink_binary',
  'host_build_evidence', 'runtime_root', 'page_root', 'measurement_owner', 'owned_runner',
  'source_commit', 'measurement_source_commit', 'source_manifest', 'environment', 'configuration', 'workload', 'required_sources'];
const INPUT_FILES=['host_executable','sdk_library','sdk_header','miniblink_binary','host_build_evidence','owned_runner','source_manifest'];
function keys(value, required, optional = []) {
  insist(value && typeof value === 'object' && !Array.isArray(value), 'Expected an object');
  insist(required.every(key => Object.hasOwn(value, key)) &&
    Object.keys(value).every(key => required.includes(key) || optional.includes(key)), 'Missing or unknown schema field');
}
function canonical(value) {
  if (Array.isArray(value)) return value.map(canonical);
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(k => [k, canonical(value[k])]));
  return value;
}
function stable(value) { return JSON.stringify(canonical(value)); }
function ordinary(filename) {
  insist(typeof filename === 'string' && path.isAbsolute(filename) && !filename.includes('\0'), 'Absolute file path required');
  const stat = fs.lstatSync(filename);
  insist(stat.isFile() && !stat.isSymbolicLink() && stat.nlink === 1, 'Expected an ordinary unlinked file');
  return stat;
}
function pin(filename) {
  const before = ordinary(filename);
  insist(before.size <= 1024 * 1024 * 1024, 'Pinned file exceeds 1 GiB');
  const fd = fs.openSync(filename, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    const held = fs.fstatSync(fd), state = crypto.createHash('sha256'), buffer = Buffer.allocUnsafe(65536);
    insist(held.ino === before.ino && held.dev === before.dev && held.size === before.size, 'File changed while opening');
    let count, size = 0;
    while ((count = fs.readSync(fd, buffer, 0, buffer.length, null))) {
      size += count; insist(size <= held.size, 'Pinned file grew'); state.update(buffer.subarray(0, count));
    }
    const after = fs.fstatSync(fd);
    insist(size === held.size && after.size === held.size && after.mtimeMs === held.mtimeMs, 'Pinned file changed');
    return {path: fs.realpathSync(filename), bytes: size, sha256: state.digest('hex')};
  } finally { fs.closeSync(fd); }
}
function readJson(filename) {
  const before = ordinary(filename);
  insist(before.size > 0 && before.size <= 4 * 1024 * 1024, 'Metadata size exceeds bound');
  const fd = fs.openSync(filename, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    const held = fs.fstatSync(fd);
    insist(held.ino === before.ino && held.dev === before.dev && held.size === before.size, 'Metadata changed while opening');
    return JSON.parse(new TextDecoder('utf-8', {fatal: true}).decode(trace.readHeldFile(fd, held.size)));
  } finally { fs.closeSync(fd); }
}
function tree(directory) {
  insist(path.isAbsolute(directory) && !fs.lstatSync(directory).isSymbolicLink() && fs.statSync(directory).isDirectory(), 'Plain absolute directory required');
  const files = []; let bytes = 0;
  const visit = (current, prefix, depth) => {
    insist(depth <= 16, 'Inventory directory depth exceeded');
    for (const entry of fs.readdirSync(current, {withFileTypes: true}).sort((a,b) => a.name.localeCompare(b.name))) {
      insist(!entry.isSymbolicLink(), 'Inventory contains a linked entry');
      const relative = prefix + entry.name, filename = path.join(current, entry.name);
      if (entry.isDirectory()) visit(filename, relative + '/', depth + 1);
      else {
        insist(entry.isFile() && files.length < 10000, 'Inventory contains unsupported/too many files');
        const item = pin(filename); bytes += item.bytes;
        insist(bytes <= 2 * 1024 * 1024 * 1024, 'Inventory byte bound exceeded');
        files.push({...item, path: relative});
      }
    }
  };
  visit(directory, '', 0); insist(files.length > 0, 'Empty inventory'); return files;
}
function template(request, frozen = false) {
  keys(request, REQUEST_KEYS.concat(frozen ? ['frozen_inputs'] : []), ['measurement_toolchain_request']);
  insist(request.schema === 'soui.input-ui-profile-request/' + (frozen ? 'v2' : 'v1'), 'Wrong profile request version');
  insist(/^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/.test(request.experiment_id) && ['x86','x64'].includes(request.architecture), 'Invalid experiment/architecture');
  keys(request.profile, ['engine_budget_us']);
  insist([0,1000,2000].includes(request.profile.engine_budget_us) && request.input_mode === 'manual' && request.trace_mode === 'e2e', 'Only explicit manual scalar profiles are supported');
  insist(isVersion(request.sdk_raw_version) && request.sdk_package_version === request.sdk_raw_version.split('-')[0], 'Invalid SDK raw/numeric version');
  for (const name of ['source_commit','measurement_source_commit']) insist(/^[a-f0-9]{40}$/.test(request[name]), 'Invalid declared source commit');
  keys(request.environment, GPU);
  insist(GPU.every(name => request.environment[name] === null), 'This software Host clears GPU environment; request both fields as null');
  keys(request.configuration, ['window_width','window_height','device_pixel_ratio','instruction']);
  insist(request.configuration.window_width === 1100 && request.configuration.window_height === 720 &&
    request.configuration.device_pixel_ratio === 1 && typeof request.configuration.instruction === 'string' &&
    request.configuration.instruction.length > 0 && request.configuration.instruction.length <= 4096, 'Host configuration/manual instructions differ from the protocol');
  keys(request.workload, ['kind','requested_window_ms','deterministic_replay']);
  insist(['pointer_move','wheel'].includes(request.workload.kind) && whole(request.workload.requested_window_ms,1000,120000) &&
    request.workload.deterministic_replay === false && equal(request.required_sources,['sdk','kernel']), 'Invalid manual workload/source requirements');
  if(frozen) {
    const value=request.frozen_inputs;
    keys(value,['files','runtime','page','sources','measurement_sources','measurement_preparation','child_environment','machine','node']);
    keys(value.files,INPUT_FILES);
    const descriptor=(item,relative=false)=>{
      keys(item,['path','bytes','sha256']);
      insist(typeof item.path==='string'&&item.path.length>0&&!item.path.includes('\0')&&whole(item.bytes,0,1024*1024*1024)&&digest(item.sha256),'Invalid frozen descriptor');
      if(relative)insist(!path.isAbsolute(item.path)&&!item.path.includes('\\')&&!item.path.includes(':')&&item.path.split('/').every(part=>part&&part!=='.'&&part!=='..'),'Unsafe relative inventory path');
      else insist(path.isAbsolute(item.path),'Absolute frozen descriptor required');
    };
    Object.values(value.files).forEach(item=>descriptor(item));descriptor(value.measurement_preparation);
    for(const name of ['runtime','page','measurement_sources']) {
      insist(Array.isArray(value[name])&&value[name].length>0&&value[name].length<=10000,'Invalid frozen inventory');
      const seen=new Set();for(const item of value[name]){descriptor(item,name!=='measurement_sources');const key=item.path.toLowerCase();insist(!seen.has(key),'Duplicate frozen path');seen.add(key);}
    }
    keys(value.sources,['schema','source_commit','measurement_source_commit','dirty','files']);
    insist(value.sources.schema==='soui.input-ui-profile-source/v1'&&value.sources.source_commit===request.source_commit&&
      value.sources.measurement_source_commit===request.measurement_source_commit&&typeof value.sources.dirty==='boolean'&&
      Array.isArray(value.sources.files)&&value.sources.files.length>0&&value.sources.files.length<=20000,'Invalid frozen source declaration');
    value.sources.files.forEach(item=>descriptor(item));
    keys(value.child_environment,[BUDGET],SYSTEM.concat(['Path',...GPU]));
    insist(value.child_environment[BUDGET]===String(request.profile.engine_budget_us)&&GPU.every(key=>!Object.hasOwn(value.child_environment,key)), 'Frozen child environment differs from software profile');
    insist(Object.values(value.child_environment).every(item=>typeof item==='string'&&item.length<=32768&&!item.includes('\0')),'Invalid frozen child environment value');
    keys(value.machine,['hostname','platform','release','architecture','cpu_model']);
    keys(value.node,['executable','version','versions']);
  }
}
function childEnvironment(request, parent = process.env) {
  const result = {};
  for (const key of SYSTEM) {
    const name = Object.keys(parent).find(name => name.toLowerCase() === key.toLowerCase());
    if (name !== undefined) {
      const value = parent[name]; insist(typeof value === 'string' && value.length <= 32768 && !value.includes('\0'), 'Invalid system environment');
      result[key] = value;
    }
  }
  // No inherited SOUI switches, NODE_OPTIONS, arbitrary PATH entries or secrets.
  if (result.SystemRoot) result.Path = path.join(result.SystemRoot, 'System32');
  for (const key of GPU) if (request.environment[key] !== null) result[key] = request.environment[key];
  result[BUDGET] = String(request.profile.engine_budget_us); return result;
}
function sourceInventory(request) {
  const manifest = readJson(request.source_manifest);
  keys(manifest, ['schema','source_commit','measurement_source_commit','dirty','files']);
  insist(manifest.schema === 'soui.input-ui-profile-source/v1' && typeof manifest.dirty === 'boolean' &&
    manifest.source_commit === request.source_commit && manifest.measurement_source_commit === request.measurement_source_commit &&
    Array.isArray(manifest.files) && manifest.files.length > 0 && manifest.files.length <= 20000, 'Invalid explicit dirty/source manifest');
  const seen = new Set();
  for (const item of manifest.files) {
    keys(item, ['path','bytes','sha256']); insist(digest(item.sha256) && whole(item.bytes,0,1024*1024*1024), 'Invalid source pin');
    const actual = pin(item.path), key = actual.path.toLowerCase();
    insist(!seen.has(key) && equal(actual,item), 'Duplicate or changed source pin'); seen.add(key);
  }
  for(const name of ['main.cpp','trace_capture.cpp','trace_capture.h','harness_options.h','profile_options.h','manual_report.h','manual-observation-gate.h','CMakeLists.txt','sdk_version.cmake'])
    insist(seen.has(fs.realpathSync(path.join(root,'benchmarks/input-ui-host',name)).toLowerCase()), 'Source manifest omits current native consumer: '+name);
  for(const name of ['manual-input-page.html','manual-input-page.js'])
    insist(seen.has(fs.realpathSync(path.join(__dirname,name)).toLowerCase()), 'Source manifest omits current manual page: '+name);
  // Commit/dirty provenance is a declaration by the build owner; these tools do
  // not invent a commit or authenticate that declaration from a writable file.
  return manifest;
}
function inputs(request, parentEnvironment = process.env) {
  const files = Object.fromEntries(INPUT_FILES.map(name => [name,pin(request[name])]));
  const header = fs.readFileSync(files.sdk_header.path, 'utf8');
  const versions = [...header.matchAll(/^[ \t]*#[ \t]*define[ \t]+SOUI_SDK_VERSION_STRING[ \t]+"([^"]+)"[ \t]*\r?$/gm)];
  insist(versions.length===1 && versions[0][1] === request.sdk_raw_version, 'Actual SDK header raw version differs');
  const prepared = measurement.preparedAt(request.measurement_owner,request.architecture,
    request.measurement_toolchain_request ? {toolchainRequest:request.measurement_toolchain_request} : {});
  insist(fs.realpathSync(prepared.tools.OwnedProcessRunner) === files.owned_runner.path, 'Runner differs from independently prepared owner');
  // Keep the old formal preparation's second gate: a self-consistent archived
  // owner does not permit stale native tool source against this measurement tree.
  for(const item of prepared.report.source.files) {
    const actual=pin(path.join(root,item.path));
    insist(actual.bytes===item.bytes&&actual.sha256===item.sha256,
      'Prepared native tool source differs from current measurement source: '+item.path);
  }
  const receipt = pin(path.join(measurement.buildDirectory(request.architecture,request.measurement_owner),'measurement-tools.json'));
  const sources = sourceInventory(request);
  const runtime = tree(request.runtime_root), page = tree(request.page_root);
  for(const name of ['host_executable','miniblink_binary'])
    insist(runtime.some(item => path.resolve(fs.realpathSync(request.runtime_root),item.path).toLowerCase() === files[name].path.toLowerCase()), 'Host/kernel is outside frozen runtime closure');
  insist(page.some(item => item.path === 'manual-input-page.html') && page.some(item => item.path === 'manual-input-page.js'), 'Missing independent manual input page');
  const modules = [__filename, path.join(__dirname,'launch-input-ui-profile.cjs'),
    ...fs.readdirSync(__dirname).filter(name => name.endsWith('.cjs')).map(name => path.join(__dirname,name)),
    ...fs.readdirSync(path.join(root,'tools/internal')).filter(name => name.endsWith('.cjs')).map(name => path.join(root,'tools/internal',name))];
  const measurementSources = [...new Set(modules)].sort().map(pin);
  return {files, runtime, page, sources, measurement_sources:measurementSources, measurement_preparation:receipt,
    child_environment:childEnvironment(request,parentEnvironment),
    machine:{hostname:os.hostname(),platform:os.platform(),release:os.release(),architecture:os.arch(),cpu_model:os.cpus()[0]?.model || null},
    node:{executable:process.execPath,version:process.version,versions:{...process.versions}}};
}
function freezeRequest(request) { template(request); return {...request,schema:'soui.input-ui-profile-request/v2',frozen_inputs:inputs(request)}; }
function verifyRequest(request) {
  template(request,true); insist(equal(inputs(request),request.frozen_inputs), 'Frozen profile inputs changed; retain this request and prepare a new one'); return request;
}
function hostArguments(request, output, runId) {
  insist(path.isAbsolute(output) && /^[a-f0-9]{32}$/.test(runId) && !/^0+$/.test(runId), 'Invalid output/run identity');
  return ['--miniblink='+request.miniblink_binary,'--page-root='+request.page_root,'--result-dir='+output,
    '--run='+runId,'--kind='+request.workload.kind,'--trace=e2e','--manual-input','--engine-budget='+request.profile.engine_budget_us];
}
function identity(value) {
  keys(value,['version','run_id','process_id','process_incarnation','qpc_frequency']);
  insist(value.version === 1 && /^[a-f0-9]{32}$/.test(value.run_id) && !/^0+$/.test(value.run_id) && whole(value.process_id,1), 'Invalid scalar identity');
  exact(value.process_incarnation,false); exact(value.qpc_frequency,false); return value;
}
function snapshot(value, expected, final) {
  keys(value,['session','mode','registered_threads','active_producers','pending_registrations',...WIDE,
    'stopped','quiescent','available',...FAULTS,'stages','counters','durations']);
  identity(value.session); trace.sameIdentity(value.session,expected);
  insist(value.mode === 'scalar' && value.available === true && whole(value.registered_threads,0,32) &&
    whole(value.active_producers,0,32) && whole(value.pending_registrations,0,32), 'Unavailable/non-scalar snapshot');
  for (const key of WIDE) exact(value[key]);
  insist(value.recorded === '0' && value.queued === '0' && ['dropped','unregistered_writes','registration_rejections','causal_lost'].every(k=>value[k]==='0'), 'Scalar source lost data');
  insist(FAULTS.every(k=>value[k]===false) && ['stopped','quiescent'].every(k=>typeof value[k]==='boolean'), 'Scalar source fault/missing flag');
  if (!final) insist(!value.stopped, 'Initial source was already stopped');
  insist(value.active_producers<=value.registered_threads,'Active producer count exceeds registered threads');
  if (final) insist(value.stopped && value.quiescent && value.active_producers === 0 && value.pending_registrations === 0, 'Final source is not quiescent');
  for (const [name,count] of [['stages',31],['counters',16],['durations',15]]) insist(Array.isArray(value[name]) && value[name].length===count, 'Wrong scalar dimensions');
  for (const item of [...value.stages,...value.counters]) exact(item);
  for (const buckets of value.durations) { insist(Array.isArray(buckets)&&buckets.length===64,'Wrong histogram dimensions'); for(const item of buckets)exact(item); }
  return value;
}
function subtract(before, after) { return after.map((value,i) => { const delta=exact(value)-exact(before[i]); insist(delta>=0n,'Scalar counter reversed'); return String(delta); }); }
function histogram(buckets, frequency) {
  const counts=buckets.map(value=>exact(value)), total=counts.reduce((a,b)=>a+b,0n);
  const quantile=percent => {
    if (!total) return null;
    const rank=(total*BigInt(percent)+99n)/100n; let accumulated=0n;
    for(let bucket=0;bucket<64;bucket++) if((accumulated+=counts[bucket])>=rank) {
      const ticks=bucket===63?null:(1n<<BigInt(bucket+1))-1n;
      return {bucket,upper_ticks:ticks===null?null:String(ticks),unbounded:bucket===63,
        upper_ms: ticks===null?null:decimalMilliseconds(ticks,exact(frequency,false))};
    }
    throw new Error('Unreachable histogram rank');
  };
  return {samples:String(total),p50:quantile(50),p95:quantile(95),p99:quantile(99)};
}
function decimalMilliseconds(ticks, frequency) {
  // Decimal rounded upward to 9 places, preserving the histogram upper bound.
  const scale=1000000000n, scaled=(ticks*1000n*scale+frequency-1n)/frequency;
  return String(scaled/scale)+'.'+String(scaled%scale).padStart(9,'0');
}
function coldProfile(value, request) {
  keys(value,['schema','requested_budget_us','observed_env','engine_effective_observed','environment']);
  keys(value.environment,[BUDGET,...GPU]);
  insist(value.schema==='soui.input-ui-cold-profile/v1' && value.requested_budget_us===request.profile.engine_budget_us &&
    value.observed_env===String(request.profile.engine_budget_us) && value.engine_effective_observed===false &&
    equal(value.environment,{[BUDGET]:String(request.profile.engine_budget_us),...request.environment}), 'Actual cold environment differs from request');
}
function manual(record) {
  insist(record.input_mode==='manual' && record.deterministic_replay===false && record.measurement_path==='public-sdk', 'Not a public SDK manual run');
  keys(record.diagnostics,['suppress_focus','borderless','foreground','logging']);
  insist(Object.values(record.diagnostics).every(value=>value===false),'Diagnostic harness intervention');
}
function normalizeArtifacts(request, artifacts) {
  const {ready,result,cold,initial,status,streams,subject}=artifacts;
  manual(ready); manual(result); coldProfile(cold,request);
  for(const record of [ready,result]) insist(record.compiled_sdk_version===request.sdk_raw_version&&
    record.compiled_sdk_package_version===request.sdk_package_version,'Host compiled SDK version differs from requested header/library identity');
  insist(ready.schema==='soui.input-ui-host-ready/v1' && result.schema==='soui.input-ui-host-result/v1' &&
    ready.pid===subject.pid && ready.creationFileTime===subject.creationFileTime && result.process_id===subject.pid &&
    result.process_incarnation===subject.creationFileTime && ready.run_id===result.run_id &&
    result.normal_runtime_close===true && result.scalar_io_complete===true && result.scalar_usable===true &&
    result.performanceGate===false && result.releaseReady===false, 'Host closure/identity/scalar qualification differs');
  exact(ready.hwnd,false); exact(ready.qpc_frequency,false);
  insist(initial.schema==='soui.input-ui-scalar-boundary/v1' && initial.phase==='initial' &&
    initial.scope==='whole-process-lifetime' && status.schema==='soui.input-ui-scalar-status/v1' &&
    status.mode==='scalar' && status.scope===initial.scope && status.initial_file==='scalar-initial.json' &&
    status.io_complete===true && status.all_requested_sources_usable===true && status.full_causal_eligible===false &&
    status.physical_latency_measured===false, 'Invalid scalar boundaries/status');
  identity(initial.identity); identity(status.identity); trace.sameIdentity(initial.identity,status.identity);
  insist(initial.identity.run_id===ready.run_id && initial.identity.process_id===subject.pid &&
    initial.identity.process_incarnation===subject.creationFileTime && initial.identity.qpc_frequency===ready.qpc_frequency &&
    equal(initial.cold_profile,cold), 'Scalar boundary identity/cold profile differs');
  const ticks=[result.started_qpc,initial.capture_begin_qpc,initial.capture_end_qpc,status.capture_begin_qpc,status.capture_end_qpc,result.ended_qpc].map(value=>exact(value,false));
  insist(ticks.every((value,i)=>i===0||value>=ticks[i-1]), 'Scalar/Host boundary clock reversed');
  keys(initial.sources,['sdk','kernel']); keys(status.sources,['sdk','kernel']); keys(streams,['sdk','kernel']);
  const deltas={};
  for(const name of ['sdk','kernel']) {
    const first=initial.sources[name], last=status.sources[name];
    insist(first.available===true && last.available===true && last.persisted===true && last.usable===true &&
      equal(last.reasons,[]) && last.final_file==='scalar-'+name+'-final.ndjson', 'Requested scalar source unavailable/incomplete');
    if(name==='kernel')insist(first.backend_status==='loaded','Kernel API was unavailable');
    const a=snapshot(first.snapshot,initial.identity,false), b=snapshot(last.final_snapshot,initial.identity,true);
    const checked=trace.checkTrace(streams[name]); trace.sameIdentity(checked.begin,initial.identity);
    insist(checked.begin.mode==='scalar' && checked.loss==='0' && checked.faults.length===0,'Invalid standard scalar trace');
    for(const key of [...WIDE,...FAULTS,'registered_threads','active_producers','stages','counters','durations'])
      insist(equal(checked.end[key],b[key]),'Final held snapshot differs from standard trace: '+key);
    subtract(WIDE.map(k=>a[k]),WIDE.map(k=>b[k]));
    insist(b.registered_threads>=a.registered_threads,'Registered thread counter reversed');
    const durations=b.durations.map((values,i)=>subtract(a.durations[i],values));
    deltas[name]={attempted:String(exact(b.attempted)-exact(a.attempted)),stages:subtract(a.stages,b.stages),
      counters:subtract(a.counters,b.counters),durations,
      duration_bounds:durations.map(values=>histogram(values,initial.identity.qpc_frequency))};
  }
  const manualPage=manualPageResult(request,artifacts);
  return {identity:initial.identity,scope:'whole-process-lifetime',sources:deltas,manual_page:manualPage,
    foreground_validity:'unknown',workload_comparable:'unknown',provider_prefix_attribution:'unknown',
    physical_latency_measured:false,performanceGate:false,adoption:'pending_measurement',releaseReady:false};
}

const ARTIFACTS = {ready:'host-ready.json',result:'host-result.json',cold:'cold-profile.json',
  initial:'scalar-initial.json',status:'scalar-final-status.json',sdk:'scalar-sdk-final.ndjson',kernel:'scalar-kernel-final.ndjson'};
const MANUAL_REPORT_CAPABILITY='manual-page-snapshot-v1';
function artifactFiles(ready) {
  if(ready.manual_report_capability===undefined)return ARTIFACTS;
  insist(ready.manual_report_capability===MANUAL_REPORT_CAPABILITY,'Unknown manual report capability');
  return {...ARTIFACTS,manual:'manual-input-result.json'};
}
function manualPageSnapshot(value,request,run) {
  keys(value,['schema','run_id','requested_kind','counts','position','wheel','lifecycle','overflow','geometry_invalid',
    'device_pixel_ratio','viewport','deterministic_replay','native_input_ids_available','physical_latency_measured',
    'foreground_validity','workload_comparable','render_pending']);
  insist(value.schema==='soui.manual-input-page/v1'&&value.run_id===run&&value.requested_kind===request.workload.kind,'Page snapshot run/kind differs');
  const count=value=>Number.isSafeInteger(value)&&value>=0&&value<=2147483647;
  const number=value=>typeof value==='number'&&Number.isFinite(value)&&Math.abs(value)<=Number.MAX_SAFE_INTEGER;
  const nullable=value=>value===null||number(value);
  keys(value.counts,['pointer','wheel','renders','invalid']);insist(Object.values(value.counts).every(count),'Invalid page counters');
  keys(value.position,['x','y']);insist(Object.values(value.position).every(nullable),'Invalid page position');
  keys(value.wheel,['deltaX','deltaY','deltaMode','legacyWheelDelta']);
  insist(['deltaX','deltaY','legacyWheelDelta'].every(key=>nullable(value.wheel[key]))&&
    (value.wheel.deltaMode===null||[0,1,2].includes(value.wheel.deltaMode)),'Invalid page wheel');
  keys(value.lifecycle,['blur','hidden','resize','focus_at_start']);
  insist(['blur','hidden','resize'].every(key=>count(value.lifecycle[key]))&&
    (value.lifecycle.focus_at_start===null||typeof value.lifecycle.focus_at_start==='boolean'),'Invalid page lifecycle');
  keys(value.viewport,['width','height']);insist(Object.values(value.viewport).every(count)&&number(value.device_pixel_ratio)&&value.device_pixel_ratio>0,'Invalid page geometry');
  insist(['overflow','geometry_invalid','render_pending'].every(key=>typeof value[key]==='boolean')&&
    value.deterministic_replay===false&&value.native_input_ids_available===false&&value.physical_latency_measured===false&&
    value.foreground_validity==='unknown'&&value.workload_comparable==='unknown','Invalid page claims');
  return value;
}
function manualPageResult(request,{ready,result,manual,subject}) {
  if(ready.manual_report_capability===undefined&&result.manual_report_capability===undefined) {
    insist(manual===undefined&&result.manual_report_submission===undefined&&result.manual_report_persisted===undefined,'Legacy Host cannot claim a manual report');
    return {capability:null,submission:'unknown',persisted:false,page_snapshot:null,snapshot_scope:'legacy-unobserved'};
  }
  insist(ready.manual_report_capability===MANUAL_REPORT_CAPABILITY&&result.manual_report_capability===MANUAL_REPORT_CAPABILITY,'Manual report capabilities differ');
  keys(manual,['schema','capability','run_id','process_id','process_incarnation','qpc_frequency','submission','accepted_begin_qpc',
    'accepted_end_qpc','page_snapshot','snapshot_scope','foreground_validity','workload_comparable','unique_frames',
    'physical_latency_measured','performanceGate','releaseReady']);
  insist(manual.schema==='soui.input-ui-manual-result/v1'&&manual.capability===MANUAL_REPORT_CAPABILITY&&manual.run_id===ready.run_id&&
    manual.process_id===subject.pid&&manual.process_incarnation===subject.creationFileTime&&manual.qpc_frequency===ready.qpc_frequency&&
    manual.snapshot_scope==='user-requested-page-snapshot'&&manual.foreground_validity==='unknown'&&manual.workload_comparable==='unknown'&&
    manual.unique_frames===null&&manual.physical_latency_measured===false&&manual.performanceGate===false&&manual.releaseReady===false,'Manual report native identity/claims differ');
  insist(manual.submission===result.manual_report_submission&&['accepted','unsubmitted'].includes(manual.submission)&&
    result.manual_report_persisted===(manual.submission==='accepted'),'Manual report persistence status differs');
  if(manual.submission==='accepted') {
    const begin=exact(manual.accepted_begin_qpc,false),end=exact(manual.accepted_end_qpc,false);
    insist(begin>=exact(result.started_qpc,false)&&end>=begin&&end<=exact(result.ended_qpc,false),'Manual acceptance clock lies outside Host');
    manualPageSnapshot(manual.page_snapshot,request,ready.run_id);
  } else insist(manual.page_snapshot===null&&manual.accepted_begin_qpc===null&&manual.accepted_end_qpc===null,'Unsubmitted report fabricates page data');
  return {...manual,persisted:result.manual_report_persisted};
}
function readRun(directory) {
  insist(!fs.existsSync(path.join(directory,'input-ui-profile-check-failure.json')), 'A retained post-run failure invalidates this run');
  const record=owned.readRunJson(directory,'input-ui-profile-run.json');
  keys(record,['schema','capture_root','run_id','status','error','request','host_arguments','processes','owned_evidence',
    'artifacts','page_bundle','foreground_validity','workload_comparable','performanceGate','releaseReady']);
  insist(record.schema==='soui.input-ui-profile-run/v1' && record.status==='completed' && record.error===null &&
    path.isAbsolute(record.capture_root) && record.foreground_validity==='unknown' && record.workload_comparable==='unknown' &&
    record.performanceGate===false && record.releaseReady===false, 'Run is incomplete/failed or overclaims manual evidence');
  const verified=(descriptor,filename)=>{
    const actual=owned.describe(directory,filename); insist(equal(actual,descriptor),'Run artifact hash/path changed: '+filename);
    return owned.readRunJson(directory,filename);
  };
  const request=verified(record.request,'frozen-request.json'); template(request,true);
  // Historical result reading uses held evidence, not today's mutable source or
  // system environment. Launch verifies the frozen inputs before AND after use.
  insist(equal(record.host_arguments,hostArguments(request,record.capture_root,record.run_id)), 'Recorded Host arguments differ');
  insist(Array.isArray(record.processes)&&record.processes.length===1&&Array.isArray(record.owned_evidence)&&record.owned_evidence.length===1,'Expected exactly one owned Host, no injector');
  const files=request.frozen_inputs.files;
  insist(digest(files.host_executable.sha256)&&digest(files.owned_runner.sha256),'Missing frozen executable identity');
  const process=owned.verifyOwnedProcess(directory,'host',record.processes[0],record.owned_evidence[0],
    {runnerSha256:files.owned_runner.sha256,executableSha256:files.host_executable.sha256,
      executable:request.host_executable,cwd:path.dirname(request.host_executable),args:record.host_arguments},record.capture_root);
  insist(process.naturalCompletion,'Manual result requires natural root AND Job completion');
  const nativeReady=owned.readRunJson(directory,'host-ready.json'),artifactNames=artifactFiles(nativeReady);
  keys(record.artifacts,Object.keys(artifactNames));
  const artifacts={subject:process.subject,streams:{}};
  for(const [key,filename] of Object.entries(artifactNames)) {
    if(key==='manual')insist(record.artifacts[key]?.bytes>0&&record.artifacts[key].bytes<=8192,'Manual result exceeds 8 KiB');
    if(key==='sdk'||key==='kernel') {
      insist(equal(owned.describe(directory,filename),record.artifacts[key]),'Trace artifact hash/path changed');
      artifacts.streams[key]=trace.readNdjson(owned.runFile(directory,filename));
    } else artifacts[key]=verified(record.artifacts[key],filename);
  }
  insist(artifacts.ready.run_id===record.run_id,'Run record differs from native run');
  const actualPage=owned.describeTree(directory,'page-bundle'); insist(equal(actualPage,record.page_bundle),'Page bundle changed after collection');
  const page=Object.fromEntries(request.frozen_inputs.page.map(item=>[item.path,item]));
  const expectedPage=[['manual-input-page.html',page['manual-input-page.html']],['index.html',page['manual-input-page.html']],['manual-input-page.js',page['manual-input-page.js']]];
  for(const [name,expected]of expectedPage) {
    const actual=actualPage.find(item=>item.path===name);
    insist(actual&&actual.bytes===expected.bytes&&actual.sha256===expected.sha256,'Loaded page copy differs');
  }
  const seed="window.souiManualInputConfig={run:'"+record.run_id+"',kind:'"+request.workload.kind+"'};\n";
  const seedPin=actualPage.find(item=>item.path==='manual-input-config.js');
  insist(actualPage.length===4&&seedPin?.sha256===hash(Buffer.from(seed))&&seedPin.bytes===Buffer.byteLength(seed),'Generated page seed/closure differs');
  return {schema:'soui.input-ui-profile-normalized/v1',request,run_id:record.run_id,
    ...normalizeArtifacts(request,artifacts),input_counts_are_stage_observations:true,
    input_terminal_outcomes:'unknown-scalar-has-no-per-event-outcome',
    limits:['No automatic input, physical observation or continuous foreground evidence.',
      'Requested duration is manual guidance, not a measured Begin/End window.',
      'A manual page snapshot covers only the user-requested observation; it does not prove continuous foreground or equal work. Legacy runs have no page snapshot.',
      'Commit/coverage counts may repeat one frame; no FPS or unique-frame claim.',
      'Source commit/dirty declarations and file hashes establish local consistency, not build provenance authentication.',
      'System DLLs, loaded process memory and Node binary/dependencies are not certified by this file inventory.']};
}
function compareRuns(runs) {
  insist(Array.isArray(runs)&&runs.length>=2&&runs.length<=6,'Compare 2 to 6 bounded manual runs');
  const projection=run=>{
    const value=structuredClone(run.request); template(value,true);
    delete value.profile.engine_budget_us;
    delete value.frozen_inputs.child_environment[BUDGET];
    return value;
  };
  const reference=projection(runs[0]), differences=[];
  const diff=(a,b,prefix,index)=>{
    if(equal(a,b))return;
    if(a&&b&&typeof a==='object'&&typeof b==='object'&&!Array.isArray(a)&&!Array.isArray(b)) {
      for(const key of [...new Set([...Object.keys(a),...Object.keys(b)])].sort())diff(a[key],b[key],prefix?prefix+'.'+key:key,index);
    } else differences.push({run:index,path:prefix,before:a??null,after:b??null});
  };
  runs.slice(1).forEach((run,index)=>diff(reference,projection(run),'',index+1));
  const budgets=runs.map(run=>run.request.profile.engine_budget_us);
  return {schema:'soui.input-ui-profile-comparison/v1',same_common_inputs:differences.length===0,differences,
    intended_variable:'profile.engine_budget_us',profiles:budgets,
    semantic_differences:runs.slice(1).map((run,index)=>({run:index+1,path:'profile.engine_budget_us',before:budgets[0],after:budgets[index+1]})),
    repeated_same_profile:new Set(budgets).size===1,
    runs:runs.map(run=>({run_id:run.run_id,identity:run.identity,profile:run.request.profile,sources:run.sources})),
    scope:'whole-process-lifetime',foreground_validity:'unknown',workload_comparable:'unknown',
    provider_prefix_attribution:'unknown',physical_latency_measured:false,speedup:null,
    adoption:'pending_measurement',performanceGate:false,releaseReady:false};
}
function writeNew(filename,value) { fs.writeFileSync(filename,JSON.stringify(value,null,2)+'\n',{flag:'wx'}); }
if(require.main===module) {
  try {
    const [command,...args]=process.argv.slice(2);
    if(command==='freeze'&&args.length===2) {
      const output=path.resolve(args[1]); writeNew(output,freezeRequest(readJson(path.resolve(args[0]))));
      console.log(JSON.stringify({prepared:output,started:false,next_argv:[process.execPath,path.join(__dirname,'launch-input-ui-profile.cjs'),output,'ABS_NEW_RUN_DIRECTORY'],
        note:'Only the operator explicitly invokes launch. Activate/input/close manually; preparation never launches.'},null,2));
    } else if(command==='check'&&args.length===1) console.log(JSON.stringify(readRun(path.resolve(args[0])),null,2));
    else if(command==='compare'&&args.length>=2&&args.length<=6)console.log(JSON.stringify(compareRuns(args.map(arg=>readRun(path.resolve(arg)))),null,2));
    else throw new Error('Usage: input-ui-profile.cjs freeze TEMPLATE NEW_FROZEN | check RUN | compare RUN RUN [RUN...]');
  } catch(error) {console.error(error.message);process.exitCode=1;}
}
module.exports={template,freezeRequest,verifyRequest,inputs,sourceInventory,pin,readJson,tree,childEnvironment,hostArguments,
  normalizeArtifacts,compareRuns,readRun,histogram,decimalMilliseconds,writeNew,ARTIFACTS,artifactFiles,MANUAL_REPORT_CAPABILITY,manualPageSnapshot,keys,stable};
