(function createScratchDiagnosticProbe() {
  'use strict';
  const api = window.souiScratchPerformance;
  const vm = window.souiScratch && window.souiScratch.vm;
  if (!api || !api.configuration || !vm || !vm.runtime) throw Error('Load the identical performance scene first');
  const configuration = api.configuration;
  const events = [], timerIntervalsMs = [];
  let timer = null, started = null, previous = null, overflow = false, finished = false;
  const event = kind => {
    if (events.length < 64) events.push({kind, now: performance.now(), utc: new Date().toISOString()});
    else overflow = true;
  };
  const visibility = () => event('visibility:' + document.visibilityState);
  const resize = () => event('resize');
  const pagehide = () => event('pagehide');
  const focus = () => event('focus');
  const blur = () => event('blur');
  document.addEventListener('visibilitychange', visibility);
  window.addEventListener('resize', resize);
  window.addEventListener('pagehide', pagehide);
  window.addEventListener('focus', focus);
  window.addEventListener('blur', blur);
  function snapshot() {
    const runtime = vm.runtime, targets = runtime.targets || [];
    let originals = 0, clones = 0;
    for (const target of targets) if (target.isOriginal === false) ++clones; else ++originals;
    let graphics = null;
    try {
      const gl = runtime.renderer && runtime.renderer._gl;
      const debug = gl && gl.getExtension('WEBGL_debug_renderer_info');
      if (gl) graphics = {vendor: gl.getParameter(gl.VENDOR), renderer: gl.getParameter(gl.RENDERER),
        version: gl.getParameter(gl.VERSION), unmaskedVendor: debug ? gl.getParameter(debug.UNMASKED_VENDOR_WEBGL) : null,
        unmaskedRenderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : null};
    } catch (_) { graphics = null; }
    return {utc: new Date().toISOString(), now: performance.now(), timeOrigin: performance.timeOrigin,
      url: location.href, visibility: document.visibilityState, width: innerWidth, height: innerHeight,
      devicePixelRatio, hasFocus: document.hasFocus(), configuration: JSON.parse(JSON.stringify(api.configuration)),
      sameConfiguration: api.configuration === configuration, targets: targets.length, originals, clones,
      threads: runtime.threads ? runtime.threads.length : null, runtimeStepMs: runtime.currentStepTime,
      graphics};
  }
  const before = snapshot();
  function cleanup() {
    if (timer !== null) { clearInterval(timer); timer = null; }
    document.removeEventListener('visibilitychange', visibility);
    window.removeEventListener('resize', resize);
    window.removeEventListener('pagehide', pagehide);
    window.removeEventListener('focus', focus);
    window.removeEventListener('blur', blur);
  }
  return {
    warmup: before,
    snapshot,
    start() {
      if (started !== null || finished || !snapshot().sameConfiguration) throw Error('Diagnostic probe start state changed');
      if (api.meter.snapshot().recording) throw Error('An existing page measurement is active');
      started = previous = performance.now();
      performance.mark('soui-diagnostic-capture-start');
      api.meter.start({...configuration, durationMs: 10000});
      timer = setInterval(() => {
        const now = performance.now();
        if (timerIntervalsMs.length < 2048) timerIntervalsMs.push(now - previous); else overflow = true;
        previous = now;
      }, 16);
      return snapshot();
    },
    finish() {
      if (started === null || finished) throw Error('Diagnostic probe has not started or already finished');
      finished = true;
      const ended = performance.now(); cleanup();
      performance.mark('soui-diagnostic-capture-end');
      const meterFinishedNaturally = !api.meter.snapshot().recording;
      if (!meterFinishedNaturally) api.meter.stop(); // Keep the actual stopped status, never invent completion.
      const report = api.meter.collectReport(), after = snapshot();
      const workloadStable = after.sameConfiguration && JSON.stringify(before.configuration) === JSON.stringify(after.configuration) &&
        before.targets === after.targets && before.originals === after.originals && before.clones === after.clones &&
        before.threads === after.threads && before.runtimeStepMs === after.runtimeStepMs;
      return {schema: 'soui.scratch-diagnostic-page/v1', diagnosticOnly: true, formalPerformancePassed: null,
        before, after, actualElapsedMs: ended - started, timerProbeIntervalMs: 16, timerIntervalsMs,
        timerLatenessMs: timerIntervalsMs.map(value => Math.max(0, value - 16)), events, overflow,
        workloadStable,
        pageGuardsQualified: workloadStable && !overflow && before.hasFocus && after.hasFocus && before.visibility === 'visible' && after.visibility === 'visible' &&
          before.width === after.width && before.height === after.height && before.devicePixelRatio === after.devicePixelRatio &&
          after.sameConfiguration && events.length === 0,
        pageReport: report, meterFinishedNaturally,
        limitations: ['Probe timer and CPU/browser profiling add overhead.',
          'Timer lateness is observed interval minus requested 16ms, not an engine deadline or a present timestamp.',
          'Endpoint workload checks do not prove every intermediate logical tick.',
          'DOM visibility/focus do not prove native foreground or absence of occlusion.']};
    },
    abort() {
      cleanup();
      if (started !== null && api.meter.snapshot().recording) api.meter.stop();
      finished = true;
      return {aborted: true, diagnosticOnly: true};
    }
  };
})
