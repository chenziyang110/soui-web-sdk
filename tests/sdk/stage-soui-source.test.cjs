'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const identity = require('../../tools/internal/delivery-identity.cjs');

const script = path.resolve(__dirname, '../../tools/Stage-SouiSource.cjs');
const prefix = 'third_party/sources/soui/';
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-stage-source-'));
  t.after(() => {
    if (!root.startsWith(os.tmpdir() + path.sep)) throw new Error('Unexpected test directory');
    fs.rmSync(root, {recursive: true, force: true});
  });
  fs.mkdirSync(path.join(root, 'out'));
  return root;
}
function write(root, name, data) {
  const file = path.join(root, name);
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, data);
}
function run(root, name) {
  return cp.spawnSync(process.execPath, [script, root, path.join(root, 'out', name)], {encoding: 'utf8'});
}

test('Git-free staging copies only manifest-listed SOUI source and leaves generated files behind', t => {
  const root = fixture(t);
  const relative = prefix + 'SOUI/example.cpp';
  const content = Buffer.from('int example() { return 1; }\n');
  write(root, relative, content);
  write(root, prefix + 'obj/release/stale.obj', 'old output');
  write(root, 'source-manifest.json', JSON.stringify({schema: 'soui.full-source/v2', files: [
    {path: relative, bytes: content.length, sha256: identity.hash(content), normalization: 'none'},
  ]}));
  const result = run(root, 'source-one');
  assert.equal(result.status, 0, result.stderr);
  assert.equal(fs.readFileSync(path.join(root, 'out/source-one/SOUI/example.cpp'), 'utf8'), content.toString());
  assert.equal(fs.existsSync(path.join(root, 'out/source-one/obj/release/stale.obj')), false);
  assert.equal(fs.readFileSync(path.join(root, relative), 'utf8'), content.toString());
});

test('Git-free staging rejects source content that differs from the frozen manifest', t => {
  const root = fixture(t);
  const relative = prefix + 'SOUI/example.cpp';
  write(root, relative, 'changed');
  write(root, 'source-manifest.json', JSON.stringify({schema: 'soui.full-source/v2', files: [
    {path: relative, bytes: 8, sha256: identity.hash(Buffer.from('original')), normalization: 'none'},
  ]}));
  const result = run(root, 'source-two');
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /differs from full-source manifest/);
});

test('Git staging includes tracked and new source while excluding ignored build outputs', t => {
  const root = fixture(t);
  cp.execFileSync('git', ['init', '-q', root], {windowsHide: true});
  write(root, '.gitignore', 'third_party/sources/soui/obj/\n');
  write(root, prefix + 'SOUI/tracked.cpp', 'tracked');
  cp.execFileSync('git', ['-C', root, 'add', '.gitignore', prefix + 'SOUI/tracked.cpp'], {windowsHide: true});
  write(root, prefix + 'SOUI/new.cpp', 'new source');
  write(root, prefix + 'obj/release/stale.obj', 'old output');
  const result = run(root, 'source-three');
  assert.equal(result.status, 0, result.stderr);
  assert.equal(fs.readFileSync(path.join(root, 'out/source-three/SOUI/tracked.cpp'), 'utf8'), 'tracked');
  assert.equal(fs.readFileSync(path.join(root, 'out/source-three/SOUI/new.cpp'), 'utf8'), 'new source');
  assert.equal(fs.existsSync(path.join(root, 'out/source-three/obj/release/stale.obj')), false);
});
