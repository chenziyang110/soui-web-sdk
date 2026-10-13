'use strict';
// Real CMake generation/build selection, with LANGUAGES NONE and dummy imported
// archives. This exercises no compiler, linker, SDK ABI or native application.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const profile = require('../../tools/internal/llvm-profile-inputs.cjs');
const root = path.resolve(__dirname, '../..');
function fixture(t) {
  const directory = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'soui SDK configuration 中文 ')));
  t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
  const filename = process.env.SOUI_REFERENCE_TEST_PROFILE_REQUEST;
  assert.ok(filename && path.isAbsolute(filename), 'The LLVM test entry must supply its actual Build request');
  const value = JSON.parse(fs.readFileSync(filename, 'utf8'));
  const request = profile.readRequest(filename, value.architecture, root);
  const cache = profile.pin(path.join(root, 'out', request.value.architecture, 'CMakeCache.txt'));
  const cmake = profile.pin(profile.cacheValue(fs.readFileSync(cache.path, 'utf8'), 'CMAKE_COMMAND', 'INTERNAL'));
  const ninja = request.value.discovery.ToolPins.NINJA;
  profile.verifyPin(cmake); profile.verifyPin(ninja);
  t.after(() => { profile.verifyPin(cache); profile.verifyPin(cmake); profile.verifyPin(ninja); });
  const source = path.join(directory, 'source'); fs.mkdirSync(source);
  const release = path.join(source, 'Release SDK.lib'), debug = path.join(source, 'Debug SDK.lib');
  fs.writeFileSync(release, 'distinct Release configuration fixture');
  fs.writeFileSync(debug, 'distinct Debug configuration fixture');
  const module = path.join(root, 'cmake/SouiReferenceSdkConfiguration.cmake').replaceAll('\\', '/');
  fs.writeFileSync(path.join(source, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.29)
project(ReferenceSdkSelection LANGUAGES NONE)
add_library(Soui::Sdk STATIC IMPORTED)
set_target_properties(Soui::Sdk PROPERTIES IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
  IMPORTED_LOCATION_DEBUG "${debug.replaceAll('\\', '/')}"
  IMPORTED_LOCATION_RELEASE "${release.replaceAll('\\', '/')}")
if(TEST_WRONG_MAPPING)
  set_property(TARGET Soui::Sdk PROPERTY MAP_IMPORTED_CONFIG_RELEASE DEBUG)
endif()
if(TEST_MISSING_RELEASE)
  set_property(TARGET Soui::Sdk PROPERTY IMPORTED_LOCATION_RELEASE "${source.replaceAll('\\', '/')}/absent.lib")
endif()
include("${module}")
soui_reference_sdk_configuration(Soui::Sdk "\${VERIFIED_CONFIGURATION}")
add_custom_target(consumer_probe COMMAND "\${CMAKE_COMMAND}" -E touch "\${CMAKE_BINARY_DIR}/built-$<CONFIG>.txt" VERBATIM)
add_dependencies(consumer_probe soui_reference_sdk_configuration_check)
file(GENERATE OUTPUT "\${CMAKE_BINARY_DIR}/selected-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:Soui::Sdk>")
`);
  const run = args => {
    const result = cp.spawnSync(cmake.path, args, {encoding: 'utf8', timeout: 30000, maxBuffer: 1048576, windowsHide: true});
    assert.ifError(result.error); assert.equal(result.signal, null);
    return result;
  };
  const configure = (name, configuration, extra = []) => {
    const build = path.join(directory, name);
    const result = run(['-S', source, '-B', build, '-G', 'Ninja Multi-Config',
      '-DCMAKE_MAKE_PROGRAM:FILEPATH=' + ninja.path, '-DVERIFIED_CONFIGURATION=' + configuration, ...extra]);
    return {build, result};
  };
  return {run, configure, release, debug};
}
test('distinct imported Debug and Release archives cannot replace the verified consumer configuration', t => {
  const f = fixture(t);
  for (const selected of ['Release', 'Debug']) {
    const {build, result} = f.configure('verified-' + selected, selected);
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.equal(path.resolve(fs.readFileSync(path.join(build, 'selected-' + selected + '.txt'), 'utf8')),
      path.resolve(selected === 'Release' ? f.release : f.debug));
    const accepted = f.run(['--build', build, '--config', selected, '--target', 'consumer_probe']);
    assert.equal(accepted.status, 0, accepted.stdout + accepted.stderr);
    assert.ok(fs.existsSync(path.join(build, 'built-' + selected + '.txt')));
    const other = selected === 'Release' ? 'Debug' : 'Release';
    const rejected = f.run(['--build', build, '--config', other, '--target', 'consumer_probe']);
    assert.notEqual(rejected.status, 0);
    assert.match(rejected.stdout + rejected.stderr, /consumer configuration differs/);
    assert.equal(fs.existsSync(path.join(build, 'built-' + other + '.txt')), false);
  }
});
test('wrong imported mapping and absent selected archive fail during real configure', t => {
  const f = fixture(t);
  for (const [name, flag, reason] of [['mapping', '-DTEST_WRONG_MAPPING=ON', /mapping differs/],
    ['missing', '-DTEST_MISSING_RELEASE=ON', /no actual archive/]]) {
    const {result} = f.configure(name, 'Release', [flag]);
    assert.notEqual(result.status, 0);
    assert.match(result.stdout + result.stderr, reason);
  }
});
