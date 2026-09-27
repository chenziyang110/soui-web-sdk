'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const attempt = path.join(root, 'probe-rejections-queued-attempt1');
fs.mkdirSync(attempt);
const gatesFile = fs.openSync(path.join(attempt, 'gates.ndjson'), 'wx');
const script = path.join(root, 'Test-ProbeRejections.cjs');
const scriptSha256 = 'b78ad5b98b96ce9e2452f29960cf9df3cb335de80c0cb3163ffec481fb589009';
const builds = JSON.parse(fs.readFileSync(path.join(root, 'probe-build-queued-attempt1/result.json'), 'utf8'));
const state = {schema: 'soui.v02.probe-rejections-queued/v1', startedAt: new Date().toISOString(),
  status: 'waiting', scriptSha256, attempts: [], releaseReady: false};
const env = {...process.env,
  PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')};
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const save = () => fs.writeFileSync(path.join(attempt, 'result.json'), JSON.stringify(state, null, 2) + '\n');
function verify() {
  assert.equal(hash(script), scriptSha256, 'rejection test script changed');
  assert.equal(builds.status, 'built-x64-x86');
  for (const architecture of ['x64', 'x86']) {
    const exe = builds.executables.find(item => item.architecture === architecture);
    assert(exe && hash(exe.path) === exe.sha256, `${architecture} probe identity changed`);
  }
}
function gate() {
  const command = "$ErrorActionPreference='Stop';$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | " +
    "Where-Object{$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$'} | " +
    "Select-Object ProcessId,Name,CreationDate);[ordered]@{busy=$busy}|ConvertTo-Json -Depth 4 -Compress";
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', command],
    {env, encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const parsed = JSON.parse(result.stdout);
  const busy = Array.isArray(parsed.busy) ? parsed.busy : parsed.busy ? [parsed.busy] : [];
  fs.writeSync(gatesFile, JSON.stringify({at: new Date().toISOString(), busy}) + '\n');
  fs.fsyncSync(gatesFile);
  return busy;
}
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function waitForIdle() {
  const started = Date.now();
  let idleSince = null;
  while (Date.now() - started < 30 * 60 * 1000) {
    const busy = gate();
    if (busy.length) idleSince = null;
    else if (idleSince === null) idleSince = Date.now();
    else if (Date.now() - idleSince >= 10000) return true;
    await delay(5000);
  }
  return false;
}
function run(architecture, attemptName) {
  if (gate().length) throw Error(`external workload before ${architecture}`);
  verify();
  const driverName = `${attemptName}-driver`;
  const args = [path.join(root, 'Capture-Command.cjs'), driverName, '30000', 'node',
    script, architecture, attemptName];
  const startedAt = new Date().toISOString();
  const result = cp.spawnSync('node', args,
    {cwd: root, env, encoding: 'utf8', windowsHide: true, timeout: 45000});
  const record = {architecture, attemptName, driverName, args, startedAt,
    finishedAt: new Date().toISOString(), exitCode: result.status,
    signal: result.signal, error: result.error ? String(result.error) : null,
    stdout: result.stdout, stderr: result.stderr};
  state.attempts.push(record);
  save();
  const after = gate();
  record.externalAfter = after;
  save();
  verify();
  if (result.error || result.status !== 0 || after.length) {
    throw Error(`${architecture} rejection attempt failed or external workload appeared`);
  }
}
async function main() {
  save();
  try {
    verify();
    for (const name of ['probe-rejections-x64-attempt2', 'probe-rejections-x86-attempt1']) {
      assert.equal(fs.existsSync(path.join(root, name)), false, `${name} already exists`);
      assert.equal(fs.existsSync(path.join(root, 'command-logs', `${name}-driver`)), false,
        `${name} driver logs already exist`);
    }
    if (!(await waitForIdle())) {
      state.status = 'busy-timeout-no-tests';
      process.exitCode = 2;
      return;
    }
    run('x64', 'probe-rejections-x64-attempt2');
    run('x86', 'probe-rejections-x86-attempt1');
    state.status = 'both-rejections-passed';
  } catch (error) {
    state.status = 'failed';
    state.error = String(error.stack || error);
    process.exitCode = 1;
  } finally {
    state.finishedAt = new Date().toISOString();
    save();
    fs.closeSync(gatesFile);
    console.log(JSON.stringify({status: state.status, attempts: state.attempts.map(({architecture, exitCode}) =>
      ({architecture, exitCode}))}));
  }
}
void main();
