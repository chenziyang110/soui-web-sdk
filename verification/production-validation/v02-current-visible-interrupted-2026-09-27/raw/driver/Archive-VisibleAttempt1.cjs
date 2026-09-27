'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const name = 'visible-v02-x64-attempt1';
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/v02-current-visible-interrupted-2026-09-27';
assert.equal(fs.existsSync(target), false, 'evidence directory already exists');
const read = relative => fs.readFileSync(path.join(root, relative));
const json = relative => JSON.parse(read(relative).toString('utf8'));
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

const result = json(`${name}/result.json`);
const driver = json(`command-logs/${name}-driver/result.json`);
const inspect = json(`${name}/inspect.json`);
const wheel = json(`${name}/wheel-1.json`);
const exactCreation = read(`${name}/inspect.json`).toString('utf8').match(/"creationFiletime":(\d+)/)?.[1];
const wheelCreation = read(`${name}/wheel-1.json`).toString('utf8').match(/"creationFiletime":(\d+)/)?.[1];
const baseline = json(`${name}/baseline.json`);
const afterWheel = json(`${name}/after-wheel.json`);
assert.equal(result.status, 'interrupted-external');
assert.equal(result.architecture, 'x64');
assert.equal(result.beforeGate.busy.length, 0);
assert(result.interference?.busy.some(item => /^qemu-/.test(item.Name)));
assert.equal(result.forcedCleanup?.reason, 'external-workload');
assert.equal(result.forcedCleanup?.exitCode, 0);
assert.equal(result.afterGate.busy.length > 0, true);
assert.equal(result.realInputVerified, false);
assert.equal(driver.exitCode, 1);
assert.equal(driver.timedOut, false);
assert.equal(driver.error, null);
assert.equal(inspect.pid, result.launch.pid);
assert.equal(wheel.pid, result.launch.pid);
assert(exactCreation && exactCreation === wheelCreation);
assert.equal(inspect.hwnd, wheel.hwnd);
assert.equal(inspect.foregroundBefore, true);
assert.equal(wheel.foregroundBefore, true);
assert.equal(wheel.action, 'wheel');
for (const [record, file] of [[baseline, 'baseline.png'], [afterWheel, 'after-wheel.png']]) {
  assert.equal(record.pid, result.launch.pid);
  assert.equal(record.naturallyForeground, true);
  assert.equal(record.imageSha256, hash(read(`${name}/${file}`)));
}
assert.notEqual(baseline.imageSha256, afterWheel.imageSha256);
assert(Date.parse(afterWheel.at) < Date.parse(result.interference.at));

const pairs = [
  ['Run-VisibleV02.cjs', 'driver/Run-VisibleV02.cjs'],
  ['Capture-Command.cjs', 'driver/Capture-Command.cjs'],
  ['Capture-V02Window.ps1', 'driver/Capture-V02Window.ps1'],
  ['Archive-VisibleAttempt1.cjs', 'driver/Archive-VisibleAttempt1.cjs'],
];
for (const [sourceDir, destinationDir] of [
  [name, 'attempt'], [`command-logs/${name}-driver`, 'command'],
]) {
  for (const file of fs.readdirSync(path.join(root, sourceDir))) {
    if (fs.statSync(path.join(root, sourceDir, file)).isFile())
      pairs.push([`${sourceDir}/${file}`, `${destinationDir}/${file}`]);
  }
}
const files = [];
for (const [sourceRelative, destinationRelative] of pairs) {
  const source = path.join(root, sourceRelative);
  const destination = path.join(target, 'raw', destinationRelative);
  fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  const original = read(sourceRelative);
  assert.equal(hash(fs.readFileSync(destination)), hash(original));
  files.push({source, path: `raw/${destinationRelative}`, bytes: original.length, sha256: hash(original)});
}
files.sort((left, right) => left.path.localeCompare(right.path));
fs.writeFileSync(path.join(target, 'raw-manifest.json'), JSON.stringify({
  schema: 'soui.v02.visible-interrupted-raw/v1', fileCount: files.length,
  files, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify({
  schema: 'soui.v02.visible-interrupted-review/v1',
  status: result.status, driverExitCode: driver.exitCode,
  architecture: 'x64', sourceSha256: result.buildBefore.sourceSha256,
  buildId: result.buildBefore.buildId, payloadSha256: result.buildBefore.payloadSha256,
  targetProcessId: result.launch.pid, creationFiletime: exactCreation,
  hwnd: inspect.hwnd, baselineSha256: baseline.imageSha256,
  afterWheelSha256: afterWheel.imageSha256,
  wheelResultCreated: true, externalQemu: result.interference.busy,
  appForcedCleanup: result.forcedCleanup,
  imeCompositionVerified: false, taskParallelVerified: false,
  resizeVerified: false, fullV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, status: result.status}));
