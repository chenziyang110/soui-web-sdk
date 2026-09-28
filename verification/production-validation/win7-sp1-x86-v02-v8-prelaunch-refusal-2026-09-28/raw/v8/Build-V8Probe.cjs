'use strict';

const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');

const root = __dirname;
const attemptName = process.argv[2];
if (!/^probe-v8-build-attempt[0-9]+$/.test(attemptName || ''))
  throw Error('Usage: node Build-V8Probe.cjs probe-v8-build-attemptN');
const attempt = path.join(root, attemptName);
fs.mkdirSync(attempt);
const input = path.join(root, 'layout-interaction-v8-source');
const expected = {
  'CMakeLists.txt': '1f8dd40a30c36a8ffa9533362b8f85c1ad90cbdfd07a064e63a5bf9b31a185ce',
  'V02InputProbe.cpp': '6e7d8b00f0819074dbe800c2dffcbee81ac425e1c27414c5539e75ff11e98d65',
  'run-layout-interaction.ps1': 'a19dba351c307d4a264cbfb98e1c0937606e9667c1debdb5557021ed06a22cb5',
  'Validate-Source.cjs': 'af07efc82df3e79e118ad6965c84986c90d567246ccff7f7b1c5751f7432c646',
};
const state = {schema: 'soui.v02.probe-build/v8', startedAt: new Date().toISOString(),
  status: 'waiting', stages: [], executables: [], releaseReady: false};
const waitStarted = Date.now();
const waitLimitMs = 60 * 1000;
const stageLimitMs = 180000;
const gateLog = fs.openSync(path.join(attempt, 'gates.ndjson'), 'wx');
const environment = {...process.env,
  PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')};

function hash(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}
function verifyStaticRuntime(build) {
  const project = path.join(build, 'V02InputProbe.vcxproj');
  const projectText = fs.readFileSync(project, 'utf8');
  const releaseMarker = '<ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'Release|Win32\'">';
  const start = projectText.indexOf(releaseMarker);
  const end = start < 0 ? -1 : projectText.indexOf('</ItemDefinitionGroup>', start);
  if (start < 0 || end < 0 ||
      !projectText.slice(start, end).includes('<RuntimeLibrary>MultiThreaded</RuntimeLibrary>'))
    throw Error('Generated Release Win32 project does not select the static MSVC runtime');
  const command = path.join(build, 'V02InputProbe.dir', 'Release', 'V02InputProbe.tlog', 'CL.command.1.tlog');
  const commandBytes = fs.readFileSync(command);
  if (commandBytes.length < 2 || commandBytes[0] !== 0xff || commandBytes[1] !== 0xfe)
    throw Error('Unexpected compiler command log encoding');
  const commandText = commandBytes.subarray(2).toString('utf16le');
  if (!/\/MT(?:\s|$)/.test(commandText) || /\/MDd?(?:\s|$)/.test(commandText))
    throw Error('Actual compiler command does not use static MSVC runtime');
  return {projectSha256: hash(project), commandSha256: hash(command), releaseWin32StaticRuntime: true};
}
function verifyInput() {
  for (const [name, sha256] of Object.entries(expected)) {
    if (hash(path.join(input, name)) !== sha256) throw Error(`Frozen probe input changed: ${name}`);
  }
}
function save() {
  fs.writeFileSync(path.join(attempt, 'result.json'), JSON.stringify({...state, updatedAt: new Date().toISOString()}, null, 2) + '\n');
}
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
  const busy = Array.isArray(parsed.busy) ? parsed.busy : parsed.busy ? [parsed.busy] : [];
  fs.writeSync(gateLog, JSON.stringify({at: new Date().toISOString(), owned, busy}) + '\n');
  fs.fsyncSync(gateLog);
  return busy;
}
function delay(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }
async function waitForIdle() {
  let idleSince = null;
  while (Date.now() - waitStarted < waitLimitMs) {
    const busy = gate();
    if (busy.length) idleSince = null;
    else if (idleSince === null) idleSince = Date.now();
    else if (Date.now() - idleSince >= 10000) return true;
    await delay(5000);
  }
  return false;
}
function stopTree(pid) {
  if (!pid) return null;
  const result = cp.spawnSync('taskkill.exe', ['/PID', String(pid), '/T', '/F'],
    {windowsHide: true, timeout: 15000});
  return {exitCode: result.status, error: result.error ? String(result.error) : null};
}
function runStage(name, args) {
  return new Promise(resolve => {
    const stdout = fs.openSync(path.join(attempt, `${name}.stdout.log`), 'wx');
    const stderr = fs.openSync(path.join(attempt, `${name}.stderr.log`), 'wx');
    const child = cp.spawn('cmake.exe', args, {cwd: root, env: environment, windowsHide: true,
      stdio: ['ignore', stdout, stderr]});
    const stage = {name, program: 'cmake.exe', args, pid: child.pid ?? null,
      startedAt: new Date().toISOString(), exitCode: null, signal: null,
      timedOut: false, interference: null, gateError: null, spawnError: null};
    state.stages.push(stage);
    save();
    child.once('error', error => { stage.spawnError = String(error); });
    let stopping = false;
    const stop = reason => {
      if (stopping || child.exitCode !== null || child.signalCode !== null) return;
      stopping = true;
      stage.cleanupReason = reason;
      stage.cleanup = stopTree(child.pid);
    };
    const poll = setInterval(() => {
      if (stopping || child.exitCode !== null || child.signalCode !== null) return;
      try {
        const busy = gate(child.pid);
        if (busy.length) { stage.interference = busy; stop('external-workload'); }
      } catch (error) { stage.gateError = String(error); stop('gate-error'); }
    }, 5000);
    const timeout = setTimeout(() => { stage.timedOut = true; stop('timeout'); }, stageLimitMs);
    child.once('close', (code, signal) => {
      clearInterval(poll);
      clearTimeout(timeout);
      fs.closeSync(stdout);
      fs.closeSync(stderr);
      stage.exitCode = code;
      stage.signal = signal;
      stage.finishedAt = new Date().toISOString();
      save();
      resolve(stage);
    });
  });
}
async function main() {
  save();
  try {
    verifyInput();
    const validation = cp.spawnSync(process.execPath, [path.join(input, 'Validate-Source.cjs')],
      {encoding: 'utf8', windowsHide: true, timeout: 15000});
    fs.writeFileSync(path.join(attempt, 'static-source-validation.json'),
      JSON.stringify({exitCode: validation.status, stdout: validation.stdout,
        stderr: validation.stderr, error: validation.error ? String(validation.error) : null}, null, 2) + '\n',
      {flag: 'wx'});
    if (validation.error || validation.status !== 0 || validation.stderr.trim())
      throw Error('V8 source action contract failed');
    if (!(await waitForIdle())) { state.status = 'busy-timeout-no-build'; process.exitCode = 2; return; }
    for (const [arch, platform] of [['x86', 'Win32']]) {
      const build = path.join(attempt, 'build', arch);
      for (const [kind, args] of [
        ['configure', ['-S', input, '-B', build, '-G', 'Visual Studio 17 2022', '-A', platform]],
        ['build', ['--build', build, '--config', 'Release', '--target', 'V02InputProbe', '--', '/m:1']],
      ]) {
        verifyInput();
        if (gate().length) { state.status = 'external-busy-before-stage'; process.exitCode = 2; return; }
        const stage = await runStage(`${arch}-${kind}`, args);
        verifyInput();
        if (stage.exitCode !== 0 || stage.timedOut || stage.interference || stage.gateError || stage.spawnError) {
          state.status = `failed-${arch}-${kind}`;
          process.exitCode = 1;
          return;
        }
      }
      const exe = path.join(build, 'Release', 'V02InputProbe.exe');
      state.staticRuntime = verifyStaticRuntime(build);
      state.executables.push({architecture: arch, path: exe, bytes: fs.statSync(exe).size,
        sha256: hash(exe)});
      save();
    }
    state.status = 'built-x86';
  } catch (error) {
    state.status = 'driver-error';
    state.error = String(error.stack || error);
    process.exitCode = 1;
  } finally {
    state.finishedAt = new Date().toISOString();
    save();
    fs.closeSync(gateLog);
    console.log(JSON.stringify({status: state.status, stages: state.stages.map(({name, exitCode}) => ({name, exitCode}))}));
  }
}
void main();
