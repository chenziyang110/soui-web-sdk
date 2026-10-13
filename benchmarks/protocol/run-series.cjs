'use strict';

// Runs paired scenes in alternating order. A clean preflight is still not a
// physical-presentation, idle, startup, or product-workload verdict.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn, spawnSync, execFileSync} = require('node:child_process');
const {compare, loadRun} = require('./check-pair-runs.cjs');

const prohibited = new Set([
  'souiacceptance.exe', 'souibenchmark.exe', 'electron.exe',
  'souistarter.exe', 'souiworkbench.exe', 'installedrenewalprobe.exe',
  'renewalstarter.exe', 'upgradeledger.exe', 'scrollstarter.exe',
  'v02inputprobe.exe', 'git.exe', 'cmake.exe', 'ninja.exe',
  'msbuild.exe', 'cl.exe', 'link.exe', 'cdb.exe',
  'processtreesampler.exe', 'inputtrace.exe',
]);
const allowedOwnedWorkload = new Set([
  'souibenchmark.exe', 'electron.exe',
  'processtreesampler.exe', 'inputtrace.exe',
]);
const liveGateIntervalMs = 5000;

function isProhibitedProcessName(name) {
  const lower = name.toLowerCase();
  return prohibited.has(lower) || /^soui.*\.exe$/.test(lower) ||
    /^qemu.*\.exe$/.test(lower);
}

function requireFile(value, label) {
  if (!path.isAbsolute(value) || !fs.statSync(value).isFile())
    throw new Error(`${label} must be an existing absolute file`);
  return fs.realpathSync(value);
}

function requireDirectory(value, label) {
  if (!path.isAbsolute(value) || !fs.statSync(value).isDirectory())
    throw new Error(`${label} must be an existing absolute directory`);
  return fs.realpathSync(value);
}

function bundleDigest(root) {
  const entries = [];
  const walk = (directory, prefix = '') => {
    for (const item of fs.readdirSync(directory, {withFileTypes: true})) {
      const relative = prefix ? `${prefix}/${item.name}` : item.name;
      const file = path.join(directory, item.name);
      if (item.isDirectory()) walk(file, relative);
      else if (item.isFile()) entries.push({path: relative,
        bytes: fs.statSync(file).size,
        sha256: crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')});
      else throw new Error(`Unsupported page bundle entry: ${relative}`);
    }
  };
  walk(root);
  entries.sort((left, right) => left.path.localeCompare(right.path));
  return crypto.createHash('sha256').update(JSON.stringify(entries)).digest('hex');
}

function parseTasklistCsv(output) {
  const rows = output.trim().split(/\r?\n/);
  if (rows.length === 0 || !rows[0])
    throw new Error('Tasklist returned an empty process list');
  const processes = rows.map(row => {
    const fields = /^"([^"]+)","(\d+)"(?:,|$)/.exec(row);
    const pid = fields && Number(fields[2]);
    if (!fields || !Number.isSafeInteger(pid))
      throw new Error(`Tasklist returned an invalid process row: ${row}`);
    // tasklist includes this kernel placeholder; it is not a runnable workload.
    if (pid === 0 && fields[1].toLowerCase() === 'system idle process') return null;
    if (pid <= 0)
      throw new Error(`Tasklist returned an invalid process row: ${row}`);
    return {name: fields[1].toLowerCase(), pid};
  }).filter(Boolean);
  if (!processes.length)
    throw new Error('Tasklist returned no runnable processes');
  return processes;
}

function competingProcesses() {
  if (process.platform !== 'win32') throw new Error('The series runner requires Windows');
  const output = execFileSync('tasklist', ['/fo', 'csv', '/nh'], {
    encoding: 'utf8', windowsHide: true, maxBuffer: 4 * 1024 * 1024,
  });
  return parseTasklistCsv(output).filter(process => isProhibitedProcessName(process.name));
}

function interferenceCheck() {
  return {checkedAt: new Date().toISOString(), competingProcesses: competingProcesses()};
}

function creationTimestamp(value) {
  if (typeof value !== 'string') return NaN;
  const powershell = /^\/Date\(([0-9]+)(?:[+-][0-9]+)?\)\/$/.exec(value);
  return powershell ? Number(powershell[1]) : Date.parse(value);
}

function classifyLiveProcesses(processes, rootPid, observedOwned) {
  const owned = new Map(observedOwned);
  const present = new Map(processes.map(process => [process.pid, process.createdAt]));
  for (const process of processes) {
    if (process.pid === rootPid && owned.has(rootPid) &&
        owned.get(rootPid) !== process.createdAt)
      throw new Error('Benchmark runner process identity changed during a run');
    if (owned.has(process.pid) && owned.get(process.pid) !== process.createdAt)
      owned.delete(process.pid);
    if (process.pid === rootPid)
      owned.set(process.pid, process.createdAt);
  }
  let changed;
  do {
    changed = false;
    for (const process of processes) {
      if (owned.has(process.parentPid) &&
          present.get(process.parentPid) === owned.get(process.parentPid) &&
          !owned.has(process.pid)) {
        const parentTime = creationTimestamp(owned.get(process.parentPid));
        const childTime = creationTimestamp(process.createdAt);
        if (Number.isFinite(parentTime) && Number.isFinite(childTime) &&
            childTime < parentTime) continue;
        owned.set(process.pid, process.createdAt);
        changed = true;
      }
    }
  } while (changed);
  const competingProcesses = processes.filter(process => {
    return isProhibitedProcessName(process.name) &&
      (!owned.has(process.pid) ||
        !allowedOwnedWorkload.has(process.name.toLowerCase()));
  });
  return {owned, competingProcesses,
    rootPresent: present.has(rootPid) && present.get(rootPid) === owned.get(rootPid)};
}

function liveInterferenceCheck(rootPid, observedOwned) {
  if (process.platform !== 'win32') throw new Error('The series runner requires Windows');
  const script = "$ErrorActionPreference='Stop';" +
    "$p=@(Get-CimInstance Win32_Process -ErrorAction Stop | " +
    "Select-Object ProcessId,ParentProcessId,Name,CreationDate);" +
    "[ordered]@{processes=$p}|ConvertTo-Json -Depth 3 -Compress";
  const scan = spawnSync('powershell.exe', ['-NoProfile', '-Command', script], {
    encoding: 'utf8', windowsHide: true, timeout: 15000,
    env: {...process.env, PSModulePath: path.join(process.env.SystemRoot,
      'System32/WindowsPowerShell/v1.0/Modules')},
  });
  if (scan.error || scan.status !== 0 || scan.stderr.trim())
    throw new Error(`Live process query failed: ${scan.error || scan.stderr || scan.status}`);
  const decoded = JSON.parse(scan.stdout.replace(/^\uFEFF/, ''));
  const rows = Array.isArray(decoded.processes) ? decoded.processes :
    decoded.processes ? [decoded.processes] : [];
  const processes = rows.filter(row => row.ProcessId !== 0) // System Idle Process.
    .map(row => ({pid: Number(row.ProcessId), parentPid: Number(row.ParentProcessId),
      name: row.Name, createdAt: String(row.CreationDate)}));
  if (processes.some(row => !Number.isSafeInteger(row.pid) || row.pid <= 0 ||
      !Number.isSafeInteger(row.parentPid) || row.parentPid < 0 ||
      typeof row.name !== 'string' || !row.name ||
      !Number.isSafeInteger(creationTimestamp(row.createdAt)) ||
      creationTimestamp(row.createdAt) <= 0))
    throw new Error('Live process query returned invalid identity');
  const result = classifyLiveProcesses(processes, rootPid, observedOwned);
  return {checkedAt: new Date().toISOString(), owned: result.owned,
    competingProcesses: result.competingProcesses, rootPresent: result.rootPresent};
}

function assertIdle(check) {
  if (check.competingProcesses.length)
    throw new Error(`Competing process is running: ${check.competingProcesses
      .map(process => `${process.name} (PID ${process.pid})`).join(', ')}`);
}

function monitorChild(child, gateFile, pairNumber, host,
  scan = liveInterferenceCheck, intervalMs = liveGateIntervalMs) {
  return new Promise((resolve, reject) => {
    const observedOwned = new Map();
    let failure = null;
    let stopping = false;
    const appendGate = row => {
      try {
        fs.appendFileSync(gateFile, `${JSON.stringify(row)}\n`);
        return true;
      } catch (error) {
        if (!failure) failure = error;
        return false;
      }
    };
    const stopOwnRun = reason => {
      if (stopping || child.exitCode !== null || child.signalCode !== null || !child.pid) return;
      stopping = true;
      // The owned ChildProcess is safe to stop; /T can traverse a stale parent
      // PID that now names this runner and reach an unrelated older process.
      let killRequested = false;
      let cleanupError = null;
      try { killRequested = child.kill(); }
      catch (error) { cleanupError = String(error); }
      appendGate({checkedAt: new Date().toISOString(),
        pairNumber, host, ownRunPid: child.pid,
        ownRunCreatedAt: observedOwned.get(child.pid) ?? null, cleanupReason: reason,
        cleanupMethod: 'process-handle', cleanupCompletenessUnverified: true,
        cleanupSignalRequested: killRequested, cleanupError});
    };
    const poll = () => {
      if (stopping || child.exitCode !== null || child.signalCode !== null || !child.pid) return;
      try {
        const check = scan(child.pid, observedOwned);
        for (const [pid, createdAt] of check.owned) observedOwned.set(pid, createdAt);
        if (!observedOwned.has(child.pid))
          throw new Error('Live gate did not observe the runner process identity');
        if (!appendGate({checkedAt: check.checkedAt,
          pairNumber, host, ownRunPid: child.pid,
          ownRunCreatedAt: observedOwned.get(child.pid),
          ownedPidCount: observedOwned.size,
          rootPresent: check.rootPresent,
          competingProcesses: check.competingProcesses})) {
          stopOwnRun('gate-log-write-failed');
          return;
        }
        if (check.rootPresent !== true) {
          failure = new Error(`Benchmark runner process identity was absent during ${host} run`);
          stopOwnRun('runner-identity-absent');
          return;
        }
        if (check.competingProcesses.length) {
          failure = new Error(`External workload appeared during ${host} run: ${check.competingProcesses
            .map(process => `${process.name} (PID ${process.pid})`).join(', ')}`);
          stopOwnRun('external-workload');
        }
      } catch (error) {
        if (!failure) failure = error;
        appendGate({checkedAt: new Date().toISOString(),
          pairNumber, host, ownRunPid: child.pid, gateError: String(error)});
        stopOwnRun('gate-error');
      }
    };
    const timer = setInterval(poll, intervalMs);
    poll();
    child.once('error', error => { failure = error; });
    child.once('close', (code, signal) => {
      clearInterval(timer);
      if (failure) reject(failure);
      else if (code === 0 && observedOwned.has(child.pid))
        resolve({pid: child.pid, createdAt: observedOwned.get(child.pid)});
      else if (code === 0) reject(new Error('Runner exited without a verified live process identity'));
      else reject(new Error(`Single run failed: code=${code}, signal=${signal}`));
    });
  });
}

function runOne(args, gateFile, pairNumber, host) {
  const child = spawn(process.execPath, [path.join(__dirname, 'run-one.cjs'), ...args], {
    stdio: 'inherit', windowsHide: false,
  });
  return monitorChild(child, gateFile, pairNumber, host);
}

function frozenIdentity(run) {
  const record = run.record;
  return {machine: record.machine, pageSha256: record.pageSha256,
    pageFiles: record.pageFiles, hostExeSha256: record.hostExeSha256,
    dependency: record.dependency, identity: record.identity, tools: record.tools,
    ...(record.desktopObservation === true ? {desktopObservation:true} : {})};
}

async function main() {
  const arguments_ = process.argv.slice(2);
  const desktopObservation = arguments_.at(-1) === '--desktop-observe';
  if (desktopObservation) arguments_.pop();
  const [scene, pageArgument, souiArgument, nodeArgument, electronArgument,
    appArgument, samplerArgument, inputArgument, outputArgument,
    libraryArgument, lockArgument, roundsArgument] = arguments_;
  if (![11, 12].includes(arguments_.length) ||
      !['S01', 'S02', 'S04', 'S05'].includes(scene))
    throw new Error('Usage: node run-series.cjs S01|S02|S04|S05 ABS_PAGE_DIR '
      + 'ABS_SOUI_EXE ABS_NODE_DLL ABS_ELECTRON_EXE ABS_ELECTRON_APP_DIR '
      + 'ABS_SAMPLER_EXE ABS_INPUT_EXE ABS_NEW_OUTPUT_DIR ABS_SDK_LIB '
      + 'ABS_ELECTRON_LOCKFILE [ROUNDS=5] [--desktop-observe]');
  const rounds = roundsArgument === undefined ? 5 : Number(roundsArgument);
  if (!Number.isSafeInteger(rounds) || rounds < 1 || rounds > 20)
    throw new Error('ROUNDS must be an integer from 1 to 20');
  const page = requireDirectory(pageArgument, 'Shared page');
  const souiExe = requireFile(souiArgument, 'SOUI host');
  const nodeDll = requireFile(nodeArgument, 'Miniblink DLL');
  const electronExe = requireFile(electronArgument, 'Electron host');
  const app = requireDirectory(appArgument, 'Electron app');
  const sampler = requireFile(samplerArgument, 'Process sampler');
  const input = requireFile(inputArgument, 'External input driver');
  const library = requireFile(libraryArgument, 'Installed SDK library');
  const lock = requireFile(lockArgument, 'Electron lockfile');
  if (!path.isAbsolute(outputArgument) || fs.existsSync(outputArgument))
    throw new Error('Output must be a new absolute directory');
  const firstCheck = interferenceCheck();
  assertIdle(firstCheck);
  const root = path.resolve(outputArgument);
  fs.mkdirSync(root, {recursive: true});
  const record = {schema: 'soui.benchmark-series/v1', seriesId: crypto.randomUUID(),
    scene, plannedPairs: rounds, startedAt: new Date().toISOString(),
    order: 'alternating-soui-electron-and-electron-soui', checks: [firstCheck],
    pairs: [], benchmarkValid: false, releaseReady: false};
  if (desktopObservation) record.desktopObservation = true;
  const liveGateFile = path.join(root, 'live-interference-gates.ndjson');
  fs.writeFileSync(liveGateFile, '', {flag: 'wx'});
  record.liveInterferenceGate = {path: path.basename(liveGateFile),
    intervalMs: liveGateIntervalMs};
  try {
    const frozenPage = path.join(root, 'frozen-page');
    fs.cpSync(page, frozenPage, {recursive: true, errorOnExist: true, force: false});
    record.pageBundle = 'frozen-page';
    record.pageBundleSha256 = bundleDigest(frozenPage);
    for (let index = 0; index < rounds; ++index) {
      const order = index % 2 === 0 ? ['soui', 'electron'] : ['electron', 'soui'];
      const pair = {number: index + 1, order, runs: {}};
      record.pairs.push(pair);
      for (const host of order) {
        const before = interferenceCheck();
        record.checks.push(before);
        assertIdle(before);
        const runPath = path.join(root, `pair-${index + 1}-${host}`);
        const args = host === 'soui'
          ? ['soui', scene, frozenPage, souiExe, nodeDll, sampler, input, runPath, library]
          : ['electron', scene, frozenPage, electronExe, app, sampler, input, runPath, lock];
        if (desktopObservation) args.push('--desktop-observe');
        const runner = await runOne(args, liveGateFile, index + 1, host);
        if (!pair.runnerProcesses) pair.runnerProcesses = {};
        pair.runnerProcesses[host] = runner;
        pair.runs[host] = path.basename(runPath);
        const after = interferenceCheck();
        record.checks.push(after);
        assertIdle(after);
        if (bundleDigest(frozenPage) !== record.pageBundleSha256)
          throw new Error('Frozen series page bundle changed during a run');
      }
      const soui = loadRun(path.join(root, pair.runs.soui), 'soui');
      const electron = loadRun(path.join(root, pair.runs.electron), 'electron');
      const identities = {soui: frozenIdentity(soui), electron: frozenIdentity(electron)};
      if (record.fixedIdentities &&
          JSON.stringify(identities) !== JSON.stringify(record.fixedIdentities))
        throw new Error(`Pair ${index + 1} changed its frozen machine, page, host or tool identity`);
      if (!record.fixedIdentities) record.fixedIdentities = identities;
      const report = compare(soui, electron);
      pair.preflightAccepted = report.preflightAccepted;
      const reportPath = path.join(root, `pair-${index + 1}-preflight.json`);
      fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`, {flag: 'wx'});
      if (!report.preflightAccepted) throw new Error(`Pair ${index + 1} failed preflight`);
    }
    if (bundleDigest(frozenPage) !== record.pageBundleSha256)
      throw new Error('Frozen series page bundle changed before completion');
    record.status = 'preflight-completed';
  } catch (error) {
    record.status = 'stopped';
    record.error = error.message;
    process.exitCode = 1;
  } finally {
    record.finishedAt = new Date().toISOString();
    record.completedPairs = record.pairs.filter(pair => pair.preflightAccepted).length;
    fs.writeFileSync(path.join(root, 'series.json'), `${JSON.stringify(record, null, 2)}\n`,
      {flag: 'wx'});
    process.stdout.write(`${JSON.stringify({root, status: record.status,
      completedPairs: record.completedPairs,
      error: record.error ?? null, benchmarkValid: false, releaseReady: false})}\n`);
  }
}

if (require.main === module)
  main().catch(error => {console.error(error.message); process.exitCode = 1;});

module.exports = {competingProcesses, parseTasklistCsv, isProhibitedProcessName,
  classifyLiveProcesses, liveInterferenceCheck,
  monitorChild, frozenIdentity, bundleDigest};
