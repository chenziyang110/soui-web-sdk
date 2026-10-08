'use strict';

// Preparation for a real-input host run. A zero exit means input and native close
// completed; screenshot review is separate and realScrollVerified stays false.
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const [architecture, attemptName] = process.argv.slice(2);
assert.equal(process.argv.length, 4);
assert(['x64', 'x86'].includes(architecture));
assert(/^template-scroll-(?:x64|x86)-attempt[0-9]+$/.test(attemptName));
assert(attemptName.includes(architecture));

const root = __dirname;
const repo = path.join(root, 'generated/gen-idle-attempt1');
const previousControls = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927';
const buildEvidence = require(path.join(repo, 'tools/internal/build-evidence.cjs'));
const expectedSource = 'edece3019fd29db841425671071367eb13e891d5cefb85a1d70594b9bd3cc227';
const expectedBuild = architecture === 'x64' ?
  '5a5224a2-883d-41da-ac2a-45c7b010413e' : '24283c4e-d1fa-4f1a-b82e-6065ec1d7853';
const expectedProbe = architecture === 'x64' ?
  '4bd647c75f08e1f14b5ae4418e30c1c7c480e69e07bc425988cad09128826ecd' :
  '929ecfcb2d842b1639b31a349ca83bd2f037d2111600526e1aa9cdb0001ab26e';
const probe = path.join(previousControls, 'probe-v2-build-queued-attempt1', 'build',
  architecture, 'Release', 'V02InputProbe.exe');
const activationRoot = path.join(root, 'activation-probe-input-attempt1');
const activationProbe = path.join(activationRoot, 'build', architecture, 'Release', 'V02InputProbe.exe');
const activationFreeze = JSON.parse(fs.readFileSync(path.join(activationRoot, 'freeze.json'), 'utf8'));

const capture = path.join(previousControls, 'Capture-V02Window.ps1');
const closeWindow = path.join(previousControls, 'Close-V02Window.ps1');
const coverInspector = path.join(root, 'Inspect-TemplateTitleCover.ps1');
const payloadVerifier = path.join(root, 'Verify-TemplateLivePayload.cjs');
const {verifyLivePayload} = require(payloadVerifier);
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
assert.equal(activationFreeze.schema, 'soui.template-title-activation/v1');
assert.equal(sha(activationProbe), activationFreeze.binaries[architecture].sha256);
const environment = {...process.env,
  PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')};
for (const key of Object.keys(environment)) if (key.startsWith('SOUI_')) delete environment[key];
assert.equal(sha(probe), expectedProbe);
const helperHashes = {probe: expectedProbe, activationProbe: sha(activationProbe), coverInspector: sha(coverInspector), payloadVerifier: sha(payloadVerifier), capture: sha(capture), closeWindow: sha(closeWindow)};
const output = path.join(root, attemptName);
assert.equal(fs.existsSync(output), false, 'Attempt output already exists');
fs.mkdirSync(output);

const state = {schema: 'soui.template-scrollbar-real-input-host/v1', architecture, attemptName,
  sourceSha256: expectedSource, buildId: expectedBuild, helperHashes,
  driverSha256Before: sha(__filename), driverSha256After: null,
  startedAt: new Date().toISOString(), status: 'preparing', target: null,
  actions: [], gates: [], screenshotHashes: {}, nativeExitCode: null,
  externalLoadObserved: false, realScrollVerified: false, releaseReady: false};
const save = () => fs.writeFileSync(path.join(output, 'result.json'), JSON.stringify(state, null, 2) + '\n');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
let child = null;
let monitor = null;
let childClosed = null;

function gate(stage, allowOwn = true) {
  const script = "$ErrorActionPreference='Stop';$all=@(Get-CimInstance Win32_Process -ErrorAction Stop);" +
    '$own=[Collections.Generic.HashSet[int]]::new();' +
    (allowOwn ? `[void]$own.Add(${process.pid});` : '') +
    (allowOwn && child?.pid ? `[void]$own.Add(${child.pid});` : '') +
    "do{$changed=$false;foreach($p in $all){if($own.Contains([int]$p.ParentProcessId) -and $own.Add([int]$p.ProcessId)){$changed=$true}}}while($changed);" +
    "$busy=@($all|Where-Object{$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|V02InputProbe|ScrollStarter|MSBuild|cl|link|cdb|InputTrace|ProcessTreeSampler|electron|git)\\.exe$' -and -not $own.Contains([int]$_.ProcessId)});" +
    '[ordered]@{busy=@($busy|Select-Object ProcessId,ParentProcessId,Name,CreationDate)}|ConvertTo-Json -Depth 4 -Compress';
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
    {env: environment, encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const parsed = JSON.parse(result.stdout);
  const observed = {stage, at: new Date().toISOString(),
    busy: Array.isArray(parsed.busy) ? parsed.busy : parsed.busy ? [parsed.busy] : []};
  state.gates.push(observed);
  save();
  if (observed.busy.length) {
    state.externalLoadObserved = true;
    throw Error(`External workload at ${stage}: ${JSON.stringify(observed.busy)}`);
  }
}
function command(label, program, args, timeout = 15000) {
  gate(`before-${label}`);
  const result = cp.spawnSync(program, args,
    {env: environment, encoding: 'utf8', windowsHide: true, timeout});
  const record = {label, at: new Date().toISOString(), program, args,
    exitCode: result.status, error: result.error ? String(result.error) : null,
    stdout: result.stdout, stderr: result.stderr};
  state.actions.push(record);
  save();
  if (result.error || result.status !== 0) throw Error(`${label}: ${record.error || record.stderr || record.exitCode}`);
  return record;
}
function action(label, kind, argument) {
  const file = path.join(output, `${label}.json`);
  command(label, probe, [String(state.target.pid), state.target.creationFiletime,
    kind, argument, file], 10000);
  const raw = fs.readFileSync(file, 'utf8');
  assert.equal(raw.match(/"creationFiletime":([0-9]+)/)?.[1], state.target.creationFiletime);
  const observed = JSON.parse(raw);
  assert.equal(observed.pid, state.target.pid);
  assert.equal(observed.hwnd, state.target.hwnd);
  assert.equal(observed.action, kind);
  assert.equal(observed.foregroundBefore, true);
}
function screenshot(label) {
  const png = path.join(output, `${label}.png`);
  const metadata = path.join(output, `${label}.json`);
  command(`capture-${label}`, 'powershell.exe', ['-NoProfile', '-File', capture,
    '-TargetProcessId', String(state.target.pid),
    '-ExpectedCreationFiletime', state.target.creationFiletime,
    '-ExpectedHwnd', String(state.target.hwnd), '-OutputPng', png,
    '-OutputJson', metadata], 15000);
  const observed = JSON.parse(fs.readFileSync(metadata, 'utf8'));
  assert.equal(observed.imageSha256, sha(png));
  assert.equal(observed.naturallyForeground, true);
  state.screenshotHashes[label] = observed.imageSha256;
  save();
}
async function main() {
  save();
  const stdout = fs.openSync(path.join(output, 'application.stdout.log'), 'wx');
  const stderr = fs.openSync(path.join(output, 'application.stderr.log'), 'wx');
  let closed = null;
  try {
    gate('before-build-check');
    const report = buildEvidence.verifyBuild(repo, architecture, 'Release');
    assert.equal(report.source.sha256, expectedSource);
    assert.equal(report.buildId, expectedBuild);
    assert.equal(report.nativeBuild, 'passed');
    const executable = path.join(root, 'staged', `${architecture}-gui-attempt1`, 'ScrollStarter.exe');
    const entry = report.payloads.ScrollStarter.files.find(file => file.path === 'ScrollStarter.exe');
    assert(entry && sha(executable) === entry.sha256);
    state.livePayloadBefore = verifyLivePayload(path.dirname(executable), report.payloads.ScrollStarter);
    assert.equal(state.livePayloadBefore.checkedFiles, 71);
    assert.equal(state.livePayloadBefore.legalFiles, 5);
    state.payloadSha256 = report.payloads.ScrollStarter.sha256;
    state.executableSha256 = entry.sha256;
    gate('before-launch');
    child = cp.spawn(executable, [],
      {cwd: path.dirname(executable), env: environment, windowsHide: false,
        stdio: ['ignore', stdout, stderr]});
    child.once('error', error => {state.spawnError = String(error); save();});
    childClosed = new Promise(resolve => child.once('close', (code, signal) => resolve({code, signal})));
    state.target = {pid: child.pid, creationFiletime: null, hwnd: null};
    assert(state.target.pid, 'Application PID missing');
    monitor = setInterval(() => {
      if (child.exitCode !== null || child.signalCode !== null) return;
      try { gate('during-app'); }
      catch (error) {
        state.monitorError = String(error);
        if (child.exitCode === null && child.signalCode === null) {
          state.ownedKillRequestedAt = new Date().toISOString();
          state.ownedKillReturned = child.kill();
          save();
        }
      }
    }, 5000);
    const readyUntil = Date.now() + 30000;
    let inspected = null;
    while (Date.now() < readyUntil && !inspected) {
      await delay(1000);
      gate('wait-visible');
      const file = path.join(output, 'inspect.json');
      const result = cp.spawnSync(probe, [String(child.pid), '0', 'inspect', '-', file],
        {env: environment, encoding: 'utf8', windowsHide: true, timeout: 10000});
      if (result.status === 0) inspected = fs.readFileSync(file, 'utf8');
      else if (fs.existsSync(file)) throw Error('Failed inspect left an output file');
      if (state.spawnError) throw Error(`Application spawn failed: ${state.spawnError}`);
      if (child.exitCode !== null) throw Error('Application exited before window inspection');
    }
    assert(inspected, 'Visible window not ready within 30 seconds');
    const inspectedRecord = JSON.parse(inspected);
    state.target.creationFiletime = inspected.match(/"creationFiletime":([0-9]+)/)?.[1];
    state.target.hwnd = inspectedRecord.hwnd;
    assert(state.target.creationFiletime && state.target.hwnd);
    state.initiallyForeground = inspectedRecord.foregroundBefore;
    save();
    const coverFile = path.join(output, 'title-cover.json');
    command('inspect-title-cover', 'powershell.exe', ['-NoProfile', '-File', coverInspector,
      '-TargetProcessId', String(state.target.pid),
      '-ExpectedHwnd', String(state.target.hwnd), '-OutputJson', coverFile], 15000);
    const cover = JSON.parse(fs.readFileSync(coverFile, 'utf8'));
    assert.equal(cover.targetProcessId, state.target.pid);
    assert.equal(cover.targetHwnd, state.target.hwnd);
    state.titleCover = cover;
    save();
    if (!state.initiallyForeground && !cover.pointBelongsToTarget)
      throw Error('Target title covered by PID ' + cover.hitRootProcessId);
    if (!state.initiallyForeground) {
      const activationFile = path.join(output, 'activation-title.json');
      command('activation-title', activationProbe,
        [String(state.target.pid), state.target.creationFiletime, 'activate-title', '-', activationFile], 10000);
      const activationRaw = fs.readFileSync(activationFile, 'utf8');
      assert.equal(activationRaw.match(/"creationFiletime":([0-9]+)/)?.[1], state.target.creationFiletime);
      const activation = JSON.parse(activationRaw);
      assert.equal(activation.pid, state.target.pid);
      assert.equal(activation.hwnd, state.target.hwnd);
      assert.equal(activation.action, 'activate-title');
      assert.equal(activation.foregroundBefore, false);
      const postFile = path.join(output, 'inspect-after-activation.json');
      command('inspect-after-activation', probe,
        [String(state.target.pid), state.target.creationFiletime, 'inspect', '-', postFile], 10000);
      const post = JSON.parse(fs.readFileSync(postFile, 'utf8'));
      assert.equal(post.pid, state.target.pid);
      assert.equal(post.hwnd, state.target.hwnd);
      assert.equal(post.foregroundBefore, true, 'Title click did not activate target');
      state.activationMode = 'verified-native-title-click';
      save();
    }
    await delay(2000);
    screenshot('before-wheel');
    for (let index = 1; index <= 8; index += 1) {
      action(`down-${index}`, 'wheel', '500,450');
      await delay(60);
      if (index === 2) screenshot('after-down-2');
    }
    screenshot('after-down');
    for (let index = 1; index <= 8; index += 1) {
      action(`up-${index}`, 'wheel-up', '500,450');
      await delay(60);
      if (index === 2) screenshot('after-up-2');
    }
    screenshot('after-up');
    const closeOutput = path.join(output, 'natural-close.json');
    command('natural-close', 'powershell.exe', ['-NoProfile', '-File', closeWindow,
      '-TargetProcessId', String(state.target.pid),
      '-ExpectedCreationFiletime', state.target.creationFiletime,
      '-ExpectedHwnd', String(state.target.hwnd), '-OutputJson', closeOutput], 60000);
    const close = JSON.parse(fs.readFileSync(closeOutput, 'utf8'));
    assert.equal(close.posted, true);
    assert.equal(close.processExited, true);
    assert.equal(close.windowDestroyed, true);
    assert.equal(close.nativeExitCode, 0);
    closed = await Promise.race([childClosed, delay(5000).then(() => ({code: null, signal: 'wait-timeout'}))]);
    assert.equal(closed.code, 0);
    state.nativeExitCode = closed.code;
    state.status = 'input-and-close-complete';
  } catch (error) {
    state.status = 'failed';
    state.error = String(error.stack || error);
    if (child && child.exitCode === null && child.signalCode === null) {
      state.ownedKillRequestedAt = new Date().toISOString();
      state.ownedKillReturned = child.kill();
    }
  } finally {
    if (monitor) clearInterval(monitor);
    if (childClosed && !closed) {
      closed = await Promise.race([childClosed,
        delay(10000).then(() => ({code: null, signal: 'cleanup-wait-timeout'}))]);
      if (closed.signal === 'cleanup-wait-timeout')
        state.cleanupError = 'Owned application did not close within 10 seconds';
      else {
        state.nativeExitCode = closed.code;
        state.cleanupSignal = closed.signal;
      }
    }
    fs.closeSync(stdout);
    fs.closeSync(stderr);
    try { gate('after-close', false); } catch (error) {state.afterGateError = String(error);}
    state.driverSha256After = sha(__filename);
    if (state.driverSha256After !== state.driverSha256Before)
      state.postflightError = 'Driver changed during attempt';
    if (!state.afterGateError && state.status === 'input-and-close-complete') {
      try {
        const after = buildEvidence.verifyBuild(repo, architecture, 'Release');
        assert.equal(after.source.sha256, expectedSource);
        assert.equal(after.buildId, expectedBuild);
        assert.equal(after.payloads.ScrollStarter.sha256, state.payloadSha256);
        const entry = after.payloads.ScrollStarter.files.find(file => file.path === 'ScrollStarter.exe');
        assert(entry && entry.sha256 === state.executableSha256);
        const staged = path.join(root, 'staged', `${architecture}-gui-attempt1`);
        state.livePayloadAfter = verifyLivePayload(staged, after.payloads.ScrollStarter);
        assert.equal(state.livePayloadAfter.checkedFiles, 71);
        assert.equal(state.livePayloadAfter.legalFiles, 5);
        assert.equal(sha(payloadVerifier), helperHashes.payloadVerifier);
        state.payloadVerifiedAfter = true;
      } catch (error) {state.postflightError = String(error.stack || error);}
    }
    state.finishedAt = new Date().toISOString();
    save();
    console.log(JSON.stringify({status: state.status, architecture, attemptName,
      nativeExitCode: state.nativeExitCode, externalLoadObserved: state.externalLoadObserved,
      realScrollVerified: false, releaseReady: false}));
    process.exitCode = state.status === 'input-and-close-complete' &&
      state.payloadVerifiedAfter === true && !state.afterGateError && !state.postflightError ? 0 : 1;
  }
}
void main();
