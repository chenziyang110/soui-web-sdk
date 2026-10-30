'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const sdk = require('../../tools/sdk/soui-sdk.cjs');
const identity = require('../../tools/internal/delivery-identity.cjs');
const frontend = require('../../tools/internal/devtools-frontend.cjs');

const root = path.resolve(__dirname, '../..');
const relative = 'sdk/resources/devtools/miniblink';
const supplied = path.join(root, relative);
const baselineManifestSha256 = '2ecbd35cbc51c30efe97717c2fb8713fb032979b47286673cf70d2e6b1dcf7c2';
const sha = body => crypto.createHash('sha256').update(body).digest('hex');
const encoded = value => Buffer.from(JSON.stringify(value, null, 2) + '\n');
const read = file => fs.readFileSync(file);
const write = (file, body) => {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, body, {flag: 'wx'});
};

function workspace(t) {
  const parent = fs.realpathSync.native(os.tmpdir());
  const prefix = '.soui-devtools-baseline-sdk-';
  const directory = fs.realpathSync.native(fs.mkdtempSync(path.join(parent, prefix)));
  const created = fs.lstatSync(directory);
  t.after(() => {
    // Delete only this test's original mkdtemp directory, never a computed source path.
    assert.equal(path.isAbsolute(directory), true);
    assert.equal(path.dirname(directory), parent);
    assert.ok(path.basename(directory).startsWith(prefix));
    assert.equal(identity.safePath(directory), directory);
    assert.equal(fs.realpathSync.native(directory), directory);
    const current = fs.lstatSync(directory);
    assert.equal(current.isDirectory(), true);
    assert.equal(current.dev, created.dev);
    assert.equal(current.ino, created.ino);
    fs.rmSync(directory, {recursive: true, force: true});
  });
  return directory;
}

test('source SDK preserves the pinned Inspector baseline through install and composition, rejecting EOL drift', t => {
  const temporary = workspace(t);
  const input = path.join(temporary, 'input');
  const packed = path.join(temporary, 'package');
  const installed = path.join(temporary, 'installed');
  const manifestBody = read(path.join(supplied, 'baseline-manifest.json'));
  assert.equal(sha(manifestBody), baselineManifestSha256);
  const members = JSON.parse(manifestBody);
  assert.equal(members.length, 650);
  // Ordinary SDK metadata must survive a CRLF checkout through the LF contract.
  const sourceBody = Buffer.from(identity.normalize(read(path.join(supplied, 'source.json')), 'lf')
    .toString('latin1').replace(/\n/g, '\r\n'), 'latin1');
  const normalizedSourceBody = identity.normalize(sourceBody, 'lf');
  assert.notDeepEqual(sourceBody, normalizedSourceBody);
  const declaration = JSON.parse(sourceBody);
  assert.equal(declaration.base.manifestSha256, baselineManifestSha256);
  assert.equal(declaration.files.length, 7);
  const licenses = members.filter(row => /licen[sc]e|notice|copying|author/i.test(row.path));
  assert.equal(licenses.length, 7);

  // Other managed members are placeholders: this is a packaging contract fixture,
  // not a complete SDK build, a shipped artifact or an executable kernel pair.
  for (const name of sdk.managedRoots) write(path.join(input, name, 'fixture.txt'), 'ordinary\r\nsource\r\n');
  for (const name of sdk.managedFiles) write(path.join(input, name), 'fixture\r\n');
  write(path.join(input, relative, 'baseline-manifest.json'), manifestBody);
  write(path.join(input, relative, 'source.json'), sourceBody);
  for (const row of members) {
    const body = read(path.join(supplied, 'baseline', row.path));
    assert.equal(body.length, row.bytes, row.path);
    assert.equal(sha(body), row.sha256, row.path);
    write(path.join(input, relative, 'baseline', row.path), body);
  }
  for (const row of declaration.files) {
    const body = read(path.join(supplied, row.path));
    assert.equal(body.length, row.bytes, row.path);
    assert.equal(sha(body), row.sha256, row.path);
    write(path.join(input, relative, row.path), body);
  }
  const neighbors = ['baseline-neighbor/fixture.js', 'baseline-manifest-extra.json'];
  for (const name of neighbors) write(path.join(input, relative, name), 'ordinary\r\nsource\r\n');

  const version = read(path.join(root, 'VERSION')).toString('utf8').trim();
  const result = sdk.pack(input, packed, version);
  assert.equal(result.status, 'packed');
  assert.equal(result.releaseReady, false);
  const packaged = sdk.readPackage(packed);
  const rows = new Map(packaged.files.map(row => [row.path, row]));
  for (const row of members) {
    const name = relative + '/baseline/' + row.path;
    assert.equal(rows.get(name).sourceNormalization, 'none', name);
    assert.equal(rows.get(name).sha256, row.sha256, name);
    assert.deepEqual(read(path.join(packed, 'payload', name)), read(path.join(supplied, 'baseline', row.path)), name);
  }
  const manifestName = relative + '/baseline-manifest.json';
  assert.equal(rows.get(manifestName).sourceNormalization, 'none');
  assert.deepEqual(read(path.join(packed, 'payload', manifestName)), manifestBody);
  assert.equal(rows.get(relative + '/source.json').sourceNormalization, 'lf');
  for (const name of ['sdk/fixture.txt', ...neighbors.map(name => relative + '/' + name)]) {
    assert.equal(rows.get(name).sourceNormalization, 'lf', name);
    assert.equal(read(path.join(packed, 'payload', name)).toString('utf8'), 'ordinary\nsource\n', name);
    assert.equal(read(path.join(input, name)).toString('utf8'), 'ordinary\r\nsource\r\n', name);
  }

  fs.mkdirSync(installed);
  assert.equal(sdk.install(packed, installed).status, 'installed');
  const lock = sdk.readLock(installed);
  assert.deepEqual(lock.manifest, packaged);
  assert.equal(lock.manifestSha256, sha(encoded(packaged)));
  const sourceRoot = path.join(installed, relative);
  for (const row of members) assert.deepEqual(read(path.join(sourceRoot, 'baseline', row.path)),
    read(path.join(supplied, 'baseline', row.path)), row.path);
  assert.deepEqual(read(path.join(sourceRoot, 'baseline-manifest.json')), manifestBody);
  assert.deepEqual(read(path.join(sourceRoot, 'source.json')), normalizedSourceBody);
  for (const row of declaration.files) assert.deepEqual(read(path.join(sourceRoot, row.path)), read(path.join(supplied, row.path)), row.path);

  // A pure fixture digest is only a requested identity, never a measured DLL pin.
  const requestedKernelSha256 = sha(Buffer.from('devtools-baseline-sdk-test: requested-only kernel fixture\n'));
  const options = {baselineRoot: path.join(sourceRoot, 'baseline'),
    baselineManifest: path.join(sourceRoot, 'baseline-manifest.json'), baselineManifestSha256,
    sourceRoot, sourceSha256: sha(read(path.join(sourceRoot, 'source.json'))), kernelSha256: requestedKernelSha256,
    output: path.join(temporary, 'composed')};
  const composition = frontend.compose(options);
  const composedManifest = read(path.join(composition.output, 'approved-manifest.json'));
  const composedRows = JSON.parse(composedManifest);
  assert.equal(composedRows.length, 650);
  assert.equal(composition.manifestSha256, sha(composedManifest));
  const replacements = new Map(declaration.files.map(row => [row.path.slice('overrides/'.length), row]));
  assert.deepEqual(composedRows, members.map(row => {
    const selected = replacements.get(row.path) || row;
    return {path: row.path, bytes: selected.bytes, sha256: selected.sha256};
  }));
  for (const row of members) assert.deepEqual(read(path.join(composition.payloadRoot, row.path)),
    read(path.join(supplied, replacements.has(row.path) ? 'overrides' : 'baseline', row.path)), row.path);
  for (const row of licenses) assert.equal(sha(read(path.join(composition.payloadRoot, row.path))), row.sha256, row.path);
  const receipt = JSON.parse(read(path.join(composition.output, 'composition-receipt.json')));
  assert.equal(receipt.requestedKernelSha256, requestedKernelSha256);
  assert.equal(receipt.kernelVerified, false);
  assert.equal(receipt.runtimeVerified, false);
  assert.equal(receipt.approvalCreated, false);
  assert.equal(receipt.nativePolicyChanged, false);
  assert.equal(fs.existsSync(path.join(composition.output, 'package-approval.json')), false);

  // One disposable installed-file rewrite must fail both independent readers.
  const changed = path.join(sourceRoot, 'baseline/front_end/Runtime.js');
  const original = read(changed);
  const rewritten = Buffer.from(original.toString('latin1').replace(/\r\n/g, '\n'), 'latin1');
  assert.notDeepEqual(rewritten, original);
  fs.writeFileSync(changed, rewritten);
  assert.throws(() => sdk.readLock(installed), /SDK file missing or modified/);
  const rejectedOutput = path.join(temporary, 'must-not-publish');
  assert.throws(() => frontend.compose({...options, output: rejectedOutput}), /E_DEVTOOLS_FRONTEND_FILE_DIGEST/);
  assert.equal(fs.existsSync(rejectedOutput), false);
  assert.deepEqual(read(path.join(supplied, 'baseline/front_end/Runtime.js')), original);
});
