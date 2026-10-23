'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

test('cross-config staging builds and copies the output configuration executable', t => {
  if (process.platform !== 'linux' || process.arch !== 'x64') return t.skip('tiny ELF fixture requires Linux x64');
  const cmake = process.env.CMAKE || 'cmake', ninja = process.env.NINJA || 'ninja';
  for (const tool of [cmake, ninja]) if (spawnSync(tool, ['--version']).status !== 0) return t.skip(`${tool} unavailable`);
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-cross-config-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const root = path.resolve(__dirname, '../..').replaceAll('\\', '/');
  fs.mkdirSync(path.join(dir, 'web'));
  fs.writeFileSync(path.join(dir, 'web/index.html'), 'asset fixture\n');
  fs.writeFileSync(path.join(dir, 'main.cpp'), 'int main() { return 0; }\n');
  fs.writeFileSync(path.join(dir, 'CMakeLists.txt'), `
cmake_minimum_required(VERSION 3.24)
project(CrossConfig LANGUAGES CXX)
include("${root}/sdk/desktop/SouiDesktopApplication.cmake")
add_library(Soui::Desktop INTERFACE IMPORTED)
set_target_properties(Soui::Desktop PROPERTIES SOUI_APPLICATION_PLATFORM linux SOUI_APPLICATION_ARCH x64)
add_executable(app main.cpp)
soui_sync_application_assets(TARGET app ASSETS "\${CMAKE_SOURCE_DIR}/web")
soui_stage_application(TARGET app NAME app ASSETS "\${CMAKE_SOURCE_DIR}/web"
  OUTPUT "\${CMAKE_BINARY_DIR}/staged-$<CONFIG>" VERSION 1.0.0 USE_INSTALL_RPATH)
`);
  const run = args => {
    const result = spawnSync(cmake, args, { encoding: 'utf8', timeout: 60000 });
    assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}\n${result.stderr}`);
  };
  const build = path.join(dir, 'build');
  run(['-S', dir, '-B', build, '-G', 'Ninja Multi-Config', `-DCMAKE_MAKE_PROGRAM=${ninja}`, '-DCMAKE_CROSS_CONFIGS=all']);
  run(['--build', build, '--config', 'Debug', '--target', 'app_stage:Release']);
  assert.deepEqual(fs.readFileSync(path.join(build, 'staged-Release/app')), fs.readFileSync(path.join(build, 'Release/app')));
  assert.deepEqual(fs.readFileSync(path.join(build, 'Release/web/index.html')), fs.readFileSync(path.join(dir, 'web/index.html')));
  assert.equal(fs.existsSync(path.join(build, 'staged-Debug')), false);
  // Archive is deliberately independent and must select the same handoff directory.
  run(['--build', build, '--config', 'Debug', '--target', 'app_archive_stage:Release']);
  assert.ok(fs.existsSync(path.join(build, 'staged-Release-delivery/app-1.0.0-linux-x64.zip')));
  assert.equal(fs.existsSync(path.join(build, 'staged-Debug-delivery')), false);
});
