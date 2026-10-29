'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {deploy} = require('./deploy-native-fixtures.cjs');
const source = path.resolve(__dirname, '../fixtures');

function temporary(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-workbench-native-fixtures-'));
  t.after(() => {
    const target = path.resolve(root);
    assert.equal(path.dirname(target), path.resolve(os.tmpdir()));
    assert.ok(path.basename(target).startsWith('soui-workbench-native-fixtures-'));
    fs.rmSync(target, {recursive: true, force: true});
  });
  return root;
}

function manifest(root, relative = '') {
  return fs.readdirSync(path.join(root, relative), {withFileTypes: true}).flatMap(entry => {
    const name = relative ? relative + '/' + entry.name : entry.name;
    return entry.isDirectory() ? manifest(root, name) : [{path: name, bytes: fs.readFileSync(path.join(root, name))}];
  }).sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
}

test('native Unicode fixtures retain their bytes outside the HTTP tree', t => {
  const root = temporary(t), result = deploy(source, root);
  assert.equal(result.files, 9); assert.equal(result.bytes, 36);
  for (const [name, files, bytes] of [['fileinsight-fixture', 5, 21], ['fileinsight-fixture-alt', 4, 15]]) {
    const native = manifest(path.join(root, 'fixture-data', name));
    assert.equal(native.length, files);
    assert.equal(native.reduce((sum, row) => sum + row.bytes.length, 0), bytes);
    assert.deepEqual(native, manifest(path.join(source, name)));
  }
  assert.equal(fs.existsSync(path.join(root, 'resources')), false);
  assert.equal(deploy(source, root).reused, true);
});

test('a different existing native payload is rejected without overwriting it', t => {
  const root = temporary(t);
  deploy(source, root);
  const changed = path.join(root, 'fixture-data/fileinsight-fixture/a.txt');
  fs.writeFileSync(changed, 'changed');
  assert.throws(() => deploy(source, root), /identity differs/);
  assert.equal(fs.readFileSync(changed, 'utf8'), 'changed');
});

test('an extra sample is rejected before destination publication', t => {
  const root = temporary(t), copy = path.join(root, 'source'), output = path.join(root, 'output');
  fs.mkdirSync(output); fs.mkdirSync(copy);
  for (const name of ['fileinsight-fixture', 'fileinsight-fixture-alt']) {
    fs.cpSync(path.join(source, name), path.join(copy, name), {recursive: true});
  }
  fs.writeFileSync(path.join(copy, 'fileinsight-fixture/unexpected.txt'), 'extra');
  assert.throws(() => deploy(copy, output), /Unexpected native fixture file/);
  assert.equal(fs.existsSync(path.join(output, 'fixture-data')), false);
});

test('a fixture-only source change updates the owned output without relinking', t => {
  const root = temporary(t), copy = path.join(root, 'source'), output = path.join(root, 'output');
  fs.mkdirSync(output); fs.mkdirSync(copy);
  for (const name of ['fileinsight-fixture', 'fileinsight-fixture-alt']) {
    fs.cpSync(path.join(source, name), path.join(copy, name), {recursive: true});
  }
  deploy(copy, output);
  fs.writeFileSync(path.join(copy, 'fileinsight-fixture/a.txt'), 'xyz');
  assert.equal(deploy(copy, output).reused, false);
  assert.equal(fs.readFileSync(path.join(output, 'fixture-data/fileinsight-fixture/a.txt'), 'utf8'), 'xyz');
  assert.equal(deploy(copy, output).reused, true);
  assert.deepEqual(fs.readdirSync(output).sort(), ['.soui-workbench-native-fixtures.json', 'fixture-data'].sort());
});

test('configured HTTP public assets preserve the media URL and exclude native data', () => {
  const web = path.resolve(__dirname, '../web');
  const config = fs.readFileSync(path.join(web, 'vite.config.ts'), 'utf8');
  const setting = config.match(/publicDir:\s*['"]([^'"]+)['"]/);
  assert.ok(setting, 'Explicit HTTP public directory is required');
  const assets = manifest(path.resolve(web, setting[1]));
  assert.ok(assets.some(row => row.path === 'media/aurora-poster.png'));
  for (const row of assets) {
    assert.match(row.path, /^[A-Za-z0-9_./-]+$/);
    assert.equal(row.path.startsWith('fixtures/'), false);
  }
  const poster = assets.find(row => row.path === 'media/aurora-poster.png').bytes;
  assert.deepEqual(poster.subarray(0, 8), Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
  assert.ok(fs.existsSync(path.join(web, 'public/fixtures/fileinsight-fixture/nested/报告.txt')));
  assert.ok(fs.existsSync(path.join(web, 'public/fixtures/fileinsight-fixture-alt/nested/配置.ini')));
});

test('application files: verifier binds the nine Unicode samples and ownership bytes', t => {
  const root = temporary(t), {verify} = require('./deploy-native-fixtures.cjs');
  deploy(source, root);
  const rows = verify(source, root);
  assert.equal(rows.length, 10);
  assert.equal(rows.filter(row => row.path.startsWith('fixture-data/')).length, 9);
  assert.ok(rows.some(row => row.path.endsWith('/报告.txt')));
  assert.ok(rows.some(row => row.path.endsWith('/配置.ini')));
  for (const row of rows) {
    const bytes = fs.readFileSync(path.join(root, row.path));
    assert.equal(bytes.length, row.bytes);
    assert.equal(require('node:crypto').createHash('sha256').update(bytes).digest('hex'), row.sha256);
  }
});

test('application files: verifier rejects altered owner, shared owner and extra native directories', t => {
  const root = temporary(t), {verify} = require('./deploy-native-fixtures.cjs');
  deploy(source, root);
  const owner = path.join(root, '.soui-workbench-native-fixtures.json'), saved = fs.readFileSync(owner);
  const invalid = JSON.parse(saved.toString('utf8')); invalid.schema = 'untrusted';
  fs.writeFileSync(owner, JSON.stringify(invalid));
  assert.throws(() => verify(source, root));
  fs.writeFileSync(owner, saved);
  const alias = path.join(root, 'owner-alias');
  fs.linkSync(owner, alias);
  assert.throws(() => verify(source, root), /ownership/);
  fs.unlinkSync(alias);
  fs.mkdirSync(path.join(root, 'fixture-data/fileinsight-fixture/extra'));
  assert.throws(() => verify(source, root), /Unexpected native fixture directory/);
});
