'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

assert.equal(process.argv.length, 3, 'Usage: node Capture-Archive.cjs EXTERNAL_ROOT');
const originRoot = path.resolve(process.argv[2]);
const guestName = 'guest-scrollbar-style-attempt1';
const transferName = 'scrollbar-win7-transfer-attempt1';
const payloadName = 'scrollbar-win7-payload-attempt1';
const root = __dirname;
const raw = path.join(root, 'raw');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

const guestFiles = fs.readdirSync(path.join(originRoot, guestName)).sort();
assert.equal(guestFiles.length, 37);
for (const name of guestFiles) {
  assert.match(name, /^[a-z0-9][a-z0-9.-]*$/);
  assert(fs.statSync(path.join(originRoot, guestName, name)).isFile());
}
for (const name of ['serial-export.txt', 'review-scrollbar.json', 'visual-review.json',
  'app-check3.ppm', 'app-check3.png', 'qemu-result.json', 'command-send.json',
  'desktop-confirmation.json', 'desktop-confirmation-v2.json']) {
  assert(guestFiles.includes(name), `Missing original: ${name}`);
}
assert.equal(guestFiles.filter(name => /^monitor-run-[0-9]+\.log$/.test(name)).length, 1);
assert.equal(guestFiles.filter(name => /^monitor-hmp-[0-9]+\.log$/.test(name)).length, 1);
assert.equal(guestFiles.filter(name => /^monitor-screen-[0-9]+\.log$/.test(name)).length, 6);

// Keep host-specific absolute paths out of the repository. Their exact original
// identities and path-neutral fields are recorded in provenance.json instead.
const hostSpecific = guestFiles.filter(name =>
  name === 'launch-script.ps1' || name === 'launch.json' || /^monitor-screen-[0-9]+\.log$/.test(name));
assert.equal(hostSpecific.length, 8);
const sources = guestFiles.filter(name => !hostSpecific.includes(name)).map(name => ({
  archive: `guest/${name}`, origin: `${guestName}/${name}`,
}));
for (const name of ['freeze.json']) sources.push({
  archive: `transport/${name}`, origin: `${transferName}/${name}`,
});
for (const name of ['run-display-explore.cmd', 'run-display-explore.ps1',
  'export-serial.ps1', 'transfer-files.tsv',
  'SouiStarter-payload.tsv', 'SouiWorkbench-payload.tsv']) sources.push({
  archive: `transport/${name}`, origin: `${transferName}/volume/soui-input/${name}`,
});
for (const name of ['payload-preparation.json', 'build-Release.json']) sources.push({
  archive: `payload/${name}`, origin: `${payloadName}/${name}`,
});
sources.push({archive: 'control/Review-ScrollbarGuest.cjs', origin: 'Review-ScrollbarGuest.cjs'});
assert.equal(sources.length, 39);
assert.equal(new Set(sources.map(item => item.archive)).size, sources.length);
for (const item of sources) {
  assert(!item.archive.split('/').includes('..'));
  assert(!item.origin.split('/').includes('..'));
  assert(fs.statSync(path.join(originRoot, item.origin)).isFile(), item.origin);
}
const existing = fs.existsSync(raw);
if (!existing) fs.mkdirSync(raw);
const manifest = sources.map(item => {
  const bytes = fs.readFileSync(path.join(originRoot, item.origin));
  const output = path.join(raw, item.archive);
  if (existing) assert(fs.readFileSync(output).equals(bytes),
    `Existing archive differs from original: ${item.archive}`);
  else {
    fs.mkdirSync(path.dirname(output), {recursive: true});
    fs.writeFileSync(output, bytes, {flag: 'wx'});
  }
  return {...item, bytes: bytes.length, sha256: sha(bytes)};
}).sort((a, b) => a.archive.localeCompare(b.archive));
if (existing) {
  const walk = (base, prefix = '') => fs.readdirSync(base, {withFileTypes: true}).flatMap(item => {
    const relative = prefix ? `${prefix}/${item.name}` : item.name;
    return item.isDirectory() ? walk(path.join(base, item.name), relative) : [relative];
  }).sort();
  assert.deepEqual(walk(raw), manifest.map(item => item.archive).sort(),
    'Existing archive has extra or missing files');
}
const manifestPath = path.join(root, 'raw-manifest.json');
const manifestBytes = Buffer.from(JSON.stringify(manifest, null, 2) + '\n');
if (fs.existsSync(manifestPath))
  assert(fs.readFileSync(manifestPath).equals(manifestBytes), 'Existing manifest differs');
else fs.writeFileSync(manifestPath, manifestBytes, {flag: 'wx'});
const inputBytes = fs.readFileSync(path.join(originRoot, transferName, 'input.json'));
const input = JSON.parse(inputBytes.toString('utf8').replace(/^\uFEFF/, ''));
const launchBytes = fs.readFileSync(path.join(originRoot, guestName, 'launch.json'));
const launch = JSON.parse(launchBytes.toString('utf8').replace(/^\uFEFF/, ''));
const omitted = [
  {origin: `${transferName}/input.json`, bytes: inputBytes.length, sha256: sha(inputBytes)},
  ...hostSpecific.map(name => {
    const bytes = fs.readFileSync(path.join(originRoot, guestName, name));
    return {origin: `${guestName}/${name}`, bytes: bytes.length, sha256: sha(bytes)};
  }),
];
assert.equal(omitted.length, 9);
const provenance = {
  schema: 'soui.scrollbar-win7-public-provenance/v1',
  input: {schema: input.schema, buildId: input.buildId,
    sourceSha256: input.sourceSha256, guestScriptSha256: input.guestScriptSha256,
    payloadPreparationSha256: input.payloadPreparationSha256,
    transferManifestSha256: input.transferManifestSha256,
    guestCommand: input.guestCommand, appArguments: input.appArguments,
    requestedDisplay: input.requestedDisplay, appDeadlineMs: input.appDeadlineMs,
    mode: input.mode, guestExecuted: input.guestExecuted, releaseReady: input.releaseReady},
  launch: {pid: launch.pid, inputName: launch.inputName,
    freezeSha256: launch.freezeSha256, startedAt: launch.startedAt,
    monitorPort: launch.monitorPort, vncPort: launch.vncPort,
    diskSha256Before: launch.diskSha256Before,
    snapshotEnabled: launch.arguments.includes('-snapshot'),
    networkDisabled: launch.arguments.some((argument, index) =>
      argument === '-net' && launch.arguments[index + 1] === 'none')},
  omitted,
};
const provenancePath = path.join(root, 'provenance.json');
fs.writeFileSync(provenancePath, JSON.stringify(provenance, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({captured: true, rawFiles: manifest.length,
  omittedHostSpecific: omitted.length,
  manifestSha256: sha(fs.readFileSync(manifestPath)),
  provenanceSha256: sha(fs.readFileSync(provenancePath))}));
