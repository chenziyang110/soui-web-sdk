'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const external = 'C:/SOUI-SDK-production-validation/work-area-placement-20260928';
const commands = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const attempts = ['workarea-gui-x64-attempt1',
  'workarea-gui-x64-attempt2', 'workarea-gui-x86-attempt1'];
const commandNames = attempts.map(name => `${name}-driver`).concat('workarea-v9-static-attempt1');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const archivePath = relative => path.join(root, 'raw', relative);
const files = [
  ...['Run-Window-Gui.ps1', 'Verify-GuiIdentity.cjs', 'Validate-V9Source.cjs',
    'V9-PREPARATION.md'].map(name => ({relative: `control/${name}`,
    original: path.join(external, name)})),
  ...['run-layout-interaction.ps1', 'export-serial.ps1', 'verify-apps.ps1'].map(name =>
    ({relative: `v9-source/${name}`,
      original: path.join(external, 'win7-v9-source', name)})),
  ...attempts.flatMap(name => fs.readdirSync(path.join(external, 'gui-runs', name))
    .sort().map(file => ({relative: `gui/${name}/${file}`,
      original: path.join(external, 'gui-runs', name, file)}))),
  ...commandNames.flatMap(name => ['result.json', 'stdout.log', 'stderr.log']
    .map(file => ({relative: `commands/${name}/${file}`,
      original: path.join(commands, name, file)}))),
];
const manifestPath = path.join(root, 'raw-manifest.json');

function verify() {
  const manifest = readJson(manifestPath);
  assert.equal(manifest.schema, 'soui.work-area-placement-gui-archive/v1');
  assert.equal(manifest.files.length, files.length);
  for (let index = 0; index < files.length; ++index) {
    const expected = files[index];
    const entry = manifest.files[index];
    assert.equal(entry.path, `raw/${expected.relative}`);
    assert.equal(entry.original, expected.original);
    const archived = archivePath(expected.relative);
    const bytes = fs.readFileSync(archived);
    assert.equal(bytes.length, entry.bytes, entry.path);
    assert.equal(hash(bytes), entry.sha256, entry.path);
    if (fs.existsSync(expected.original))
      assert.equal(hash(fs.readFileSync(expected.original)), entry.sha256,
        `${entry.path} differs from the external original`);
  }
  const source = '60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640';
  const builds = new Map([
    ['workarea-gui-x64-attempt1', 'c8d857b1-0083-40bf-ae3a-3b5ed5f4ad47'],
    ['workarea-gui-x64-attempt2', 'c8d857b1-0083-40bf-ae3a-3b5ed5f4ad47'],
    ['workarea-gui-x86-attempt1', 'b6caec6c-117e-49eb-86fa-4182e64d5b55'],
  ]);
  for (const name of attempts) {
    const result = readJson(archivePath(`gui/${name}/result.json`));
    const driver = readJson(archivePath(`commands/${name}-driver/result.json`));
    const before = readJson(archivePath(`gui/${name}/identity-before.json`));
    const after = readJson(archivePath(`gui/${name}/identity-after.json`));
    assert.equal(result.sourceSha256, source);
    assert.equal(result.buildId, builds.get(name));
    assert.equal(before.sourceSha256, source);
    assert.equal(after.sourceSha256, source);
    assert.equal(before.buildId, builds.get(name));
    assert.equal(after.buildId, builds.get(name));
    assert.deepEqual(before.payloads, after.payloads);
    assert.equal(result.appSha256Before, result.appSha256After);
    assert.equal(result.probeSha256,
      '2ad0e9eb92c7ef7b2ba8e72c672db58fd9fa250f5dbbb1698795c359cb0e5d96');
    const gates = fs.readFileSync(archivePath(`gui/${name}/gates.ndjson`), 'utf8')
      .trim().split(/\r?\n/).map(JSON.parse);
    assert(gates.length >= 2);
    assert(gates.every(gate => Array.isArray(gate.processes) &&
      gate.processes.length === 0), `${name} has external workload`);
    if (name === 'workarea-gui-x64-attempt1') {
      assert.equal(result.guiVerified, false);
      assert.match(result.failure, /visible unowned top-level target window missing/);
      assert.equal(driver.exitCode, 1);
      continue;
    }
    assert.equal(driver.exitCode, 0);
    assert.equal(driver.timedOut, false);
    assert.equal(driver.error, null);
    assert.equal(result.guiVerified, true);
    assert.equal(result.failure, '');
    assert.equal(result.closePosted, true);
    assert.equal(result.exitCode, 0);
    assert.equal(result.timedOut, false);
    assert.equal(result.inspect.foregroundBefore, true);
    assert.equal(result.resize.foregroundBefore, true);
    assert.equal(result.inspect.hwnd, result.resize.hwnd);
    const [left, top, right, bottom] = result.inspect.before;
    const [endLeft, endTop, endRight, endBottom] = result.resize.after;
    assert.equal(right - left, 850);
    assert.equal(bottom - top, 640);
    assert.equal(endLeft, left);
    assert.equal(endTop, top);
    assert.equal(endRight - right, 80);
    assert.equal(endBottom - bottom, 60);
    const [workLeft, workTop, workRight, workBottom] = result.workArea;
    assert(left >= workLeft && top >= workTop &&
      endRight <= workRight && endBottom <= workBottom);
  }
  const staticResult = readJson(archivePath('commands/workarea-v9-static-attempt1/result.json'));
  assert.equal(staticResult.exitCode, 0);
  assert.equal(staticResult.timedOut, false);
  const staticOutput = JSON.parse(fs.readFileSync(
    archivePath('commands/workarea-v9-static-attempt1/stdout.log'), 'utf8'));
  assert.equal(staticOutput.scriptSha256,
    hash(fs.readFileSync(archivePath('v9-source/run-layout-interaction.ps1'))));
  assert.equal(staticOutput.expectedActions, 39);
  assert.equal(staticOutput.expectedSerialFrames, 116);
  assert.equal(staticOutput.guestExecuted, false);
  console.log(`verified ${files.length} raw files; two modern GUI resize runs, one preserved pre-ready failure`);
}

function copy() {
  assert(!fs.existsSync(manifestPath), 'Refuse to replace GUI archive');
  for (const {relative, original} of files) {
    assert(fs.statSync(original).isFile(), `Missing original: ${original}`);
    assert(!fs.existsSync(archivePath(relative)), `Archive target exists: ${relative}`);
  }
  const entries = files.map(({relative, original}) => {
    const destination = archivePath(relative);
    fs.mkdirSync(path.dirname(destination), {recursive: true});
    fs.copyFileSync(original, destination, fs.constants.COPYFILE_EXCL);
    const bytes = fs.readFileSync(destination);
    return {path: `raw/${relative}`, original, bytes: bytes.length, sha256: hash(bytes)};
  });
  fs.writeFileSync(manifestPath, JSON.stringify({
    schema: 'soui.work-area-placement-gui-archive/v1',
    modernWindowsGuiVerified: true, win7Verified: false,
    v02FullVerified: false, releaseReady: false, files: entries,
  }, null, 2) + '\n', {flag: 'wx'});
  verify();
}

if (process.argv[2] === 'copy') copy();
else if (process.argv[2] === 'verify') verify();
else throw new Error('Usage: node Archive.cjs copy|verify');
