'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const buildEvidence = require(path.join(repo, 'tools/internal/build-evidence.cjs'));
const [architecture, attemptName] = process.argv.slice(2);
assert.equal(process.argv.length, 4);
assert(['x64', 'x86'].includes(architecture));
assert(/^[a-z0-9-]+$/.test(attemptName));

const expectedSource = '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d';
const expectedBuildId = architecture === 'x64' ?
  '5ddcec93-d244-456c-877b-1bdeabee568d' : 'f0c38056-058a-456e-be99-17446127f7ec';
const environment = {...process.env,
  PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')};
for (const key of Object.keys(environment)) if (key.startsWith('SOUI_')) delete environment[key];
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

function gate(owned = 0) {
  const script = "$ErrorActionPreference='Stop';$all=@(Get-CimInstance Win32_Process -ErrorAction Stop);" +
    "$own=[Collections.Generic.HashSet[int]]::new();if(" + Number(owned) + " -gt 0){[void]$own.Add(" + Number(owned) + ")};" +
    "do{$changed=$false;foreach($p in $all){if($own.Contains([int]$p.ParentProcessId) -and $own.Add([int]$p.ProcessId)){$changed=$true}}}while($changed);" +
    "$busy=@($all|Where-Object{$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$' -and -not $own.Contains([int]$_.ProcessId)});" +
    "[ordered]@{busy=@($busy|Select-Object ProcessId,ParentProcessId,Name,CreationDate)}|ConvertTo-Json -Depth 4 -Compress";
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
    {env: environment, encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const parsed = JSON.parse(result.stdout);
  return {at: new Date().toISOString(), busy: Array.isArray(parsed.busy) ? parsed.busy :
    parsed.busy ? [parsed.busy] : []};
}
function verifyBuild() {
  const report = buildEvidence.verifyBuild(repo, architecture, 'Release');
  assert.equal(report.version, '0.3.0-preview.15');
  assert.equal(report.source.sha256, expectedSource);
  assert.equal(report.buildId, expectedBuildId);
  const executable = path.join(repo, 'out', architecture, 'bin/Release/SouiWorkbench/SouiWorkbench.exe');
  const entry = report.payloads.SouiWorkbench.files.find(file => file.path === 'SouiWorkbench.exe');
  assert(entry && sha(executable) === entry.sha256, 'Workbench executable differs from formal build');
  return {buildId: report.buildId, sourceSha256: report.source.sha256,
    payloadSha256: report.payloads.SouiWorkbench.sha256,
    executable, executableSha256: entry.sha256};
}
const before = gate();
if (before.busy.length) {
  console.log(JSON.stringify({status: 'busy-no-launch', before}));
  process.exitCode = 2;
  return;
}
const attempt = path.join(root, attemptName);
fs.mkdirSync(attempt);
const state = {schema: 'soui.v02.visible-run/v2', architecture, attemptName,
  startedAt: new Date().toISOString(), status: 'preparing',
  beforeGate: before, buildBefore: null, launch: null,
  interference: null, gateError: null, timedOut: false,
  nativeExitCode: null, signal: null, forcedCleanup: null,
  buildAfter: null, afterGate: null, realInputVerified: false, releaseReady: false};
const save = () => fs.writeFileSync(path.join(attempt, 'result.json'), JSON.stringify(state, null, 2) + '\n');
const gates = fs.openSync(path.join(attempt, 'gates.ndjson'), 'wx');
let child = null;
let out = null;
let err = null;
let monitor = null;
let timeout = null;
let stopping = false;
function recordGate(value) {
  fs.writeSync(gates, JSON.stringify(value) + '\n');
  fs.fsyncSync(gates);
}
function stopOwn(reason) {
  if (stopping || !child?.pid || child.exitCode !== null || child.signalCode !== null) return;
  stopping = true;
  const result = cp.spawnSync('taskkill.exe', ['/PID', String(child.pid), '/T', '/F'],
    {windowsHide: true, timeout: 15000});
  state.forcedCleanup = {reason, at: new Date().toISOString(), exitCode: result.status,
    error: result.error ? String(result.error) : null};
  save();
}
async function main() {
  save();
  try {
    state.buildBefore = verifyBuild();
    const second = gate();
    recordGate(second);
    if (second.busy.length) throw Error('external workload appeared before launch');
    const args = ['--react', '--page=data'];
    out = fs.openSync(path.join(attempt, 'application.stdout.log'), 'wx');
    err = fs.openSync(path.join(attempt, 'application.stderr.log'), 'wx');
    child = cp.spawn(state.buildBefore.executable, args,
      {cwd: path.dirname(state.buildBefore.executable), env: environment, windowsHide: false,
        stdio: ['ignore', out, err]});
    state.launch = {program: state.buildBefore.executable, args, pid: child.pid ?? null,
      at: new Date().toISOString(), supervisorSha256: sha(__filename)};
    state.status = 'running';
    save();
    let spawnError = null;
    child.once('error', error => {spawnError = String(error);});
    monitor = setInterval(() => {
      if (stopping || child.exitCode !== null || child.signalCode !== null) return;
      try {
        const observation = gate(child.pid);
        recordGate(observation);
        if (observation.busy.length) {
          state.interference = observation;
          stopOwn('external-workload');
        }
      } catch (error) {
        state.gateError = String(error);
        stopOwn('gate-error');
      }
    }, 5000);
    timeout = setTimeout(() => {state.timedOut = true; stopOwn('run-timeout');}, 600000);
    const result = await new Promise(resolve => child.once('close', (code, signal) => resolve({code, signal})));
    state.nativeExitCode = result.code;
    state.signal = result.signal;
    state.spawnError = spawnError;
    state.status = state.interference ? 'interrupted-external' : state.gateError ?
      'gate-error' : state.timedOut ? 'timeout' : spawnError ? 'spawn-error' :
      result.code === 0 ? 'native-exit-zero' : 'native-nonzero';
  } catch (error) {
    state.status = 'prelaunch-failed';
    state.error = String(error.stack || error);
  } finally {
    if (monitor) clearInterval(monitor);
    if (timeout) clearTimeout(timeout);
    if (out !== null) fs.closeSync(out);
    if (err !== null) fs.closeSync(err);
    try {
      state.afterGate = gate();
      recordGate(state.afterGate);
      if (state.afterGate.busy.length) state.postflightDeferredDueToExternal = true;
      else {
        state.buildAfter = verifyBuild();
        assert.deepEqual(state.buildAfter, state.buildBefore);
      }
    } catch (error) {
      state.postflightError = String(error.stack || error);
    }
    fs.closeSync(gates);
    state.finishedAt = new Date().toISOString();
    save();
    console.log(JSON.stringify({status: state.status, pid: state.launch?.pid ?? null,
      nativeExitCode: state.nativeExitCode, interference: Boolean(state.interference),
      postflightError: state.postflightError ?? null}));
    process.exitCode = state.status === 'native-exit-zero' && !state.afterGate?.busy.length &&
      !state.postflightError ? 0 : 1;
  }
}
void main();
