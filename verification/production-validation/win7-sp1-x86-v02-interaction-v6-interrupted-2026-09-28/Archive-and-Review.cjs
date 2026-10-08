'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const external = 'C:/SOUI-SDK-production-validation/preview15-v02-win7-current-20260927';
const guest = path.join(external, 'guest-layout-interaction-v6-attempt1');
const driver = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/win7-v6-attempt1-driver';
const freezeSha256 = '8e9b6d96266e3f72af6e49c498efd942886266bd9fbfdbc71d6e9f58df5b830f';
const diskSha256 = 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4';
const raw = path.join(root, 'raw');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const source = item => item.origin === 'guest' ? path.join(guest, item.sourceName)
  : item.origin === 'driver' ? path.join(driver, item.sourceName)
  : path.join(external, item.sourceName);
const archived = item => path.join(raw, item.name);
const json = name => JSON.parse(fs.readFileSync(path.join(raw, name), 'utf8').replace(/^\uFEFF/, ''));
const mode = process.argv[2];

if (mode === 'archive') {
  assert.equal(fs.existsSync(raw), false, 'Refuse to replace archived evidence');
  const entries = fs.readdirSync(guest, {withFileTypes: true});
  assert(entries.every(entry => entry.isFile()));
  const snapshots = entries.filter(entry => entry.name.startsWith('vl.'))
    .map(entry => ({name: entry.name, bytes: fs.statSync(path.join(guest, entry.name)).size,
      sha256: sha(fs.readFileSync(path.join(guest, entry.name)))}))
    .sort((a, b) => a.name.localeCompare(b.name));
  assert.equal(snapshots.length, 2);
  const guestFiles = entries.filter(entry => !entry.name.startsWith('vl.'))
    .map(entry => entry.name).sort();
  assert.equal(guestFiles.length, 12);
  for (const name of ['gate-before-launch.json', 'gates-during.json', 'launch.json',
    'qemu-result.json', 'serial-export.txt', 'frozen-check-before.log',
    'frozen-check-after.log']) assert(guestFiles.includes(name), name);
  assert.equal(guestFiles.includes('command-send.json'), false);
  const items = [
    ...guestFiles.map(name => ({origin: 'guest', sourceName: name, name})),
    ...['result.json', 'stdout.log', 'stderr.log'].map(name => ({
      origin: 'driver', sourceName: name, name: `driver-${name}`})),
    {origin: 'control', sourceName: 'v6-control-preflight.json',
      name: 'v6-control-preflight.json'},
    {origin: 'control', sourceName: 'v5-not-executed-static-refusal.json',
      name: 'v5-not-executed-static-refusal.json'},
  ];
  fs.mkdirSync(raw);
  const copied = items.map(item => {
    const from = source(item);
    const to = archived(item);
    fs.copyFileSync(from, to, fs.constants.COPYFILE_EXCL);
    const bytes = fs.readFileSync(from);
    assert.equal(sha(fs.readFileSync(to)), sha(bytes));
    return {...item, bytes: bytes.length, sha256: sha(bytes)};
  });
  const record = {schema: 'soui.win7-v02-v6-interrupted-evidence/v1',
    freezeSha256, excludedGuestTempSnapshots: snapshots, items: copied};
  fs.writeFileSync(path.join(raw, 'copy-review.json'), JSON.stringify(record, null, 2) + '\n',
    {flag: 'wx'});
} else if (mode !== 'review' && mode !== '--check-index') {
  throw Error('Usage: node Archive-and-Review.cjs archive|review|--check-index');
}

const copy = json('copy-review.json');
assert.equal(copy.schema, 'soui.win7-v02-v6-interrupted-evidence/v1');
assert.equal(copy.freezeSha256, freezeSha256);
assert.equal(copy.excludedGuestTempSnapshots.length, 2);
for (const item of copy.items) {
  assert.match(item.name, /^[A-Za-z0-9][A-Za-z0-9._-]*$/);
  const bytes = fs.readFileSync(archived(item));
  assert.equal(bytes.length, item.bytes, item.name);
  assert.equal(sha(bytes), item.sha256, item.name);
  assert.equal(sha(fs.readFileSync(source(item))), item.sha256, `original: ${item.name}`);
  if (mode === '--check-index') {
    const repo = path.resolve(root, '../../..');
    const relative = path.relative(repo, archived(item)).split(path.sep).join('/');
    const staged = cp.execFileSync('git', ['-C', repo, 'show', `:${relative}`],
      {encoding: null, windowsHide: true, maxBuffer: item.bytes + 1024});
    assert.equal(staged.length, item.bytes, `index size: ${item.name}`);
    assert.equal(sha(staged), item.sha256, `index hash: ${item.name}`);
  }
}
for (const snapshot of copy.excludedGuestTempSnapshots) {
  assert.match(snapshot.name, /^vl\.[A-Za-z0-9]+$/);
  const bytes = fs.readFileSync(path.join(guest, snapshot.name));
  assert.equal(bytes.length, snapshot.bytes);
  assert.equal(sha(bytes), snapshot.sha256);
}

const launch = json('launch.json');
const result = json('qemu-result.json');
const driverResult = json('driver-result.json');
const gate = json('gate-before-launch.json');
const samplesValue = json('gates-during.json');
const samples = Array.isArray(samplesValue) ? samplesValue : [samplesValue];
assert.equal(launch.freezeSha256, freezeSha256);
assert.equal(launch.pid, 75276);
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
assert.equal(fs.statSync(path.join(raw, 'serial-export.txt')).size, 0);
assert.equal(fs.existsSync(path.join(guest, 'command-send.json')), false);
assert.equal((gate.processes || []).length, 0);
assert(samples.length >= 2);
assert(samples.slice(0, -1).every(sample =>
  !sample.error && (!sample.processes || sample.processes.length === 0)));
const last = samples.at(-1);
assert.equal(last.error, null);
assert.equal(last.processes.length, 1);
assert.equal(last.processes[0].ProcessId, 63876);
assert.equal(last.processes[0].Name, 'qemu-system-x86_64.exe');
const externalStarted = Number(/\/Date\(([0-9]+)\)\//.exec(last.processes[0].CreationDate)?.[1]);
assert(Number.isFinite(externalStarted));
assert(new Date(launch.startedAt).getTime() < externalStarted);
assert(externalStarted <= new Date(last.at).getTime());
assert(new Date(last.at).getTime() <= new Date(result.finishedAt).getTime());
assert.equal(fs.readFileSync(path.join(raw, 'frozen-check-before.log')).equals(
  fs.readFileSync(path.join(raw, 'frozen-check-after.log'))), true);
const refusal = json('v5-not-executed-static-refusal.json');
assert.equal(refusal.guestStarted, false);
assert.equal(refusal.driverStarted, false);
assert.equal(refusal.wouldFailBeforeSendingFirstWheel, true);
console.log(JSON.stringify({files: copy.items.length, indexChecked: mode === '--check-index',
  ownQemuPid: launch.pid, externalQemuPid: 63876, commandSent: false, serialBytes: 0,
  driverExitCode: driverResult.exitCode, qemuExitCode: result.exitCode,
  frozenInputUnchanged: true, diskUnchanged: true, win7CurrentV02Verified: false,
  releaseReady: false}));
