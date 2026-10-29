'use strict';
// Fixed local profiling protocol. No remote debugger port, browser flags or
// product Bridge command is created. Instrumented timing is never a score.
const fs = require('node:fs/promises'), path = require('node:path'), crypto = require('node:crypto');
const inputRoot = path.resolve(__dirname, '../../..', 'experiments/scratch-webview2-baseline');
const recipe = require(path.join(inputRoot, 'diagnostic-protocol.json'));
const scripts = require(path.join(inputRoot, 'diagnostic-page-scripts.json'));
const need = (ok, text) => {if (!ok) throw Error(text);};
const stamp = () => ({utc: new Date().toISOString(), monotonicNs: process.hrtime.bigint().toString()});
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function parameters(method) {
  switch (method) {
    case 'Profiler.enable': case 'Profiler.start': case 'Profiler.stop': case 'Profiler.disable':
    case 'Tracing.getCategories': case 'Tracing.end': return {};
    case 'Profiler.setSamplingInterval': return {interval: recipe.samplingIntervalUs};
    case 'Tracing.start': return {categories: recipe.traceCategories, options: recipe.traceOptions,
      bufferUsageReportingInterval: 1000, transferMode: 'ReportEvents'};
    default: throw Error('Command is outside fixed local diagnostic protocol');
  }
}
function createTraceBuffer(limit = recipe.maxTraceBytes) {
  const chunks = []; let bytes = 0, overflow = false;
  return {
    append(method, params) {
      if (!['Tracing.dataCollected', 'Tracing.bufferUsage', 'Tracing.tracingComplete'].includes(method)) return false;
      const raw = JSON.stringify(params), length = Buffer.byteLength(raw);
      if (length > recipe.maxResponseBytes || length > limit - bytes || chunks.length >= 8192) {overflow = true; return false;}
      chunks.push({method, raw, received: stamp()}); bytes += length; return true;
    },
    get chunks() {return chunks;}, get bytes() {return bytes;}, get overflow() {return overflow;}
  };
}
async function runDiagnostic(adapter, options) {
  const wait = options.wait ?? (ms => new Promise(resolve => setTimeout(resolve, ms)));
  const monotonicMs = options.monotonicMs ?? (() => Number(process.hrtime.bigint() / 1000000n));
  const deadlineAt = monotonicMs() + 120000;
  const pendingNative = new Map(); let nextRequest = 0;
  const output = path.resolve(options.output);
  await fs.mkdir(output); // Refuse an existing evidence directory.
  const probe = await fs.readFile(path.join(inputRoot, 'diagnostic-probe.js'), 'utf8');
  const record = {schema: 'soui.scratch-diagnostic-capture/v1', diagnosticOnly: true,
    formalPerformancePassed: null, started: stamp(), metadata: options.metadata,
    recipe, sourceInputs: [], commands: [], failures: [], nativeGuards: [],
    cpuProfileQualified: false, traceQualified: false, collectionCompleted: false};
  for (const name of ['diagnostic-protocol.json', 'diagnostic-page-scripts.json', 'diagnostic-probe.js']) {
    const raw = await fs.readFile(path.join(inputRoot, name)); record.sourceInputs.push({name, bytes: raw.length, sha256: sha(raw)});
  }
  const trace = createTraceBuffer(); let traceComplete = false, traceStarted = false, profilerEnabled = false, profilerStarted = false;
  let cpuProfile = null, page = null;
  const unsubscribe = adapter.subscribe((method, params) => {
    if (trace.append(method, params) && method === 'Tracing.tracingComplete') traceComplete = true;
  });
  const remaining = operation => {
    const left = deadlineAt - monotonicMs();
    need(left > 0, 'Diagnostic total 120s deadline exceeded before ' + operation); return left;
  };
  const bounded = async (invoke, ms, operation, native = true) => {
    const cap = Math.min(ms, remaining(operation));
    const request = {id: ++nextRequest, operation, began: stamp()};
    let promise;
    // Check the remaining budget before admitting a new native request.
    try {promise = Promise.resolve(invoke());} catch (error) {throw error;}
    if (native) {
      pendingNative.set(request.id, request);
      promise = promise.then(value => {pendingNative.delete(request.id); return value;},
        error => {pendingNative.delete(request.id); throw error;});
    }
    let timer;
    try {
      const result = await Promise.race([promise, new Promise((_, reject) => {timer = setTimeout(() => reject(Error(operation + ' deadline exceeded; underlying request is not cancelled')), cap);})]);
      need(monotonicMs() <= deadlineAt, 'Diagnostic total 120s deadline exceeded after ' + operation); return result;
    }
    finally {clearTimeout(timer);}
  };
  const pause = async ms => {
    const left = remaining('delay');
    await bounded(() => wait(Math.min(ms, left)), Math.min(ms + 100, left), 'delay', false);
    need(ms <= left, 'Diagnostic total 120s deadline exhausted during delay');
  };
  async function command(method) {
    const args = parameters(method), row = {method, params: args, began: stamp()}; record.commands.push(row);
    try {
      const value = await bounded(() => adapter.send(method, args), 15000, method);
      need(Buffer.byteLength(JSON.stringify(value)) <= recipe.maxResponseBytes, 'Diagnostic response exceeds 16 MiB');
      row.ended = stamp(); row.result = value; return value;
    } catch (error) {row.ended = stamp(); row.error = String(error); throw error;}
  }
  async function evaluate(operation) {
    const expression = operation === 'install' ?
      `(()=>{if(window.__souiDiagnosticProbe)throw Error('Diagnostic probe already exists');window.__souiDiagnosticProbe=(${probe})();return window.__souiDiagnosticProbe.warmup;})()` : scripts[operation];
    need(typeof expression === 'string', 'Unknown fixed page script');
    const result = await bounded(() => adapter.evaluate(expression), 15000, 'page:' + operation);
    need(Buffer.byteLength(JSON.stringify(result)) <= recipe.maxResponseBytes, 'Diagnostic page response exceeds 16 MiB');
    return result;
  }
  const guard = async phase => {
    const value = await bounded(() => adapter.nativeGuard(), 15000, 'native-guard:' + phase);
    record.nativeGuards.push({phase, stamp: stamp(), value});
    need(value.focused === true && value.minimized === false && value.visible === true, 'Native foreground/visibility guard failed: ' + phase);
  };
  try {
    // The fresh consumer owns its initial lesson; use its existing load handler.
    await evaluate('setup');
    let selected = null;
    for (let attempt = 0; attempt < 180; ++attempt) {
      selected = await evaluate('query'); if (selected.eligible) break; await pause(500);
    }
    need(selected?.eligible === true, 'The fixed heavy workload did not become ready');
    need(selected.width === options.width && selected.height === options.height && selected.devicePixelRatio === 1 && selected.visibility === 'visible', 'Diagnostic viewport differs');
    record.configuration = selected;
    await guard('warmup-start'); record.pageWarmup = await evaluate('install');
    try {
      const available = await command('Tracing.getCategories'); record.availableCategories = available.categories;
      record.missingCategories = recipe.traceCategories.split(',').filter(name => !available.categories?.includes(name));
    } catch (error) {record.failures.push({phase: 'trace-capability', error: String(error)});}
    try {await command('Profiler.enable'); profilerEnabled = true; await command('Profiler.setSamplingInterval');}
    catch (error) {record.failures.push({phase: 'cpu-capability', error: String(error)});}
    await pause(recipe.warmupMs); await guard('capture-start');
    try {await command('Tracing.start'); traceStarted = true;} catch (error) {record.failures.push({phase: 'trace-start', error: String(error)});}
    if (profilerEnabled) try {await command('Profiler.start'); profilerStarted = true;} catch (error) {record.failures.push({phase: 'cpu-start', error: String(error)});}
    record.pageCaptureStart = await evaluate('start'); record.captureStarted = stamp();
    await pause(recipe.captureMs);
    page = await evaluate('finish'); record.captureEnded = stamp();
    await guard('capture-end'); record.collectionCompleted = true;
  } catch (error) {record.failures.push({phase: 'capture', error: String(error)});}
  finally {
    if (profilerStarted) try {cpuProfile = (await command('Profiler.stop')).profile;} catch (error) {record.failures.push({phase: 'cpu-stop', error: String(error)});}
    if (traceStarted) try {
      await command('Tracing.end');
      for (let attempt = 0; attempt < 300 && !traceComplete; ++attempt) await pause(50);
      need(traceComplete, 'Tracing.tracingComplete was not observed');
    } catch (error) {record.failures.push({phase: 'trace-stop', error: String(error)});}
    if (profilerEnabled) try {await command('Profiler.disable');} catch (error) {record.failures.push({phase: 'cpu-disable', error: String(error)});}
    try {await evaluate('abort');} catch (error) {record.failures.push({phase: 'probe-abort', error: String(error)});}
    try {await evaluate('stop-workload');} catch (error) {record.failures.push({phase: 'stop-workload', error: String(error)});}
    try {unsubscribe();} catch (error) {record.failures.push({phase: 'trace-unsubscribe', error: String(error)});}
  }
  const events = []; let dataLossOccurred = false, malformed = false, bufferFull = false;
  for (const chunk of trace.chunks) {
    const params = JSON.parse(chunk.raw);
    if (chunk.method === 'Tracing.dataCollected') {
      if (Array.isArray(params.value)) for (const event of params.value) events.push(event); else malformed = true;
    } else if (chunk.method === 'Tracing.tracingComplete') dataLossOccurred ||= params.dataLossOccurred === true;
    else if (chunk.method === 'Tracing.bufferUsage') bufferFull ||= params.percentFull >= 1 || params.value >= 1;
  }
  record.cpuProfileQualified = !!cpuProfile && Array.isArray(cpuProfile.nodes) && cpuProfile.nodes.length > 0 &&
    Number.isFinite(cpuProfile.startTime) && Number.isFinite(cpuProfile.endTime) && cpuProfile.endTime > cpuProfile.startTime;
  record.trace = {bytes: trace.bytes, overflow: trace.overflow, complete: traceComplete, events: events.length,
    dataLossOccurred, malformed, bufferFull};
  record.traceQualified = traceComplete && events.length > 0 && !trace.overflow && !dataLossOccurred && !malformed && !bufferFull && record.missingCategories?.length === 0;
  record.ended = stamp(); record.pageGuardsQualified = page?.pageGuardsQualified === true;
  record.nativeRequestsPendingAtReturn = [...pendingNative.values()];
  record.actualNativeRetirementVerified = null;
  record.ownerRetirementRequired = true;
  record.limitations = ['CPU/browser tracing and probe timers add overhead; no formal FPS or parity conclusion.',
    'The native snapshots do not prove absence of occlusion; retain a common passive WindowGuard/OS trace separately.',
    'Missing categories/symbols or a partial profile remain unknown, never zero cost.'];
  if (pendingNative.size) record.limitations.push('Adapter requests remain unsettled; deadline races did not cancel or prove retirement of underlying native operations. The external owner must retire this diagnostic target and verify completion.');
  const write = (name, value) => fs.writeFile(path.join(output, name), JSON.stringify(value, null, 2) + '\n', {flag: 'wx'});
  if (page) await write('page-diagnostic.json', page);
  if (cpuProfile) await write('cpu-profile.cpuprofile', cpuProfile);
  await fs.writeFile(path.join(output, 'trace-events.ndjson'), trace.chunks.map(row => JSON.stringify(row)).join('\n') + '\n', {flag: 'wx'});
  await write('trace.json', {traceEvents: events}); await write('capture.json', record);
  return record;
}
module.exports = {parameters, createTraceBuffer, runDiagnostic, recipe, scripts};
