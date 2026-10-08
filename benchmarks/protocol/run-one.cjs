'use strict';

// One real desktop benchmark run. Pairing, repeated samples, and physical
// presentation are separate gates; a successful run is not a performance pass.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const os = require('node:os');
const {launchOwned} = require('../../tools/internal/owned-process.cjs');
const {checkRunTraces} = require('./check-run-traces.cjs');
const {readPublishedSubject, captureOwnedEvidence, readOwnedRun, describe, readRunLines} = require('./owned-trace-subject.cjs');

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

function launch(name, executable, args, cwd, output, env = process.env, ownership = {}) {
  const stdout = fs.openSync(path.join(output, name + '.stdout.log'), 'wx');
  const stderr = fs.openSync(path.join(output, name + '.stderr.log'), 'wx');
  let owned;
  try {
    owned = launchOwned(executable, args, {cwd, env, stdout, stderr,
      runner: ownership.runner || path.resolve(__dirname, '../../out/benchmark-protocol/Release/OwnedProcessRunner.exe'),
      report: path.join(output, name + '.owned.ndjson'), timeoutMs: ownership.timeoutMs || 260000});
  } finally { fs.closeSync(stdout); fs.closeSync(stderr); }
  let contextSha256, contextError;
  try { contextSha256 = sha(path.join(output, name + '.owned.ndjson.context.json')); }
  catch (error) { contextError = error; }
  const ready = owned.ready.then(subject => {
    if (contextError) throw contextError;
    return readPublishedSubject(output, name, subject, {contextSha256,
      runnerSha256: sha(ownership.runner || path.resolve(__dirname, '../../out/benchmark-protocol/Release/OwnedProcessRunner.exe')),
      executableSha256: sha(executable), executable, args, cwd});
  });
  void ready.catch(() => {});
  const finished = owned.finished.then(result => ({name, pid: result.pid, contextSha256,
    code: result.exitCode, signal: result.signal,
    error: result.passed ? null : result.spawnError || result.reportError || result.terminal?.reason || 'Owned run failed',
    ownedProcess: result}));
  return {name, ready, finished, stop: owned.stop, get pid() { return owned.pid; }};
}

function stop(runs, cleanup) {
  for (const run of runs) {
    if (!run.stop()) continue;
    const record = {name: run.name, pid: run.pid, method: 'held-job-stdin',
      stopRequested: true, treeCleanupVerified: false};
    if (cleanup) cleanup.push(record);
    void run.finished.then(result => {
      record.treeCleanupVerified = result.ownedProcess.closureConfirmed;
      record.nativeReportSha256 = result.ownedProcess.reportSha256;
    });
  }
}

function resultFile(directory) {
  const files = fs.readdirSync(directory).filter(name => name.endsWith('.json') &&
    !name.endsWith('-host.json'));
  if (files.length !== 1) throw new Error(`Expected one page result, found ${files.length}`);
  return path.join(directory, files[0]);
}

function validateHostIdentity(trace, subject) {
  if (!subject || trace.hostPid !== subject.pid ||
      trace.hostCreationTime100ns !== subject.creationFileTime || trace.exactFileTimeBound !== true)
    throw new Error('Trace host differs from the owned benchmark subject');
  return {pidBound: true, creationComparison: 'exact-native-filetime', exactFileTimeBound: true};
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
  const ownedRunner = requirePath(path.join(path.dirname(samplerExe), 'OwnedProcessRunner.exe'), 'owned process runner');
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
  const record = {schema: 'soui.benchmark-single-run/v2', runId, host, scene, startedAt, captureRoot:output,
    runnerPid: process.pid,
    machine: {hostname: os.hostname(), platform: os.platform(), release: os.release(),
      architecture: os.arch(), cpuModel: os.cpus()[0]?.model ?? null,
      logicalCpus: os.cpus().length, totalMemoryBytes: os.totalmem()},
    pageSha256: sha(benchmarkPage), pageBundle: 'page-bundle', pageFiles,
    hostExeSha256: sha(hostExe),
    dependency: host === 'soui' ? {miniblinkSha256: sha(dependency), miniblinkPath:dependency}
      : {appDirectory: dependency, appFiles},
    identity: identityFile ? {kind: host === 'soui' ? 'sdk-library' : 'electron-lockfile',
      bytes: fs.statSync(identityFile).size, sha256: sha(identityFile)} : null,
    tools: {samplerSha256: sha(samplerExe), inputSha256: sha(inputExe), ownedRunnerSha256: sha(ownedRunner)},
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
      host === 'soui' ? path.dirname(hostExe) : dependency, output, hostEnv,
      {runner: ownedRunner, timeoutMs: maxDurationMs + 10000});
    runs.push(hostRun);
    const subject = await hostRun.ready;
    const pid = String(subject.pid);
    runs.push(launch('sampler', samplerExe,
      [pid, '1000', String(maxDurationMs), path.join(output, 'process-tree.ndjson'), subject.creationFileTime],
      path.dirname(samplerExe), output, process.env, {runner: ownedRunner, timeoutMs: maxDurationMs + 10000}));
    runs.push(launch('input', inputExe,
      [pid, scene, path.join(output, 'input.ndjson'), 'full', subject.creationFileTime],
      path.dirname(inputExe), output, process.env, {runner: ownedRunner, timeoutMs: maxDurationMs + 10000}));
    for (const run of runs) {
      void run.finished.then(result => {
        if (result.code !== 0 || result.error) stop(runs, record.cleanup);
      });
    }
    timer = setTimeout(() => {timedOut = true; stop(runs, record.cleanup);}, maxDurationMs + 10000);
    await Promise.all(runs.map(run => run.ready));
    record.processes = await Promise.all(runs.map(run => run.finished));
    if (timedOut || record.processes.some(item => item.code !== 0 || item.error))
      throw new Error(timedOut ? 'Benchmark run exceeded deadline' : 'Benchmark process failed');
    record.ownedEvidence = captureOwnedEvidence(output, record.processes);
    record.traceEvidence = {input:describe(output, 'input.ndjson'), processTree:describe(output, 'process-tree.ndjson')};
    const verified = readOwnedRun(output, record);
    const trace = checkRunTraces(readRunLines(output, 'input.ndjson'),
      readRunLines(output, 'process-tree.ndjson'), scene, {ownedSubject:verified.subject});
    record.traceIdentity = validateHostIdentity(trace, verified.subject);
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
    if (!record.ownedEvidence && record.processes) {
      try { record.ownedEvidence = captureOwnedEvidence(output, record.processes); }
      catch (error) { record.evidenceError = error.message; }
    }
    record.finishedAt = new Date().toISOString();
    fs.writeFileSync(path.join(output, 'run.json'), `${JSON.stringify(record, null, 2)}\n`, {flag: 'wx'});
    process.stdout.write(`${JSON.stringify({output, status: record.status,
      error: record.error ?? null, runId, traceIntegrityAccepted: record.trace?.traceIntegrityAccepted ?? false})}\n`);
  }
}

if (require.main === module)
  main().catch(error => {console.error(error.message); process.exitCode = 1;});

module.exports = {launch, stop, validateHostIdentity};
