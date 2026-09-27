'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const [architecture, attemptName] = process.argv.slice(2);
assert.equal(process.argv.length, 4);
assert(['x86', 'x64'].includes(architecture));
assert(/^[a-z0-9-]+$/.test(attemptName));

const build = JSON.parse(fs.readFileSync(path.join(root, 'probe-build-queued-attempt1', 'result.json'), 'utf8'));
assert.equal(build.status, 'built-x64-x86', 'probe build is not terminal-success');
const executable = build.executables.find(item => item.architecture === architecture);
assert(executable, 'architecture missing from build result');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
assert.equal(hash(executable.path), executable.sha256, 'compiled probe identity changed');

const attempt = path.join(root, attemptName);
fs.mkdirSync(attempt);
const state = {
  schema: 'soui.v02.probe-rejections/v1',
  architecture,
  attemptName,
  executable: {path: executable.path, sha256: executable.sha256, bytes: executable.bytes},
  startedAt: new Date().toISOString(),
  cases: [],
  status: 'running',
  releaseReady: false,
};
const env = {...process.env, PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')};
let gateIndex = 0;
function save() {
  fs.writeFileSync(path.join(attempt, 'result.json'), JSON.stringify(state, null, 2) + '\n');
}
function gate() {
  const script = "$ErrorActionPreference='Stop';" +
    "$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {" +
    "$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$'" +
    "} | Select-Object ProcessId,Name,CreationDate);" +
    "[ordered]@{busy=$busy}|ConvertTo-Json -Depth 4 -Compress";
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
    {env, encoding: 'utf8', windowsHide: true, timeout: 15000});
  assert.equal(result.error, undefined, 'CIM gate spawn failed');
  assert.equal(result.status, 0, `CIM gate exited ${result.status}`);
  assert.equal(result.stderr, '', 'CIM gate reported stderr');
  const parsed = JSON.parse(result.stdout);
  const busy = Array.isArray(parsed.busy) ? parsed.busy : parsed.busy ? [parsed.busy] : [];
  fs.writeFileSync(path.join(attempt, `gate-${gateIndex++}.json`),
    JSON.stringify({at: new Date().toISOString(), busy}, null, 2) + '\n', {flag: 'wx'});
  assert.equal(busy.length, 0, 'external workload is active');
}
function negativeCase(name, args, output) {
  gate();
  const startedAt = new Date().toISOString();
  const result = cp.spawnSync(executable.path, args, {
    cwd: attempt, env, encoding: 'utf8', windowsHide: true, timeout: 10000,
  });
  const observed = {name, args, startedAt, finishedAt: new Date().toISOString(),
    exitCode: result.status, signal: result.signal, error: result.error ? String(result.error) : null,
    stdout: result.stdout, stderr: result.stderr,
    outputExists: output ? fs.existsSync(output) : false};
  state.cases.push(observed);
  save();
  assert.equal(result.error, undefined, `${name}: process error`);
  assert.equal(result.status, 1, `${name}: expected native rejection`);
  assert.equal(result.stdout, '', `${name}: unexpected stdout`);
  if (output) assert.equal(fs.existsSync(output), false, `${name}: rejected input created output`);
}

save();
try {
  gate();
  negativeCase('missing-arguments', [], null);
  negativeCase('non-absolute-output',
    [String(process.pid), '1', 'inspect', '-', 'relative.json'], null);
  const wrongIdentityOutput = path.join(attempt, 'wrong-identity.json');
  negativeCase('wrong-process-identity',
    [String(process.pid), '1', 'inspect', '-', wrongIdentityOutput], wrongIdentityOutput);
  const unavailableOutput = path.join(attempt, 'unavailable-process.json');
  negativeCase('unavailable-process',
    ['4294967295', '1', 'inspect', '-', unavailableOutput], unavailableOutput);
  const existingOutput = path.join(attempt, 'existing-output.json');
  fs.writeFileSync(existingOutput, 'sentinel\n', {flag: 'wx'});
  const existingSha = hash(existingOutput);
  gate();
  const occupied = cp.spawnSync(executable.path,
    [String(process.pid), '1', 'inspect', '-', existingOutput],
    {cwd: attempt, env, encoding: 'utf8', windowsHide: true, timeout: 10000});
  const observed = {name: 'existing-output', exitCode: occupied.status, signal: occupied.signal,
    error: occupied.error ? String(occupied.error) : null,
    stdout: occupied.stdout, stderr: occupied.stderr,
    outputSha256Before: existingSha, outputSha256After: hash(existingOutput)};
  state.cases.push(observed);
  save();
  assert.equal(occupied.error, undefined);
  assert.equal(occupied.status, 1);
  assert.equal(occupied.stdout, '');
  assert.equal(observed.outputSha256After, existingSha, 'existing output was modified');
  gate();
  assert.equal(hash(executable.path), executable.sha256, 'probe changed during rejection tests');
  state.status = 'rejections-passed';
} catch (error) {
  state.status = 'failed';
  state.error = String(error.stack || error);
  process.exitCode = 1;
} finally {
  state.finishedAt = new Date().toISOString();
  save();
  console.log(JSON.stringify({status: state.status, architecture, cases: state.cases.length}));
}
