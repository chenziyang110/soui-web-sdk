'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

assert.equal(process.argv.length, 3, 'Usage: node Capture-Archive.cjs EXTERNAL_ROOT');
const originRoot = path.resolve(process.argv[2]);
const attemptName = 'guest-native-resize-v11-attempt3';
const inputName = 'win7-v11-transfer-attempt1';
const archiveRoot = __dirname;
const rawRoot = path.join(archiveRoot, 'raw');
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

const sources = [
  ...['result.json', 'stdout.log', 'stderr.log'].map(name => ({
    name: `driver-${name}`, origin: `command-logs/guest-native-resize-v11-driver-attempt3/${name}`})),
  ...['result.json', 'stdout.log', 'stderr.log'].map(name => ({
    name: `review-${name}`, origin: `command-logs/guest-native-resize-v11-review-attempt3/${name}`})),
  {name: 'review-script.cjs', origin: 'Review-V11.cjs'},
  ...['freeze.json', 'input.json'].map(name => ({name: `transfer-${name}`, origin: `${inputName}/${name}`})),
  ...['run-display-explore.cmd', 'run-display-explore.ps1', 'export-serial.ps1'].map(name => ({
    name: `transfer-${name}`, origin: `${inputName}/volume/soui-input/${name}`})),
  ...[
    'gate.json', 'gate-before-launch.json', 'gates-during.json',
    'frozen-check-before.log', 'frozen-check-after.log',
    'launch-script.ps1', 'launch.json', 'monitor-script.cjs', 'command-send.json',
    'desktop-precommand-1.ppm', 'desktop-precommand-1.png',
    'desktop-precommand-2.ppm', 'desktop-precommand-2.png',
    'desktop-aftercommand-1.ppm', 'desktop-aftercommand-1.png',
    'desktop-aftercommand-2.ppm', 'desktop-aftercommand-2.png',
    'desktop-aftercommand-3.ppm', 'desktop-aftercommand-3.png',
    'qemu-result.json', 'qemu.stderr.log', 'qemu.stdout.log',
    'review-v11.json', 'serial-export.txt',
  ].map(name => ({name, origin: `${attemptName}/${name}`})),
];
const monitorRoot = path.join(originRoot, attemptName);
const monitors = fs.readdirSync(monitorRoot).filter(name => /^monitor-(?:screen|run|hmp)-[0-9]+\.log$/.test(name));
assert.equal(monitors.filter(name => name.startsWith('monitor-run-')).length, 1);
assert.equal(monitors.filter(name => name.startsWith('monitor-hmp-')).length, 1);
assert.equal(monitors.filter(name => name.startsWith('monitor-screen-')).length, 5);
sources.push(...monitors.map(name => ({name, origin: `${attemptName}/${name}`})));
assert.equal(sources.length, 43);
assert.equal(new Set(sources.map(item => item.name)).size, sources.length);
assert(!fs.existsSync(rawRoot), 'Archive already exists; never replace raw evidence');
for (const item of sources) {
  assert.match(item.name, /^[a-z0-9][a-z0-9.-]*$/);
  assert(fs.statSync(path.join(originRoot, item.origin)).isFile(), item.origin);
}

fs.mkdirSync(rawRoot);
const manifest = sources.map(item => {
  const bytes = fs.readFileSync(path.join(originRoot, item.origin));
  fs.writeFileSync(path.join(rawRoot, item.name), bytes, {flag: 'wx'});
  return {...item, bytes: bytes.length, sha256: sha256(bytes)};
}).sort((a, b) => a.name.localeCompare(b.name));
const manifestPath = path.join(archiveRoot, 'raw-manifest.json');
fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({status: 'captured', rawFiles: manifest.length,
  manifestSha256: sha256(fs.readFileSync(manifestPath))}));
