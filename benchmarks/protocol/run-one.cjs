'use strict';

// One real desktop benchmark run. Pairing, repeated samples, and physical
// presentation are separate gates; a successful run is not a performance pass.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const os = require('node:os');
const {spawn, spawnSync} = require('node:child_process');
const {checkRunTraces, readLines} = require('./check-run-traces.cjs');

function requirePath(value, kind) {
  if (!path.isAbsolute(value)) throw new Error(`${kind} must be an absolute path`);
  const target = fs.realpathSync(value);
  if (kind.endsWith('directory') ? !fs.statSync(target).isDirectory() : !fs.statSync(target).isFile())
    throw new Error(`${kind} has the wrong file type`);
  return target;
}

function sha(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function bundleFiles(root) {
  const output = [];
  const walk = (directory, prefix = '') => {
    for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
      const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) walk(file, relative);
      else if (entry.isFile()) output.push({path: relative,
        bytes: fs.statSync(file).size, sha256: sha(file)});
      else throw new Error(`Unsupported page bundle entry: ${relative}`);
    }
  };
  walk(root);
  return output.sort((left, right) => left.path.localeCompare(right.path));
}

function launch(name, executable, args, cwd, output, env = process.env) {
  const realExecutable = fs.realpathSync(executable);
  const stdout = fs.openSync(path.join(output, `${name}.stdout.log`), 'wx');
  const stderr = fs.openSync(path.join(output, `${name}.stderr.log`), 'wx');
  let child;
  const spawnRequestedAtMs = Date.now();
  try {
    child = spawn(executable, args, {cwd, env, windowsHide: name !== 'host',
      stdio: ['ignore', stdout, stderr]});
  } finally {
    fs.closeSync(stdout);
    fs.closeSync(stderr);
  }
  let settled = false;
  const finished = new Promise(resolve => {
    let spawnError = null;
    const complete = (code, signal, error) => {
      if (settled) return;
      settled = true;
      resolve({name, pid: child.pid ?? null, code, signal, error: error?.message ?? null});
    };
    child.once('error', error => { spawnError = error; });
    child.once('close', (code, signal) => complete(code, signal, spawnError));
  });
  return {child, finished, name, executable: realExecutable,
    spawnRequestedAtMs, spawnReturnedAtMs: Date.now(), stopRequested: false};
}

function normalizeExecutable(value) {
  return path.win32.normalize(fs.realpathSync.native(value)).replace(/^\\\\\?\\/, '').toLowerCase();
}

function verifiedDirectChild(run) {
  if (process.platform !== 'win32' || !run.child.pid) return false;
  const script = "$ErrorActionPreference='Stop';" +
    `$targetId=${run.child.pid};` +
    '$item=Get-CimInstance Win32_Process -Filter "ProcessId=$targetId" -ErrorAction Stop;' +
    'if($null -ne $item){[ordered]@{ProcessId=$item.ProcessId;ParentProcessId=$item.ParentProcessId;' +
    "ExecutablePath=$item.ExecutablePath;CreationUtc=$item.CreationDate.ToUniversalTime().ToString('o')}|ConvertTo-Json -Compress}";
  const query = spawnSync('powershell.exe', ['-NoProfile', '-Command', script], {
    encoding: 'utf8', windowsHide: true, timeout: 15000,
    env: {...process.env, PSModulePath: path.join(process.env.SystemRoot,
      'System32/WindowsPowerShell/v1.0/Modules')},
  });
  if (query.error || query.status !== 0 || query.stderr.trim() || !query.stdout.trim())
    return false;
  let identity;
  try { identity = JSON.parse(query.stdout.replace(/^\uFEFF/, '')); }
  catch { return false; }
  if (identity.ProcessId !== run.child.pid || identity.ParentProcessId !== process.pid ||
      typeof identity.ExecutablePath !== 'string' || !identity.ExecutablePath ||
      typeof identity.CreationUtc !== 'string' || !identity.CreationUtc) return false;
  const createdAtMs = Date.parse(identity.CreationUtc);
  if (!Number.isFinite(createdAtMs) ||
      !Number.isSafeInteger(run.spawnRequestedAtMs) ||
      !Number.isSafeInteger(run.spawnReturnedAtMs) ||
      run.spawnReturnedAtMs < run.spawnRequestedAtMs ||
      createdAtMs < run.spawnRequestedAtMs - 2000 ||
      createdAtMs > run.spawnReturnedAtMs + 2000) return false;
  try {
    return normalizeExecutable(identity.ExecutablePath) === normalizeExecutable(run.executable);
  } catch { return false; }
}

function stop(runs, cleanup) {
  for (const run of runs) {
    if (!run.child.pid || run.child.exitCode !== null || run.child.killed ||
        run.stopRequested) continue;
    run.stopRequested = true;
    const verified = verifiedDirectChild(run);
    const killed = verified ? spawnSync('taskkill.exe', ['/PID', String(run.child.pid),
      '/T', '/F'], {encoding: 'utf8', windowsHide: true, timeout: 15000}) : null;
    const treeStopped = killed && !killed.error && killed.status === 0;
    if (!treeStopped) run.child.kill();
    if (cleanup) cleanup.push({name: run.name, pid: run.child.pid,
      method: treeStopped ? 'verified-tree' : 'process-handle',
      treeCleanupVerified: !!treeStopped,
      taskkillExitCode: killed?.status ?? null,
      queryIdentityVerified: verified});
  }
}

function resultFile(directory) {
  const files = fs.readdirSync(directory).filter(name => name.endsWith('.json') &&
    !name.endsWith('-host.json'));
  if (files.length !== 1) throw new Error(`Expected one page result, found ${files.length}`);
  return path.join(directory, files[0]);
}

async function main() {
  const [host, scene, pageArgument, hostArgument, dependencyArgument,
    samplerArgument, inputArgument, outputArgument, identityArgument] = process.argv.slice(2);
  if (![10, 11].includes(process.argv.length) || !['soui', 'electron'].includes(host) ||
      !['S01', 'S02', 'S03', 'S04', 'S05'].includes(scene))
    throw new Error('Usage: node run-one.cjs soui|electron S01|... ABS_PAGE_DIRECTORY '
      + 'ABS_HOST_EXE ABS_NODE_DLL_OR_ELECTRON_APP_DIRECTORY ABS_SAMPLER_EXE ABS_INPUT_EXE '
      + 'ABS_NEW_OUTPUT_DIRECTORY [ABS_SDK_LIBRARY_OR_ELECTRON_LOCKFILE]');
  const page = requirePath(pageArgument, 'page directory');
  const hostExe = requirePath(hostArgument, 'host executable');
  const dependency = requirePath(dependencyArgument,
    host === 'soui' ? 'Miniblink DLL' : 'Electron app directory');
  const samplerExe = requirePath(samplerArgument, 'sampler executable');
  const inputExe = requirePath(inputArgument, 'input executable');
  const benchmarkPage = requirePath(path.join(page, 'benchmark.html'), 'benchmark page');
  const entryPage = requirePath(path.join(page, 'index.html'), 'index page');
  if (sha(benchmarkPage) !== sha(entryPage))
    throw new Error('Both hosts must load the same normal benchmark page');
  if (!path.isAbsolute(outputArgument) || fs.existsSync(outputArgument))
    throw new Error('Output must be a new absolute directory');
  const identityFile = identityArgument ? requirePath(identityArgument, 'identity file') : null;
  const pageFiles = bundleFiles(page);
  const appFiles = host === 'electron'
    ? ['main.cjs', 'preload.cjs', 'package.json', 'package-lock.json'].map(name => {
      const file = requirePath(path.join(dependency, name), 'Electron app file');
      return {path: name, bytes: fs.statSync(file).size, sha256: sha(file)};
    }) : null;
  const output = path.resolve(outputArgument);
  fs.mkdirSync(output, {recursive: true});
  const pageResults = path.join(output, 'page-results');
  fs.mkdirSync(pageResults);
  const pageSnapshot = path.join(output, 'page-bundle');
  try {
    fs.cpSync(page, pageSnapshot, {recursive: true, errorOnExist: true, force: false});
    if (JSON.stringify(bundleFiles(pageSnapshot)) !== JSON.stringify(pageFiles))
      throw new Error('Copied page bundle differs from the requested input');
  } catch (error) {
    fs.writeFileSync(path.join(output, 'setup-error.json'),
      `${JSON.stringify({status: 'failed', error: error.message, releaseReady: false})}\n`,
      {flag: 'wx'});
    throw error;
  }
  const maxDurationMs = scene === 'S05' ? 250000 : 195000;
  const runs = [];
  const startedAt = new Date().toISOString();
  const runId = crypto.randomUUID();
  const record = {schema: 'soui.benchmark-single-run/v1', runId, host, scene, startedAt,
    runnerPid: process.pid,
    machine: {hostname: os.hostname(), platform: os.platform(), release: os.release(),
      architecture: os.arch(), cpuModel: os.cpus()[0]?.model ?? null,
      logicalCpus: os.cpus().length, totalMemoryBytes: os.totalmem()},
    pageSha256: sha(benchmarkPage), pageBundle: 'page-bundle', pageFiles,
    hostExeSha256: sha(hostExe),
    dependency: host === 'soui' ? {miniblinkSha256: sha(dependency)}
      : {appDirectory: dependency, appFiles},
    identity: identityFile ? {kind: host === 'soui' ? 'sdk-library' : 'electron-lockfile',
      bytes: fs.statSync(identityFile).size, sha256: sha(identityFile)} : null,
    tools: {samplerSha256: sha(samplerExe), inputSha256: sha(inputExe)},
    maxDurationMs, cleanup: [], releaseReady: false, benchmarkValid: false};
  let timer;
  let timedOut = false;
  try {
    const args = host === 'soui'
      ? [`--resources=${pageSnapshot}`, `--miniblink=${dependency}`,
        `--result-dir=${pageResults}`, '--exit-on-result']
      : [dependency, `--benchmark-page=${path.join(pageSnapshot, 'benchmark.html')}`,
        `--result-dir=${pageResults}`, '--exit-on-result'];
    const hostEnv = {...process.env};
    delete hostEnv.ELECTRON_RUN_AS_NODE;
    const hostRun = launch('host', hostExe, args,
      host === 'soui' ? path.dirname(hostExe) : dependency, output, hostEnv);
    runs.push(hostRun);
    if (!hostRun.child.pid) throw new Error('Host process did not start');
    const pid = String(hostRun.child.pid);
    runs.push(launch('sampler', samplerExe,
      [pid, '1000', String(maxDurationMs), path.join(output, 'process-tree.ndjson')],
      path.dirname(samplerExe), output));
    runs.push(launch('input', inputExe,
      [pid, scene, path.join(output, 'input.ndjson'), 'full'],
      path.dirname(inputExe), output));
    for (const run of runs) {
      void run.finished.then(result => {
        if (result.code !== 0 || result.error) stop(runs, record.cleanup);
      });
    }
    timer = setTimeout(() => {timedOut = true; stop(runs, record.cleanup);}, maxDurationMs + 10000);
    record.processes = await Promise.all(runs.map(run => run.finished));
    if (timedOut || record.processes.some(item => item.code !== 0 || item.error))
      throw new Error(timedOut ? 'Benchmark run exceeded deadline' : 'Benchmark process failed');
    const trace = checkRunTraces(readLines(path.join(output, 'input.ndjson')),
      readLines(path.join(output, 'process-tree.ndjson')), scene);
    const pageResultPath = resultFile(pageResults);
    const pageResult = JSON.parse(fs.readFileSync(pageResultPath, 'utf8'));
    if (JSON.stringify(bundleFiles(pageSnapshot)) !== JSON.stringify(pageFiles))
      throw new Error('Copied page bundle changed during the run');
    if (pageResult.scene !== scene || pageResult.status !== 'measured' ||
        pageResult.hostKind !== (host === 'soui' ? 'soui-miniblink' : 'electron') ||
        pageResult.sceneChecks?.completed !== true ||
        !Number.isFinite(pageResult.timing?.durationMs) ||
        pageResult.timing.durationMs < (scene === 'S05' ? 120000 : 60000))
      throw new Error('Page result did not complete the requested full scene');
    record.trace = trace;
    record.pageResult = {path: path.relative(output, pageResultPath).replaceAll('\\', '/'),
      bytes: fs.statSync(pageResultPath).size, sha256: sha(pageResultPath),
      durationMs: pageResult.timing.durationMs};
    record.status = 'completed';
  } catch (error) {
    stop(runs, record.cleanup);
    record.status = 'failed';
    record.error = error.message;
    if (!record.processes) record.processes = await Promise.all(runs.map(run => run.finished));
    process.exitCode = 1;
  } finally {
    clearTimeout(timer);
    record.finishedAt = new Date().toISOString();
    fs.writeFileSync(path.join(output, 'run.json'), `${JSON.stringify(record, null, 2)}\n`, {flag: 'wx'});
    process.stdout.write(`${JSON.stringify({output, status: record.status,
      error: record.error ?? null, runId, traceIntegrityAccepted: record.trace?.traceIntegrityAccepted ?? false})}\n`);
  }
}

if (require.main === module)
  main().catch(error => {console.error(error.message); process.exitCode = 1;});

module.exports = {launch, stop, verifiedDirectChild};
