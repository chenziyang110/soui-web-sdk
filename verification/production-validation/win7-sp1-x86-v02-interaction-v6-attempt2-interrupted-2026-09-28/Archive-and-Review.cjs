'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const external = 'C:/SOUI-SDK-production-validation/preview15-v02-win7-current-20260927';
const guest = path.join(external, 'guest-layout-interaction-v6-attempt2');
const driver = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/win7-v6-attempt2-driver';
const raw = path.join(root, 'raw');
const freezeSha256 = '8e9b6d96266e3f72af6e49c498efd942886266bd9fbfdbc71d6e9f58df5b830f';
const diskSha256 = 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4';
const selectedFrames = new Set(['001', '028', '037', '039', '040', '041']);
const mode = process.argv[2];
const guestPath = name => path.join(guest, name);
const rawPath = name => path.join(raw, name);
const json = name => JSON.parse(fs.readFileSync(rawPath(name), 'utf8').replace(/^\uFEFF/, ''));

function fileIdentity(file) {
  const size = fs.statSync(file).size;
  const hash = crypto.createHash('sha256');
  const fd = fs.openSync(file, 'r');
  const buffer = Buffer.allocUnsafe(1024 * 1024);
  try {
    for (;;) {
      const length = fs.readSync(fd, buffer, 0, buffer.length, null);
      if (!length) break;
      hash.update(buffer.subarray(0, length));
    }
  } finally { fs.closeSync(fd); }
  return {bytes: size, sha256: hash.digest('hex')};
}

function original(item) {
  return item.origin === 'driver' ? path.join(driver, item.sourceName) :
    guestPath(item.sourceName);
}

if (mode === 'archive') {
  assert.equal(fs.existsSync(raw), false, 'Refuse to replace archived evidence');
  const entries = fs.readdirSync(guest, {withFileTypes: true});
  assert(entries.every(entry => entry.isFile()));
  const names = entries.map(entry => entry.name).sort();
  for (const name of ['command-send.json', 'timeline-result.json', 'launch.json',
    'gates-during.json', 'qemu-result.json', 'serial-export.txt', 'boot-check-1.ppm',
    'boot-check-2.ppm', 'timeline-001.ppm', 'timeline-041.ppm'])
    assert(names.includes(name), name);
  const unarchived = names.filter(name => name.startsWith('vl.') ||
    /^timeline-\d{3}\.(ppm|png)$/.test(name) && !selectedFrames.has(name.slice(9, 12)));
  assert.equal(unarchived.filter(name => name.startsWith('vl.')).length, 2);
  const guestItems = names.filter(name => !unarchived.includes(name))
    .map(name => ({origin: 'guest', sourceName: name, name}));
  const driverItems = ['result.json', 'stdout.log', 'stderr.log'].map(name => ({
    origin: 'driver', sourceName: name, name: `driver-${name}`}));
  fs.mkdirSync(raw);
  const items = [...guestItems, ...driverItems].map(item => {
    const from = original(item);
    const to = rawPath(item.name);
    const identity = fileIdentity(from);
    fs.copyFileSync(from, to, fs.constants.COPYFILE_EXCL);
    assert.deepEqual(fileIdentity(to), identity, item.name);
    return {...item, ...identity};
  });
  const externalOnly = unarchived.map(name => ({name, ...fileIdentity(guestPath(name))}));
  const copyReview = {schema: 'soui.win7-v02-v6-attempt2-interrupted-evidence/v1',
    freezeSha256, items, externalOnly};
  fs.writeFileSync(rawPath('copy-review.json'), JSON.stringify(copyReview, null, 2) + '\n',
    {flag: 'wx'});
} else if (mode !== 'review' && mode !== '--check-index') {
  throw Error('Usage: node Archive-and-Review.cjs archive|review|--check-index');
}

const copied = json('copy-review.json');
assert.equal(copied.schema, 'soui.win7-v02-v6-attempt2-interrupted-evidence/v1');
assert.equal(copied.freezeSha256, freezeSha256);
assert.equal(copied.externalOnly.filter(item => item.name.startsWith('vl.')).length, 2);
for (const item of copied.items) {
  assert.match(item.name, /^[A-Za-z0-9][A-Za-z0-9._-]*$/);
  assert.deepEqual(fileIdentity(rawPath(item.name)),
    {bytes: item.bytes, sha256: item.sha256}, item.name);
  assert.deepEqual(fileIdentity(original(item)),
    {bytes: item.bytes, sha256: item.sha256}, `original: ${item.name}`);
  if (mode === '--check-index') {
    const repo = path.resolve(root, '../../..');
    const relative = path.relative(repo, rawPath(item.name)).split(path.sep).join('/');
    const staged = cp.execFileSync('git', ['-C', repo, 'show', `:${relative}`],
      {encoding: null, windowsHide: true, maxBuffer: item.bytes + 1024});
    assert.equal(staged.length, item.bytes, `index bytes: ${item.name}`);
    assert.equal(crypto.createHash('sha256').update(staged).digest('hex'), item.sha256,
      `index sha256: ${item.name}`);
  }
}
for (const item of copied.externalOnly) {
  assert.match(item.name, /^(vl\.[A-Za-z0-9]+|timeline-\d{3}\.(ppm|png))$/);
  assert.deepEqual(fileIdentity(guestPath(item.name)),
    {bytes: item.bytes, sha256: item.sha256}, `external only: ${item.name}`);
}

const launch = json('launch.json');
const command = json('command-send.json');
const result = json('qemu-result.json');
const driverResult = json('driver-result.json');
const gate = json('gate-before-launch.json');
const samplesValue = json('gates-during.json');
const samples = Array.isArray(samplesValue) ? samplesValue : [samplesValue];
const timeline = json('timeline-result.json');
assert.equal(launch.freezeSha256, freezeSha256);
assert.equal(launch.pid, 82040);
assert.equal(command.pid, launch.pid);
assert.equal(command.command, 'cmd.exe /c D:\\soui-input\\run-display-explore.cmd');
assert.equal(result.pid, launch.pid);
assert.equal(result.exitCode, -1);
assert.equal(result.externalLoadObserved, true);
assert.equal(result.timedOut, false);
assert.equal(result.diskSha256Before, diskSha256);
assert.equal(result.diskSha256After, diskSha256);
assert.equal(result.diskUnchanged, true);
assert.equal(result.frozenCheckExitCode, 0);
assert.equal(driverResult.exitCode, 1);
assert.equal(driverResult.timedOut, false);
assert.equal(driverResult.error, null);
assert.equal(fs.statSync(rawPath('serial-export.txt')).size, 0);
assert.equal((gate.processes || []).length, 0);
assert(samples.length > 40);
assert(samples.slice(0, -1).every(sample =>
  !sample.error && (!sample.processes || sample.processes.length === 0)));
const last = samples.at(-1);
assert.equal(last.error, null);
assert.equal(last.processes.length, 1);
assert.equal(last.processes[0].ProcessId, 49072);
assert.equal(last.processes[0].Name, 'qemu-system-x86_64.exe');
const externalStarted = Number(/\/Date\(([0-9]+)\)\//.exec(last.processes[0].CreationDate)?.[1]);
assert(Number.isFinite(externalStarted));
assert(new Date(command.sentAt).getTime() < externalStarted);
assert(externalStarted < new Date(last.at).getTime());
assert(new Date(last.at).getTime() <= new Date(result.finishedAt).getTime());
assert.equal(timeline.stopReason, 'monitor-failed');
assert.equal(timeline.frames.length, 42);
assert.equal(timeline.frames.at(-1).monitorExitCode, 1);
assert.equal(timeline.layoutWheelVerified, false);
assert.equal(timeline.releaseReady, false);
assert.deepEqual(fileIdentity(rawPath('frozen-check-before.log')),
  fileIdentity(rawPath('frozen-check-after.log')));
console.log(JSON.stringify({files: copied.items.length,
  externalOnly: copied.externalOnly.length, indexChecked: mode === '--check-index',
  ownQemuPid: launch.pid, externalQemuPid: 49072, commandSent: true,
  serialBytes: 0, timelineFrames: timeline.frames.length - 1,
  driverExitCode: driverResult.exitCode, qemuExitCode: result.exitCode,
  fullHostIsolation: false, taskStartedVerified: false,
  imeCompositionVerified: false, taskParallelVerified: false,
  win7CurrentV02Verified: false, releaseReady: false}));
