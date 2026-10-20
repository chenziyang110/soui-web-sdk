'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { syncAssets } = require('../../tools/sync-desktop-assets.cjs');

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-sync-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const input = path.join(root, 'input'), output = path.join(root, 'web');
  fs.mkdirSync(input);
  fs.writeFileSync(path.join(input, 'index.html'), '<p>one</p>');
  return { root, input, output };
}
test('sync propagates deletion and empty directories; unchanged input is a no-op', t => {
  const { input, output } = fixture(t);
  fs.mkdirSync(path.join(input, 'empty'));
  fs.writeFileSync(path.join(input, 'obsolete.js'), 'old');
  assert.equal(syncAssets(input, output).changed, true);
  assert.equal(syncAssets(input, output).changed, false);
  fs.unlinkSync(path.join(input, 'obsolete.js'));
  fs.rmdirSync(path.join(input, 'empty'));
  fs.writeFileSync(path.join(input, 'index.html'), '<p>two</p>');
  assert.equal(syncAssets(input, output).changed, true);
  assert.deepEqual(fs.readdirSync(output), ['index.html']);
  assert.equal(fs.readFileSync(path.join(output, 'index.html'), 'utf8'), '<p>two</p>');
});
test('existing directories are never adopted', t => {
  const { input, output } = fixture(t);
  fs.mkdirSync(output);
  assert.throws(() => syncAssets(input, output), /Refusing to adopt/);
  assert.deepEqual(fs.readdirSync(output), []);
});
test('manual edits, extra files and extra directories prevent all deletion', t => {
  const { input, output } = fixture(t);
  syncAssets(input, output);
  const index = path.join(output, 'index.html');
  fs.writeFileSync(index, 'manual');
  assert.throws(() => syncAssets(input, output), /edits or extra/);
  assert.equal(fs.readFileSync(index, 'utf8'), 'manual');
  fs.copyFileSync(path.join(input, 'index.html'), index);
  fs.writeFileSync(path.join(output, 'userdata'), 'keep');
  assert.throws(() => syncAssets(input, output), /edits or extra/);
  fs.unlinkSync(path.join(output, 'userdata'));
  fs.mkdirSync(path.join(output, 'userdata'));
  assert.throws(() => syncAssets(input, output), /edits or extra/);
  assert.ok(fs.statSync(path.join(output, 'userdata')).isDirectory());
});
test('source identity, output overlap and concurrent lock are rejected', t => {
  const { root, input, output } = fixture(t);
  syncAssets(input, output);
  const other = path.join(root, 'other');
  fs.mkdirSync(other);
  fs.writeFileSync(path.join(other, 'index.html'), 'other');
  assert.throws(() => syncAssets(other, output), /source identity/);
  assert.throws(() => syncAssets(input, path.join(input, 'web')), /overlap/);
  fs.writeFileSync(`${output}.soui-assets.lock`, 'other invocation');
  assert.throws(() => syncAssets(input, output), /EEXIST/);
  assert.equal(fs.readFileSync(`${output}.soui-assets.lock`, 'utf8'), 'other invocation');
});
test('links in source, output and ancestors are rejected without touching targets', t => {
  const { root, input, output } = fixture(t);
  const link = path.join(input, 'link');
  try { fs.symlinkSync('index.html', link); }
  catch (error) { if (error.code === 'EPERM') return t.skip('Symlink creation unavailable'); throw error; }
  assert.throws(() => syncAssets(input, output), /links/);
  assert.equal(fs.existsSync(output), false);
  fs.unlinkSync(link);
  syncAssets(input, output);
  fs.symlinkSync(path.join(input, 'index.html'), path.join(output, 'extra'));
  assert.throws(() => syncAssets(input, output), /links/);
  const parentLink = path.join(root, 'alias');
  fs.symlinkSync(input, parentLink, 'dir');
  assert.throws(() => syncAssets(parentLink, path.join(root, 'new-web')), /ancestors/);
  assert.equal(fs.readFileSync(path.join(input, 'index.html'), 'utf8'), '<p>one</p>');
});
