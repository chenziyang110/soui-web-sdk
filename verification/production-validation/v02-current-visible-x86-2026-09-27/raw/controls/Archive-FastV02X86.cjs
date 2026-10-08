'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/v02-current-visible-x86-2026-09-27';
assert.equal(fs.existsSync(target), false, 'evidence directory already exists');
const read = relative => fs.readFileSync(path.join(root, relative));
const json = relative => JSON.parse(read(relative).toString('utf8'));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const app = json('visible-v02-x86-attempt4/result.json');
const fast = json('visible-v02-x86-attempt4-fast-driver/result.json');
const command = json('command-logs/fast-v02-x86-attempt4-driver/result.json');
const started = fast.actions.find(item => item.label === 'start-task');
const listWheel = fast.actions.find(item => item.label === 'inner-list-wheel');
const naturalClose = json('visible-v02-x86-attempt4-fast-driver/natural-close.json');
const resize = json('visible-v02-x86-attempt4-fast-driver/resize.json');

assert.equal(command.exitCode, 0);
assert.equal(command.timedOut, false);
assert.equal(command.error, null);
assert.equal(fast.status, 'actions-and-natural-close');
assert.equal(fast.runner.exitCode, 0);
assert.equal(fast.forcedCleanup, null);
assert.equal(fast.afterGateError, undefined);
assert.equal(app.status, 'native-exit-zero');
assert.equal(app.nativeExitCode, 0);
assert.equal(app.interference, null);
assert.equal(app.forcedCleanup, null);
assert.equal(app.beforeGate.busy.length, 0);
assert.equal(app.afterGate.busy.length, 0);
assert.deepEqual(app.buildBefore, app.buildAfter);
assert.equal(app.buildBefore.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(app.buildBefore.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(app.buildBefore.payloadSha256, '7ee53c78a1c5c0c2deecfbb38f70d443d3315d6d420ec756c1b5113ccc7cbffb');
assert.equal(fast.target.pid, app.launch.pid);
assert.equal(String(naturalClose.creationFiletime), String(fast.target.creationFiletime));
assert.equal(String(naturalClose.hwnd), String(fast.target.hwnd));
assert.equal(naturalClose.pid, fast.target.pid);
assert(started && listWheel);
const taskToWheelMs = new Date(listWheel.at).getTime() - new Date(started.at).getTime();
assert(taskToWheelMs > 0 && taskToWheelMs < 30000, 'list wheel must occur inside 30-second task interval');
assert.equal(resize.pid, app.launch.pid);
assert.equal(resize.foregroundBefore, true);
assert.deepEqual(resize.before, [234, 234, 1334, 954]);
assert.deepEqual(resize.after, [234, 234, 1414, 1014]);
assert.equal(naturalClose.posted, true);
assert.equal(naturalClose.processExited, true);
assert.equal(naturalClose.windowDestroyed, true);
assert.equal(naturalClose.nativeExitCode, 0);
assert.equal(naturalClose.forcedTermination, false);
for (const name of ['baseline', 'task-location', 'concurrent-input-resize', 'list-return', 'concurrent-list-scroll']) {
  const metadata = json(`visible-v02-x86-attempt4-fast-driver/${name}.json`);
  assert.equal(metadata.pid, app.launch.pid);
  assert.equal(String(metadata.creationFiletime), String(fast.target.creationFiletime));
  assert.equal(String(metadata.hwnd), String(fast.target.hwnd));
  assert.equal(metadata.naturallyForeground, true);
  assert.equal(metadata.imageBytes, read(`visible-v02-x86-attempt4-fast-driver/${name}.png`).length);
  assert.equal(metadata.imageSha256, sha(read(`visible-v02-x86-attempt4-fast-driver/${name}.png`)));
}
for (const item of fast.actions) assert.equal(item.exitCode, 0, `${item.label} failed`);
const pairs = [];
function collect(directory, destination) {
  for (const item of fs.readdirSync(path.join(root, directory), {withFileTypes: true})) {
    const source = `${directory}/${item.name}`;
    const output = `${destination}/${item.name}`;
    if (item.isDirectory()) collect(source, output);
    else if (item.isFile()) pairs.push([source, output]);
    else throw Error(`Unsupported raw item: ${source}`);
  }
}
collect('visible-v02-x86-attempt4', 'attempt4');
collect('visible-v02-x86-attempt4-fast-driver', 'fast-driver');
collect('command-logs/fast-v02-x86-attempt4-driver', 'command');
const controls = [
  'Archive-FastV02X86.cjs', 'Run-V02FastVisible-v2.cjs', 'Run-VisibleV02-v2.cjs', 'Capture-Command.cjs',
  'Capture-V02Window.ps1', 'Close-V02Window.ps1',
  'probe-build-input-v2-attempt1/V02InputProbe.cpp',
  'probe-build-input-v2-attempt1/CMakeLists.txt',
  'probe-v2-build-queued-attempt1/result.json',
  'probe-v2-build-queued-attempt1/build/x86/Release/V02InputProbe.exe',
  'probe-v2-build-queued-attempt1/x86-configure.stdout.log',
  'probe-v2-build-queued-attempt1/x86-configure.stderr.log',
  'probe-v2-build-queued-attempt1/x86-build.stdout.log',
  'probe-v2-build-queued-attempt1/x86-build.stderr.log',
];
for (const relative of controls) pairs.push([relative, `controls/${relative}`]);

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
  schema: 'soui.v02.visible-x86-raw/v1', fileCount: files.length, files,
  releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify({
  schema: 'soui.v02.visible-x86-review/v1',
  architecture: 'x86', buildId: app.buildBefore.buildId,
  sourceSha256: app.buildBefore.sourceSha256,
  payloadSha256: app.buildBefore.payloadSha256,
  nativeExitCode: app.nativeExitCode, fastDriverExitCode: command.exitCode,
  naturallyForeground: true, naturalCloseVerified: true,
  outerWheelActions: fast.actions.filter(item => item.label.startsWith('outer-down-')).length,
  innerListWheelWithinTaskMs: taskToWheelMs,
  resizeBefore: resize.before, resizeAfter: resize.after,
  visualObservation: {
    method: 'manual review of original target-window PNGs and matching screenshot SHA-256',
    rowsLoaded: 10000,
    committedHanText: '中文',
    inputCompositionEventsVisible: 3,
    taskRunningOnInputResizeScreenshot: true,
    taskProgressVisiblePercent: 18,
    listFirstVisibleRowBeforeInnerWheel: 1,
    listFirstVisibleRowAfterInnerWheel: 4,
    viewportAfterResize: [1180, 780],
  },
  x86CurrentV02OneRoundVerified: true, win7CurrentV02Verified: false,
  fullV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, taskToWheelMs}));
