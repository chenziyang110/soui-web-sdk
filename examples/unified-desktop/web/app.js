'use strict';
const log = document.getElementById('log');
const lines = [];
function record(label, value) {
  lines.unshift(`${label}  ${typeof value === 'string' ? value : JSON.stringify(value)}`);
  if (lines.length > 30) lines.length = 30;
  log.textContent = lines.join('\n');
}
async function invoke(method, params) {
  try { const result = await window.souiDesktop.invoke(method, params); record(method, result); return result; }
  catch (error) { record(error.code || 'error', error.message); throw error; }
}
// Keep the classic-script example usable without the optional HTTP module origin.
// The SDK ESM windowControls facade wraps these same invoke/on operations.
let stateRevision = 0;
function showWindowState(state) {
  if (!state || typeof state.minimized !== 'boolean' || typeof state.maximized !== 'boolean') {
    record('window state error', 'Malformed observed state');
    return;
  }
  ++stateRevision;
  document.getElementById('window-state').textContent =
    `Observed: minimized=${state.minimized}, maximized=${state.maximized}`;
}
async function readWindowState() {
  const revision = stateRevision;
  const state = await invoke('desktop.window.getState', {});
  // An intervening stateChanged event is newer than this query's snapshot.
  if (revision === stateRevision) showWindowState(state);
}
if (!window.souiDesktop) {
  document.getElementById('status').textContent = 'Open this page with the native example executable.';
} else {
  const protection = document.getElementById('close-protection');
  const closeStatus = document.getElementById('close-status');
  let closeProtected = false;
  protection.checked = false;
  protection.disabled = false;
  protection.addEventListener('change', async () => {
    protection.disabled = true;
    try {
      closeProtected = await invoke('window.setCloseProtection', protection.checked);
      closeStatus.textContent = closeProtected ?
        'Unsaved-change protection is active for this window.' : 'This window can be closed.';
    } catch (error) {
      closeStatus.textContent = 'Could not update close protection. The previous setting is unchanged.';
    } finally {
      protection.checked = closeProtected;
      protection.disabled = false;
    }
  });
  window.souiDesktop.on('native.ready', value => {
    closeProtected = false;
    protection.checked = false;
    closeStatus.textContent = 'This window can be closed.';
    record('native.ready', value);
  });
  window.souiDesktop.on('app.closeBlocked', value => {
    closeStatus.textContent = value.message;
    record('app.closeBlocked', value);
  });
  window.souiDesktop.on('native.command', value => record('native.command', value));
  window.souiDesktop.on('desktop.window.stateChanged', showWindowState);
  readWindowState().catch(() => {});
  invoke('app.capabilities').then(caps => {
    document.getElementById('status').textContent = `${caps.backend} / local Bridge connected`;
    document.getElementById('dot').classList.add('connected');
    document.querySelector('[data-action="modules"]').disabled = !caps.resourceServer;
    for (const [action, available] of [['copy', caps.clipboardWrite], ['paste', caps.clipboardRead],
      ['directory', caps.directoryDialog], ['menu', caps.popupMenu], ['tray', caps.tray], ['clearTray', caps.tray]])
      document.querySelector(`[data-action="${action}"]`).disabled = !available;
  }).catch(() => {});
  const actions = {
    modules: async () => {
      try {
        const feature = await import('./feature.mjs');
        record('ES modules / fetch', await feature.loadConfiguration());
      } catch (error) { record('resource error', error.message); }
    },
    asset: async () => {
      try {
        const asset = await window.souiDesktop.readAsset('config.json');
        const bytes = Uint8Array.from(atob(asset.base64), value => value.charCodeAt(0));
        const config = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
        record('local asset', { mime: asset.mime, size: asset.size, config });
      } catch (error) { record(error.code || 'asset error', error.message); }
    },
    echo: () => invoke('app.echo', { greeting: 'Hello / 你好', values: [1, true, null] }),
    title: () => invoke('window.title'),
    resize: () => invoke('window.resize'),
    minimize: () => invoke('desktop.window.minimize', {}),
    maximize: () => invoke('desktop.window.maximize', {}),
    restore: () => invoke('desktop.window.restore', {}),
    state: readWindowState,
    copy: () => invoke('clipboard.write'),
    paste: () => invoke('clipboard.read'),
    directory: () => invoke('directory.choose'),
    reload: () => invoke('app.reload'),
    create: () => invoke('window.create'),
    close: () => invoke('window.close'),
    menu: () => invoke('window.menu'),
    tray: () => invoke('window.tray'),
    clearTray: () => invoke('window.clearTray'),
    quit: () => invoke('app.quit'),
  };
  for (const button of document.querySelectorAll('[data-action]')) button.addEventListener('click', () => {
    const action = actions[button.dataset.action];
    if (action) action().catch(() => {});
  });
}
