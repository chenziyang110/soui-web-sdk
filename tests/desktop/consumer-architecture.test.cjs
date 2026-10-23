'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

test('installed-layout staging selects application slices, not universal SDK identity', t => {
  const cmake = process.env.CMAKE || 'cmake', ninja = process.env.NINJA || 'ninja';
  for (const tool of [cmake, ninja]) if (spawnSync(tool, ['--version']).status !== 0) return t.skip(`${tool} unavailable`);
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-consumer-arch-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const root = path.resolve(__dirname, '../..');
  const moduleDir = path.join(dir, 'sdk/lib/cmake/SouiDesktop');
  const toolsDir = path.join(dir, 'sdk/share/SouiDesktop/tools');
  fs.mkdirSync(moduleDir, { recursive: true }); fs.mkdirSync(toolsDir, { recursive: true });
  fs.copyFileSync(path.join(root, 'sdk/desktop/SouiDesktopApplication.cmake'), path.join(moduleDir, 'SouiDesktopApplication.cmake'));
  for (const name of ['stage-desktop-app.cjs', 'verify-desktop-app.cjs', 'archive-desktop-app.cjs'])
    fs.copyFileSync(path.join(root, 'tools', name), path.join(toolsDir, name));
  fs.writeFileSync(path.join(dir, 'main.cpp'), 'int main() { return 0; }\n');
  // Configuration metadata fixture only, not an Apple SDK or native compilation.
  fs.writeFileSync(path.join(dir, 'CMakeLists.txt'), `
cmake_minimum_required(VERSION 3.24)
project(ConsumerArchitecture LANGUAGES CXX)
include("${moduleDir.replaceAll('\\', '/')}/SouiDesktopApplication.cmake")
add_library(Soui::Desktop INTERFACE IMPORTED)
set_target_properties(Soui::Desktop PROPERTIES SOUI_APPLICATION_PLATFORM macos SOUI_APPLICATION_ARCH universal2)
function(package_scope)
  set(SOUI_TARGET_ARCH x64)
  set_property(TARGET Soui::Desktop PROPERTY SOUI_APPLICATION_CONSUMER_ARCH "\${SOUI_TARGET_ARCH}")
endfunction()
package_scope()
if(FORCE_UNIVERSAL)
  set_property(TARGET Soui::Desktop PROPERTY SOUI_APPLICATION_CONSUMER_ARCH universal2)
endif()
foreach(name default_app thin_arm universal_app)
  add_executable(\${name} main.cpp)
endforeach()
set_property(TARGET thin_arm PROPERTY OSX_ARCHITECTURES arm64)
set_property(TARGET universal_app PROPERTY OSX_ARCHITECTURES "arm64;x86_64")
if(DEFINED BAD_ARCH)
  set_property(TARGET default_app PROPERTY OSX_ARCHITECTURES "\${BAD_ARCH}")
endif()
if(CLEAR_ARCH)
  set_property(TARGET default_app PROPERTY OSX_ARCHITECTURES)
endif()
if(BAD_CONFIG)
  set_property(TARGET default_app PROPERTY OSX_ARCHITECTURES_RELEASE arm64)
endif()
if(EMPTY_CONFIG)
  set_property(TARGET universal_app PROPERTY OSX_ARCHITECTURES_RELEASE "")
endif()
if(THIN_SDK)
  set_property(TARGET Soui::Desktop PROPERTY SOUI_APPLICATION_ARCH x64)
endif()
foreach(name default_app thin_arm universal_app)
  soui_stage_application(TARGET \${name} NAME \${name} ASSETS "\${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT "\${CMAKE_BINARY_DIR}/stage-\${name}" VERSION 0.1.0 IDENTIFIER org.example.app)
endforeach()
`);
  const configure = (name, args = [], generator = 'Ninja') => spawnSync(cmake, ['-S', dir, '-B', path.join(dir, name), '-G', generator,
    `-DCMAKE_MAKE_PROGRAM=${ninja}`, '-DCMAKE_BUILD_TYPE=Release', ...args], { encoding: 'utf8', timeout: 60000 });
  const good = configure('good'); assert.equal(good.status, 0, good.stdout + good.stderr);
  const build = fs.readFileSync(path.join(dir, 'good/build.ninja'), 'utf8');
  for (const [name, arch] of [['default_app', 'x64'], ['thin_arm', 'arm64'], ['universal_app', 'universal2']])
    assert.match(build, new RegExp(`--name ${name} --platform macos --arch ${arch} `));
  for (const [name, args, diagnostic] of [
    ['unsupported', ['-DBAD_ARCH=OFF'], /only x86_64 and arm64/],
    ['config', ['-DBAD_CONFIG=ON'], /Per-configuration/],
    ['empty-config', ['-DEMPTY_CONFIG=ON'], /Per-configuration/],
    ['empty', ['-DBAD_ARCH=', '-DFORCE_UNIVERSAL=ON'], /empty target/],
    ['cleared', ['-DCLEAR_ARCH=ON', '-DFORCE_UNIVERSAL=ON'], /unset OSX_ARCHITECTURES/],
    ['subset', ['-DTHIN_SDK=ON'], /does not contain/],
  ]) {
    const result = configure(name, args); assert.notEqual(result.status, 0);
    assert.match(result.stdout + result.stderr, diagnostic);
  }
  const multi = configure('empty-multi-config', ['-DEMPTY_CONFIG=ON'], 'Ninja Multi-Config');
  assert.notEqual(multi.status, 0);
  assert.match(multi.stdout + multi.stderr, /Per-configuration/);
});
