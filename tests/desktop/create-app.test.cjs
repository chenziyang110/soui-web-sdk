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
  for (const name of ['startup_report.h', 'startup_dialog.h', 'startup_dialog.cpp', 'startup_dialog_macos.mm', 'startup_trace.h'])
    assert.ok(fs.statSync(path.join(output, name)).isFile());
  assert.match(fs.readFileSync(path.join(output, 'main.cpp'), 'utf8'), /--diagnose/);
  assert.match(cmake, /startup_dialog_macos\.mm/);
  assert.match(cmake, /PkgConfig::SOUI_APP_GTK/);
  assert.equal(cmake.includes(root), false);
  assert.equal(cmake.includes('@APP_'), false);
  assert.match(cmake, /option\(SOUI_APP_STARTUP_TRACE [^\n]+ OFF\)/);
  assert.match(cmake, /SOUI_APP_STARTUP_TRACE_FILE "" CACHE STRING/);
  assert.match(cmake, /target_compile_definitions\(HelloApp PRIVATE SOUI_STARTUP_TRACE_ENABLED=1\)/);
  assert.match(cmake, /#define SOUI_STARTUP_TRACE_FILE "@SOUI_STARTUP_TRACE_FILE_ESCAPED@"/);
  assert.equal(fs.existsSync(path.join(output, 'startup_trace_config.h')), false);
  assert.deepEqual(fs.readFileSync(path.join(output, 'startup_trace.h')),
    fs.readFileSync(path.join(__dirname, '../../sdk/src/desktop/startup_trace.h')));
  for (const name of ['desktop.mjs', 'desktop.d.mts'])
    assert.deepEqual(fs.readFileSync(path.join(output, 'web', 'soui', name)),
      fs.readFileSync(path.join(__dirname, '../../sdk/desktop/frontend', name)));
  assert.match(fs.readFileSync(path.join(output, 'web', 'app.mjs'), 'utf8'), /soui\/desktop\.mjs/);
  assert.throws(() => createProject(options(output)), /already exists/);
}));
test('rejects build-file collisions before creating a project', () => fixture(root => {
  const output = path.join(root, 'new-app');
  for (const name of ['CMakeFiles', 'cmakefiles', 'CMAKEFILES', 'Makefile', 'makefile', 'MAKEFILE']) {
    assert.throws(() => createProject({ ...options(output), name }), /reserved/);
    assert.equal(fs.existsSync(output), false);
  }
  // These collide too, but are already excluded by the ASCII identifier grammar.
  for (const name of ['CMakeCache.txt', 'cmake_install.cmake', 'build.ninja']) {
    assert.throws(() => createProject({ ...options(output), name }), /Name must/);
    assert.equal(fs.existsSync(output), false);
  }
  assert.equal(createProject({ ...options(output), name: 'MakefileApp' }).name, 'MakefileApp');
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
test('installed generator rejects source overlap and incomplete frontend before creating output', () => fixture(root => {
  const share = path.join(root, 'share', 'SouiDesktop');
  fs.mkdirSync(path.join(share, 'tools'), { recursive: true });
  fs.copyFileSync(path.join(__dirname, '../../tools/create-desktop-app.cjs'),
    path.join(share, 'tools/create-desktop-app.cjs'));
  fs.cpSync(path.join(__dirname, '../../templates/unified-desktop'),
    path.join(share, 'templates/unified-desktop'), { recursive: true });
  fs.cpSync(path.join(__dirname, '../../sdk/desktop/frontend'),
    path.join(share, 'frontend'), { recursive: true });
  const installed = require(path.join(share, 'tools/create-desktop-app.cjs'));
  const output = path.join(root, 'new-app');
  for (const source of ['frontend', 'templates/unified-desktop']) {
    const unsafeOutput = path.join(share, source, 'new-app');
    assert.throws(() => installed.createProject(options(unsafeOutput)), /trees must not overlap/);
    assert.equal(fs.existsSync(unsafeOutput), false);
  }
  assert.deepEqual(fs.readdirSync(path.join(share, 'frontend')).sort(), ['desktop.d.mts', 'desktop.mjs']);
  for (const name of ['app.mjs', 'feature.mjs', 'details.mjs', 'config.json', 'style.css']) {
    const file = path.join(share, 'templates/unified-desktop/web', name);
    const saved = fs.readFileSync(file);
    fs.unlinkSync(file);
    assert.throws(() => installed.createProject(options(output)), /Incomplete project template/);
    assert.equal(fs.existsSync(output), false);
    fs.writeFileSync(file, saved);
  }
  for (const name of ['startup_report.h', 'startup_dialog.h', 'startup_dialog.cpp', 'startup_dialog_macos.mm', 'startup_trace.h']) {
    const file = path.join(share, 'templates/unified-desktop', name);
    const saved = fs.readFileSync(file);
    fs.unlinkSync(file);
    assert.throws(() => installed.createProject(options(output)), /Incomplete project template/);
    assert.equal(fs.existsSync(output), false);
    fs.writeFileSync(file, saved);
  }
  installed.createProject(options(output));
  assert.deepEqual(fs.readFileSync(path.join(output, 'web/soui/desktop.mjs')),
    fs.readFileSync(path.join(share, 'frontend/desktop.mjs')));
}));
test('CLI requires identity and rejects duplicate or unknown arguments', () => {
  const args = ['--output', 'new-app', '--name', 'HelloApp', '--identifier', 'org.example.hello'];
  assert.equal(parseArgs(args).version, '0.1.0');
  assert.throws(() => parseArgs([...args, '--name', 'Other']));
  assert.throws(() => parseArgs([...args, '--download', 'yes']));
  assert.throws(() => parseArgs(['--output', 'new-app', '--name', 'HelloApp']));
});
