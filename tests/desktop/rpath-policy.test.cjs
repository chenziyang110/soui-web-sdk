'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

// A real tiny ELF consumer, without GTK/WebKit or running the resulting binaries.
test('Linux staging opt-in preserves explicit deployment RPATH and link dependencies', t => {
  if (process.platform !== 'linux') return t.skip('ELF policy check requires Linux');
  const cmake = process.env.CMAKE || 'cmake';
  const ninja = process.env.NINJA || 'ninja';
  for (const tool of [cmake, ninja, 'readelf']) {
    if (spawnSync(tool, ['--version']).status !== 0) return t.skip(`${tool} unavailable`);
  }
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-rpath-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const root = path.resolve(__dirname, '../..').replaceAll('\\', '/');
  fs.writeFileSync(path.join(dir, 'main.cpp'), 'int probe(); int main() { return probe(); }\n');
  fs.writeFileSync(path.join(dir, 'probe.cpp'), 'int probe() { return 0; }\n');
  fs.writeFileSync(path.join(dir, 'CMakeLists.txt'), `
cmake_minimum_required(VERSION 3.24)
project(RpathPolicy LANGUAGES CXX)
include("${root}/sdk/desktop/SouiDesktopApplication.cmake")
add_library(Soui::Desktop INTERFACE IMPORTED)
set_target_properties(Soui::Desktop PROPERTIES SOUI_APPLICATION_PLATFORM linux SOUI_APPLICATION_ARCH x64)
add_library(probe SHARED probe.cpp)
set_target_properties(probe PROPERTIES LIBRARY_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/development-only")
foreach(name baseline deployed explicit)
  add_executable(\${name} main.cpp)
  target_link_libraries(\${name} PRIVATE probe)
endforeach()
set_target_properties(explicit PROPERTIES INSTALL_RPATH "$ORIGIN/runtime" INSTALL_RPATH_USE_LINK_PATH TRUE)
soui_stage_application(TARGET baseline NAME baseline ASSETS "\${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT baseline-stage VERSION 0.1.0)
soui_stage_application(TARGET deployed NAME deployed ASSETS "\${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT deployed-stage VERSION 0.1.0 USE_INSTALL_RPATH)
soui_stage_application(TARGET explicit NAME explicit ASSETS "\${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT explicit-stage VERSION 0.1.0 USE_INSTALL_RPATH)
`);
  const run = (tool, args) => {
    const result = spawnSync(tool, args, { encoding: 'utf8', timeout: 60000 });
    assert.equal(result.status, 0, `${tool}: ${result.error || ''}\n${result.stdout}\n${result.stderr}`);
    return result.stdout;
  };
  const build = path.join(dir, 'build');
  run(cmake, ['-S', dir, '-B', build, '-G', 'Ninja', `-DCMAKE_MAKE_PROGRAM=${ninja}`]);
  run(cmake, ['--build', build, '--parallel', '2']);
  const entries = name => run('readelf', ['-d', path.join(build, name)]);
  const baseline = entries('baseline'), deployed = entries('deployed'), explicit = entries('explicit');
  assert.match(baseline, /\((?:RUNPATH|RPATH)\).*development-only/);
  assert.doesNotMatch(deployed, /\((?:RUNPATH|RPATH)\)/);
  assert.match(explicit, /\((?:RUNPATH|RPATH)\).*\[\$ORIGIN\/runtime\]/);
  assert.doesNotMatch(explicit, /development-only/);
  const dependencies = value => value.split('\n').filter(line => line.includes('(NEEDED)'));
  assert.deepEqual(dependencies(deployed), dependencies(baseline));
  assert.deepEqual(dependencies(explicit), dependencies(baseline));
});
