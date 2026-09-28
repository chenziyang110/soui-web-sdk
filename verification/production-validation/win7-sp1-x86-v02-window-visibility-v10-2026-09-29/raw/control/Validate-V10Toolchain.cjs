'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const expected = new Map([
  ['Prepare-V10Transfer.cjs', 'b225de77024addf2c56dcd2ad8b202f6a53cd88b6a26669bc170d2e9cc46a81f'],
  ['Freeze-V10Transfer.cjs', 'b7fb261483a7cbb199c92857108a0906a0aadeee55c8076ebfe45be7d3471196'],
  ['Verify-V10Transfer.cjs', '1ce3182ae881c018e48a0bd8a87c89fb927d993335ec706b442e57c13d7e1115'],
  ['Run-V10.ps1', 'c953c938706de897c7fa6d8ef58d757a914120d111000a563a268df5d289e7b5'],
  ['Launch-V10.ps1', '07a6d8919ea34c6f27d72815e8ea30e5ea5b7fd23f3f78eac4b893acffdbe8bc'],
  ['win7-v10-source/run-visibility-diagnostic.ps1', '585f5c614fda1cbd7a9d183013da0c15a0cd88de8bd61712eaabd304f15cd84a'],
]);
for (const [relative, value] of expected) {
  const file = path.join(root, relative);
  assert.equal(sha(fs.readFileSync(file)), value, relative);
  if (relative.endsWith('.cjs')) {
    const check = cp.spawnSync(process.execPath, ['--check', file], {encoding: 'utf8', timeout: 10000});
    assert.equal(check.status, 0, check.stderr);
    assert.equal(check.stderr, '');
  }
}
const v9Freeze = path.join(root, 'win7-v9-transfer-attempt1/freeze.json');
assert.equal(sha(fs.readFileSync(v9Freeze)),
  '98acf9d01407a3c4bcbe843b6d9289598d607f3c32e20485759e6fc4b1dc6e80');
const script = fs.readFileSync(path.join(root, 'win7-v10-source/run-visibility-diagnostic.ps1'), 'utf8');
const parser = JSON.parse(fs.readFileSync(path.join(root, 'v10-powershell-parser.json'), 'utf8').replace(/^\uFEFF/, ''));
assert.equal(parser.schema, 'soui.win7-v02-v10-parser/v1');
for (const item of parser.files) {
  assert.equal(item.sha256, expected.get(item.file));
  assert.equal(item.parserErrors, 0);
  assert.equal(item.nativeActionCalls, 0);
}
assert.equal(parser.files.length, 3);
for (const marker of ['SmokeBase64=', 'MainWindowHandle=',
  '$nextInspectMs=10000;$nextCloseMs=108000',
  '$watch.ElapsedMilliseconds -lt 108000',
  'TimeoutMs=120000',
  'scope=1024x768-v02-window-visibility-diagnostic-no-input-not-production'])
  assert(script.includes(marker), `Missing marker ${marker}`);
assert.equal(/\bRun-Action\s+\$pidValue/.test(script), false);
assert.equal(fs.existsSync(path.join(root, 'win7-v10-transfer-attempt1')), false);
assert.equal(fs.existsSync(path.join(root, 'guest-window-visibility-v10-attempt1')), false);
const result = {schema: 'soui.win7-v02-window-visibility-v10-static-preparation/v1',
  checkedAt: new Date().toISOString(), files: [...expected].map(([file, sha256]) => ({file, sha256})),
  v9FreezeSha256: sha(fs.readFileSync(v9Freeze)), nodeSyntaxChecked: true,
  powerShellParserSha256: sha(fs.readFileSync(path.join(root, 'v10-powershell-parser.json'))),
  noNativeAppInputCalls: true,
  originalAppDeadlineMs: 120000, guestExecuted: false, transferFrozen: false,
  win7CurrentV02Verified: false, releaseReady: false};
fs.writeFileSync(path.join(root, 'v10-static-preparation.json'), JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({checkedFiles: expected.size, guestExecuted: false, transferFrozen: false}));
