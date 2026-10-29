// Visible, default-GPU Electron wrapper for the unmodified packaged Scratch page.
const {app, BrowserWindow, ipcMain, session} = require('electron');
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const os = require('node:os');
const {setTimeout: delay} = require('node:timers/promises');

const options = {};
for (let index = 2; index < process.argv.length; index += 2) {
  const key = process.argv[index];
  if (!['--resources', '--baseline', '--output', '--runs', '--diagnostic'].includes(key) || !process.argv[index + 1])
    throw new Error('Expected --resources DIR --baseline JSON --output NEW-DIR [--runs 3].');
  options[key.slice(2)] = process.argv[index + 1];
}
if (!options.resources || !options.baseline || !options.output) throw new Error('Required paths are missing.');
const runs = Number(options.runs ?? 3);
if (!Number.isInteger(runs) || runs < 1 || runs > 3) throw new Error('Run count must be 1–3.');
if (options.diagnostic !== undefined && options.diagnostic !== 'profile') throw new Error('Only --diagnostic profile is supported.');
const output = path.resolve(options.output);
// Create before Chromium initializes userData, while refusing existing evidence.
require('node:fs').mkdirSync(output);
app.setPath('userData', path.join(output, 'profile'));
const entryUrl = 'https://soui-webview2.invalid/index.html';
const assetHost = new URL(entryUrl).host;
let window;
const failures = [], blockedRequests = [];
const record = {scope:'Same packaged Scratch editor and measurement code in visible Electron; historical user WebView2 baseline',
  startedUtc:new Date().toISOString(), runs:[], failures, blockedRequests};
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
async function save() {await fs.writeFile(path.join(output, 'session.json'), JSON.stringify(record, null, 2));}
async function waitFor(test, message, timeout = 90000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (!window || window.isDestroyed()) throw new Error('Comparison window was closed.');
    if (await test()) return;
    await delay(500);
  }
  throw new Error(message);
}
function evaluate(source, ...values) {
  return window.webContents.executeJavaScript(`(${source})(${values.map(value => JSON.stringify(value)).join(',')})`);
}
app.on('window-all-closed', () => {
  if (record.completedUtc) return app.quit();
  failures.push({kind:'window-closed-before-completion'});record.pass = false;
  save().finally(() => app.exit(1));
});
app.whenReady().then(async () => {
  const root = await fs.realpath(options.resources);
  const baselineBytes = await fs.readFile(options.baseline);
  const baseline = JSON.parse(baselineBytes);
  const baselineCollected = baseline.measurementVersion === 2
    ? baseline.validForWindowRateComparison === true && Number.isFinite(baseline.windowRafFps)
    : baseline.validForComparison === true;
  if (baseline.schema !== 'soui.scratch-performance/v1' || !baselineCollected ||
      baseline.sceneId !== 'sprites' || baseline.level !== 'heavy' || baseline.durationMs !== 30000 ||
      !Number.isInteger(baseline.viewport?.width) || !Number.isInteger(baseline.viewport?.height))
    throw new Error('This comparison expects a completed sprites/heavy 30-second baseline.');
  await fs.writeFile(path.join(output, 'webview2-user-baseline.json'), baselineBytes);
  record.baselineSha256 = sha256(baselineBytes);
  record.resourceRoot = root;
  record.resourceHashes = {};
  for (const name of ['index.html','app.js','app.css','performance-scenes.js','performance-meter.js','scratch-gui-standalone.js'])
    record.resourceHashes[name] = sha256(await fs.readFile(path.join(root, name)));
  record.harnessHashes = {};
  for (const name of ['main.cjs','preload.cjs']) record.harnessHashes[name] = sha256(await fs.readFile(path.join(__dirname, name)));
  record.environment = {electron:process.versions.electron, chromium:process.versions.chrome,
    node:process.versions.node, platform:process.platform, arch:process.arch,
    osRelease:os.release(), cpu:os.cpus()[0]?.model, logicalProcessors:os.cpus().length,
    preferences:{nodeIntegration:false,contextIsolation:true,sandbox:true,backgroundThrottling:true,zoomFactor:1},
    warmupMsPerRun:10000, durationMsPerRun:30000,
    procedure:'One scene loaded through existing page controls, then three consecutive samples; no simulated interaction during sampling.'};
  const mime = {'.html':'text/html;charset=utf-8','.js':'text/javascript;charset=utf-8','.css':'text/css',
    '.json':'application/json','.svg':'image/svg+xml','.png':'image/png','.wav':'audio/wav','.mp3':'audio/mpeg','.wasm':'application/wasm'};
  // Use the identical HTTPS origin: Scratch's Paper loader branches on protocol.
  // Every HTTPS request is intercepted locally; there is no certificate bypass.
  session.defaultSession.protocol.handle('https', async request => {
    try {
      const url = new URL(request.url);
      if (url.host !== assetHost || request.method !== 'GET') return new Response(null,{status:403});
      const file = path.resolve(root, '.' + decodeURIComponent(url.pathname));
      if (!file.startsWith(root + path.sep)) return new Response(null,{status:403});
      const real = await fs.realpath(file);
      if (!real.startsWith(root + path.sep)) return new Response(null,{status:403});
      const stat = await fs.stat(real);
      if (!stat.isFile() || stat.size > 32*1024*1024) return new Response(null,{status:403});
      return new Response(await fs.readFile(real),{headers:{'Content-Type':mime[path.extname(real)] ?? 'application/octet-stream'}});
    } catch (error) {failures.push({kind:'resource',url:request.url,error:String(error)});return new Response(null,{status:404});}
  });
  session.defaultSession.webRequest.onBeforeRequest((details, callback) => {
    const allowed = details.url.startsWith(`https://${assetHost}/`) || /^(about:|data:|blob:)/.test(details.url);
    if (!allowed) blockedRequests.push({url:details.url,resourceType:details.resourceType});
    callback({cancel:!allowed});
  });
  const identity = {sessionId:crypto.randomUUID(),origin:'app://scratch',token:crypto.randomUUID()};
  ipcMain.handle('scratch-comparison:request', (event, envelope) => {
    if (event.sender !== window.webContents || event.senderFrame !== window.webContents.mainFrame ||
        event.senderFrame.url !== entryUrl || typeof envelope !== 'string' || Buffer.byteLength(envelope) > 65536)
      throw new Error('Untrusted comparison caller.');
    const request = JSON.parse(envelope);
    let result;
    if (request.method === 'bridge.bootstrap') result = identity;
    else if (request.sessionId !== identity.sessionId || request.token !== identity.token || request.origin !== identity.origin)
      throw new Error('Invalid comparison session.');
    else if (request.method === 'app.info') result = {sdkVersion:baseline.metadata.sdkVersion,
      buildId:`Electron comparison ${process.versions.electron}`,backend:'Electron',runtimeVersion:process.versions.chrome};
    else if (request.method === 'bridge.probe.report') result = {ready:true};
    else return JSON.stringify({requestId:request.requestId,ok:false,error:{code:'E_UNSUPPORTED',message:'Rendering comparison only; native file actions are unavailable.'}});
    return JSON.stringify({requestId:request.requestId,ok:true,result});
  });
  window = new BrowserWindow({title:'Scratch · Electron 对照测试',width:baseline.viewport.width,height:baseline.viewport.height,
    useContentSize:true,show:true,autoHideMenuBar:true,
    webPreferences:{preload:path.join(__dirname,'preload.cjs'),nodeIntegration:false,contextIsolation:true,sandbox:true}});
  window.setMenu(null);
  window.webContents.setWindowOpenHandler(() => ({action:'deny'}));
  window.webContents.on('preload-error',(_,file,error) => failures.push({kind:'preload',file,error:String(error)}));
  window.webContents.on('did-fail-load',(_,code,description,url,isMainFrame) => {
    failures.push({kind:'load',code,description,url,isMainFrame});
  });
  window.webContents.on('console-message',(_,details,legacyMessage) => {
    if (details?.level === 'error' || details === 3)
      failures.push({kind:'console',message:details?.message ?? legacyMessage});
  });
  window.webContents.on('will-navigate',(event, url) => {if (url !== entryUrl) event.preventDefault();});
  window.webContents.on('render-process-gone',(_,details) => failures.push({kind:'renderer-gone',details}));
  let activeRun = null;
  for (const event of ['blur','minimize','hide','resize']) window.on(event,() => {
    if (activeRun !== null) failures.push({kind:'window-'+event,run:activeRun,utc:new Date().toISOString()});
  });
  await window.loadURL(entryUrl);
  window.focus();
  await waitFor(() => evaluate(() => !!window.souiScratch?.ready), 'Scratch editor did not become ready.');
  record.gpuFeatures = app.getGPUFeatureStatus();
  record.gpuInfo = await app.getGPUInfo('complete');
  record.environment.page = await evaluate(() => ({url:location.href,width:innerWidth,height:innerHeight,
    devicePixelRatio,visibility:document.visibilityState,hardwareConcurrency:navigator.hardwareConcurrency}));
  await save();
  if (options.diagnostic === 'profile') {
    const {runDiagnostic} = require('./diagnostic-driver.cjs');
    const debuggerApi = window.webContents.debugger;
    if (debuggerApi.isAttached()) throw new Error('The diagnostic consumer already has a debugger attached.');
    debuggerApi.attach('1.3');
    activeRun = 'diagnostic';
    try {
      record.diagnostic = await runDiagnostic({
        send: (method, params) => debuggerApi.sendCommand(method, params),
        evaluate: source => window.webContents.executeJavaScript(source),
        subscribe: callback => {
          const handler = (_event, method, params) => callback(method, params);
          debuggerApi.on('message', handler);
          return () => debuggerApi.removeListener('message', handler);
        },
        nativeGuard: async () => ({focused: window.isFocused(), visible: window.isVisible(), minimized: window.isMinimized(),
          bounds: window.getContentBounds(), nativeHandleHex: window.getNativeWindowHandle().toString('hex')})
      }, {output: path.join(output, 'diagnostic'), width: baseline.viewport.width, height: baseline.viewport.height,
        metadata: {runtime: record.environment, resourceRoot: root, resourceHashes: record.resourceHashes,
          harnessHashes: record.harnessHashes, hostPid: process.pid, gpuInfo: record.gpuInfo, gpuFeatures: record.gpuFeatures}});
    } finally {activeRun = null; if (debuggerApi.isAttached()) debuggerApi.detach();}
    record.completedUtc = new Date().toISOString();
    record.pass = record.diagnostic.collectionCompleted && record.diagnostic.pageGuardsQualified && failures.length === 0 && blockedRequests.length === 0;
    record.passKind = 'diagnostic collection only; profiler overhead, trace capability and native occlusion qualification are separate';
    record.formalPerformancePassed = null;
    await save();
    console.log(`DIAGNOSTIC COMPLETE: ${output}; CPU=${record.diagnostic.cpuProfileQualified}; trace=${record.diagnostic.traceQualified}; collection=${record.pass}.`);
    if (!record.pass) process.exitCode = 1;
    // This newly created diagnostic target owns its profile. Normal window/app
    // teardown is observed by the external owner; debugger detach is not proof.
    window.close();
    return;
  }
  console.log('READY: use 性能实验 → 重负载 → 载入并运行 → 继续. Samples start automatically after the scene is ready.');
  await waitFor(() => evaluate(() => window.souiScratchPerformance?.configuration?.sceneId === 'sprites' &&
    window.souiScratchPerformance.configuration.level === 'heavy' && window.souiScratch.vm.runtime.targets.length === 241 &&
    window.souiScratch.vm.runtime.threads.length === 240), 'Heavy scene was not loaded through the page controls.', 600000);
  const configuration = await evaluate(() => window.souiScratchPerformance.configuration);
  if (configuration.metadata.workloadSha256 !== baseline.metadata.workloadSha256) throw new Error('Workload SHA256 differs from the baseline.');
  for (let index = 1; index <= runs; ++index) {
    console.log(`WARMUP ${index}/${runs}: 10 seconds`);
    await delay(10000);
    const actual = await evaluate(() => ({width:innerWidth,height:innerHeight,devicePixelRatio,visibility:document.visibilityState}));
    if (actual.width !== baseline.viewport.width || actual.height !== baseline.viewport.height ||
        actual.devicePixelRatio !== baseline.viewport.devicePixelRatio || actual.visibility !== 'visible' ||
        !window.isFocused() || window.isMinimized()) throw new Error('Window size, DPI or foreground state differs from the baseline requirement.');
    activeRun = index;
    console.log(`MEASURE ${index}/${runs}: 30 seconds`);
    // Only the existing meter runs in the document during sampling; no remote polling/screenshots.
    const report = await evaluate(async () => {
      const perf = window.souiScratchPerformance;
      perf.meter.start({...perf.configuration,durationMs:30000});
      await new Promise(resolve => setTimeout(resolve,30300));
      return perf.meter.collectReport();
    });
    activeRun = null;
    const file = `electron-run-${index}.json`;
    await fs.writeFile(path.join(output,file),JSON.stringify(report,null,2));
    record.runs.push({file,windowRafFps:report.windowRafFps,rafFps:report.rafFps,
      rafCallbacks:report.rafCallbacks,censored:report.censored,p95Ms:report.frameP95Ms,p99Ms:report.frameP99Ms,
      validForWindowRateComparison:report.validForWindowRateComparison,
      validForComparison:report.validForComparison,windowEvents:failures.filter(entry => entry.run === index)});
    await save();
    if (report.measurementVersion !== 2 || !report.validForWindowRateComparison ||
        !Number.isFinite(report.windowRafFps) || report.targets !== 241 || report.threads !== 240 ||
        failures.some(entry => entry.run === index)) throw new Error(`Run ${index} was invalid.`);
    // Censored/zero-frame windows are retained poor results, never retried as
    // environment failures. Their closed-interval percentiles cannot qualify.
    console.log(`RESULT ${index}: window ${report.windowRafFps} callbacks/s; observed cadence ${report.rafFps}; censored=${report.censored}; P95 ${report.frameP95Ms} ms`);
  }
  await evaluate(() => window.souiScratch.vm.stopAll());
  record.completedUtc = new Date().toISOString();
  record.pass = failures.length === 0 && blockedRequests.length === 0 && record.runs.length === runs;
  record.passKind = 'collection-only; does not imply performance or percentile qualification';
  record.percentilesQualified = record.runs.every(run => run.validForComparison);
  await save();
  console.log(`COMPLETE: ${output}; pass=${record.pass}. Window remains open; scripts stopped.`);
  if (!record.pass) process.exitCode = 1;
}).catch(async error => {
  console.error(error);
  failures.push({kind:'harness',error:String(error),stack:error.stack});record.pass = false;
  try {await save();} catch {}
  app.exit(1);
});
