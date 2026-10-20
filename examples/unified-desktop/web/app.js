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
if (!window.souiDesktop) {
  document.getElementById('status').textContent = 'Open this page with the native example executable.';
} else {
  window.souiDesktop.on('native.ready', value => record('native.ready', value));
  window.souiDesktop.on('native.command', value => record('native.command', value));
  invoke('app.capabilities').then(caps => {
    document.getElementById('status').textContent = `${caps.backend} / local Bridge connected`;
    document.getElementById('dot').classList.add('connected');
    for (const [action, available] of [['copy', caps.clipboardWrite], ['paste', caps.clipboardRead],
      ['directory', caps.directoryDialog], ['menu', caps.popupMenu], ['tray', caps.tray], ['clearTray', caps.tray]])
      document.querySelector(`[data-action="${action}"]`).disabled = !available;
  }).catch(() => {});
  const actions = {
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
