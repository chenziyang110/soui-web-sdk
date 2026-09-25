'use strict';

const {app, BrowserWindow, Menu, ipcMain, session} = require('electron');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {pathToFileURL} = require('node:url');

const scenes = new Set(['S01', 'S02', 'S03', 'S04', 'S05']);
const maxResultBytes = 8 * 1024 * 1024;

function option(name) {
  const prefix = `--${name}=`;
  const matches = process.argv.filter(arg => arg.startsWith(prefix));
  if (matches.length !== 1 || !matches[0].slice(prefix.length))
    throw new Error(`Exactly one ${prefix}<path> argument is required`);
  return path.resolve(matches[0].slice(prefix.length));
}

function validateResult(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      value.schemaVersion !== 1 || !scenes.has(value.scene) ||
      !['measured', 'invalid'].includes(value.status) ||
      value.hostKind !== 'electron' ||
      value.fixture?.fixtureId !== 'electron-smoothness-10k-v1' ||
      value.fixture?.rowCount !== 10000 ||
      !Number.isFinite(value.timing?.durationMs) ||
      !Array.isArray(value.js?.rafIntervalsMs))
    throw new Error('Invalid benchmark result');
  const body = JSON.stringify(value);
  if (Buffer.byteLength(body, 'utf8') > maxResultBytes)
    throw new Error('Benchmark result exceeds size limit');
  return `${body}\n`;
}

async function main() {
  const smoke = process.argv.includes('--smoke');
  const exitOnResult = process.argv.includes('--exit-on-result');
  if (process.argv.filter(arg => arg === '--smoke').length > 1)
    throw new Error('Duplicate --smoke option');
  if (process.argv.filter(arg => arg === '--exit-on-result').length > 1 || (smoke && exitOnResult))
    throw new Error('Expected at most one of --smoke or --exit-on-result');
  const page = option('benchmark-page');
  const resultDir = option('result-dir');
  if (path.basename(page).toLowerCase() !== 'benchmark.html' || !fs.statSync(page).isFile())
    throw new Error('The shared built benchmark.html is required');
  fs.mkdirSync(resultDir, {recursive: true});
  const pageUrl = pathToFileURL(page).href;
  const pageHash = crypto.createHash('sha256').update(fs.readFileSync(page)).digest('hex');
  const runId = crypto.randomUUID();
  let watchdog;
  const metadata = {
    schema: 'soui.benchmark-electron-run/v1',
    runId,
    startedAt: new Date().toISOString(),
    host: 'electron',
    versions: {electron: process.versions.electron, chromium: process.versions.chrome, node: process.versions.node},
    platform: process.platform,
    architecture: process.arch,
    pageSha256: pageHash,
    page,
    resultDir,
    security: {sandbox: true, contextIsolation: true, nodeIntegration: false, webSecurity: true},
  };
  fs.writeFileSync(path.join(resultDir, `${runId}-host.json`), `${JSON.stringify(metadata, null, 2)}\n`, {flag: 'wx'});
  session.defaultSession.setPermissionRequestHandler((_webContents, _permission, callback) => callback(false));
  Menu.setApplicationMenu(null);
  ipcMain.handle('benchmark:write-result', (event, result) => {
    if (event.sender !== window.webContents || event.sender.getURL().split('?')[0] !== pageUrl)
      throw new Error('Unrecognized benchmark sender');
    const body = validateResult(result);
    const name = `${runId}-${result.scene}-${crypto.randomUUID()}.json`;
    fs.writeFileSync(path.join(resultDir, name), body, {flag: 'wx'});
    if (smoke || exitOnResult) {
      clearTimeout(watchdog);
      setTimeout(() => app.quit(), 100);
    }
    return {ok: true};
  });
  const window = new BrowserWindow({
    width: 1100,
    height: 720,
    useContentSize: true,
    show: false,
    backgroundColor: '#ffffff',
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      sandbox: true,
      contextIsolation: true,
      nodeIntegration: false,
      webSecurity: true,
    },
  });
  window.webContents.setWindowOpenHandler(() => ({action: 'deny'}));
  window.webContents.on('will-navigate', (event, destination) => {
    if (destination.split('?')[0] !== pageUrl) event.preventDefault();
  });
  window.once('ready-to-show', () => window.show());
  await window.loadFile(page);
  console.log(JSON.stringify({event: 'benchmark.ready', runId, pageSha256: pageHash}));
  if (smoke) {
    watchdog = setTimeout(() => {
      console.error('Benchmark smoke result was not written');
      app.exit(2);
    }, 30000);
    const probe = async () => {
      try {
        if (await window.webContents.executeJavaScript('Boolean(window.__souiBenchmarkReady && window.__souiBenchmark)')) {
          await window.webContents.executeJavaScript('window.__souiBenchmark.finish()');
        } else setTimeout(probe, 100);
      } catch (error) {
        console.error(error);
        app.exit(2);
      }
    };
    void probe();
  }
}

app.whenReady().then(main).catch(error => {
  console.error(error);
  app.exit(1);
});
app.on('window-all-closed', () => app.quit());
