'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { parseArgs, createProject } = require('../../tools/create-desktop-app.cjs');
function fixture(run) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'soui-create-')));
  try { run(root); } finally { fs.rmSync(root, { recursive: true, force: true }); }
}
const options = output => ({ output, name: 'HelloApp', identifier: 'org.example.hello', version: '0.1.0' });
test('creates a relocatable minimal project and refuses overwrite', () => fixture(root => {
  const output = path.join(root, 'new-app');
  const result = createProject(options(output));
  assert.ok(result.files >= 6);
  const cmake = fs.readFileSync(path.join(output, 'CMakeLists.txt'), 'utf8');
  assert.match(cmake, /find_package\(SouiDesktop/);
  assert.match(cmake, /soui_sync_application_assets\(TARGET HelloApp ASSETS/);
  assert.equal(cmake.includes('copy_directory'), false);
  assert.match(cmake, /HelloApp/);
  assert.equal(cmake.includes(root), false);
  assert.equal(cmake.includes('@APP_'), false);
  for (const name of ['desktop.mjs', 'desktop.d.mts'])
    assert.deepEqual(fs.readFileSync(path.join(output, 'web', 'soui', name)),
      fs.readFileSync(path.join(__dirname, '../../sdk/desktop/frontend', name)));
  assert.match(fs.readFileSync(path.join(output, 'web', 'app.mjs'), 'utf8'), /soui\/desktop\.mjs/);
  assert.throws(() => createProject(options(output)), /already exists/);
}));
test('rejects injection, reserved names and malformed metadata before writes', () => fixture(root => {
  const output = path.join(root, 'new-app');
  for (const change of [{ name: 'Bad;Code' }, { name: 'CON' }, { name: 'ALL_BUILD' }, { name: 'install' }, { name: 'web' }, { identifier: 'bad"id' }, { version: '1.0.0;code' }])
    assert.throws(() => createProject({ ...options(output), ...change }));
  assert.equal(fs.existsSync(output), false);
}));
test('rejects symlink parent without touching its destination', () => fixture(root => {
  const directory = path.join(root, 'actual'); fs.mkdirSync(directory);
  const link = path.join(root, 'link');
  fs.symlinkSync(directory, link, process.platform === 'win32' ? 'junction' : 'dir');
  assert.throws(() => createProject(options(path.join(link, 'new-app'))), /Symlink/);
  assert.deepEqual(fs.readdirSync(directory), []);
}));
test('CLI requires identity and rejects duplicate or unknown arguments', () => {
  const args = ['--output', 'new-app', '--name', 'HelloApp', '--identifier', 'org.example.hello'];
  assert.equal(parseArgs(args).version, '0.1.0');
  assert.throws(() => parseArgs([...args, '--name', 'Other']));
  assert.throws(() => parseArgs([...args, '--download', 'yes']));
  assert.throws(() => parseArgs(['--output', 'new-app', '--name', 'HelloApp']));
});
