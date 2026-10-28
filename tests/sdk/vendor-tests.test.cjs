'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const {configuration, registry, inspect, requiredSourcePaths} = require('../../tools/internal/vendor-tests.cjs');
const manifest = require('../../tools/internal/vendor-test-names.json');
const repository = path.resolve(__dirname, '../..');

function fixture(t, mode = 'ON') {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'soui-vendor-registry-')));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  const build = path.join(root, 'out/x64');
  fs.mkdirSync(build, {recursive: true});
  const entries = {
    'CMAKE_HOME_DIRECTORY:INTERNAL': root,
    'CMAKE_CACHEFILE_DIR:INTERNAL': build,
    'CMAKE_GENERATOR_PLATFORM:INTERNAL': 'x64,version=10.0.19041.0',
    'CMAKE_CONFIGURATION_TYPES:STRING': 'Debug;Release',
    'SOUI_BUILD_TESTS:BOOL': 'ON',
    'SOUI_BUILD_VENDOR_TESTS:BOOL': mode,
  };
  const cache = path.join(build, 'CMakeCache.txt');
  const write = () => fs.writeFileSync(cache, Object.entries(entries).map(([key, value]) => `${key}=${value}`).join('\r\n') + '\r\n');
  write();
  return {root, build, cache, entries, write};
}
function jsonRegistry(names) {
  return JSON.stringify({kind: 'ctestInfo', version: {major: 1, minor: 0},
    tests: names.map(name => ({name, command: ['not-executed.exe'], properties: []}))});
}
const sdkName = 'sdk.native.contract';
const allNames = [sdkName, ...manifest.names, ...manifest.auxiliaryNames];

test('a missing cache cannot authorize unit or vendor acceptance', t => {
  const f = fixture(t);
  fs.unlinkSync(f.cache);
  assert.throws(() => configuration(f.root, 'x64', 'Release'), /ENOENT/);
});
test('portable OFF is explicit and cannot satisfy the vendor requirement', t => {
  const f = fixture(t, 'OFF');
  assert.equal(configuration(f.root, 'x64', 'Release').mode, 'OFF');
  assert.throws(() => configuration(f.root, 'x64', 'Release', true), /Required vendor regressions are OFF/);
});
test('a relocated or cross-architecture cache cannot be reused', t => {
  const f = fixture(t), other = path.join(f.root, 'other');
  fs.mkdirSync(other);
  f.entries['CMAKE_HOME_DIRECTORY:INTERNAL'] = other; f.write();
  assert.throws(() => configuration(f.root, 'x64', 'Release'), /another source root/);
  f.entries['CMAKE_HOME_DIRECTORY:INTERNAL'] = f.root;
  f.entries['CMAKE_CACHEFILE_DIR:INTERNAL'] = other; f.write();
  assert.throws(() => configuration(f.root, 'x64', 'Release'), /another build directory/);
  f.entries['CMAKE_CACHEFILE_DIR:INTERNAL'] = f.build;
  f.entries['CMAKE_GENERATOR_PLATFORM:INTERNAL'] = 'Win32'; f.write();
  assert.throws(() => configuration(f.root, 'x64', 'Release'), /architecture differs/);
});
test('disabled SDK tests, ambiguous booleans and absent configurations fail closed', t => {
  const f = fixture(t);
  f.entries['SOUI_BUILD_TESTS:BOOL'] = 'OFF'; f.write();
  assert.throws(() => configuration(f.root, 'x64', 'Release'), /SDK native tests/);
  f.entries['SOUI_BUILD_TESTS:BOOL'] = 'ON';
  f.entries['SOUI_BUILD_VENDOR_TESTS:BOOL'] = 'TRUE'; f.write();
  assert.throws(() => configuration(f.root, 'x64', 'Release'), /exactly ON or OFF/);
  f.entries['SOUI_BUILD_VENDOR_TESTS:BOOL'] = 'ON';
  f.entries['CMAKE_CONFIGURATION_TYPES:STRING'] = 'Debug'; f.write();
  assert.throws(() => configuration(f.root, 'x64', 'Release'), /configuration is not generated/);
  f.entries['CMAKE_CONFIGURATION_TYPES:STRING'] = 'Debug;Release'; f.write();
  fs.appendFileSync(f.cache, 'SOUI_BUILD_VENDOR_TESTS:BOOL=OFF\r\n');
  assert.throws(() => configuration(f.root, 'x64', 'Release'), /ambiguous SOUI_BUILD_VENDOR_TESTS/);
});
test('generated application and relative-root inputs cannot stand in for root native tests', t => {
  const f = fixture(t);
  assert.throws(() => configuration('.', 'x64', 'Release'), /absolute/);
  fs.writeFileSync(path.join(f.root, 'project.json'), '{}');
  assert.throws(() => configuration(f.root, 'x64', 'Release'), /generated app/);
});
test('ON accepts all 78 registered names and OFF preserves SDK registration only', () => {
  assert.deepEqual(registry(jsonRegistry(allNames), 'ON'), {registeredVendorTests: 78, registeredNativeTests: 83});
  assert.deepEqual(registry(jsonRegistry([sdkName]), 'OFF'), {registeredVendorTests: 0, registeredNativeTests: 1});
});
test('one missing, extra, duplicate or disabled vendor case rejects the whole registry', () => {
  assert.throws(() => registry(jsonRegistry(allNames.filter(name => name !== manifest.names[0])), 'ON'), /all 78 exact names/);
  assert.throws(() => registry(jsonRegistry([...allNames, 'kernel.native.unexpected']), 'ON'), /all 78 exact names/);
  assert.throws(() => registry(jsonRegistry([...allNames, manifest.names[0]]), 'ON'), /Duplicate/);
  const disabled = JSON.parse(jsonRegistry(allNames));
  disabled.tests[1].properties.push({name: 'DISABLED', value: true});
  assert.throws(() => registry(JSON.stringify(disabled), 'ON'), /disabled/);
  const auxiliaryDisabled = JSON.parse(jsonRegistry(allNames));
  auxiliaryDisabled.tests.find(item => item.name === manifest.auxiliaryNames[0]).properties.push({name: 'DISABLED', value: true});
  assert.throws(() => registry(JSON.stringify(auxiliaryDisabled), 'ON'), /disabled/);
  assert.throws(() => registry(jsonRegistry(allNames), 'OFF'), /OFF cache conflicts/);
  assert.throws(() => registry(jsonRegistry(allNames.filter(name => name !== manifest.auxiliaryNames[0])), 'ON'), /exact scheduler source contracts/);
  assert.throws(() => registry(jsonRegistry([...allNames, 'scheduler.unexpected']), 'ON'), /exact scheduler source contracts/);
});
test('empty, malformed or SDK-free CTest output is not a zero-test success', () => {
  assert.throws(() => registry(jsonRegistry([]), 'OFF'), /empty/);
  assert.throws(() => registry(jsonRegistry(manifest.names), 'ON'), /SDK native tests are absent/);
  assert.throws(() => registry('{', 'ON'), SyntaxError);
  assert.throws(() => registry(JSON.stringify({tests: []}), 'ON'), /json-v1/);
});
test('an actual registry subprocess error propagates instead of producing an acceptance report', t => {
  const f = fixture(t);
  // Node is a real executable, but rejects CTest arguments. No test program runs.
  assert.throws(() => inspect(f.root, 'x64', 'Release', process.execPath, true), /Actual CTest registry query failed/);
});

test('manifest follows every actual vendor CMake case registration', () => {
  const leaves = ['tests/native/CMakeLists.txt', 'tests/native/vendor_lifetime/CMakeLists.txt',
    'tests/native/input_ui_policies.cmake', 'tests/native/render_deadline_policy/CMakeLists.txt',
    'tests/native/render_diagnostic_source/CMakeLists.txt', 'tests/native/host_visibility_source/CMakeLists.txt', 'tests/native/window_close_lifetime/CMakeLists.txt', 'tests/native/independent_wheel_source/CMakeLists.txt',
    'tests/native/vendor_lifetime/worker_lifetime/CMakeLists.txt', 'tests/native/vendor_lifetime/dragdata/CMakeLists.txt',
    'tests/native/vendor_lifetime/ctor_shell/CMakeLists.txt', 'tests/native/vendor_lifetime/legacy_drag/CMakeLists.txt',
    'tests/native/request_close/CMakeLists.txt'];
  const names = new Set();
  for (const leaf of leaves) {
    const source = fs.readFileSync(path.join(repository, leaf), 'utf8');
    for (const match of source.matchAll(/add_test\(NAME\s+(kernel\.native\.[^\s)$]+)\s/g)) names.add(match[1]);
    for (const match of source.matchAll(/soui_input_ui_policy_test\([^)]*\s+(kernel\.native\.[^\s)]+)\)/g)) names.add(match[1]);
    for (const loop of source.matchAll(/foreach\(case IN ITEMS\s+([^)]*)\)([\s\S]*?)endforeach\(\)/g)) {
      const items = loop[1].trim().split(/\s+/);
      for (const registration of loop[2].matchAll(/add_test\(NAME\s+(kernel\.native\.[^\s)]+)\s/g)) {
        for (const item of items) names.add(registration[1].replace('${case}', item));
      }
    }
  }
  assert.equal(names.size, 78);
  assert.deepEqual([...names].sort(), manifest.names);
});

test('auxiliary scheduler names bind the actual standalone source fixtures', () => {
  const names = new Set();
  for (const leaf of ['scheduler_continuation', 'scheduler_index']) {
    const source = fs.readFileSync(path.join(repository, 'tests/native', leaf, 'CMakeLists.txt'), 'utf8');
    for (const match of source.matchAll(/add_test\(NAME\s+(scheduler\.[^\s)$]+)\s/g)) names.add(match[1]);
    for (const loop of source.matchAll(/foreach\(failure IN ITEMS\s+([^)]*)\)([\s\S]*?)endforeach\(\)/g)) {
      for (const registration of loop[2].matchAll(/add_test\(NAME\s+(scheduler\.[^\s)]+)\s/g))
        for (const failure of loop[1].trim().split(/\s+/)) names.add(registration[1].replace('${failure}', failure));
    }
  }
  assert.deepEqual([...names].sort(), manifest.auxiliaryNames);
});
test('central vendor preflight covers the union of actual leaf source inputs', () => {
  const prefix = 'third_party/sources/miniblink/';
  const expected = new Set(['wke/wke.cpp', 'net/ActivatingObjCheck.cpp', 'net/ActivatingObjCheck.h',
    'wke/CursorDescriptorBroker.cpp', 'wke/CursorDescriptorBroker.h', 'wke/wke_cursor_broker.h',
    'content/web_impl_win/TaskSchedulingPolicy.h', 'content/web_impl_win/TaskSchedulingIndex.h',
    'content/common/InputFrameTrace.cpp', 'content/common/InputFrameTrace.h',
    'cc/trees/FrameDeadlinePolicy.h', 'cc/trees/LegacyPaintRetryPolicy.h',
    'cc/trees/ResourceNotificationPolicy.h',
    'cc/trees/RenderDiagnosticTrace.h', 'cc/trees/GpuSkiaSurface.cpp',
    'cc/trees/FrameBufferLeasePolicy.h',
    'cc/trees/InputFrameCoverage.h',
    'cc/trees/IndependentInputTrace.h',
    'cc/trees/ViewTraceIdentity.h',
    'cc/trees/DrawFrameWakePolicy.h',
    'cc/raster/RasterSchedulingPolicy.h',
    'cc/raster/RasterCandidateBudget.h',
    'content/browser/HostVisibilityPolicy.h',
    'content/common/PaintLifetime.h',
    'cc/input/ScrollController.h',
    'cc/input/ScrollController.cpp',
    'cc/trees/ScrollTreeSnapshot.h',
    'cc/trees/ScrollTreeSnapshot.cpp',
    'cc/trees/LayerTreeHost.cpp',
    'wke/wkeWebWindow.cpp'].map(relative => prefix + relative));
  const worker = JSON.parse(fs.readFileSync(path.join(repository, 'tests/native/vendor_lifetime/worker_lifetime/source-map.json'), 'utf8'));
  worker.sources.forEach(source => expected.add(source.path));
  const legacy = JSON.parse(fs.readFileSync(path.join(repository, 'tests/native/vendor_lifetime/legacy_drag/source-map.json'), 'utf8'));
  Object.values(legacy.sources).forEach(source => expected.add(source.relative));
  for (const leaf of ['dragdata', 'ctor_shell']) {
    const source = fs.readFileSync(path.join(repository, `tests/native/vendor_lifetime/${leaf}/CMakeLists.txt`), 'utf8');
    const block = source.match(/set\(public_\w+_source_pins\s+([\s\S]*?)\n\)/);
    assert.ok(block, 'Actual leaf source pins are required');
    for (const match of block[1].matchAll(/"(third_party\/sources\/miniblink\/[^|]+)\|/g)) expected.add(match[1]);
  }
  const module = fs.readFileSync(path.join(repository, 'cmake/SouiVendorTests.cmake'), 'utf8');
  const required = module.match(/set\(_soui_vendor_required\s+([\s\S]*?)\n\)/);
  assert.ok(required, 'Actual central required inputs are required');
  const actual = [...required[1].matchAll(/"([^"]+)"/g)].map(match => match[1]);
  assert.equal(expected.size, 50);
  assert.equal(new Set(actual).size, actual.length);
  assert.deepEqual(actual.sort(), [...expected].sort());
});

test('portable absence does not freeze or manufacture private source pins', t => {
  const f = fixture(t, 'OFF');
  assert.deepEqual(requiredSourcePaths(f.root), []);
  assert.equal(fs.existsSync(path.join(f.root, 'third_party')), false);
});
test('private source freeze binds all actual required files and rejects one absent or non-file entry', t => {
  const f = fixture(t), moduleText = fs.readFileSync(path.join(repository, 'cmake/SouiVendorTests.cmake'), 'utf8');
  const required = moduleText.match(/set\(_soui_vendor_required\s+([\s\S]*?)\n\)/);
  assert.ok(required);
  const names = [...required[1].matchAll(/"([^"]+)"/g)].map(match => match[1]).sort();
  assert.equal(names.length, 50);
  fs.mkdirSync(path.join(f.root, 'cmake'));
  fs.writeFileSync(path.join(f.root, 'cmake/SouiVendorTests.cmake'), moduleText);
  for (const [index, name] of names.entries()) {
    const filename = path.join(f.root, name);
    fs.mkdirSync(path.dirname(filename), {recursive: true});
    fs.writeFileSync(filename, 'private-source-unit-fixture-' + index);
  }
  const pins = requiredSourcePaths(f.root);
  assert.deepEqual(pins.map(item => item.path), names.map(name => path.join(f.root, name).replaceAll('\\', '/')));
  const last = path.join(f.root, names.at(-1)), previous = pins.at(-1);
  fs.appendFileSync(last, '-changed');
  assert.notEqual(requiredSourcePaths(f.root).at(-1).sha256, previous.sha256);
  fs.unlinkSync(last);
  assert.throws(() => requiredSourcePaths(f.root), /ENOENT/);
  fs.mkdirSync(last);
  assert.throws(() => requiredSourcePaths(f.root), /Linked or invalid LLVM input/);
});

test('real entrypoints reject unsupported vendor flag combinations before building', {skip: process.platform !== 'win32'}, t => {
  const shell = process.env.SOUI_GUI_TEST_POWERSHELL || 'powershell.exe';
  const run = (script, args) => cp.spawnSync(shell, ['-NoProfile', '-File', script, ...args],
    {encoding: 'utf8', windowsHide: true, timeout: 30000});
  const nativeOnly = run(path.join(repository, 'tools/Test.ps1'), ['-NativeOnly', '-RequireVendorTests']);
  assert.notEqual(nativeOnly.status, 0);
  assert.match(nativeOnly.stderr + nativeOnly.stdout, /RequireVendorTests.*NativeOnly|NativeOnly.*RequireVendorTests/);
  const sdkOnly = run(path.join(repository, 'tools/Build.ps1'), ['-SdkOnly', '-WithVendorTests']);
  assert.notEqual(sdkOnly.status, 0);
  assert.match(sdkOnly.stderr + sdkOnly.stdout, /WithVendorTests requires the source devkit root/);
  const f = fixture(t);
  fs.mkdirSync(path.join(f.root, 'tools'));
  fs.copyFileSync(path.join(repository, 'tools/Build.ps1'), path.join(f.root, 'tools/Build.ps1'));
  fs.writeFileSync(path.join(f.root, 'project.json'), '{}');
  const generated = run(path.join(f.root, 'tools/Build.ps1'), ['-WithVendorTests']);
  assert.notEqual(generated.status, 0);
  assert.match(generated.stderr + generated.stdout, /WithVendorTests requires the source devkit root/);
});

// Exercise CMake itself, independently of the synthetic cache/registry tests above.
// These fixtures test configuration policy only; they do not compile a private kernel.
function configureFixture(t, privateSource = 'absent') {
  const parent = fs.realpathSync(os.tmpdir());
  const root = fs.mkdtempSync(path.join(parent, 'soui-vendor-configure-'));
  t.after(() => {
    const resolved = fs.realpathSync(root);
    assert.equal(path.dirname(resolved), parent);
    assert.ok(path.basename(resolved).startsWith('soui-vendor-configure-'));
    fs.rmSync(resolved, {recursive: true, force: true});
  });
  fs.mkdirSync(path.join(root, 'cmake'));
  fs.copyFileSync(path.join(repository, 'cmake/SouiVendorTests.cmake'), path.join(root, 'cmake/SouiVendorTests.cmake'));
  fs.writeFileSync(path.join(root, 'CMakeLists.txt'), 'cmake_minimum_required(VERSION 3.24)\n' +
    'project(VendorConfigurationBoundary NONE)\noption(SOUI_BUILD_TESTS "SDK tests" ON)\ninclude(cmake/SouiVendorTests.cmake)\n');
  const privateRoot = path.join(root, 'third_party/sources/miniblink');
  if (privateSource !== 'absent') fs.mkdirSync(privateRoot, {recursive: true});
  if (privateSource === 'complete') {
    // Synthetic regular files suffice for this preflight. Do not require private
    // sources just to run the portable SDK's configuration-policy tests.
    const moduleText = fs.readFileSync(path.join(root, 'cmake/SouiVendorTests.cmake'), 'utf8');
    const block = moduleText.match(/set\(_soui_vendor_required\s+([\s\S]*?)\n\)/);
    assert.ok(block);
    for (const match of block[1].matchAll(/"([^"]+)"/g)) {
      const destination = path.join(root, match[1]);
      fs.mkdirSync(path.dirname(destination), {recursive: true});
      fs.writeFileSync(destination, '// Configuration-only fixture; not kernel source or build evidence.\n');
    }
  }
  const configure = (args = []) => cp.spawnSync('cmake', ['-S', root, '-B', path.join(root, 'build'), ...args],
    {encoding: 'utf8', windowsHide: true, timeout: 30000});
  const cache = () => fs.readFileSync(path.join(root, 'build/CMakeCache.txt'), 'utf8');
  return {root, privateRoot, configure, cache};
}

test('actual fresh CMake defaults portable sources OFF and preserves SDK tests ON', t => {
  const f = configureFixture(t), result = f.configure();
  assert.equal(result.status, 0, result.stderr || result.error?.message);
  assert.match(f.cache(), /^SOUI_BUILD_VENDOR_TESTS:BOOL=OFF$/m);
  assert.match(f.cache(), /^SOUI_BUILD_TESTS:BOOL=ON$/m);
  assert.match(result.stdout, /vendor regression gate not evaluated/);
});

test('actual fresh CMake defaults a complete private root ON', t => {
  const f = configureFixture(t, 'complete'), result = f.configure();
  assert.equal(result.status, 0, result.stderr || result.error?.message);
  assert.match(f.cache(), /^SOUI_BUILD_VENDOR_TESTS:BOOL=ON$/m);
  assert.match(result.stdout, /existing source-byte gates still apply/);
});

for (const scenario of [
  {name: 'absent root explicitly ON', source: 'absent', args: ['-DSOUI_BUILD_VENDOR_TESTS=ON'], error: /lack required regular files/},
  {name: 'existing incomplete root by default', source: 'incomplete', args: [], error: /lack required regular files/},
  {name: 'SDK tests OFF with vendor ON', source: 'complete', args: ['-DSOUI_BUILD_TESTS=OFF', '-DSOUI_BUILD_VENDOR_TESTS=ON'], error: /requires SOUI_BUILD_TESTS=ON/},
  {name: 'required source replaced by a directory', source: 'complete', args: [], directory: true, error: /lack required regular files/},
]) {
  test('actual CMake rejects ' + scenario.name, t => {
    const f = configureFixture(t, scenario.source);
    if (scenario.directory) {
      const file = path.join(f.privateRoot, 'wke/wke.cpp');
      fs.unlinkSync(file); fs.mkdirSync(file);
    }
    const result = f.configure(scenario.args);
    assert.equal(result.signal, null); assert.equal(result.error, undefined);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr + result.stdout, scenario.error);
  });
}

test('actual CMake keeps cached ON and refuses a later missing private input', t => {
  const f = configureFixture(t, 'complete');
  assert.equal(f.configure().status, 0);
  fs.unlinkSync(path.join(f.privateRoot, 'wke/wke.cpp'));
  const result = f.configure();
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /lack required regular files/);
  assert.match(f.cache(), /^SOUI_BUILD_VENDOR_TESTS:BOOL=ON$/m);
});
