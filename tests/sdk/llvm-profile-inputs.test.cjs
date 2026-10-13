'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const profile = require('../../tools/internal/llvm-profile-inputs.cjs');
const {headerFiles} = require('../../tools/internal/llvm-reference-sdk.cjs');
test('LLVM resource selection binds compiler and initialization to the same Windows SDK RC', () => {
  const source = fs.readFileSync(path.join(__dirname, '../../cmake/toolchains/SouiLLVM22.cmake'), 'utf8');
  const selections = ['CMAKE_RC_COMPILER', 'CMAKE_RC_COMPILER_INIT'].map(name => {
    const rows = [...source.matchAll(new RegExp('_soui_llvm_bind\\(' + name + '\\s+"([^"\\n]+)"\\s+FILEPATH\\)', 'g'))];
    assert.equal(rows.length, 1, name + ' requires one strict toolchain binding');
    return rows[0][1];
  });
  assert.equal(selections[0], '${SOUI_WINDOWS_SDK_ROOT}/bin/10.0.19041.0/x64/rc.exe');
  assert.equal(selections[1], selections[0]);
  assert.equal(/(?:set|_soui_llvm_bind)\(CMAKE_(?:RC_COMPILE_OBJECT|DEPFILE_FLAGS_RC|NINJA_CMCLDEPS_RC)\b/.test(source), false,
    'RC selection must preserve CMake dependency/compile rules');
});
function fixture(t) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'soui-llvm-input-unit-')));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  return root;
}
test('profile comparisons preserve values while permitting object property order', () => {
  profile.sameProfile({value: {profile: 'LLVM22MSVC142', architecture: 'x64', discovery: {a: 1, b: 2}}},
    {value: {discovery: {b: 2, a: 1}, architecture: 'x64', profile: 'LLVM22MSVC142'}});
  assert.throws(() => profile.sameProfile({value: {architecture: 'x64'}}, {value: {architecture: 'x86'}}), /requests differ/);
});
test('relative, oversized and mismatched profile requests fail before tool invocation', t => {
  const root = fixture(t), file = path.join(root, 'request.json');
  assert.throws(() => profile.readRequest('relative.json', 'x64', root), /absolute/);
  fs.writeFileSync(file, Buffer.alloc(1048577, 32));
  assert.throws(() => profile.readRequest(file, 'x64', root), /1 MiB/);
  fs.writeFileSync(file, JSON.stringify({schema: 'soui.build-toolchain-request/v1', profile: 'MSVC143', architecture: 'x64'}));
  assert.throws(() => profile.readRequest(file, 'x64', root), /profile, architecture/);
  assert.throws(() => profile.readRequest(file, 'arm64', root), /Invalid LLVM architecture/);
});
test('raw pin mutation and non-file input are rejected independently of requested compiler identity', t => {
  const root = fixture(t), file = path.join(root, 'source.txt');
  fs.writeFileSync(file, 'unit bytes');
  const pin = profile.pin(file);
  profile.verifyPin(pin);
  fs.appendFileSync(file, ' changed');
  assert.throws(() => profile.verifyPin(pin), /bytes changed/);
  assert.throws(() => profile.pin(root), /Linked or invalid/);
});
test('blank Ninja platform does not substitute for generator or target verification', t => {
  const root = fixture(t), build = path.join(root, 'build'); fs.mkdirSync(build);
  const rows = {'CMAKE_HOME_DIRECTORY:INTERNAL': root, 'CMAKE_CACHEFILE_DIR:INTERNAL': build,
    'CMAKE_GENERATOR:INTERNAL': 'Visual Studio 17 2022', 'CMAKE_GENERATOR_PLATFORM:INTERNAL': '',
    'CMAKE_GENERATOR_TOOLSET:INTERNAL': '', 'CMAKE_CONFIGURATION_TYPES:STRING': 'Debug;Release;RelWithDebInfo'};
  const write = () => fs.writeFileSync(path.join(build, 'CMakeCache.txt'), Object.entries(rows).map(([key, value]) => key + '=' + value).join('\n'));
  write();
  assert.throws(() => profile.inspectCache({value: {discovery: {}}}, build, root, 'Release'), /generator\/configuration differs/);
  rows['CMAKE_GENERATOR:INTERNAL'] = 'Ninja Multi-Config'; rows['CMAKE_GENERATOR_PLATFORM:INTERNAL'] = 'x64'; write();
  assert.throws(() => profile.inspectCache({value: {discovery: {}}}, build, root, 'Release'), /empty platform is not an architecture/);
});
test('installed header membership preserves all nested public leaves', t => {
  const root = fixture(t); fs.mkdirSync(path.join(root, 'soui'));
  fs.writeFileSync(path.join(root, 'soui/a.h'), '// unit fixture');
  fs.writeFileSync(path.join(root, 'z.h'), '// unit fixture');
  assert.deepEqual(headerFiles(root), ['soui/a.h', 'z.h']);
});
test('installed header membership rejects a linked file header', t => {
  const root = fixture(t); fs.mkdirSync(path.join(root, 'soui'));
  fs.writeFileSync(path.join(root, 'z.h'), '// unit fixture');
  // Keep the file-symlink boundary separate from ordinary membership coverage.
  try { fs.symlinkSync(path.join(root, 'z.h'), path.join(root, 'soui/link.h'), 'file'); }
  catch (error) {
    if (['EPERM', 'EACCES', 'ENOTSUP'].includes(error.code)) {
      t.skip(`Host does not permit this file-symlink unit fixture (${error.code}, errno=${error.errno ?? 'unavailable'})`);
      return;
    }
    throw error;
  }
  assert.throws(() => headerFiles(root), /linked entries/);
});
test('installed header membership rejects a linked directory ancestor', t => {
  const root = fixture(t), include = path.join(root, 'include'), target = path.join(root, 'target');
  fs.mkdirSync(path.join(include, 'soui'), {recursive: true});
  fs.mkdirSync(target);
  fs.writeFileSync(path.join(target, 'public.h'), '// linked public header');
  const link = path.join(include, 'soui/linked'), type = process.platform === 'win32' ? 'junction' : 'dir';
  // A local Windows junction tests the same collector without requiring a file symlink.
  try { fs.symlinkSync(target, link, type); }
  catch (error) {
    if (['EPERM', 'EACCES', 'ENOTSUP'].includes(error.code)) {
      t.skip(`Host does not permit this ${type} unit fixture (${error.code}, errno=${error.errno ?? 'unavailable'})`);
      return;
    }
    throw error;
  }
  assert.ok(fs.lstatSync(link).isSymbolicLink(), 'The fixture must be a real directory link');
  assert.throws(() => headerFiles(link), /directory must be plain/);
  assert.throws(() => headerFiles(include), /linked entries/);
});
// These portable text records exercise the production configured-cache reader.
// They are unit inputs, not real compilers, native outputs, or closure evidence.
function configuredCacheFixture(t) {
  const root = fixture(t), source = path.join(root, 'source'), build = path.join(root, 'build'), tools = path.join(root, 'tools');
  for (const directory of [source, build, tools]) fs.mkdirSync(directory);
  const cmake = value => value.replaceAll('\\', '/');
  const hash = value => require('node:crypto').createHash('sha256').update(value).digest('hex');
  const toolNames = {CLANG: 'clang-cl.exe', LINKER: 'lld-link.exe', ARCHIVER: 'llvm-lib.exe', RC: 'rc.exe', MT: 'mt.exe',
    NINJA: 'ninja.exe', CMD: 'cmd.exe', POWERSHELL: 'pwsh.exe'};
  const toolPins = {};
  for (const [role, name] of Object.entries(toolNames)) {
    const file = path.join(tools, name); fs.writeFileSync(file, `portable ${role} input\n`); toolPins[role] = profile.pin(file);
  }
  const bridgeFiles = ['Invoke-ToolFileIdentity.ps1', 'Tool-FileIdentity.ps1'].map(name => {
    const file = path.join(tools, name); fs.writeFileSync(file, '# portable bridge input; never executed\n'); return profile.pin(file);
  });
  const toolchainFile = path.join(source, 'SouiLLVM22.cmake');
  fs.writeFileSync(toolchainFile, '# portable toolchain input; never executed\n');
  const inputIdentity = hash('configured-cache unit input'), bridgeIdentity = hash('configured-cache unit bridge');
  const definitions = {SOUI_NATIVE_TOOLCHAIN: 'LLVM22MSVC142', SOUI_LLVM_ARCHITECTURE: 'x64', SOUI_LLVM_ROOT: cmake(tools),
    SOUI_MSVC142_ROOT: cmake(source), SOUI_WINDOWS_SDK_ROOT: cmake(source), SOUI_NINJA_EXECUTABLE: toolPins.NINJA.path,
    SOUI_WINDOWS_CMD_EXECUTABLE: toolPins.CMD.path, SOUI_LLVM_INPUT_IDENTITY: inputIdentity,
    SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE: toolPins.POWERSHELL.path, SOUI_TOOL_IDENTITY_BRIDGE: bridgeFiles[0].path,
    SOUI_TOOL_IDENTITY_HELPER: bridgeFiles[1].path, SOUI_LLVM_BRIDGE_IDENTITY: bridgeIdentity};
  const discovery = {Architecture: 'x64', Target: 'x86_64-pc-windows-msvc', InputIdentity: inputIdentity, BridgeIdentity: bridgeIdentity,
    ToolPins: toolPins, BridgePins: bridgeFiles, CMakeDefinitions: definitions};
  const request = {value: {profile: 'LLVM22MSVC142', discovery, toolchain: profile.pin(toolchainFile)}, inputIdentity, bridgeIdentity};
  const rows = [['CMAKE_HOME_DIRECTORY', 'INTERNAL', cmake(source)], ['CMAKE_CACHEFILE_DIR', 'INTERNAL', cmake(build)],
    ['CMAKE_GENERATOR', 'INTERNAL', 'Ninja Multi-Config'], ['CMAKE_GENERATOR_PLATFORM', 'INTERNAL', ''],
    ['CMAKE_GENERATOR_TOOLSET', 'INTERNAL', ''], ['CMAKE_CONFIGURATION_TYPES', 'STRING', 'Debug;Release;RelWithDebInfo'],
    ...Object.entries(definitions).map(([name, value]) => [name, 'STRING', value]),
    ['SOUI_LLVM_CACHE_IDENTITY', 'INTERNAL', hash(`${inputIdentity}\n${request.value.toolchain.sha256}\n${bridgeIdentity}\nNinja Multi-Config\nDebug;Release;RelWithDebInfo`)]];
  const slots = {CMAKE_C_COMPILER: 'CLANG', CMAKE_CXX_COMPILER: 'CLANG', CMAKE_LINKER: 'LINKER', CMAKE_LINKER_LLD: 'LINKER',
    CMAKE_AR: 'ARCHIVER', CMAKE_RC_COMPILER: 'RC', CMAKE_MT: 'MT', CMAKE_MAKE_PROGRAM: 'NINJA'};
  rows.push(...Object.entries(slots).map(([name, role]) => [name, 'FILEPATH', toolPins[role].path]),
    ['CMAKE_LINKER_TYPE', 'STRING', 'LLD'], ['CMAKE_SYSTEM_VERSION', 'STRING', '10.0.19041.0'],
    ['CMAKE_C_COMPILER_TARGET', 'STRING', discovery.Target], ['CMAKE_CXX_COMPILER_TARGET', 'STRING', discovery.Target]);
  const compilerRoot = path.join(build, 'CMakeFiles', '4.2.3'); fs.mkdirSync(compilerRoot, {recursive: true});
  const compilerFiles = {};
  for (const language of ['C', 'CXX']) {
    const values = {COMPILER: toolPins.CLANG.path, COMPILER_ID: 'Clang', COMPILER_VERSION: '22.1.3', COMPILER_FRONTEND_VARIANT: 'MSVC',
      SIMULATE_ID: 'MSVC', SIMULATE_VERSION: '19.29', COMPILER_ARCHITECTURE_ID: 'x64', SIZEOF_DATA_PTR: '8', COMPILER_WORKS: 'TRUE'};
    const file = path.join(compilerRoot, `CMake${language}Compiler.cmake`);
    fs.writeFileSync(file, Object.entries(values).map(([name, value]) => `set(CMAKE_${language}_${name} "${value}")`).join('\n') + '\n');
    compilerFiles[language] = file;
  }
  const write = (entries = rows, eol = '\n') => fs.writeFileSync(path.join(build, 'CMakeCache.txt'),
    entries.map(([name, type, value]) => `${name}:${type}=${value}`).join(eol) + eol);
  const inspect = () => profile.inspectCache(request, build, source, 'Release', ['C', 'CXX']);
  write(); return {root, source, build, request, rows, compilerFiles, write, inspect};
}
function cacheRowsWith(rows, name, type, value) {
  return rows.map(row => row[0] === name ? [name, type ?? row[1], value ?? row[2]] : [...row]);
}
test('configured cache accepts a CXX STRING compiler with a C FILEPATH compiler', t => {
  const f = configuredCacheFixture(t); f.write(cacheRowsWith(f.rows, 'CMAKE_CXX_COMPILER', 'STRING'));
  const observed = f.inspect();
  assert.deepEqual(observed.compilerRecords.map(record => record.language), ['C', 'CXX']);
  assert.ok(observed.aliases.every(alias => alias.physicalAlias === false));
  assert.equal(observed.configuredCompilerVerified, true);
  assert.equal(observed.compiledObjectsVerified, false);
  assert.equal(observed.nativeClosureVerified, false);
  assert.equal(observed.qualificationPassed, false);
});
test('configured cache accepts both compiler STRING entries including CRLF records', t => {
  const f = configuredCacheFixture(t);
  f.write(cacheRowsWith(cacheRowsWith(f.rows, 'CMAKE_C_COMPILER', 'STRING'), 'CMAKE_CXX_COMPILER', 'STRING'), '\r\n');
  assert.equal(f.inspect().compilerRecords.length, 2);
});
test('configured cache retains FILEPATH compiler compatibility', t => {
  const f = configuredCacheFixture(t); assert.equal(f.inspect().compilerRecords.length, 2);
});
test('configured cache rejects duplicate compiler entries even when types differ', t => {
  const f = configuredCacheFixture(t);
  for (const name of ['CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER']) {
    for (const type of ['FILEPATH', 'STRING']) {
      f.write([...f.rows, [name, type, f.request.value.discovery.ToolPins.CLANG.path]]);
      assert.throws(f.inspect, /Missing or ambiguous LLVM cache entry/);
    }
  }
});
test('configured cache rejects missing or incorrectly typed compiler entries', t => {
  const f = configuredCacheFixture(t);
  for (const name of ['CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER']) {
    for (const type of ['BOOL', 'INTERNAL', 'UNINITIALIZED']) {
      f.write(cacheRowsWith(f.rows, name, type)); assert.throws(f.inspect, /Missing or ambiguous LLVM cache entry/);
    }
    f.write(f.rows.filter(row => row[0] !== name)); assert.throws(f.inspect, /Missing or ambiguous LLVM cache entry/);
  }
});
test('configured cache keeps every non-compiler tool slot strictly FILEPATH', t => {
  const f = configuredCacheFixture(t);
  for (const name of ['CMAKE_LINKER', 'CMAKE_LINKER_LLD', 'CMAKE_AR', 'CMAKE_RC_COMPILER', 'CMAKE_MT', 'CMAKE_MAKE_PROGRAM']) {
    f.write(cacheRowsWith(f.rows, name, 'STRING')); assert.throws(f.inspect, /Missing or ambiguous LLVM cache entry/);
  }
});
test('configured compiler STRING entries still require absolute existing paths and exact tool bytes', t => {
  const f = configuredCacheFixture(t), name = 'CMAKE_CXX_COMPILER';
  f.write(cacheRowsWith(f.rows, name, 'STRING', 'relative-clang.exe')); assert.throws(f.inspect, /absolute plain LLVM input path/);
  f.write(cacheRowsWith(f.rows, name, 'STRING', path.join(f.root, 'missing-clang.exe'))); assert.throws(f.inspect, /ENOENT/);
  f.write(cacheRowsWith(f.rows, name, 'STRING'));
  const expected = f.request.value.discovery.ToolPins.CLANG, original = {...expected};
  expected.bytes++; assert.throws(f.inspect, /LLVM input bytes changed/); Object.assign(expected, original);
  expected.sha256 = '0'.repeat(64); assert.throws(f.inspect, /LLVM input bytes changed/); Object.assign(expected, original);
  fs.appendFileSync(expected.path, ' changed'); assert.throws(f.inspect, /LLVM input bytes changed/);
});
test('compiler cache type compatibility does not relax generated identity or scalar bindings', t => {
  const f = configuredCacheFixture(t), compiler = fs.readFileSync(f.compilerFiles.CXX, 'utf8');
  f.write(cacheRowsWith(f.rows, 'CMAKE_CXX_COMPILER', 'STRING'));
  fs.writeFileSync(f.compilerFiles.CXX, compiler.replace('VERSION "22.1.3"', 'VERSION "22.0.0"'));
  assert.throws(f.inspect, /Generated LLVM compiler identity differs: COMPILER_VERSION/);
  fs.writeFileSync(f.compilerFiles.CXX, compiler.replace('SIZEOF_DATA_PTR "8"', 'SIZEOF_DATA_PTR "4"'));
  assert.throws(f.inspect, /Generated LLVM compiler identity differs: SIZEOF_DATA_PTR/);
  fs.writeFileSync(f.compilerFiles.CXX, compiler + 'set(CMAKE_CXX_COMPILER_ID "Clang")\n');
  assert.throws(f.inspect, /Missing or ambiguous generated compiler field/);
  fs.writeFileSync(f.compilerFiles.CXX, compiler);
  f.write(cacheRowsWith(f.rows, 'CMAKE_SYSTEM_VERSION', 'STRING', '10.0.22621.0')); assert.throws(f.inspect, /LLVM linker\/SDK binding differs/);
  f.write(cacheRowsWith(f.rows, 'SOUI_LLVM_INPUT_IDENTITY', 'STRING', '0'.repeat(64))); assert.throws(f.inspect, /LLVM configured request differs/);
});

// Request fixtures exercise the production readRequest entry, not a second
// validator. Text .exe/.lib inputs are never executed and are not compiler proof.
function versionedRequestFixture(t, version, architecture = 'x64') {
  const root = fixture(t), cmake = value => value.replaceAll('\\', '/');
  const hash = value => require('node:crypto').createHash('sha256').update(value).digest('hex');
  const put = (relative, body) => {
    const filename = path.join(root, relative);
    fs.mkdirSync(path.dirname(filename), {recursive: true});
    fs.writeFileSync(filename, body); return profile.pin(filename);
  };
  const schema = `soui.llvm-profile-inputs/v${version}`, llvm = cmake(path.join(root, 'llvm')),
    msvc = cmake(path.join(root, 'msvc/14.29.30133')), sdk = cmake(path.join(root, 'sdk'));
  const roleFiles = {CLANG: 'llvm/bin/clang-cl.exe', LINKER: 'llvm/bin/lld-link.exe', ARCHIVER: 'llvm/bin/llvm-lib.exe',
    RC: 'sdk/bin/10.0.19041.0/x64/rc.exe', MT: 'sdk/bin/10.0.19041.0/x64/mt.exe',
    NINJA: 'tools/ninja.exe', CMD: 'tools/cmd.exe', POWERSHELL: 'tools/pwsh.exe'};
  const toolPins = Object.fromEntries(Object.entries(roleFiles).map(([role, relative]) =>
    [role, {...put(relative, `unit-only ${role} bytes\n`), hostMachine: 34404}]));
  const bridgePins = ['Invoke-ToolFileIdentity.ps1', 'Tool-FileIdentity.ps1'].map(name =>
    put('tools/internal/' + name, '# unit-only identity source, never invoked\n'));
  const toolchain = put('cmake/toolchains/SouiLLVM22.cmake', '# unit-only fixed product toolchain bytes\n');
  const includes = [`${llvm}/lib/clang/22/include`, `${msvc}/include`,
    ...(version === 3 ? ['ucrt', 'shared', 'um', 'winrt'] : ['ucrt', 'shared', 'um']).map(name => `${sdk}/Include/10.0.19041.0/${name}`)];
  const libraries = [`${msvc}/lib/${architecture}`, ...['ucrt', 'um'].map(name => `${sdk}/Lib/10.0.19041.0/${name}/${architecture}`)];
  const sentinels = [`${includes[0]}/stddef.h`, ...['vector', 'optional', 'vcruntime.h'].map(name => `${includes[1]}/${name}`),
    `${includes[2]}/stdlib.h`, `${includes[3]}/sdkddkver.h`, `${includes[4]}/Windows.h`,
    ...(version === 3 ? [`${includes[5]}/EventToken.h`] : []),
    ...['msvcrt', 'msvcprt', 'vcruntime', 'libcmt', 'libcpmt', 'libvcruntime'].map(name => `${libraries[0]}/${name}.lib`),
    ...['ucrt', 'libucrt'].map(name => `${libraries[1]}/${name}.lib`), ...['kernel32', 'user32'].map(name => `${libraries[2]}/${name}.lib`)];
  const inputPins = sentinels.map(filename => put(path.relative(root, filename), `unit sentinel ${path.basename(filename)}\n`));
  const pinLine = item => `${item.path}|${item.bytes}|${item.sha256}`;
  const bridge = bridgePins[0].path, helper = bridgePins[1].path, powershell = toolPins.POWERSHELL.path;
  const bridgeIdentity = hash(['soui.llvm-tool-bridge-inputs/v1', powershell, bridge, helper,
    ...[toolPins.POWERSHELL, ...bridgePins].map(pinLine)].join('\n'));
  const target = architecture === 'x64' ? 'x86_64-pc-windows-msvc' : 'i686-pc-windows-msvc';
  const inputIdentity = hash([schema, 'LLVM22MSVC142', '22.1.3', '19.29.30159', '10.0.19041.0', architecture, target,
    llvm, msvc, sdk, toolPins.NINJA.path, toolPins.CMD.path, powershell, bridge, helper, bridgeIdentity,
    ...[...Object.values(toolPins), ...inputPins, ...bridgePins].map(pinLine)].join('\n'));
  const definitions = {SOUI_NATIVE_TOOLCHAIN: 'LLVM22MSVC142', SOUI_LLVM_ARCHITECTURE: architecture,
    SOUI_LLVM_ROOT: llvm, SOUI_MSVC142_ROOT: msvc, SOUI_WINDOWS_SDK_ROOT: sdk, SOUI_NINJA_EXECUTABLE: toolPins.NINJA.path,
    SOUI_WINDOWS_CMD_EXECUTABLE: toolPins.CMD.path, SOUI_LLVM_INPUT_IDENTITY: inputIdentity,
    SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE: powershell, SOUI_TOOL_IDENTITY_BRIDGE: bridge,
    SOUI_TOOL_IDENTITY_HELPER: helper, SOUI_LLVM_BRIDGE_IDENTITY: bridgeIdentity};
  const value = {schema: 'soui.build-toolchain-request/v1', profile: 'LLVM22MSVC142', architecture,
    discovery: {Profile: 'LLVM22MSVC142', Architecture: architecture, HostArchitecture: 'x64', RequestedClangVersion: '22.1.3',
      MsvcVersion: '14.29.30133', MsvcCompatibilityVersion: '19.29.30159', WindowsSdkVersion: '10.0.19041.0', Target: target,
      Generator: 'Ninja Multi-Config', InputIdentitySchema: schema, ActualCompilerVersion: null, GeneratorValidated: false,
      Qualified: false, WholeToolInventoryVerified: false, ToolPins: toolPins, BridgePins: bridgePins,
      RequiredInputPins: inputPins, IncludeDirectories: includes, LibraryDirectories: libraries,
      CMakeDefinitions: definitions, NativeProcessPaths: {CMD: path.resolve(toolPins.CMD.path)}, InputIdentity: inputIdentity, BridgeIdentity: bridgeIdentity},
    toolchain};
  const requestFile = path.join(root, 'request.json'), write = () => fs.writeFileSync(requestFile, JSON.stringify(value));
  write(); return {root, value, write, read: () => profile.readRequest(requestFile, architecture, root)};
}
test('versioned requests accept exact historical v2 and WinRT v3 on each architecture', t => {
  for (const version of [2, 3]) for (const architecture of ['x64', 'x86']) {
    const f = versionedRequestFixture(t, version, architecture), result = f.read();
    assert.equal(result.value.discovery.InputIdentitySchema, `soui.llvm-profile-inputs/v${version}`);
    assert.equal(result.value.discovery.IncludeDirectories.length, version === 3 ? 6 : 5);
    assert.equal(result.value.discovery.RequiredInputPins.length, version === 3 ? 18 : 17);
    assert.equal(result.value.discovery.Qualified, false);
    assert.equal(result.value.discovery.ActualCompilerVersion, null);
  }
});
test('versioned requests reject unknown schemas and cross-version sentinel counts', t => {
  for (const version of [2, 3]) {
    const f = versionedRequestFixture(t, version), d = f.value.discovery, original = JSON.parse(JSON.stringify(d));
    d.InputIdentitySchema = 'soui.llvm-profile-inputs/v4'; f.write(); assert.throws(f.read, /discovery scope differs/);
    Object.assign(d, JSON.parse(JSON.stringify(original)));
    if (version === 3) d.RequiredInputPins.splice(7, 1); else d.RequiredInputPins.push({...d.RequiredInputPins[0]});
    f.write(); assert.throws(f.read, /Incomplete LLVM discovery inputs/);
  }
});
test('versioned requests reject malformed include roots and exact-count sentinel substitutions', t => {
  for (const version of [2, 3]) {
    const f = versionedRequestFixture(t, version), d = f.value.discovery, original = JSON.parse(JSON.stringify(d));
    if (version === 3) d.IncludeDirectories.pop(); else d.IncludeDirectories.push(d.CMakeDefinitions.SOUI_WINDOWS_SDK_ROOT + '/Include/10.0.19041.0/winrt');
    f.write(); assert.throws(f.read, /include\/CRT roots differ/);
    Object.assign(d, JSON.parse(JSON.stringify(original)));
    d.RequiredInputPins[version === 3 ? 7 : 6] = {...d.RequiredInputPins[0]};
    f.write(); assert.throws(f.read, /sentinel membership differs/);
  }
});
test('WinRT v3 rejects missing or changed EventToken bytes and stale whole identities', t => {
  const f = versionedRequestFixture(t, 3), d = f.value.discovery, eventToken = d.RequiredInputPins[7], original = fs.readFileSync(eventToken.path);
  fs.unlinkSync(eventToken.path); assert.throws(f.read, /ENOENT/); fs.writeFileSync(eventToken.path, original);
  fs.appendFileSync(eventToken.path, 'changed'); assert.throws(f.read, /LLVM input bytes changed/);
  d.RequiredInputPins[7] = profile.pin(eventToken.path); f.write(); assert.throws(f.read, /whole requested input identity differs/);
});

// Two independently owned requests share explicit vendor inputs, not owner paths.
// These are inert FS fixtures for the production readers, never native evidence.
function relocatedRequestsFixture(t, version = 3, architecture = 'x64') {
  const project = versionedRequestFixture(t, version, architecture), toolsRoot = fixture(t);
  const hash = value => require('node:crypto').createHash('sha256').update(value).digest('hex');
  const value = JSON.parse(JSON.stringify(project.value));
  const ownerFiles = ['tools/internal/Invoke-ToolFileIdentity.ps1', 'tools/internal/Tool-FileIdentity.ps1',
    'cmake/toolchains/SouiLLVM22.cmake'];
  for (const relative of ownerFiles) {
    const filename = path.join(toolsRoot, relative);
    fs.mkdirSync(path.dirname(filename), {recursive: true});
    fs.copyFileSync(path.join(project.root, relative), filename);
  }
  const directory = path.join(toolsRoot, 'out', architecture === 'x64' ? 'benchmark-protocol' : 'benchmark-protocol-x86');
  fs.mkdirSync(directory, {recursive: true});
  const requestFile = path.join(directory, 'llvm-profile-request-Release.json');
  const write = () => {
    const d = value.discovery, defs = d.CMakeDefinitions;
    d.BridgePins = ownerFiles.slice(0, 2).map(relative => profile.pin(path.join(toolsRoot, relative)));
    value.toolchain = profile.pin(path.join(toolsRoot, ownerFiles[2]));
    const bridge = d.BridgePins[0].path, helper = d.BridgePins[1].path, powershell = d.ToolPins.POWERSHELL.path;
    const pinLine = item => `${item.path}|${item.bytes}|${item.sha256}`;
    d.BridgeIdentity = hash(['soui.llvm-tool-bridge-inputs/v1', powershell, bridge, helper,
      ...[d.ToolPins.POWERSHELL, ...d.BridgePins].map(pinLine)].join('\n'));
    defs.SOUI_TOOL_IDENTITY_BRIDGE = bridge; defs.SOUI_TOOL_IDENTITY_HELPER = helper;
    defs.SOUI_LLVM_BRIDGE_IDENTITY = d.BridgeIdentity;
    const roles = ['CLANG', 'LINKER', 'ARCHIVER', 'RC', 'MT', 'NINJA', 'CMD', 'POWERSHELL'];
    d.InputIdentity = hash([d.InputIdentitySchema, value.profile, '22.1.3', '19.29.30159', '10.0.19041.0',
      value.architecture, d.Target, defs.SOUI_LLVM_ROOT, defs.SOUI_MSVC142_ROOT, defs.SOUI_WINDOWS_SDK_ROOT,
      defs.SOUI_NINJA_EXECUTABLE, defs.SOUI_WINDOWS_CMD_EXECUTABLE, powershell, bridge, helper, d.BridgeIdentity,
      ...[...roles.map(role => d.ToolPins[role]), ...d.RequiredInputPins, ...d.BridgePins].map(pinLine)].join('\n'));
    defs.SOUI_LLVM_INPUT_IDENTITY = d.InputIdentity;
    fs.writeFileSync(requestFile, JSON.stringify(value));
  };
  write();
  return {project, toolsRoot, value, requestFile, write,
    read: () => profile.readRequest(requestFile, architecture, toolsRoot),
    measurement: {buildDirectory: arch => path.join(toolsRoot, 'out', arch === 'x64' ? 'benchmark-protocol' : 'benchmark-protocol-x86')}};
}
test('relocated requests validate each owner and compare unchanged inputs on both architectures and schemas', t => {
  for (const version of [2, 3]) for (const architecture of ['x64', 'x86']) {
    const f = relocatedRequestsFixture(t, version, architecture), project = f.project.read(), stock = f.read();
    assert.notEqual(project.root, stock.root);
    assert.notEqual(project.inputIdentity, stock.inputIdentity);
    assert.notEqual(project.bridgeIdentity, stock.bridgeIdentity);
    assert.doesNotThrow(() => profile.sameRelocatedProfile(project, stock));
    assert.throws(() => profile.sameProfile(project, stock), /requests differ/);
    assert.equal(project.value.discovery.Qualified, false);
    assert.equal(stock.value.discovery.ActualCompilerVersion, null);
    assert.throws(() => profile.readRequest(project.pin.path, architecture, f.toolsRoot), /fixed product toolchain/);
    assert.throws(() => profile.sameRelocatedProfile({...project, root: f.toolsRoot}, stock), /fixed product toolchain/);
  }
});
test('relocated comparison rejects independently valid changed bridge, toolchain and vendor selection', t => {
  for (const mutation of ['bridge', 'toolchain', 'ninja']) {
    const f = relocatedRequestsFixture(t), d = f.value.discovery;
    if (mutation === 'bridge') fs.appendFileSync(path.join(f.toolsRoot, 'tools/internal/Tool-FileIdentity.ps1'), '# changed unit source\n');
    if (mutation === 'toolchain') fs.appendFileSync(path.join(f.toolsRoot, 'cmake/toolchains/SouiLLVM22.cmake'), '# changed unit source\n');
    if (mutation === 'ninja') {
      const filename = path.join(f.toolsRoot, 'alternate-ninja.exe'); fs.writeFileSync(filename, 'unit-only NINJA bytes\n');
      d.ToolPins.NINJA = {...profile.pin(filename), hostMachine: 34404};
      d.CMakeDefinitions.SOUI_NINJA_EXECUTABLE = d.ToolPins.NINJA.path;
    }
    f.write();
    const project = f.project.read(), stock = f.read(); // Both still pass the full production owner/identity reader.
    assert.throws(() => profile.sameRelocatedProfile(project, stock), /validation-tool profiles differ/);
  }
});
test('relocated comparison rejects a valid different identity schema and a mutated request snapshot', t => {
  const f = relocatedRequestsFixture(t), d = f.value.discovery;
  d.InputIdentitySchema = 'soui.llvm-profile-inputs/v2';
  d.IncludeDirectories.pop(); d.RequiredInputPins.splice(7, 1); f.write();
  assert.throws(() => profile.sameRelocatedProfile(f.project.read(), f.read()), /validation-tool profiles differ/);
  const g = relocatedRequestsFixture(t), project = g.project.read(), stock = g.read();
  fs.appendFileSync(project.pin.path, '\n');
  assert.throws(() => profile.sameRelocatedProfile(project, stock), /owned request changed/);
  const h = relocatedRequestsFixture(t), stale = h.project.read();
  h.project.value.discovery.InputIdentity = '0'.repeat(64); h.project.write();
  assert.throws(() => profile.sameRelocatedProfile(stale, h.read()), /whole requested input identity differs/);
});
test('desktop LLVM selection routes each request to its actual owner and keeps legacy selection unchanged', t => {
  const smoke = require('../../tools/internal/desktop-smoke.cjs');
  for (const architecture of ['x64', 'x86']) {
    const f = relocatedRequestsFixture(t, 3, architecture), request = f.project.read();
    const selected = smoke.profileSelection(f.project.root,
      {architecture, toolsRoot: f.toolsRoot, toolchainRequest: request.pin.path}, f.measurement);
    assert.equal(selected.selected.profileRequest, request.pin.path);
    assert.equal(selected.selected.nativeToolchain, 'LLVM22MSVC142');
    assert.equal(selected.profile.root, f.project.root);
    assert.equal(selected.validationProfile.root, f.toolsRoot);
    assert.equal(selected.measurementOptions.toolchainRequest, f.requestFile);
    assert.notEqual(selected.measurementOptions.toolchainRequest, request.pin.path);
    assert.throws(() => smoke.profileSelection(f.project.root,
      {architecture, toolsRoot: f.toolsRoot, toolchainRequest: f.requestFile}, f.measurement), /fixed product toolchain/);
    assert.throws(() => smoke.profileSelection(f.project.root,
      {architecture, toolsRoot: f.toolsRoot, toolchainRequest: request.pin.path},
      {buildDirectory: () => path.join(f.project.root, 'out', architecture === 'x64' ? 'benchmark-protocol' : 'benchmark-protocol-x86')}),
      /must own their selected architecture build directory/);
  }
  assert.deepEqual(smoke.profileSelection('unused', {}, {}), {selected: {}, profile: null, measurementOptions: {}});
});
test('desktop LLVM selection fails on missing stock preparation, wrong architecture and changed profile inputs', t => {
  const smoke = require('../../tools/internal/desktop-smoke.cjs'), f = relocatedRequestsFixture(t);
  const run = () => smoke.profileSelection(f.project.root,
    {architecture: 'x64', toolsRoot: f.toolsRoot, toolchainRequest: path.join(f.project.root, 'request.json')}, f.measurement);
  fs.unlinkSync(f.requestFile); assert.throws(run, /ENOENT/); f.write();
  assert.throws(() => smoke.profileSelection(f.project.root,
    {architecture: 'x86', toolsRoot: f.toolsRoot, toolchainRequest: path.join(f.project.root, 'request.json')}, f.measurement),
    /profile, architecture/);
  fs.appendFileSync(path.join(f.toolsRoot, 'cmake/toolchains/SouiLLVM22.cmake'), '# changed unit source\n'); f.write();
  assert.throws(run, /validation-tool profiles differ/);
});

// These controller inputs are inert files. No fixture executable, PowerShell or
// compiler is launched, and no unit result is a stock/native qualification.
const buildObserver = require('../../tools/internal/llvm-build-observer.cjs');
function preparedFixture(f) {
  return {toolsRoot: f.toolsRoot, requestPin: profile.pin(f.requestFile),
    request: JSON.parse(fs.readFileSync(f.requestFile, 'utf8'))};
}
test('LLVM build selection keeps both authentic owner identities across architectures and input schemas', t => {
  for (const architecture of ['x64', 'x86']) for (const version of [2, 3]) {
    const f = relocatedRequestsFixture(t, version, architecture), project = f.project.read();
    const selected = buildObserver.validatePreparedProfile(f.project.root, {architecture}, preparedFixture(f), project.pin.path);
    assert.equal(selected.project.root, f.project.root);
    assert.equal(selected.validation.root, f.toolsRoot);
    assert.notEqual(selected.project.inputIdentity, selected.validation.inputIdentity);
    assert.notEqual(selected.project.bridgeIdentity, selected.validation.bridgeIdentity);
    assert.equal(selected.project.pin.path, project.pin.path);
    assert.equal(selected.validation.pin.path, profile.pin(f.requestFile).path);
  }
});
test('LLVM build selection refuses missing stock, cross-owner request substitution and cross-architecture use', t => {
  const f = relocatedRequestsFixture(t), project = f.project.read(), prepared = preparedFixture(f);
  assert.throws(() => buildObserver.validatePreparedProfile(f.project.root, {architecture: 'x64'},
    {...prepared, requestPin: null}, project.pin.path), /independently owned/);
  assert.throws(() => buildObserver.validatePreparedProfile(f.project.root, {architecture: 'x64'},
    {...prepared, requestPin: project.pin}, project.pin.path), /measurement-owned/);
  assert.throws(() => buildObserver.validatePreparedProfile(f.project.root, {architecture: 'x64'}, prepared, f.requestFile), /fixed product toolchain/);
  assert.throws(() => buildObserver.validatePreparedProfile(f.project.root, {architecture: 'x86'}, prepared, project.pin.path), /measurement-owned|ENOENT/);
});
test('LLVM build selection rechecks complete identities and authenticated owner-source bytes', t => {
  const f = relocatedRequestsFixture(t), project = f.project.read(), prepared = preparedFixture(f);
  fs.appendFileSync(f.requestFile, '\n');
  assert.throws(() => buildObserver.validatePreparedProfile(f.project.root, {architecture: 'x64'}, prepared, project.pin.path), /Prepared validation request changed/);
  f.write();
  fs.appendFileSync(f.value.discovery.BridgePins[1].path, '# changed inert helper\n'); f.write();
  // The changed stock still has a valid own-root, fully recomputed identity.
  assert.doesNotThrow(f.read);
  assert.throws(() => buildObserver.validatePreparedProfile(f.project.root, {architecture: 'x64'}, preparedFixture(f), project.pin.path), /validation-tool profiles differ/);
  const g = relocatedRequestsFixture(t);
  g.project.value.discovery.InputIdentity = '0'.repeat(64); g.project.write();
  assert.throws(() => buildObserver.validatePreparedProfile(g.project.root, {architecture: 'x64'}, preparedFixture(g),
    path.join(g.project.root, 'request.json')), /whole requested input identity differs/);
});
function validationBindingFixture(t, architecture = 'x64') {
  const f = relocatedRequestsFixture(t, 3, architecture), sourcePath = 'tools/internal/measurement-validation.cjs';
  const measurement = require('../../tools/internal/measurement-validation.cjs');
  const put = (relative, body) => {
    const filename = path.join(f.toolsRoot, relative); fs.mkdirSync(path.dirname(filename), {recursive: true});
    fs.writeFileSync(filename, body); return profile.pin(filename);
  };
  for (const relative of measurement.fingerprintNames({toolchainRequest: f.requestFile}))
    if (!fs.existsSync(path.join(f.toolsRoot, relative))) put(relative, '// inert canonical source fixture: ' + relative + '\n');
  const source = profile.pin(path.join(f.toolsRoot, sourcePath));
  const executables = ['OwnedProcessRunner', 'OwnedProcessAssignmentFixture', 'ProcessTreeSampler', 'InputTrace', 'NativeTraceIdentity',
    'OwnedTailProcessFixture', 'OwnedTailMutationFixture', 'OwnedTailApiFixture', 'DisplayCalibration'].map(name => {
    const relative = 'out/' + (architecture === 'x64' ? 'benchmark-protocol' : 'benchmark-protocol-x86') + '/Release/' + name + '.exe';
    const bytes = Buffer.alloc(128); bytes.writeUInt16LE(0x5a4d, 0); bytes.writeUInt32LE(64, 60);
    bytes.writeUInt32LE(0x4550, 64); bytes.writeUInt16LE(architecture === 'x64' ? 34404 : 332, 68);
    const file = put(relative, bytes);
    return {name, path: relative, bytes: file.bytes, sha256: file.sha256, coff: architecture === 'x64' ? 34404 : 332};
  });
  const request = f.read(), hash = value => require('node:crypto').createHash('sha256').update(value).digest('hex');
  const build = path.dirname(f.requestFile), d = request.value.discovery, cmake = value => value.replaceAll('\\', '/');
  const rows = [['CMAKE_HOME_DIRECTORY', 'INTERNAL', cmake(path.join(f.toolsRoot, 'benchmarks/protocol'))],
    ['CMAKE_CACHEFILE_DIR', 'INTERNAL', cmake(build)], ['CMAKE_GENERATOR', 'INTERNAL', 'Ninja Multi-Config'],
    ['CMAKE_GENERATOR_PLATFORM', 'INTERNAL', ''], ['CMAKE_GENERATOR_TOOLSET', 'INTERNAL', ''],
    ['CMAKE_CONFIGURATION_TYPES', 'STRING', 'Debug;Release;RelWithDebInfo'],
    ...Object.entries(d.CMakeDefinitions).map(([name, value]) => [name, 'STRING', value]),
    ['SOUI_LLVM_CACHE_IDENTITY', 'INTERNAL', hash(`${d.InputIdentity}\n${request.value.toolchain.sha256}\n${d.BridgeIdentity}\nNinja Multi-Config\nDebug;Release;RelWithDebInfo`)]];
  const slots = {CMAKE_C_COMPILER: 'CLANG', CMAKE_CXX_COMPILER: 'CLANG', CMAKE_LINKER: 'LINKER', CMAKE_LINKER_LLD: 'LINKER',
    CMAKE_AR: 'ARCHIVER', CMAKE_RC_COMPILER: 'RC', CMAKE_MT: 'MT', CMAKE_MAKE_PROGRAM: 'NINJA'};
  rows.push(...Object.entries(slots).map(([name, role]) => [name, 'FILEPATH', d.ToolPins[role].path]),
    ['CMAKE_LINKER_TYPE', 'STRING', 'LLD'], ['CMAKE_SYSTEM_VERSION', 'STRING', '10.0.19041.0'],
    ['CMAKE_C_COMPILER_TARGET', 'STRING', d.Target], ['CMAKE_CXX_COMPILER_TARGET', 'STRING', d.Target]);
  const cache = put(path.relative(f.toolsRoot, path.join(build, 'CMakeCache.txt')),
    rows.map(([name, type, value]) => `${name}:${type}=${value}`).join('\n') + '\n');
  const values = {COMPILER: d.ToolPins.CLANG.path, COMPILER_ID: 'Clang', COMPILER_VERSION: '22.1.3', COMPILER_FRONTEND_VARIANT: 'MSVC',
    SIMULATE_ID: 'MSVC', SIMULATE_VERSION: '19.29', COMPILER_ARCHITECTURE_ID: architecture === 'x64' ? 'x64' : 'X86',
    SIZEOF_DATA_PTR: architecture === 'x64' ? '8' : '4', COMPILER_WORKS: 'TRUE'};
  put(path.relative(f.toolsRoot, path.join(build, 'CMakeFiles/4.2.3/CMakeCXXCompiler.cmake')),
    Object.entries(values).map(([name, value]) => `set(CMAKE_CXX_${name} "${value}")`).join('\n') + '\n');
  const configured = profile.inspectCache(request, build, path.join(f.toolsRoot, 'benchmarks/protocol'), 'Release');
  const record = put(path.relative(f.toolsRoot, path.join(build, 'inert-observation.log')), 'inert log; no actual compile\n');
  const bindings = Array.from({length: 13}, (_, index) => ({source,
    object: put(path.relative(f.toolsRoot, path.join(build, 'fixture-' + index + '.obj')), 'inert object bytes\n')}));
  const observation = {schema: 'soui.llvm-measurement-observation/v1', ...configured, records: {fixture: record}, bindings,
    links: executables.map(row => ({executable: profile.pin(path.join(f.toolsRoot, row.path))})), dependencyPins: [source],
    compileObjects: 13, executableTargets: 9, compilerVerified: false, nativeClosureVerified: false, qualificationPassed: false};
  const report = {schema: 'soui.measurement-tool-preparation/v2', prepared: true, architecture, configuration: 'Release', buildExitCode: 0,
    source: measurement.fingerprint({toolchainRequest: f.requestFile}, f.toolsRoot), executables,
    nativeToolchain: {profile: 'LLVM22MSVC142', inputIdentity: request.inputIdentity, bridgeIdentity: request.bridgeIdentity,
      requestFile: request.pin, observation, compilerVerified: false, nativeClosureVerified: false, abiQualified: false, osQualified: false}};
  const reportFile = path.join(path.dirname(f.requestFile), 'measurement-tools.json'); fs.writeFileSync(reportFile, JSON.stringify(report));
  const binding = {root: f.toolsRoot, preparation: profile.pin(reportFile), requestFile: request.pin,
    inputIdentity: request.inputIdentity, bridgeIdentity: request.bridgeIdentity, sourceFingerprint: report.source.sha256};
  const dependencies = [binding.preparation, binding.requestFile, ...report.source.files.map(row => profile.pin(path.join(f.toolsRoot, row.path))),
    ...executables.map(row => profile.pin(path.join(f.toolsRoot, row.path))), ...request.value.discovery.BridgePins, request.value.toolchain,
    record, cache, ...configured.compilerRecords.map(row => row.pin), ...bindings.map(row => row.object)];
  return {...f, architecture, binding, dependencies, source, cache, record, report, reportFile};
}
test('cached LLVM validation binding checks real request, preparation fingerprint and every frozen stock member', t => {
  for (const architecture of ['x64', 'x86']) {
    const f = validationBindingFixture(t, architecture), project = f.project.read();
    const selected = buildObserver.validateValidationBinding(f.binding, f.project.root, {architecture}, project.pin.path, f.dependencies);
    assert.equal(selected.validation.inputIdentity, f.binding.inputIdentity);
    assert.equal(selected.project.inputIdentity, project.inputIdentity);
    const missing = f.dependencies.filter(row => row.path !== f.source.path);
    assert.throws(() => buildObserver.validateValidationBinding(f.binding, f.project.root, {architecture}, project.pin.path, missing), /omit current validation inputs/);
    fs.appendFileSync(f.source.path, '// changed during build\n');
    assert.throws(() => buildObserver.validateValidationBinding(f.binding, f.project.root, {architecture}, project.pin.path, f.dependencies), /Pinned bytes changed|missing or stale/);
  }
});
test('cached LLVM validation binding refuses historical missing owner and metadata identity substitution', t => {
  const f = validationBindingFixture(t), project = f.project.read(), run = binding =>
    buildObserver.validateValidationBinding(binding, f.project.root, {architecture: 'x64'}, project.pin.path, f.dependencies);
  assert.throws(() => run(undefined), /binding missing/);
  assert.throws(() => run({...f.binding, root: f.project.root}), /preparation must be measurement-owned/);
  assert.throws(() => run({...f.binding, requestFile: project.pin}), /measurement-owned/);
  assert.throws(() => run({...f.binding, inputIdentity: project.inputIdentity}), /Frozen validation identity differs/);
  assert.throws(() => run({...f.binding, sourceFingerprint: '0'.repeat(64)}), /Frozen validation identity differs/);
});
test('cached LLVM validation binding revalidates current configuration and native observation bytes', t => {
  for (const member of ['cache', 'record']) {
    const f = validationBindingFixture(t), project = f.project.read();
    fs.appendFileSync(f[member].path, '\nchanged after original build\n');
    assert.throws(() => buildObserver.validateValidationBinding(f.binding, f.project.root, {architecture: 'x64'},
      project.pin.path, f.dependencies), /input bytes changed/);
  }
  const f = validationBindingFixture(t), project = f.project.read();
  const omitted = f.dependencies.filter(row => row.path !== f.cache.path);
  assert.throws(() => buildObserver.validateValidationBinding(f.binding, f.project.root, {architecture: 'x64'},
    project.pin.path, omitted), /omit current validation inputs/);
});
test('canonical stock source membership cannot be replaced by a self-consistent shorter declared fingerprint', t => {
  const f = validationBindingFixture(t), project = f.project.read();
  f.report.source.files.pop();
  f.report.source.sha256 = require('node:crypto').createHash('sha256').update(JSON.stringify(f.report.source.files, null, 2) + '\n').digest('hex');
  fs.writeFileSync(f.reportFile, JSON.stringify(f.report));
  f.binding.preparation = profile.pin(f.reportFile); f.binding.sourceFingerprint = f.report.source.sha256;
  f.dependencies[0] = f.binding.preparation;
  assert.throws(() => buildObserver.validateValidationBinding(f.binding, f.project.root, {architecture: 'x64'},
    project.pin.path, f.dependencies), /missing or stale/);
});
test('cached LLVM proof verifies complete current local reader code before selected-owner data', t => {
  const owner = fixture(t), local = path.resolve(__dirname, '../..');
  const rows = buildObserver.codeNames.map(relative => {
    const target = path.join(owner, relative); fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.copyFileSync(path.join(local, relative), target); return profile.pin(target);
  });
  assert.doesNotThrow(() => buildObserver.verifyCurrentCodePins(owner, rows));
  assert.throws(() => buildObserver.verifyCurrentCodePins(owner, rows.slice(1)), /Complete current LLVM reader/);
  const index = buildObserver.codeNames.indexOf('tools/internal/measurement-validation.cjs');
  fs.appendFileSync(rows[index].path, '\n// different reader implementation\n'); rows[index] = profile.pin(rows[index].path);
  assert.throws(() => buildObserver.verifyCurrentCodePins(owner, rows), /local LLVM reader code differs/);
});

test('fresh LLVM preparation reads a foreign stock module strictly as pinned data', t => {
  for (const architecture of ['x64', 'x86']) {
    const f = validationBindingFixture(t, architecture);
    fs.writeFileSync(f.source.path, 'throw new Error("stock module must never execute");\n');
    const changed = profile.pin(f.source.path), observation = f.report.nativeToolchain.observation;
    // Truthful inert DTO inputs, not real compiled/prepared qualification.
    for (const binding of observation.bindings) if (binding.source.path === changed.path) binding.source = changed;
    observation.dependencyPins = observation.dependencyPins.map(row => row.path === changed.path ? changed : row);
    f.report.source = require('../../tools/internal/measurement-validation.cjs').fingerprint({toolchainRequest: f.requestFile}, f.toolsRoot);
    fs.writeFileSync(f.reportFile, JSON.stringify(f.report));
    const prepared = buildObserver.preparation(f.toolsRoot, architecture);
    assert.equal(prepared.toolsRoot, f.toolsRoot);
    assert.equal(prepared.setup.report.nativeToolchain.qualificationPassed, undefined);
    assert.equal(prepared.setup.report.nativeToolchain.observation.qualificationPassed, false);
    assert.equal(path.resolve(prepared.requestPin.path), path.resolve(f.requestFile));
    assert.deepEqual(prepared.setup.report, f.report);
  }
});

test('fresh LOCAL LLVM preparation still refuses changed Source or native observation bytes', t => {
  for (const member of ['source', 'record']) {
    const f = validationBindingFixture(t);
    fs.appendFileSync(f[member].path, '\nchanged after frozen stock report\n');
    assert.throws(() => buildObserver.preparation(f.toolsRoot, f.architecture), /missing or stale|input bytes changed/);
  }
});
