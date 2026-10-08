'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const name = 'visible-v02-x64-attempt3';
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/v02-current-visible-partial-2026-09-27';
assert.equal(fs.existsSync(target), false, 'evidence directory already exists');
const read = relative => fs.readFileSync(path.join(root, relative));
const json = relative => JSON.parse(read(relative).toString('utf8'));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const attempt = json(`${name}/result.json`);
const driver = json(`command-logs/${name}-driver/result.json`);
const blocked = json('command-logs/visible-v02-x64-attempt2-driver/result.json');
const series = json(`${name}/outer-down/result.json`);
const resize = json(`${name}/resize-2.json`);
assert.equal(blocked.exitCode, 2);
assert.equal(blocked.timedOut, false);
assert.equal(fs.existsSync(path.join(root, 'visible-v02-x64-attempt2')), false);
assert.equal(attempt.status, 'timeout');
assert.equal(attempt.timedOut, true);
assert.equal(attempt.forcedCleanup?.reason, 'run-timeout');
assert.equal(attempt.forcedCleanup?.exitCode, 0);
assert.equal(attempt.interference, null);
assert.equal(attempt.beforeGate.busy.length, 0);
assert.equal(attempt.afterGate.busy.length, 0);
assert.deepEqual(attempt.buildBefore, attempt.buildAfter);
assert.equal(attempt.realInputVerified, false);
assert.equal(driver.exitCode, 1);
assert.equal(driver.timedOut, false);
assert.equal(driver.error, null);
assert.equal(series.status, 'actions-sent');
assert.equal(series.actions.length, 8);
assert(series.actions.every(item => item.exitCode === 0 && item.outputExists));
assert.equal(series.afterGate.busy.length, 0);
assert.equal(resize.pid, attempt.launch.pid);
assert.equal(resize.action, 'resize-drag');
assert.equal(resize.foregroundBefore, true);
assert.deepEqual(resize.before, [208, 208, 1308, 928]);
assert.deepEqual(resize.after, [208, 208, 1388, 988]);
const screenshots = ['after-outer-down', 'task-start', 'after-ime', 'concurrent'];
for (const name of screenshots) {
  const record = json(`${name === 'task-start' ? 'visible-v02-x64-attempt3/task-start-screen' :
    `visible-v02-x64-attempt3/${name === 'after-ime' ? 'after-ime-screen' :
      name === 'concurrent' ? 'concurrent-screen' : 'after-outer-down'}`}.json`);
  assert.equal(record.pid, attempt.launch.pid);
  assert.equal(record.naturallyForeground, true);
  assert.equal(record.imageSha256, sha(read(`visible-v02-x64-attempt3/${name}.png`)));
}
const pairs = [
  ['Run-VisibleV02.cjs', 'driver/Run-VisibleV02.cjs'],
  ['Run-V02WheelSeries.cjs', 'driver/Run-V02WheelSeries.cjs'],
  ['Capture-Command.cjs', 'driver/Capture-Command.cjs'],
  ['Capture-V02Window.ps1', 'driver/Capture-V02Window.ps1'],
  ['Archive-VisibleAttempt3.cjs', 'driver/Archive-VisibleAttempt3.cjs'],
];
function collect(directory, destination) {
  for (const item of fs.readdirSync(path.join(root, directory), {withFileTypes: true})) {
    const source = `${directory}/${item.name}`;
    const output = `${destination}/${item.name}`;
    if (item.isDirectory()) collect(source, output);
    else if (item.isFile()) pairs.push([source, output]);
    else throw Error(`Unsupported raw item: ${source}`);
  }
}
collect(name, 'attempt3');
collect(`command-logs/${name}-driver`, 'command-attempt3');
collect('command-logs/visible-v02-x64-attempt2-driver', 'command-attempt2');
const files = [];
for (const [sourceRelative, outputRelative] of pairs) {
  const source = path.join(root, sourceRelative);
  const destination = path.join(target, 'raw', outputRelative);
  fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  const original = fs.readFileSync(source);
  assert.equal(sha(fs.readFileSync(destination)), sha(original));
  files.push({source, path: `raw/${outputRelative}`, bytes: original.length,
    sha256: sha(original), sourceMtimeUtc: fs.statSync(source).mtime.toISOString()});
}
files.sort((left, right) => left.path.localeCompare(right.path));
fs.writeFileSync(path.join(target, 'raw-manifest.json'), JSON.stringify({
  schema: 'soui.v02.visible-partial-raw/v1', fileCount: files.length,
  files, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify({
  schema: 'soui.v02.visible-partial-review/v1',
  prelaunchAttempt2: {driverExitCode: blocked.exitCode, appStarted: false},
  attempt3: {status: attempt.status, driverExitCode: driver.exitCode,
    forcedCleanup: attempt.forcedCleanup, sourceSha256: attempt.buildBefore.sourceSha256,
    buildId: attempt.buildBefore.buildId, payloadSha256: attempt.buildBefore.payloadSha256,
    targetProcessId: attempt.launch.pid, externalInterference: false,
    outerWheelActions: series.actions.length,
    resizeBefore: resize.before, resizeAfter: resize.after,
    visualObservation: {
      method: 'review of archived native-window screenshots',
      dataRowsVisible: 10000,
      firstCommittedHanText: '中文',
      firstCompositionEventsVisible: 11,
      concurrentTaskProgressVisiblePercent: 40,
      concurrentCompositionEventsVisible: 17,
      concurrentInputStillComposing: true,
      concurrentViewportVisible: [1180, 780],
    },
    innerListScrollDuringTaskVerified: false,
    sameTaskHanCommitVerified: false,
    naturalCloseVerified: false,
    fullV02Verified: false,
  },
  releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, status: attempt.status}));
