'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const [arch, pidText, creation, point, countText, attemptName, label] = process.argv.slice(2);
assert(['x64', 'x86'].includes(arch));
assert(/^\d+$/.test(pidText) && Number(pidText) > 0);
assert(/^\d{16,20}$/.test(creation));
assert(/^\d{1,4},\d{1,4}$/.test(point));
assert(Number.isInteger(Number(countText)) && Number(countText) >= 1 && Number(countText) <= 15);
assert(/^[a-z0-9-]+$/.test(attemptName) && /^[a-z0-9-]+$/.test(label));

const root = __dirname;
const targetPid = Number(pidText);
const target = path.join(root, attemptName);
assert(fs.statSync(target).isDirectory());
const run = path.join(target, label);
fs.mkdirSync(run);
const probe = path.join(root, 'probe-build-queued-attempt1', 'build', arch, 'Release', 'V02InputProbe.exe');
const expectedProbe = arch === 'x64' ?
  'f58219498883c31729414d51a48a3ec89cd4a5105d4e5f5b39c7919fefd664c7' :
  '3ae9a6e42fa2d7980d031cf2dccf62a9245952089a54dae215bcacaadf95d5ef';
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
assert.equal(sha(probe), expectedProbe);
const state = {schema: 'soui.v02.wheel-series/v1', arch, targetPid,
  creationFiletime: creation, point, requested: Number(countText), attemptName,
  label, probeSha256: expectedProbe, startedAt: new Date().toISOString(),
  actions: [], status: 'running', releaseReady: false};
const save = () => fs.writeFileSync(path.join(run, 'result.json'), JSON.stringify(state, null, 2) + '\n');
const environment = {...process.env,
  PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')};
function gate(index) {
  const script = "$ErrorActionPreference='Stop';$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop|" +
    "Where-Object{$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$' -and $_.ProcessId -ne " + targetPid + "});" +
    "[ordered]@{busy=@($busy|Select-Object ProcessId,ParentProcessId,Name,CreationDate)}|ConvertTo-Json -Compress -Depth 4";
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
    {env: environment, encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const observation = JSON.parse(result.stdout);
  const record = {index, at: new Date().toISOString(), busy: Array.isArray(observation.busy) ?
    observation.busy : observation.busy ? [observation.busy] : []};
  fs.writeFileSync(path.join(run, `gate-${index}.json`), JSON.stringify(record, null, 2) + '\n', {flag: 'wx'});
  return record;
}
try {
  save();
  for (let index = 1; index <= state.requested; ++index) {
    const before = gate(index - 1);
    if (before.busy.length) { state.status = 'interrupted-external'; state.interference = before; break; }
    const output = path.join(run, `wheel-${index}.json`);
    const result = cp.spawnSync(probe, [pidText, creation, 'wheel', point, output],
      {encoding: 'utf8', windowsHide: true, timeout: 10000});
    const action = {index, at: new Date().toISOString(), exitCode: result.status,
      error: result.error ? String(result.error) : null,
      stdout: result.stdout, stderr: result.stderr, outputExists: fs.existsSync(output)};
    state.actions.push(action);
    save();
    if (result.error || result.status !== 0 || !action.outputExists) {
      state.status = 'input-failed';
      break;
    }
    const raw = fs.readFileSync(output, 'utf8');
    const exact = raw.match(/"creationFiletime":(\d+)/)?.[1];
    const observed = JSON.parse(raw);
    assert.equal(exact, creation);
    assert.equal(observed.pid, targetPid);
    assert.equal(observed.action, 'wheel');
    assert.equal(observed.foregroundBefore, true);
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 80);
  }
  const after = gate(state.requested);
  state.afterGate = after;
  if (after.busy.length) { state.status = 'interrupted-external'; state.interference = after; }
  else if (state.status === 'running') state.status = 'actions-sent';
} catch (error) {
  state.status = 'error';
  state.error = String(error.stack || error);
}
state.finishedAt = new Date().toISOString();
save();
console.log(JSON.stringify({status: state.status, actions: state.actions.length,
  requested: state.requested, attemptName, label}));
process.exitCode = state.status === 'actions-sent' ? 0 : 1;
