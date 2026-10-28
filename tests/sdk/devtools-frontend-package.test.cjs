'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const frontend = require('../../tools/internal/devtools-frontend.cjs');
const identity = require('../../tools/internal/delivery-identity.cjs');

const sha = body => crypto.createHash('sha256').update(body).digest('hex');
const json = value => JSON.stringify(value, null, 2) + '\n';
const selectedNames = [
  'front_end/bindings/CompilerScriptMapping.js',
  'front_end/devtools.js',
  'front_end/host/InspectorFrontendHost.js',
  'front_end/sdk/DebuggerModel.js',
  'front_end/sdk/InspectorBackend.js',
  'front_end/sdk/SourceMap.js',
  'front_end/sources/SourcesPanel.js',
];

function fixture(t) {
  const owner = fs.realpathSync(os.tmpdir());
  const base = fs.realpathSync(fs.mkdtempSync(path.join(owner, '.soui-devtools-frontend-test-')));
  t.after(() => {
    assert.equal(path.dirname(base), owner);
    assert.ok(path.basename(base).startsWith('.soui-devtools-frontend-test-'));
    assert.equal(identity.safePath(base), base);
    assert.equal(fs.realpathSync(base), base);
    fs.rmSync(base, {recursive: true, force: true});
  });
  const baselineRoot = path.join(base, 'baseline');
  const sourceRoot = path.join(base, 'source');
  const baselineManifest = path.join(base, 'baseline-manifest.json');
  const output = path.join(base, 'composed');
  const write = (file, body) => {
    fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, body);
  };
  const member = name => {
    const body = fs.readFileSync(path.join(baselineRoot, name));
    return {path: name, bytes: body.length, sha256: sha(body)};
  };
  const names = [...selectedNames, 'front_end/inspector.html',
    'licenses/WebKit-LICENSE_FOR_ABOUT_CREDITS', 'licenses/miniblink-LICENSE'];
  for (let index = 0; index < 640; ++index)
    names.push('front_end/fixture/member-' + String(index).padStart(4, '0') + '.txt');
  names.sort();
  for (const name of names) write(path.join(baselineRoot, name), 'Fixture baseline: ' + name + '\n');
  const f = {base, baselineRoot, baselineManifest, sourceRoot, output, write, names,
    entries: names.map(member), declaration: undefined, manifestSha256: undefined, sourceSha256: undefined,
    saveManifest() {
      const body = json(f.entries); write(baselineManifest, body); f.manifestSha256 = sha(body);
    },
    saveSource() {
      const body = json(f.declaration); write(path.join(sourceRoot, 'source.json'), body); f.sourceSha256 = sha(body);
    },
    options(extra = {}) {
      return {baselineRoot, baselineManifest, baselineManifestSha256: f.manifestSha256,
        sourceRoot, sourceSha256: f.sourceSha256, kernelSha256: 'a'.repeat(64), output, ...extra};
    },
    compose(extra) { return frontend.compose(f.options(extra)); },
  };
  f.saveManifest();
  const byName = new Map(f.entries.map(row => [row.path, row]));
  f.declaration = {schema: 'soui.miniblink-inspector-overrides/v1',
    base: {manifestSha256: f.manifestSha256, members: 650},
    files: selectedNames.map(name => {
      const body = Buffer.from('Fixture canonical override: ' + name + '\n');
      write(path.join(sourceRoot, 'overrides', name), body);
      return {path: 'overrides/' + name, bytes: body.length, sha256: sha(body), baseSha256: byName.get(name).sha256};
    })};
  f.saveSource();
  write(path.join(sourceRoot, 'README.md'), 'Fixture documentation is outside the override roster.\n');
  return f;
}

function inventory(root, prefix = '') {
  return fs.readdirSync(root).sort().flatMap(name => {
    const file = path.join(root, name), relative = prefix ? prefix + '/' + name : name;
    const stat = fs.lstatSync(file); assert.equal(stat.isSymbolicLink(), false);
    return stat.isDirectory() ? inventory(file, relative) : [{path: relative,
      bytes: stat.size, sha256: sha(fs.readFileSync(file))}];
  });
}

function reject(f, extra, code) {
  assert.throws(() => f.compose(extra), error => {
    assert.match(error.message, code || /^E_DEVTOOLS_FRONTEND_[A-Z_]+$/); return true;
  });
  assert.equal(fs.existsSync(f.output), false, 'Rejected input cannot publish a composition');
}

test('composition preserves 650 exact members, overrides, notices, and deterministic identities', t => {
  const f = fixture(t), baselineBefore = inventory(f.baselineRoot), sourceBefore = inventory(f.sourceRoot);
  const first = f.compose();
  const manifest = JSON.parse(fs.readFileSync(path.join(f.output, 'approved-manifest.json')));
  const receipt = JSON.parse(fs.readFileSync(path.join(f.output, 'composition-receipt.json')));
  assert.equal(manifest.length, 650);
  assert.equal(first.manifestSha256, sha(fs.readFileSync(path.join(f.output, 'approved-manifest.json'))));
  assert.deepEqual(inventory(first.payloadRoot), manifest);
  for (const name of selectedNames) assert.deepEqual(fs.readFileSync(path.join(first.payloadRoot, name)),
    fs.readFileSync(path.join(f.sourceRoot, 'overrides', name)));
  for (const name of f.names.filter(name => !selectedNames.includes(name)))
    assert.deepEqual(fs.readFileSync(path.join(first.payloadRoot, name)), fs.readFileSync(path.join(f.baselineRoot, name)));
  assert.deepEqual(inventory(f.baselineRoot), baselineBefore);
  assert.deepEqual(inventory(f.sourceRoot), sourceBefore);
  assert.equal(receipt.requestedKernelSha256, 'a'.repeat(64));
  assert.equal(receipt.kernelVerified, false);
  assert.equal(receipt.approvalCreated, false);
  assert.equal(receipt.nativePolicyChanged, false);
  assert.equal(receipt.runtimeVerified, false);
  assert.equal(Object.hasOwn(receipt, 'legalReview'), false);
  assert.equal(Object.hasOwn(receipt, 'releaseReady'), false);
  assert.equal(fs.existsSync(path.join(f.output, 'package-approval.json')), false);
  assert.equal(inventory(f.output).length, 652);
  const second = f.compose({output: path.join(f.base, 'composed-again')});
  assert.deepEqual(inventory(second.output), inventory(first.output));
  assert.equal(second.receiptSha256, first.receiptSha256);
});

test('all three externally selected identities are explicit and never inferred', t => {
  const f = fixture(t);
  for (const field of ['baselineManifestSha256', 'sourceSha256', 'kernelSha256']) {
    reject(f, {[field]: undefined}, /CONFIGURATION/);
    reject(f, {[field]: 'not-a-pin'}, /CONFIGURATION/);
  }
  reject(f, {baselineManifestSha256: '0'.repeat(64)}, /FILE_DIGEST/);
  reject(f, {sourceSha256: '0'.repeat(64)}, /FILE_DIGEST/);
});

test('unlisted baseline or override members cannot enter the composition', t => {
  const f = fixture(t);
  f.write(path.join(f.baselineRoot, 'unlisted.txt'), 'Unlisted fixture.\n');
  reject(f, undefined, /CLOSURE/);
  fs.unlinkSync(path.join(f.baselineRoot, 'unlisted.txt'));
  f.write(path.join(f.sourceRoot, 'overrides/front_end/extra.js'), 'Unlisted override.\n');
  reject(f, undefined, /CLOSURE/);
});

test('modified baseline and canonical source bytes fail against their declared pins', t => {
  const f = fixture(t), baselineFile = path.join(f.baselineRoot, 'front_end/devtools.js');
  const original = fs.readFileSync(baselineFile);
  f.write(baselineFile, 'Unexpected baseline bytes.\n'); reject(f, undefined, /FILE_DIGEST/);
  f.write(baselineFile, original);
  f.write(path.join(f.sourceRoot, 'overrides/front_end/devtools.js'), 'Unexpected override bytes.\n');
  reject(f, undefined, /FILE_DIGEST/);
});

test('an override cannot apply to another baseline or replace another member', t => {
  const f = fixture(t);
  const previous = f.declaration.files[0].baseSha256;
  f.declaration.files[0].baseSha256 = '0'.repeat(64); f.saveSource();
  reject(f, undefined, /OVERRIDE/);
  f.declaration.files[0].baseSha256 = previous;
  f.declaration.files[0].path = 'overrides/front_end/inspector.html'; f.saveSource();
  reject(f, undefined, /OVERRIDE/);
});

test('the baseline must have the exact fixed count and safe distinct member paths', t => {
  const f = fixture(t), original = f.entries;
  const reset = entries => {
    f.entries = entries; f.saveManifest(); f.declaration.base.manifestSha256 = f.manifestSha256; f.saveSource();
  };
  reset(original.slice(1)); reject(f, undefined, /MEMBER_COUNT/);
  reset(original.map((row, index) => index === 0 ? {...row, path: '../outside.txt'} : row));
  reject(f, undefined, /PATH/);
  reset(original.map((row, index) => index === 1 ? {...row, path: original[0].path} : row));
  reject(f, undefined, /CASE_ALIAS/);
});

test('duplicate JSON keys and oversized source declarations are rejected before output', t => {
  const f = fixture(t), sourceFile = path.join(f.sourceRoot, 'source.json');
  const duplicate = json(f.declaration).replace('"schema":', '"schema":"fixture-duplicate","schema":');
  f.write(sourceFile, duplicate); f.sourceSha256 = sha(duplicate);
  reject(f, undefined, /JSON_DUPLICATE/);
  const oversized = Buffer.alloc(512 * 1024 + 1, 32); f.write(sourceFile, oversized); f.sourceSha256 = sha(oversized);
  reject(f, undefined, /FILE_BUDGET/);
});

test('file and accumulated byte budgets bound the composition inputs', t => {
  const f = fixture(t);
  const original = f.entries;
  f.entries = original.map((row, index) => index === 0 ? {...row, bytes: 1024 * 1024 + 1} : row);
  f.saveManifest(); f.declaration.base.manifestSha256 = f.manifestSha256; f.saveSource();
  reject(f, undefined, /MANIFEST_ROW/);
  f.entries = original.map((row, index) => index < 17 ? {...row, bytes: 1024 * 1024} : row);
  f.saveManifest(); f.declaration.base.manifestSha256 = f.manifestSha256; f.saveSource();
  reject(f, undefined, /TOTAL_BUDGET/);
});

test('existing output and overlapping input paths are preserved and refused', t => {
  const f = fixture(t), existing = path.join(f.base, 'existing');
  f.write(path.join(existing, 'owner.txt'), 'Existing owner state.\n');
  assert.throws(() => f.compose({output: existing}), /OUTPUT_EXISTS/);
  assert.equal(fs.readFileSync(path.join(existing, 'owner.txt'), 'utf8'), 'Existing owner state.\n');
  reject(f, {output: path.join(f.baselineRoot, 'new-output')}, /OVERLAP/);
  reject(f, {output: path.join(f.sourceRoot, 'new-output')}, /OVERLAP/);
});

test('hard links cannot supply canonical member identity', t => {
  const f = fixture(t), name = 'front_end/devtools.js', original = path.join(f.baselineRoot, name);
  const linked = path.join(f.base, 'linked-original.txt'); fs.linkSync(original, linked);
  reject(f, undefined, /FILE_IDENTITY/);
});

test('linked ancestors cannot redirect a baseline outside its selected root', t => {
  const f = fixture(t), link = path.join(f.base, 'linked-baseline');
  try { fs.symlinkSync(f.baselineRoot, link, process.platform === 'win32' ? 'junction' : 'dir'); }
  catch (error) {
    if (['EPERM', 'EACCES', 'ENOSYS'].includes(error.code)) { t.skip('Creating a directory link is unavailable.'); return; }
    throw error;
  }
  reject(f, {baselineRoot: link});
});

test('the formal CLI returns metadata on success and only stable errors on rejected input', t => {
  const f = fixture(t), tool = path.resolve(__dirname, '../../tools/internal/devtools-frontend.cjs');
  const args = Object.entries(f.options()).flatMap(([key, value]) => ['--' + key.replace(/[A-Z]/g,
    letter => '-' + letter.toLowerCase()), value]);
  const success = spawnSync(process.execPath, [tool, 'compose', ...args], {encoding: 'utf8'});
  assert.equal(success.status, 0, success.stderr);
  assert.equal(JSON.parse(success.stdout).members, 650);
  const rejected = spawnSync(process.execPath, [tool, 'compose', '--output', 'fixture-private-path'], {encoding: 'utf8'});
  assert.equal(rejected.status, 1);
  assert.equal(rejected.stdout, '');
  assert.equal(rejected.stderr.trim(), 'E_DEVTOOLS_FRONTEND_ARGUMENT');
});
