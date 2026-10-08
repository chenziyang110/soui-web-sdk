'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const [architecture, attemptName] = process.argv.slice(2);
assert.equal(process.argv.length, 4);
assert(['x64', 'x86'].includes(architecture));
assert(/^[a-z0-9-]+$/.test(attemptName));
const root = __dirname;
const driver = path.join(root, `${attemptName}-fast-driver`);
const attempt = path.join(root, attemptName);
assert.equal(fs.existsSync(driver), false);
assert.equal(fs.existsSync(attempt), false);
fs.mkdirSync(driver);

const expectedSource = '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d';
const expectedBuild = architecture === 'x64' ?
  '5ddcec93-d244-456c-877b-1bdeabee568d' : 'f0c38056-058a-456e-be99-17446127f7ec';
const expectedProbe = architecture === 'x64' ?
  '4bd647c75f08e1f14b5ae4418e30c1c7c480e69e07bc425988cad09128826ecd' :
  '929ecfcb2d842b1639b31a349ca83bd2f037d2111600526e1aa9cdb0001ab26e';
const probe = path.join(root, 'probe-v2-build-queued-attempt1', 'build', architecture,
  'Release', 'V02InputProbe.exe');
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
assert.equal(sha(probe), expectedProbe);
const environment = {...process.env,
  PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')};
const state = {schema: 'soui.v02.fast-visible/v1', architecture, attemptName,
  sourceSha256: expectedSource, buildId: expectedBuild, probeSha256: expectedProbe,
  startedAt: new Date().toISOString(), status: 'preparing', actions: [],
  target: null, runner: null, forcedCleanup: null,
  realInputVerified: false, releaseReady: false};
const save = () => fs.writeFileSync(path.join(driver, 'result.json'), JSON.stringify(state, null, 2) + '\n');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
let gateIndex = 0;
function gate(ownedPid = 0) {
  const script = "$ErrorActionPreference='Stop';$all=@(Get-CimInstance Win32_Process -ErrorAction Stop);" +
    "$own=[Collections.Generic.HashSet[int]]::new();if(" + Number(ownedPid) + " -gt 0){[void]$own.Add(" + Number(ownedPid) + ")};" +
    "do{$changed=$false;foreach($p in $all){if($own.Contains([int]$p.ParentProcessId) -and $own.Add([int]$p.ProcessId)){$changed=$true}}}while($changed);" +
    "$busy=@($all|Where-Object{$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$' -and -not $own.Contains([int]$_.ProcessId)});" +
    "[ordered]@{busy=@($busy|Select-Object ProcessId,ParentProcessId,Name,CreationDate)}|ConvertTo-Json -Compress -Depth 4";
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
    {env: environment, encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const observed = JSON.parse(result.stdout);
  const record = {at: new Date().toISOString(), ownedPid,
    busy: Array.isArray(observed.busy) ? observed.busy : observed.busy ? [observed.busy] : []};
  fs.writeFileSync(path.join(driver, `gate-${gateIndex++}.json`), JSON.stringify(record, null, 2) + '\n', {flag: 'wx'});
  if (record.busy.length) throw Error(`External workload detected: ${JSON.stringify(record.busy)}`);
}
function runAction(label, program, args, timeoutMs = 15000) {
  gate(state.target?.pid ?? 0);
  const result = cp.spawnSync(program, args, {env: environment, encoding: 'utf8',
    windowsHide: true, timeout: timeoutMs});
  const action = {label, at: new Date().toISOString(), program, args,
    exitCode: result.status, error: result.error ? String(result.error) : null,
    stdout: result.stdout, stderr: result.stderr};
  state.actions.push(action);
  save();
  if (result.error || result.status !== 0) throw Error(`${label} failed: ${action.error || action.stderr || action.exitCode}`);
  return action;
}
function native(action, argument, label) {
  const output = path.join(driver, `${label}.json`);
  const result = runAction(label, probe,
    [String(state.target.pid), state.target.creationFiletime, action, argument, output], 10000);
  assert(fs.existsSync(output), `${label} output absent`);
  const raw = fs.readFileSync(output, 'utf8');
  assert.equal(raw.match(/"creationFiletime":(\d+)/)?.[1], state.target.creationFiletime);
  const record = JSON.parse(raw);
  assert.equal(record.pid, state.target.pid);
  assert.equal(record.hwnd, state.target.hwnd);
  assert.equal(record.action, action);
  assert.equal(record.foregroundBefore, true);
  return {result, record};
}
function screen(label) {
  const png = path.join(driver, `${label}.png`);
  const metadata = path.join(driver, `${label}.json`);
  runAction(label, 'powershell.exe', ['-NoProfile', '-File', path.join(root, 'Capture-V02Window.ps1'),
    '-TargetProcessId', String(state.target.pid),
    '-ExpectedCreationFiletime', state.target.creationFiletime,
    '-ExpectedHwnd', String(state.target.hwnd), '-OutputPng', png, '-OutputJson', metadata], 15000);
  const record = JSON.parse(fs.readFileSync(metadata, 'utf8'));
  assert.equal(record.imageSha256, sha(png));
  assert.equal(record.naturallyForeground, true);
}
function wheelSeries(action, point, count, label) {
  for (let index = 1; index <= count; ++index) {
    native(action, point, `${label}-${index}`);
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 60);
  }
}
function cleanupOwn(reason) {
  if (!state.target?.pid || state.forcedCleanup) return;
  const result = cp.spawnSync('taskkill.exe', ['/PID', String(state.target.pid), '/T', '/F'],
    {windowsHide: true, timeout: 15000});
  state.forcedCleanup = {reason, at: new Date().toISOString(), exitCode: result.status,
    error: result.error ? String(result.error) : null};
  save();
}
async function main() {
  save();
  const before = gate();
  const stdout = fs.openSync(path.join(driver, 'runner.stdout.log'), 'wx');
  const stderr = fs.openSync(path.join(driver, 'runner.stderr.log'), 'wx');
  const child = cp.spawn('node', [path.join(root, 'Run-VisibleV02-v2.cjs'), architecture, attemptName],
    {cwd: root, env: environment, windowsHide: true, stdio: ['ignore', stdout, stderr]});
  state.runner = {pid: child.pid ?? null, startedAt: new Date().toISOString(),
    programSha256: sha(path.join(root, 'Run-VisibleV02-v2.cjs'))};
  save();
  let childError = null;
  child.once('error', error => {childError = String(error);});
  const childClosed = new Promise(resolve => child.once('close', (code, signal) => resolve({code, signal})));
  try {
    const started = Date.now();
    while (Date.now() - started < 30000) {
      if (fs.existsSync(path.join(attempt, 'result.json'))) {
        const current = JSON.parse(fs.readFileSync(path.join(attempt, 'result.json'), 'utf8'));
        if (current.status === 'running' && current.launch?.pid) {
          assert.equal(current.buildBefore.sourceSha256, expectedSource);
          assert.equal(current.buildBefore.buildId, expectedBuild);
          state.target = {pid: current.launch.pid, creationFiletime: null, hwnd: null};
          break;
        }
      }
      if (child.exitCode !== null || childError) throw Error(`Runner exited before launch: ${child.exitCode} ${childError}`);
      await delay(250);
    }
    assert(state.target, 'Application did not launch within 30 seconds');
    await delay(2500);
    const inspectPath = path.join(driver, 'inspect.json');
    runAction('inspect', probe, [String(state.target.pid), '0', 'inspect', '-', inspectPath], 10000);
    const inspectRaw = fs.readFileSync(inspectPath, 'utf8');
    const inspect = JSON.parse(inspectRaw);
    assert.equal(inspect.pid, state.target.pid);
    assert.equal(inspect.foregroundBefore, true);
    state.target.creationFiletime = inspectRaw.match(/"creationFiletime":(\d+)/)?.[1];
    state.target.hwnd = inspect.hwnd;
    assert(state.target.creationFiletime && state.target.hwnd);
    save();
    screen('baseline');
    wheelSeries('wheel', '1060,450', 8, 'outer-down');
    screen('task-location');
    native('click', '740,650', 'start-task');
    native('click', '500,130', 'focus-input');
    native('pinyin', 'zhongwen', 'pinyin');
    native('key-space', '-', 'commit-space');
    native('resize-drag', '80,60', 'resize');
    screen('concurrent-input-resize');
    wheelSeries('wheel-up', '1140,450', 8, 'outer-up');
    screen('list-return');
    native('wheel', '500,450', 'inner-list-wheel');
    screen('concurrent-list-scroll');
    const closeOutput = path.join(driver, 'natural-close.json');
    runAction('natural-close', 'powershell.exe', ['-NoProfile', '-File', path.join(root, 'Close-V02Window.ps1'),
      '-TargetProcessId', String(state.target.pid),
      '-ExpectedCreationFiletime', state.target.creationFiletime,
      '-ExpectedHwnd', String(state.target.hwnd), '-OutputJson', closeOutput], 60000);
    const closed = JSON.parse(fs.readFileSync(closeOutput, 'utf8'));
    assert.equal(closed.posted, true);
    assert.equal(closed.processExited, true);
    assert.equal(closed.windowDestroyed, true);
    assert.equal(closed.nativeExitCode, 0);
    state.status = 'actions-and-natural-close';
  } catch (error) {
    state.status = 'actions-failed';
    state.error = String(error.stack || error);
    cleanupOwn('driver-failure');
  } finally {
    let waitTimer;
    const runnerResult = await Promise.race([childClosed,
      new Promise(resolve => {
        waitTimer = setTimeout(() => resolve({code: null, signal: 'wait-timeout'}), 70000);
      })]);
    clearTimeout(waitTimer);
    fs.closeSync(stdout);
    fs.closeSync(stderr);
    state.runner.exitCode = runnerResult.code;
    state.runner.signal = runnerResult.signal;
    state.runner.error = childError;
    if (fs.existsSync(path.join(attempt, 'result.json')))
      state.application = JSON.parse(fs.readFileSync(path.join(attempt, 'result.json'), 'utf8'));
    try { gate(); } catch (error) { state.afterGateError = String(error); }
    state.finishedAt = new Date().toISOString();
    save();
    console.log(JSON.stringify({status: state.status, runnerExitCode: state.runner.exitCode,
      actionCount: state.actions.length, appStatus: state.application?.status ?? null,
      forcedCleanup: state.forcedCleanup}));
    process.exitCode = state.status === 'actions-and-natural-close' &&
      state.runner.exitCode === 0 && state.application?.status === 'native-exit-zero' &&
      !state.afterGateError ? 0 : 1;
  }
}
void main().catch(error => {state.status = 'driver-error'; state.error = String(error.stack || error); save(); console.error(state.error); process.exitCode = 1;});
