'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');

const [flag, originalArg] = process.argv.slice(2);
assert(process.argv.length === 2 || (process.argv.length === 4 && flag === '--originals' && originalArg),
  'Usage: node Verify-Archive.cjs [--originals EXTERNAL_ROOT]');
const originals = originalArg && path.resolve(originalArg);
const root = __dirname;
const repository = path.resolve(root, '../../..');
const prefix = 'verification/production-validation/scrollbar-style-win7-2026-09-29/raw';
const raw = path.join(root, 'raw');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = name => fs.readFileSync(path.join(raw, name));
const decode = bytes => bytes[0] === 0xff && bytes[1] === 0xfe
  ? bytes.subarray(2).toString('utf16le')
  : bytes.toString('utf8').replace(/^\uFEFF/, '');
const json = name => JSON.parse(decode(read(name)));
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'raw-manifest.json'), 'utf8'));
const provenance = JSON.parse(fs.readFileSync(path.join(root, 'provenance.json'), 'utf8'));
assert.equal(manifest.length, 39);
assert.equal(provenance.schema, 'soui.scrollbar-win7-public-provenance/v1');
assert.equal(provenance.omitted.length, 9);
for (const name of ['raw-manifest.json', 'provenance.json']) {
  const bytes = fs.readFileSync(path.join(root, name));
  const staged = spawnSync('git', ['show', `:verification/production-validation/scrollbar-style-win7-2026-09-29/${name}`],
    {cwd: repository, encoding: null});
  assert.equal(staged.status, 0, `${name}: Git index read failed`);
  assert(staged.stdout.equals(bytes), `${name}: Git index differs`);
}
function walk(base, prefixPath = '') {
  return fs.readdirSync(base, {withFileTypes: true}).flatMap(item => {
    assert.equal(item.isSymbolicLink(), false);
    const relative = prefixPath ? `${prefixPath}/${item.name}` : item.name;
    if (item.isDirectory()) return walk(path.join(base, item.name), relative);
    assert.equal(item.isFile(), true);
    return [relative];
  }).sort();
}
assert.deepEqual(walk(raw), manifest.map(item => item.archive).sort());
for (const item of manifest) {
  assert.match(item.archive, /^(?:guest|transport|payload|control)\/[A-Za-z0-9][A-Za-z0-9._-]*$/);
  assert(!item.origin.split('/').includes('..'));
  assert(Number.isSafeInteger(item.bytes) && item.bytes >= 0);
  assert.match(item.sha256, /^[a-f0-9]{64}$/);
  const bytes = read(item.archive);
  assert.equal(bytes.length, item.bytes, item.archive);
  assert.equal(sha(bytes), item.sha256, item.archive);
  const staged = spawnSync('git', ['show', `:${prefix}/${item.archive}`],
    {cwd: repository, encoding: null, maxBuffer: 6 * 1024 * 1024});
  assert.equal(staged.status, 0, `${item.archive}: Git index read failed`);
  assert(staged.stdout.equals(bytes), `${item.archive}: Git index differs`);
  if (originals) assert(fs.readFileSync(path.join(originals, item.origin)).equals(bytes),
    `${item.archive}: external original differs`);
}
for (const item of provenance.omitted) {
  assert.match(item.origin, /^(?:guest-scrollbar-style-attempt1|scrollbar-win7-transfer-attempt1)\/[A-Za-z0-9][A-Za-z0-9._-]*$/);
  assert(Number.isSafeInteger(item.bytes) && item.bytes > 0);
  assert.match(item.sha256, /^[a-f0-9]{64}$/);
  if (originals) {
    const bytes = fs.readFileSync(path.join(originals, item.origin));
    assert.equal(bytes.length, item.bytes, item.origin);
    assert.equal(sha(bytes), item.sha256, item.origin);
  }
}
if (originals) {
  const originalInput = JSON.parse(decode(fs.readFileSync(path.join(originals,
    'scrollbar-win7-transfer-attempt1/input.json'))));
  const originalLaunch = JSON.parse(decode(fs.readFileSync(path.join(originals,
    'guest-scrollbar-style-attempt1/launch.json'))));
  for (const [field, value] of Object.entries(provenance.input))
    assert.equal(originalInput[field], value, `Input summary differs: ${field}`);
  for (const [field, value] of Object.entries(provenance.launch)) {
    if (field === 'snapshotEnabled') assert.equal(originalLaunch.arguments.includes('-snapshot'), value);
    else if (field === 'networkDisabled') assert.equal(originalLaunch.arguments.some((argument, index) =>
      argument === '-net' && originalLaunch.arguments[index + 1] === 'none'), value);
    else assert.equal(originalLaunch[field], value, `Launch summary differs: ${field}`);
  }
}

const freezeSha = '06ac9481357e387dbc266a27dea032c25a24c69b93d3521ad3318ca45fcc33c1';
assert.equal(sha(read('transport/freeze.json')), freezeSha);
const input = provenance.input;
const freeze = json('transport/freeze.json');
const prepared = json('payload/payload-preparation.json');
const build = json('payload/build-Release.json');
assert.equal(input.schema, 'soui.scrollbar-win7-input/v1');
assert.equal(freeze.inputSha256, provenance.omitted.find(item =>
  item.origin === 'scrollbar-win7-transfer-attempt1/input.json').sha256);
assert.equal(input.buildId, '111abe32-bfed-42aa-ae9c-80dcb3ba2f6e');
assert.equal(input.sourceSha256,
  '33adadfc3643208c05c27f7ac28c430175d9dcc5be69e254ea8796f8c711dd09');
assert.equal(input.requestedDisplay, '1024x768');
assert.equal(input.appDeadlineMs, 120000);
assert.equal(prepared.buildId, input.buildId);
assert.equal(prepared.sourceSha256, input.sourceSha256);
assert.equal(prepared.payloadPrepared, true);
assert.equal(build.buildId, input.buildId);
assert.equal(build.source.sha256, input.sourceSha256);
assert.equal(build.architecture, 'x86');
assert.equal(build.releaseReady, false);
assert.equal(sha(read('transport/run-display-explore.ps1')), input.guestScriptSha256);
assert.equal(sha(read('transport/transfer-files.tsv')), input.transferManifestSha256);
assert.equal(sha(read('payload/payload-preparation.json')), input.payloadPreparationSha256);
for (const app of ['SouiStarter', 'SouiWorkbench']) {
  const item = prepared.packages[app];
  assert.equal(item.legal.length, 5);
  assert.equal(build.payloads[app].sha256, item.payloadSha256);
  assert.equal(sha(read(`transport/${app}-payload.tsv`)), item.manifestSha256);
}

const launch = provenance.launch;
const sent = json('guest/command-send.json');
const result = json('guest/qemu-result.json');
const desktop = json('guest/desktop-confirmation-v2.json');
assert.throws(() => json('guest/desktop-confirmation.json'),
  'Malformed first desktop metadata must remain unaltered');
assert.equal(launch.inputName, 'scrollbar-win7-transfer-attempt1');
assert.equal(launch.freezeSha256, freezeSha);
assert.equal(launch.pid, 45864);
assert.equal(sent.pid, launch.pid);
assert.equal(result.pid, launch.pid);
assert.equal(desktop.qemuPid, launch.pid);
assert.equal(sent.command, input.guestCommand);
assert(Date.parse(desktop.checkedAt) <= Date.parse(sent.sentAt));
assert.equal(launch.snapshotEnabled, true);
assert.equal(launch.networkDisabled, true);
assert.equal(result.exitCode, 0);
assert.equal(result.timedOut, false);
assert.equal(result.externalLoadObserved, false);
assert.equal(result.gateFailure, null);
assert.equal(result.frozenCheckExitCode, 0);
assert.equal(result.diskUnchanged, true);
assert.equal(result.diskSha256Before,
  'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert.equal(result.diskSha256After, result.diskSha256Before);
for (const name of ['gate.json', 'gate-before-launch.json'])
  assert.deepEqual(json(`guest/${name}`).processes, []);
const gates = json('guest/gates-during.json');
assert.equal(gates.length, 82);
assert(gates.every(gate => !gate.error && Array.isArray(gate.processes) && gate.processes.length === 0));
const sampleTimes = gates.map(gate => Date.parse(gate.at));
assert(sampleTimes.every(Number.isFinite));
assert(sampleTimes[0] >= Date.parse(launch.startedAt) &&
  sampleTimes[0] - Date.parse(launch.startedAt) <= 15000);
for (let index = 1; index < sampleTimes.length; index += 1)
  assert(sampleTimes[index] >= sampleTimes[index - 1] &&
    sampleTimes[index] - sampleTimes[index - 1] <= 15000,
  'Host interference sample gap is too long');
assert.equal(gates.at(-1).phase, 'after-qemu-exit');
assert(Date.parse(gates.at(-1).at) >= Date.parse(result.qemuExitedAt));
assert(Date.parse(gates.at(-1).at) - Date.parse(result.qemuExitedAt) <= 15000);
assert.equal(manifest.filter(item => /^guest\/monitor-run-[0-9]+\.log$/.test(item.archive)).length, 1);
assert.equal(manifest.filter(item => /^guest\/monitor-hmp-[0-9]+\.log$/.test(item.archive)).length, 1);

const serial = read('guest/serial-export.txt');
const lines = serial.toString('ascii').split(/\r?\n/);
assert.equal(lines.shift(), 'SOUI_SERIAL_V1');
assert.equal(lines.pop(), '');
assert.equal(lines.pop(), 'SOUI_SERIAL_END');
assert.equal(lines.length % 2, 0);
const files = new Map();
for (let index = 0; index < lines.length; index += 2) {
  const match = /^FILE\|([a-z0-9][a-z0-9._-]*)\|([0-9]+)\|([A-F0-9]{64})$/.exec(lines[index]);
  assert(match, `Invalid serial frame ${index}`);
  assert.equal(files.has(match[1]), false);
  const bytes = Buffer.from(lines[index + 1], 'base64');
  assert.equal(bytes.toString('base64'), lines[index + 1]);
  assert.equal(bytes.length, Number(match[2]));
  assert.equal(sha(bytes).toUpperCase(), match[3]);
  files.set(match[1], bytes);
}
assert.equal(files.size, 51);
const text = name => {
  assert(files.has(name), `Missing serial file: ${name}`);
  return decode(files.get(name));
};
assert.match(text('guest-identity.txt'), /Version=6\.1\.7601/);
assert.match(text('guest-identity.txt'), /ServicePackMajorVersion=1/);
assert.match(text('guest-identity.txt'), /OSArchitecture=32-bit/);
assert.match(text('guest-identity.txt'), /PointerBytes=4/);
assert.equal(text('run-exitcode.txt').trim(), '0');
assert.equal(text('workbench-scrollbar-style-exitcode.txt').trim(), '0');
for (const phase of ['pre', 'post']) {
  assert.equal(text(`payload-${phase}-exitcode.txt`).trim(), '0');
  assert.match(text(`payload-${phase}.log`), /SouiStarter=71,legal=5,all-hashes=matched/);
  assert.match(text(`payload-${phase}.log`), /SouiWorkbench=475,legal=5,all-hashes=matched/);
}
const review = json('guest/review-scrollbar.json');
assert.equal(review.serialSha256, sha(serial));
assert.equal(review.serialFrames, files.size);
assert.equal(review.guestIdentityVerified, true);
assert.equal(review.payloadPrePostVerified, true);
assert.equal(review.applicationNativeExitCode, 0);
assert.equal(review.closeWasHarnessPosted, true);
assert.deepEqual(review.initialBounds, [87, 24, 937, 664]);
assert.deepEqual(review.resizeAfter, [87, 24, 1017, 724]);
assert.equal(review.screenshotReviewed, false);

const visual = json('guest/visual-review.json');
const ppm = read('guest/app-check3.ppm');
assert(ppm.subarray(0, 16).toString('ascii') === 'P6\n1024 768\n255\n');
function colorCount(x1, x2, y1, y2, color) {
  let count = 0;
  for (let y = y1; y <= y2; y += 1) {
    for (let x = x1; x <= x2; x += 1) {
      const offset = 16 + 3 * (y * 1024 + x);
      if (ppm[offset] === color[0] && ppm[offset + 1] === color[1] &&
          ppm[offset + 2] === color[2]) count += 1;
    }
  }
  return count;
}
const thumb = [0x5a, 0x7f, 0xa2];
const track = [0xea, 0xf2, 0xf8];
assert(colorCount(1006, 1013, 80, 340, thumb) > 1000, 'Outer styled thumb pixels missing');
assert(colorCount(1006, 1013, 80, 340, track) > 300, 'Outer styled track pixels missing');
assert(colorCount(957, 964, 385, 450, thumb) > 30, 'List styled thumb pixels missing');
assert(colorCount(957, 964, 385, 450, track) > 300, 'List styled track pixels missing');
assert.equal(visual.source, 'app-check3.ppm');
assert.equal(visual.sourceSha256, sha(ppm));
assert.equal(visual.previewSha256, sha(read('guest/app-check3.png')));
assert.equal(visual.mechanicalReviewSha256, sha(read('guest/review-scrollbar.json')));
assert.equal(visual.screenshotReviewed, true);
for (const field of ['realScrollVerified', 'imeCompositionVerified', 'taskParallelVerified',
  'releaseReady']) assert.equal(visual[field], false);
for (const field of ['realScrollVerified', 'imeCompositionVerified', 'taskParallelVerified',
  'win7CurrentV02Verified', 'benchmarkValid', 'releaseReady']) assert.equal(review[field], false);
console.log(JSON.stringify({verified: true, rawFiles: manifest.length,
  originalsCompared: Boolean(originals), guestIdentityVerified: true,
  serialFrames: files.size, screenshotReviewed: true,
  realScrollVerified: false, win7CurrentV02Verified: false, releaseReady: false}));
