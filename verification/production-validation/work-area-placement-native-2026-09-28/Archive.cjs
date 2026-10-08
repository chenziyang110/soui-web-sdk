'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const originalRoot = 'C:\\SOUI-SDK-production-validation\\work-area-placement-20260928';
const sourceSha256 = '60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640';
const builds = [
  {arch: 'x64', attempt: 'workarea-x64-attempt2', buildId: 'c8d857b1-0083-40bf-ae3a-3b5ed5f4ad47'},
  {arch: 'x86', attempt: 'workarea-x86-attempt1', buildId: 'b6caec6c-117e-49eb-86fa-4182e64d5b55'},
];
const commands = builds.flatMap(build => [
  `${build.attempt}-driver`, `${build.attempt}-official-build`,
  `workarea-${build.arch}-placement-attempt1`,
]);
const commandFiles = ['result.json', 'stdout.log', 'stderr.log', 'external-gates.ndjson'];
const relativeFiles = [
  'Run-OfficialBuild.ps1', 'Bind-OfficialBuild.cjs',
  'Capture-IsolatedBuild.cjs', 'Run-Placement-Test.ps1',
  ...builds.flatMap(({arch, attempt}) => [
    `${attempt}-before-official-gate.json`, `${attempt}-after-official-gate.json`,
    `${attempt}-before-official-input.json`, `${attempt}-after-official-input.json`,
    `workarea-${arch}-placement-attempt1-before-test-gate.json`,
    `workarea-${arch}-placement-attempt1-after-test-gate.json`,
    `workarea-${arch}-placement-attempt1-test-identity.json`,
  ]),
  ...commands.flatMap(command => commandFiles.map(name =>
    path.join('command-logs', command, name))),
];
const manifestPath = path.join(root, 'raw-manifest.json');
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const original = relative => path.join(originalRoot, relative);
const archived = relative => path.join(root, 'raw', relative);

function assertEmptyGate(file) {
  const gate = readJson(archived(file));
  assert(Array.isArray(gate.processes) && gate.processes.length === 0, file);
}

function verify() {
  const manifest = readJson(manifestPath);
  assert.equal(manifest.schema, 'soui.work-area-placement-native-archive/v1');
  assert.equal(manifest.files.length, relativeFiles.length);
  for (let index = 0; index < relativeFiles.length; ++index) {
    const relative = relativeFiles[index];
    const entry = manifest.files[index];
    assert.equal(entry.path, path.posix.join('raw', relative.replaceAll('\\', '/')));
    assert.equal(entry.original, original(relative));
    const file = archived(relative);
    assert.equal(fs.statSync(file).size, entry.bytes);
    assert.equal(sha256(fs.readFileSync(file)), entry.sha256);
    if (fs.existsSync(entry.original))
      assert.equal(sha256(fs.readFileSync(entry.original)), entry.sha256);
  }
  for (const {arch, attempt, buildId} of builds) {
    const before = readJson(archived(`${attempt}-before-official-input.json`));
    const after = readJson(archived(`${attempt}-after-official-input.json`));
    assert.equal(before.source.sha256, sourceSha256);
    assert.equal(after.source.sha256, sourceSha256);
    assert.equal(after.buildId, buildId);
    assert.equal(after.payloads.SouiStarter.files, 71);
    assert.equal(after.payloads.SouiWorkbench.files, 475);
    for (const stage of ['before', 'after']) {
      assertEmptyGate(`${attempt}-${stage}-official-gate.json`);
      assertEmptyGate(`workarea-${arch}-placement-attempt1-${stage}-test-gate.json`);
    }
    const test = readJson(archived(`workarea-${arch}-placement-attempt1-test-identity.json`));
    assert.equal(test.exitCode, 0);
    assert.equal(test.sha256Before, test.sha256After);
    const output = fs.readFileSync(archived(path.join('command-logs',
      `workarea-${arch}-placement-attempt1`, 'stdout.log')), 'utf8');
    assert.match(output, /1\/1 Test #\d+: demo\.native\.window-placement/);
    assert.match(output, /100% tests passed, 0 tests failed out of 1/);
  }
  for (const command of commands) {
    const dir = path.join('command-logs', command);
    const result = readJson(archived(path.join(dir, 'result.json')));
    assert.equal(result.exitCode, 0);
    assert.equal(result.timedOut, false);
    assert.equal(result.interruptedByExternal, null);
    assert.equal(result.gateError, null);
    assert.equal(result.error, null);
    const gateText = fs.readFileSync(archived(path.join(dir, 'external-gates.ndjson')), 'utf8');
    const gates = gateText.trim() ? gateText.trim().split(/\r?\n/).map(JSON.parse) : [];
    if (!command.includes('-placement-')) assert(gates.length > 0);
    assert(gates.every(gate => !gate.error && Array.isArray(gate.processes) &&
      gate.processes.length === 0), command);
  }
  console.log(`verified ${relativeFiles.length} raw files; both official builds and placement tests exited 0`);
}

function copy() {
  assert(!fs.existsSync(manifestPath), 'Archive exists; refusing overwrite');
  for (const relative of relativeFiles) {
    assert(fs.existsSync(original(relative)), `Missing original: ${relative}`);
    assert(!fs.existsSync(archived(relative)), `Archive destination exists: ${relative}`);
  }
  const entries = relativeFiles.map(relative => {
    const destination = archived(relative);
    fs.mkdirSync(path.dirname(destination), {recursive: true});
    fs.copyFileSync(original(relative), destination, fs.constants.COPYFILE_EXCL);
    const bytes = fs.readFileSync(destination);
    return {path: path.posix.join('raw', relative.replaceAll('\\', '/')),
      original: original(relative), bytes: bytes.length, sha256: sha256(bytes)};
  });
  fs.writeFileSync(manifestPath, JSON.stringify({
    schema: 'soui.work-area-placement-native-archive/v1',
    sourceSha256, guiVerified: false, win7Verified: false, releaseReady: false,
    files: entries,
  }, null, 2) + '\n', {flag: 'wx'});
  verify();
}

if (process.argv[2] === 'copy') copy();
else if (process.argv[2] === 'verify') verify();
else throw new Error('Usage: node Archive.cjs copy|verify');
