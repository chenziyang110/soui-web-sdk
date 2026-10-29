// These measurements describe JavaScript rAF scheduling in this document.
// They do not measure input-to-photon latency, GPU presentation or native memory.
// Maximum recording length (120 s) at the declared 1000 Hz ceiling, plus
// boundary intervals. The array grows only with observed frames, not wall time.
const defaultCapacity = 120002;
const longFrameMs = 50;
const counterObservers = new WeakMap();
const precision = value => Number.isFinite(value) ? Math.round(value * 1000) / 1000 : null;

export function summarizeFrameIntervals(intervals) {
  const values = intervals.filter(value => Number.isFinite(value) && value > 0);
  if (!values.length) return {count:0, fps:null, p50Ms:null, p95Ms:null, p99Ms:null, maxMs:null};
  const sorted = [...values].sort((a,b) => a-b);
  const percentile = fraction => precision(sorted[Math.max(0, Math.ceil(sorted.length * fraction)-1)]);
  return {
    count: values.length,
    fps: precision(1000 * values.length / values.reduce((sum,value) => sum+value, 0)),
    p50Ms: percentile(.5), p95Ms: percentile(.95), p99Ms: percentile(.99),
    maxMs: precision(sorted[sorted.length-1])
  };
}

/**
 * A bounded, opt-in meter. Periodic updates run at most once per second;
 * lifecycle transitions notify immediately so controls can reflect their state.
 * Percentiles are computed once after recording, keeping polling/sorting out of
 * the frame path. Explicitly bounded short windows remain diagnostic only.
 */
export function createPerformanceMeter({getVM = () => null, onUpdate = () => {}, environment = globalThis,
  sampleCapacity = defaultCapacity} = {}) {
  if (typeof getVM !== 'function' || typeof onUpdate !== 'function') throw new TypeError('Meter callbacks must be functions.');
  if (!Number.isInteger(sampleCapacity) || sampleCapacity < 600 || sampleCapacity > defaultCapacity)
    throw new RangeError('Frame sample capacity must be between 600 and 120002.');
  const env = environment;
  const document = env.document;
  const performance = env.performance;
  const now = () => performance?.now?.() ?? Date.now();
  let state, raf = null, deadline = null, observer = null, disposed = false, runId = 0;
  let observedVM = null, observedRuntime = null, counters = null, counterLease = null, counterBaseline = null;

  function empty() {
    return {status:'idle', startedAt:null, endedAt:null, startedUtc:null, durationMs:30000,
      sceneId:null, level:null, metadata:{}, invalidReasons:[], samples:[], sampleCursor:0, summary:null,
      intervalsObserved:0, intervalSum:0, longFrames:0, maxFrameMs:null,
      histogram:[0,0,0,0,0], lastFrame:null, lastUpdate:null, stopReason:null,
      longTaskSupported:false, longTaskCount:0, longTaskTotalMs:0, longTaskMaxMs:null,
      heapSupported:false, heapBytes:null, heapPeakBytes:null, runtimeAtEnd:null, updateError:null,
      targetsUpdates:0, targetsUpdatesSupported:false, workloadAtEnd:null, graphics:null,
      rafCallbacks:0, firstObservedAt:null, lastObservedAt:null, boundaryBudgetMs:1000/30};
  }
  state = empty();

  function heap() {
    const bytes = performance?.memory?.usedJSHeapSize;
    if (Number.isFinite(bytes) && bytes >= 0) {
      state.heapSupported = true;
      state.heapBytes = bytes;
      state.heapPeakBytes = Math.max(state.heapPeakBytes ?? bytes, bytes);
    } else state.heapBytes = null;
  }
  function runtime() {
    const unavailable = {targets:null,sprites:null,clones:null,threads:null};
    try {
      const current = getVM()?.runtime;
      if (!current || !Array.isArray(current.targets)) return unavailable;
      const targets = current.targets;
      let sprites = 0, clones = 0;
      for (const target of targets) if (!target.isStage) {
        if (target.isOriginal === false) ++clones; else ++sprites;
      }
      return {targets:targets.length, sprites, clones,
        threads:Array.isArray(current.threads) ? current.threads.length : null};
    } catch { return unavailable; }
  }
  function workload() {
    const delta = name => counters && observedRuntime?.souiPerformanceCounters === counters &&
      Number.isSafeInteger(counters[name]) && Number.isSafeInteger(counterBaseline?.[name]) &&
      counters[name] >= counterBaseline[name] ? counters[name] - counterBaseline[name] : null;
    return {vmSteps:delta('steps'), drawCalls:delta('draws'),
      targetsUpdates:state.targetsUpdatesSupported ? state.targetsUpdates : null};
  }
  function graphics() {
    try {
      // Reuse the already-running renderer context. Creating a diagnostic
      // canvas/context here would change GPU load and memory during sampling.
      const gl = observedRuntime?.renderer?._gl;
      if (!gl || gl.isContextLost?.()) return null;
      const bounded = value => typeof value === 'string' ? value.slice(0,256) : null;
      const debug = gl.getExtension('WEBGL_debug_renderer_info');
      return {vendor:bounded(gl.getParameter(gl.VENDOR)),renderer:bounded(gl.getParameter(gl.RENDERER)),
        version:bounded(gl.getParameter(gl.VERSION)),
        unmaskedVendor:debug ? bounded(gl.getParameter(debug.UNMASKED_VENDOR_WEBGL)) : null,
        unmaskedRenderer:debug ? bounded(gl.getParameter(debug.UNMASKED_RENDERER_WEBGL)) : null};
    } catch { return null; }
  }
  function targetsUpdated() { if (state.status === 'running') ++state.targetsUpdates; }
  function observeWorkload() {
    try {
      observedVM = getVM(); observedRuntime = observedVM?.runtime;
      // This capability is added by the pinned source patch. Do not wrap VM
      // methods or invent unavailable step/draw counts for the stock bundle.
      if (observedRuntime && Object.hasOwn(observedRuntime, 'souiPerformanceCounters')) {
        let lease = counterObservers.get(observedRuntime);
        if (observedRuntime.souiPerformanceCounters === null) {
          observedRuntime.souiPerformanceCounters = {steps:0,draws:0};
          lease = {counters:observedRuntime.souiPerformanceCounters,owned:true,users:0};
        }
        counters = observedRuntime.souiPerformanceCounters;
        if (counters && typeof counters === 'object') {
          if (!lease || lease.counters !== counters) lease = {counters,owned:false,users:0};
          counterObservers.set(observedRuntime,lease); counterLease = lease; ++lease.users;
          counterBaseline = {steps:counters.steps,draws:counters.draws};
        } else counters = null;
      }
      if (typeof observedVM?.on === 'function' &&
          (typeof observedVM?.off === 'function' || typeof observedVM?.removeListener === 'function')) {
        observedVM.on('targetsUpdate',targetsUpdated); state.targetsUpdatesSupported = true;
      }
    } catch { stopObservingWorkload(); }
  }
  function stopObservingWorkload() {
    if (state.targetsUpdatesSupported) (observedVM.off ?? observedVM.removeListener).call(observedVM,'targetsUpdate',targetsUpdated);
    if (counterLease && --counterLease.users === 0) {
      if (counterLease.owned && observedRuntime?.souiPerformanceCounters === counters) observedRuntime.souiPerformanceCounters = null;
      if (counterObservers.get(observedRuntime) === counterLease) counterObservers.delete(observedRuntime);
    }
    observedVM = observedRuntime = counters = counterBaseline = counterLease = null;
  }
  function chronologicalIntervals() {
    if (state.samples.length < sampleCapacity || state.sampleCursor === 0) return [...state.samples];
    return state.samples.slice(state.sampleCursor).concat(state.samples.slice(0,state.sampleCursor));
  }
  function snapshot() {
    if (state.status === 'running') heap();
    const elapsed = state.startedAt === null ? 0 : Math.max(0, (state.endedAt ?? now()) - state.startedAt);
    // Arrival times share the wall clock used by start/end. Head/tail gaps are
    // censored intervals, never invented rAF intervals or dropped stall runs.
    const leadingGap = state.firstObservedAt === null ? elapsed : Math.max(0,state.firstObservedAt-state.startedAt);
    const trailingGap = state.lastObservedAt === null ? elapsed : Math.max(0,(state.endedAt ?? now())-state.lastObservedAt);
    const censored = state.startedAt !== null && (state.rafCallbacks === 0 ||
      leadingGap > state.boundaryBudgetMs || trailingGap > state.boundaryBudgetMs);
    // A UI or harness may poll snapshot frequently. Only finish() sorts, once.
    const summary = state.summary;
    return {
      recording:state.status === 'running', completed:state.status === 'completed', status:state.status,
      valid:state.startedAt !== null && state.invalidReasons.length === 0,
      invalidReasons:[...state.invalidReasons], stopReason:state.stopReason,
      sceneId:state.sceneId, level:state.level, durationMs:state.durationMs, elapsedMs:precision(elapsed),
      rafFps:state.intervalSum > 0 ? precision(1000 * state.intervalsObserved / state.intervalSum) : null,
      windowRafFps:elapsed > 0 ? precision(1000 * state.rafCallbacks / elapsed) : null,
      rafCallbacks:state.rafCallbacks, leadingGapMs:precision(leadingGap), trailingGapMs:precision(trailingGap),
      boundaryBudgetMs:precision(state.boundaryBudgetMs), censored,
      frameP50Ms:censored ? null : summary?.p50Ms ?? null,
      frameP95Ms:censored ? null : summary?.p95Ms ?? null,
      frameP99Ms:censored ? null : summary?.p99Ms ?? null,
      observedFramePercentiles:summary ? {p50Ms:summary.p50Ms,p95Ms:summary.p95Ms,p99Ms:summary.p99Ms} : null,
      maxFrameMs:precision(state.maxFrameMs), longFrames:state.longFrames,
      intervalsObserved:state.intervalsObserved, sampledIntervals:state.samples.length, sampleCapacity,
      sampleCoverage:state.intervalsObserved > state.samples.length ? 'rolling-window' : 'complete',
      workload:state.workloadAtEnd ?? workload(),
      graphics:state.graphics,
      ...(state.runtimeAtEnd ?? runtime()), heapBytes:state.heapBytes, heapPeakBytes:state.heapPeakBytes,
      longTasks:state.longTaskSupported ? {count:state.longTaskCount,totalMs:precision(state.longTaskTotalMs),maxMs:precision(state.longTaskMaxMs)} : null,
      capabilities:{raf:typeof env.requestAnimationFrame === 'function', longTasks:state.longTaskSupported, jsHeap:state.heapSupported}
    };
  }
  function notify(force = false) {
    const timestamp = now();
    if (!force && state.lastUpdate !== null && timestamp-state.lastUpdate < 1000) return;
    state.lastUpdate = timestamp;
    try { onUpdate(snapshot()); } catch (error) {state.updateError = String(error?.message ?? error).slice(0,200);}
  }
  function acceptLongTasks(entries, endedAt = Infinity) {
    for (const entry of entries) {
      if (!Number.isFinite(entry.startTime) || !Number.isFinite(entry.duration) || entry.duration < 0 ||
          entry.startTime < state.startedAt || entry.startTime > endedAt) continue;
      ++state.longTaskCount;
      state.longTaskTotalMs += entry.duration;
      state.longTaskMaxMs = Math.max(state.longTaskMaxMs ?? 0, entry.duration);
    }
  }
  function cancelScheduled() {
    if (raf !== null) env.cancelAnimationFrame?.(raf);
    if (deadline !== null) env.clearTimeout?.(deadline);
    raf = deadline = null;
  }
  function finish(reason, invalid = false) {
    if (state.status !== 'running') return snapshot();
    if (document?.visibilityState === 'hidden') { reason = 'document-hidden'; invalid = true; }
    state.endedAt = now();
    heap(); state.runtimeAtEnd = runtime();
    state.workloadAtEnd = workload(); stopObservingWorkload();
    state.summary = summarizeFrameIntervals(state.samples);
    if (invalid && !state.invalidReasons.includes(reason)) state.invalidReasons.push(reason);
    state.stopReason = reason;
    state.status = invalid ? 'invalid' : reason === 'duration-complete' ? 'completed' : 'stopped';
    cancelScheduled();
    if (observer) {
      try {acceptLongTasks(observer.takeRecords(),state.endedAt);} catch { /* Collection failure is exposed as unsupported. */ state.longTaskSupported = false;}
      observer.disconnect(); observer = null;
    }
    notify(true);
    return snapshot();
  }
  function onVisibilityChange() {
    if (document?.visibilityState === 'hidden') finish('document-hidden',true);
  }
  function onPageHide() {finish('document-unloaded',true);}
  document?.addEventListener?.('visibilitychange',onVisibilityChange);
  env.addEventListener?.('pagehide',onPageHide);

  function frame(timestamp) {
    raf = null;
    if (state.status !== 'running') return;
    if (document?.visibilityState === 'hidden') {finish('document-hidden',true);return;}
    if (!Number.isFinite(timestamp)) {finish('invalid-frame-clock',true);return;}
    const observedAt = now();
    if (!Number.isFinite(observedAt) || observedAt < state.startedAt ||
        (state.lastObservedAt !== null && observedAt < state.lastObservedAt)) {finish('invalid-observation-clock',true);return;}
    ++state.rafCallbacks;
    if (state.firstObservedAt === null) state.firstObservedAt = observedAt;
    state.lastObservedAt = observedAt;
    if (state.lastFrame !== null) {
      const interval = timestamp - state.lastFrame;
      if (interval < 0) {finish('non-monotonic-frame-clock',true);return;}
      if (interval > 0) {
        if (state.samples.length < sampleCapacity) state.samples.push(interval);
        else {state.samples[state.sampleCursor] = interval;state.sampleCursor = (state.sampleCursor+1)%sampleCapacity;}
        ++state.intervalsObserved;
        state.intervalSum += interval;
        if (interval > longFrameMs) ++state.longFrames;
        state.maxFrameMs = Math.max(state.maxFrameMs ?? 0,interval);
        ++state.histogram[interval <= 8.33 ? 0 : interval <= 16.67 ? 1 : interval <= 33.33 ? 2 : interval <= 50 ? 3 : 4];
      }
    }
    state.lastFrame = timestamp;
    if (now()-state.startedAt >= state.durationMs) {finish('duration-complete');return;}
    notify();
    raf = env.requestAnimationFrame(frame);
  }
  function start({sceneId, scene, level = 'unspecified', durationMs = 30000, metadata = {}, expectedFrameRate = 60} = {}) {
    if (disposed) throw new Error('Performance meter is disposed.');
    if (typeof env.requestAnimationFrame !== 'function' || typeof env.setTimeout !== 'function')
      throw new Error('requestAnimationFrame and timers are required.');
    if (!Number.isFinite(durationMs) || durationMs < 500 || durationMs > 120000)
      throw new RangeError('Measurement duration must be between 500 and 120000 ms.');
    if (!Number.isFinite(expectedFrameRate) || expectedFrameRate < 30 || expectedFrameRate > 1000)
      throw new RangeError('Expected frame rate must be between 30 and 1000 Hz.');
    const serializedMetadata = JSON.stringify(metadata);
    if (!serializedMetadata || serializedMetadata.length > 2048) throw new RangeError('Measurement metadata exceeds its bound.');
    finish('restarted');
    state = empty(); ++runId;
    state.sceneId = String(sceneId ?? scene ?? 'unspecified').slice(0,128);
    state.level = String(level).slice(0,64);
    state.metadata = JSON.parse(serializedMetadata);
    state.durationMs = durationMs;
    state.boundaryBudgetMs = 2000/expectedFrameRate;
    state.status = 'running'; state.startedAt = now(); state.startedUtc = new Date().toISOString();
    if (document?.visibilityState === 'hidden') return finish('document-hidden-at-start',true);
    observeWorkload();
    state.graphics = graphics();
    const Observer = env.PerformanceObserver;
    if (typeof Observer === 'function' && Observer.supportedEntryTypes?.includes('longtask')) {
      try {
        const observedRun = runId;
        observer = new Observer(list => {if (state.status === 'running' && observedRun === runId) acceptLongTasks(list.getEntries());});
        observer.observe({type:'longtask',buffered:false}); state.longTaskSupported = true;
      } catch {observer?.disconnect();observer = null;}
    }
    notify(true);
    raf = env.requestAnimationFrame(frame);
    deadline = env.setTimeout(() => finish('duration-complete'),durationMs);
    return snapshot();
  }
  function stop(reason = 'user-stop') {return finish(String(reason).slice(0,128));}
  function reset() {
    if (disposed) throw new Error('Performance meter is disposed.');
    finish('reset'); state = empty(); ++runId; notify(true); return snapshot();
  }
  function collectReport({includeIntervals = false} = {}) {
    const measured = snapshot();
    return {
      schema:'soui.scratch-performance/v1', sceneId:state.sceneId, level:state.level, metadata:{...state.metadata},
      startedUtc:state.startedUtc,
      userAgent:typeof env.navigator?.userAgent === 'string' ? env.navigator.userAgent : null,
      viewport:{width:Number.isFinite(env.innerWidth) ? env.innerWidth : null,height:Number.isFinite(env.innerHeight) ? env.innerHeight : null,
        devicePixelRatio:Number.isFinite(env.devicePixelRatio) ? env.devicePixelRatio : null},
      ...measured,
      measurementVersion:2,
      validForWindowRateComparison:measured.completed && measured.valid && measured.elapsedMs > 0,
      validForComparison:measured.completed && measured.valid && !measured.censored && measured.sampleCoverage === 'complete' && measured.intervalsObserved >= 2,
      frameTimePercentileScope:measured.censored ? 'unclosed leading/trailing boundary; observed percentiles are diagnostic only' :
        measured.sampleCoverage === 'complete' ? 'all observed rAF intervals; head/tail gaps within declared boundary budget' : `last ${sampleCapacity} observed rAF intervals`,
      rafFpsScope:'historical observed-interval cadence; excludes unobserved head/tail gaps',
      windowRafFpsScope:'observed rAF callbacks divided by the full actual wall-clock recording window; zero callbacks gives zero',
      histogram:{boundsMs:['<=8.33','<=16.67','<=33.33','<=50','>50'],counts:[...state.histogram]},
      ...(includeIntervals ? {frameIntervalsMs:chronologicalIntervals().map(precision)} : {}),
      limitations:[
        'rAF callback cadence is not GPU/display presentation or physical input-to-photon latency.',
        'JS heap observations do not include total renderer, GPU or native process memory.',
        'Hidden documents invalidate the run; lack of a visibility event cannot prove the window was unobscured.',
        'Percentiles are computed after recording; a rolling sample window is diagnostic and cannot pass the comparison gate.',
        'VM step/draw counts require the pinned source capability; unavailable counts stay null.',
        'A completed stall or zero-frame window remains a valid poor window-rate sample; do not discard or retry it as an environmental failure.'
      ],
      ...(state.updateError ? {updateCallbackError:state.updateError} : {})
    };
  }
  function dispose() {
    if (disposed) return;
    finish('disposed'); disposed = true;
    document?.removeEventListener?.('visibilitychange',onVisibilityChange);
    env.removeEventListener?.('pagehide',onPageHide);
  }
  return {start,stop,snapshot,reset,dispose,collectReport};
}
