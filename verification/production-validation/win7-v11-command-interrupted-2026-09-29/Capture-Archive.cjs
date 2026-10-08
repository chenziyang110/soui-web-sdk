'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const originRoot = 'C:/SOUI-SDK-production-validation/work-area-placement-20260928';
const attempt = path.join(originRoot, 'guest-native-resize-v11-attempt2');
const driver = path.join(originRoot, 'command-logs/guest-native-resize-v11-driver-attempt2');
const transfer = path.join(originRoot, 'win7-v11-transfer-attempt1');
const root = __dirname;
const raw = path.join(root, 'raw');
assert(!fs.existsSync(raw), 'Archive already exists; never replace raw evidence');
const files = [
  ['driver-exit.txt', path.join(driver, 'exit.txt')],
  ['driver-stdout.log', path.join(driver, 'stdout.log')],
  ['driver-stderr.log', path.join(driver, 'stderr.log')],
  ['transfer-freeze.json', path.join(transfer, 'freeze.json')],
  ['transfer-input.json', path.join(transfer, 'input.json')],
  ...[
    'gate.json', 'gate-before-launch.json', 'gates-during.json',
    'frozen-check-before.log', 'frozen-check-after.log',
    'launch-script.ps1', 'launch.json', 'monitor-script.cjs',
    'command-send.json', 'desktop-precommand-1.ppm', 'desktop-precommand-1.png',
    'desktop-aftercommand-1.ppm', 'desktop-aftercommand-1.png',
    'qemu-result.json', 'qemu.stderr.log', 'qemu.stdout.log', 'serial-export.txt',
  ].map(name => [name, path.join(attempt, name)]),
];
fs.mkdirSync(raw);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const manifest = files.map(([name, original]) => {
  const bytes = fs.readFileSync(original);
  fs.writeFileSync(path.join(raw, name), bytes, {flag: 'wx'});
  return {name, bytes: bytes.length, sha256: sha(bytes)};
}).sort((a, b) => a.name.localeCompare(b.name));
fs.writeFileSync(path.join(root, 'raw-manifest.json'), JSON.stringify(manifest, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({status: 'captured', rawFiles: manifest.length,
  manifestSha256: sha(fs.readFileSync(path.join(root, 'raw-manifest.json')))}));
