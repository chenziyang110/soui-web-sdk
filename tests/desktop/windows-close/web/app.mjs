import { invoke, windowControls } from './desktop.mjs';
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
try {
  const { mode } = await invoke('fixture.start', null);
  // Real DOM calls. WebView2 may coalesce/suppress immediate repeat events.
  window.close();
  window.close();
  window.close();
  if (mode === 'veto') {
    const until = performance.now() + 2000;
    let state;
    do {
      await delay(40);
      state = await invoke('fixture.state', null);
    } while (!state.requests && performance.now() < until);
    // Leave time for any queued duplicate proposal; veto must protect all of them.
    await delay(100);
    state = await invoke('fixture.state', null);
    const nativeState = await windowControls.getState();
    const ok = state.requests >= 1 && state.requests <= 3 && state.closed === 0 &&
      typeof nativeState.maximized === 'boolean';
    document.getElementById('status').textContent = JSON.stringify({ ok, state });
    await invoke('fixture.vetoDone', { ok, state });
  }
} catch (error) {
  void invoke('fixture.fail', { reason: String(error?.code || error?.message).slice(0, 96) });
}
