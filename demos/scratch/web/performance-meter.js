// These measurements describe JavaScript rAF scheduling in this document.
// They do not measure input-to-photon latency, GPU presentation or native memory.
const defaultCapacity = 2000;
const longFrameMs = 50;
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
 * Percentiles cover the retained interval window, explicitly marked as rolling
 * if a run exceeds capacity. FPS, long-frame counts and histogram cover the run.
 */
export function createPerformanceMeter({getVM = () => null, onUpdate = () => {}, environment = globalThis,
  sampleCapacity = defaultCapacity} = {}) {
  if (typeof getVM !== 'function' || typeof onUpdate !== 'function') throw new TypeError('Meter callbacks must be functions.');
  if (!Number.isInteger(sampleCapacity) || sampleCapacity < 600 || sampleCapacity > defaultCapacity)
    throw new RangeError('Frame sample capacity must be between 600 and 2000.');
  const env = environment;
  const document = env.document;
  const performance = env.performance;
  const now = () => performance?.now?.() ?? Date.now();
  let state, raf = null, deadline = null, observer = null, disposed = false, runId = 0;

  function empty() {
    return {status:'idle', startedAt:null, endedAt:null, startedUtc:null, durationMs:30000,
      sceneId:null, level:null, metadata:{}, invalidReasons:[], samples:[], sampleCursor:0,
      intervalsObserved:0, intervalSum:0, longFrames:0, maxFrameMs:null,
      histogram:[0,0,0,0,0], lastFrame:null, lastUpdate:null, stopReason:null,
      longTaskSupported:false, longTaskCount:0, longTaskTotalMs:0, longTaskMaxMs:null,
      heapSupported:false, heapBytes:null, heapPeakBytes:null, runtimeAtEnd:null, updateError:null};
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
      return {targets:targets.length,
        sprites:targets.filter(target => !target.isStage && target.isOriginal !== false).length,
        clones:targets.filter(target => !target.isStage && target.isOriginal === false).length,
        threads:Array.isArray(current.threads) ? current.threads.length : null};
    } catch { return unavailable; }
  }
  function chronologicalIntervals() {
    if (state.samples.length < sampleCapacity || state.sampleCursor === 0) return [...state.samples];
    return state.samples.slice(state.sampleCursor).concat(state.samples.slice(0,state.sampleCursor));
  }
  function snapshot() {
    if (state.status === 'running') heap();
    const elapsed = state.startedAt === null ? 0 : Math.max(0, (state.endedAt ?? now()) - state.startedAt);
    const summary = summarizeFrameIntervals(state.samples);
    return {
      recording:state.status === 'running', completed:state.status === 'completed', status:state.status,
      valid:state.startedAt !== null && state.invalidReasons.length === 0,
      invalidReasons:[...state.invalidReasons], stopReason:state.stopReason,
      sceneId:state.sceneId, level:state.level, durationMs:state.durationMs, elapsedMs:precision(elapsed),
      rafFps:state.intervalSum > 0 ? precision(1000 * state.intervalsObserved / state.intervalSum) : null,
      frameP50Ms:summary.p50Ms, frameP95Ms:summary.p95Ms, frameP99Ms:summary.p99Ms,
      maxFrameMs:precision(state.maxFrameMs), longFrames:state.longFrames,
      intervalsObserved:state.intervalsObserved, sampledIntervals:summary.count,
      sampleCoverage:state.intervalsObserved > state.samples.length ? 'rolling-window' : 'complete',
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
    state.endedAt = now();
    heap(); state.runtimeAtEnd = runtime();
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
  function start({sceneId, scene, level = 'unspecified', durationMs = 30000, metadata = {}} = {}) {
    if (disposed) throw new Error('Performance meter is disposed.');
    if (typeof env.requestAnimationFrame !== 'function' || typeof env.setTimeout !== 'function')
      throw new Error('requestAnimationFrame and timers are required.');
    if (!Number.isFinite(durationMs) || durationMs < 500 || durationMs > 120000)
      throw new RangeError('Measurement duration must be between 500 and 120000 ms.');
    const serializedMetadata = JSON.stringify(metadata);
    if (!serializedMetadata || serializedMetadata.length > 2048) throw new RangeError('Measurement metadata exceeds its bound.');
    finish('restarted');
    state = empty(); ++runId;
    state.sceneId = String(sceneId ?? scene ?? 'unspecified').slice(0,128);
    state.level = String(level).slice(0,64);
    state.metadata = JSON.parse(serializedMetadata);
    state.durationMs = durationMs;
    state.status = 'running'; state.startedAt = now(); state.startedUtc = new Date().toISOString();
    if (document?.visibilityState === 'hidden') return finish('document-hidden-at-start',true);
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
      validForComparison:measured.completed && measured.valid && measured.intervalsObserved >= 2,
      frameTimePercentileScope:measured.sampleCoverage === 'complete' ? 'all observed rAF intervals' : `last ${sampleCapacity} observed rAF intervals`,
      histogram:{boundsMs:['<=8.33','<=16.67','<=33.33','<=50','>50'],counts:[...state.histogram]},
      ...(includeIntervals ? {frameIntervalsMs:chronologicalIntervals().map(precision)} : {}),
      limitations:[
        'rAF callback cadence is not GPU/display presentation or physical input-to-photon latency.',
        'JS heap observations do not include total renderer, GPU or native process memory.',
        'Hidden documents invalidate the run; lack of a visibility event cannot prove the window was unobscured.',
        'Percentiles use the explicitly reported bounded interval window; counters and FPS cover the run.'
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
