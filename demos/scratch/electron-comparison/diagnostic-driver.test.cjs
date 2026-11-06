'use strict';
// Authored CDP/page fixtures test protocol behavior only. No browser, compiler,
// GUI, native input or actual performance result is produced.
const test = require('node:test'), assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), vm = require('node:vm');
const {runDiagnostic, createTraceBuffer, parameters, recipe} = require('./diagnostic-driver.cjs');
const source = fs.readFileSync(path.resolve(__dirname, '../../../experiments/scratch-webview2-baseline/diagnostic-probe.js'), 'utf8');
function pageFixture() {
  let clock = 0, recording = false, interval = null, stopped = 0, workloadStops = 0;
  const listeners = new Map(), handlers = new Map();
  const document = {visibilityState: 'visible', hasFocus: () => true,
    addEventListener: (name, callback) => listeners.set(name, callback),
    removeEventListener: name => listeners.delete(name)};
  const configuration = {sceneId: 'sprites', level: 'heavy', metadata: {workloadSha256: 'a'.repeat(64)}};
  const runtime = {targets: Array.from({length: 241}, () => ({isOriginal: true})), threads: Array(240), currentStepTime: 1000 / 60};
  const meter = {snapshot: () => ({recording}), start: () => {recording = true;}, stop: () => {recording = false; ++stopped;},
    collectReport: () => ({status: recording ? 'running' : 'stopped', measurementVersion: 2})};
  const api = {configuration, meter};
  const window = {souiScratch: {ready: true, vm: {runtime, stopAll() {++workloadStops;}}}, souiScratchPerformance: api,
    addEventListener: (name, callback) => handlers.set(name, callback), removeEventListener: name => handlers.delete(name)};
  const context = vm.createContext({window, document, location: {href: 'https://soui-webview2.invalid/index.html'},
    performance: {now: () => clock, timeOrigin: 1, mark() {}}, Date, innerWidth: 1424, innerHeight: 861, devicePixelRatio: 1,
    setInterval: callback => {interval = callback; return 1;}, clearInterval: () => {interval = null;}, Event: class {constructor(type) {this.type = type;}}});
  return {context, window, api, runtime, handlers, listeners, get stopped() {return stopped;},
    get workloadStops() {return workloadStops;}, get clock() {return clock;},
    advance(ms) {clock += ms; if (interval) interval();}, probe() {return vm.runInContext('(' + source + ')()', context);}};
}
test('probe preserves stopped status, bounded interval evidence and cleans all listeners/timers', () => {
  const f = pageFixture(), probe = f.probe(); probe.start(); f.advance(17); f.advance(25);
  const result = probe.finish();
  assert.deepEqual(Array.from(result.timerIntervalsMs), [17, 25]);
  assert.deepEqual(Array.from(result.timerLatenessMs), [1, 9]);
  assert.equal(result.pageReport.status, 'stopped'); assert.equal(result.meterFinishedNaturally, false);
  assert.equal(result.formalPerformancePassed, null); assert.equal(result.pageGuardsQualified, true);
  assert.equal(f.stopped, 1); assert.equal(f.handlers.size, 0); assert.equal(f.listeners.size, 0);
  assert.throws(() => probe.finish(), /already finished/);
});
test('blur or same-configuration workload mutation prevents page guard qualification', () => {
  for (const change of ['blur', 'threads', 'metadata']) {
    const f = pageFixture(), probe = f.probe(); probe.start();
    if (change === 'blur') f.handlers.get('blur')();
    else if (change === 'threads') f.runtime.threads.pop();
    else f.api.configuration.metadata.workloadSha256 = 'b'.repeat(64);
    const result = probe.finish(); assert.equal(result.pageGuardsQualified, false, change);
    if (change !== 'blur') assert.equal(result.workloadStable, false);
  }
});
test('fixed commands reject arbitrary domains and trace overflow remains visible', () => {
  assert.throws(() => parameters('Runtime.evaluate'), /outside fixed/);
  assert.deepEqual(parameters('Profiler.setSamplingInterval'), {interval: 1000});
  const buffer = createTraceBuffer(10);
  assert.equal(buffer.append('Tracing.dataCollected', {value: []}), false); assert.equal(buffer.overflow, true);
  assert.equal(buffer.append('Page.frameNavigated', {}), false); assert.equal(buffer.chunks.length, 0);
});
async function captureFixture(t, options = {}) {
  const parent = fs.realpathSync(os.tmpdir()), root = fs.mkdtempSync(path.join(parent, 'soui-diagnostic-unit-'));
  t.after(() => {assert.equal(fs.realpathSync(root), root); assert.equal(path.dirname(root), parent); fs.rmSync(root, {recursive: true});});
  const f = pageFixture(), controls = {}, calls = []; let receive;
  f.api.configuration = null;
  const dialog = {open: false, close(value) {assert.equal(value, 'continue');this.open = false;
    f.api.configuration = {sceneId: 'sprites', level: 'heavy', metadata: {workloadSha256: 'a'.repeat(64)}};}};
  controls['performance-scene'] = {value: '', dispatchEvent() {}}; controls['performance-level'] = {value: '', dispatchEvent() {}};
  controls['performance-load'] = {click() {dialog.open = true;}}; controls['confirm-dialog'] = dialog;
  f.context.document.getElementById = id => controls[id];
  const record = await runDiagnostic({
    subscribe: callback => {receive = callback; return () => {receive = null;};},
    evaluate: expression => Promise.resolve(vm.runInContext(expression, f.context)),
    nativeGuard: async () => ({focused: options.badGuard !== true, minimized: false, visible: true}),
    async send(method, params) {
      calls.push([method, params]);
      if (method === 'Tracing.getCategories') return {categories: recipe.traceCategories.split(',')};
      if (method === 'Profiler.stop') return {profile: {nodes: [{id: 1}], startTime: 1, endTime: 2}};
      if (method === 'Tracing.end') {
        receive('Tracing.dataCollected', {value: [{name: 'authored-event', ph: 'I', ts: 1}]});
        receive('Tracing.tracingComplete', {dataLossOccurred: options.dataLoss === true});
      }
      return {};
    }
  }, {output: path.join(root, 'capture'), width: 1424, height: 861, metadata: {authoredUnitFixture: true},
    wait: async ms => f.advance(ms), monotonicMs: () => f.clock});
  return {record, calls, root, page: f};
}
test('fixed capture uses identical scene controls, start/stop order, and retains raw CPU/trace/page artifacts', async t => {
  const f = await captureFixture(t);
  assert.deepEqual(f.calls.map(row => row[0]), ['Tracing.getCategories', 'Profiler.enable', 'Profiler.setSamplingInterval',
    'Tracing.start', 'Profiler.start', 'Profiler.stop', 'Tracing.end', 'Profiler.disable']);
  assert.equal(f.record.collectionCompleted, true); assert.equal(f.record.cpuProfileQualified, true);
  assert.equal(f.record.traceQualified, true); assert.equal(f.record.formalPerformancePassed, null);
  assert.equal(f.page.workloadStops, 1); assert.deepEqual(f.record.nativeRequestsPendingAtReturn, []);
  for (const file of ['cpu-profile.cpuprofile', 'trace-events.ndjson', 'trace.json', 'page-diagnostic.json', 'capture.json'])
    assert.equal(fs.statSync(path.join(f.root, 'capture', file)).isFile(), true);
});
test('one total deadline bounds native admission and cleanup preserves unresolved requests as unknown', async t => {
  const parent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(parent,'soui-diagnostic-unit-'));
  t.after(()=>{assert.equal(fs.realpathSync(root),root);assert.equal(path.dirname(root),parent);fs.rmSync(root,{recursive:true});});
  let clockCalls=0,evaluations=0;
  const record=await runDiagnostic({
    subscribe:()=>()=>{},send:async()=>{throw Error('No CDP request may start');},nativeGuard:async()=>({}),
    evaluate:()=>{++evaluations;return new Promise(()=>{});}
  },{output:path.join(root,'capture'),width:1424,height:861,metadata:{authoredUnitFixture:true},
    monotonicMs:()=>++clockCalls===1?0:clockCalls===2?119999:120001});
  assert.equal(evaluations,1,'No abort/stop script may be newly admitted after the total deadline');
  assert.equal(record.collectionCompleted,false);assert.equal(record.nativeRequestsPendingAtReturn.length,1);
  assert.equal(record.actualNativeRetirementVerified,null);assert.equal(record.ownerRetirementRequired,true);
  assert.match(record.failures[0].error,/deadline/);
  assert.equal(record.failures.some(row=>row.phase==='probe-abort'),true);
  assert.equal(record.failures.some(row=>row.phase==='stop-workload'),true);
});
test('lost trace events remain raw but unqualified; a bad native guard cannot complete collection', async t => {
  const lost = await captureFixture(t, {dataLoss: true});
  assert.equal(lost.record.traceQualified, false); assert.equal(lost.record.trace.dataLossOccurred, true);
  assert.match(fs.readFileSync(path.join(lost.root, 'capture/trace-events.ndjson'), 'utf8'), /dataLossOccurred/);
  const failed = await captureFixture(t, {badGuard: true});
  assert.equal(failed.record.collectionCompleted, false); assert.equal(failed.record.failures.length > 0, true);
});
