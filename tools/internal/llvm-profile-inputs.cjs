'use strict';
// Requested inputs and configured compiler records do not prove native closure,
// source execution, ABI compatibility, or an operating-system qualification.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const cp = require('node:child_process');
const {isDeepStrictEqual} = require('node:util');
const roles = ['CLANG', 'LINKER', 'ARCHIVER', 'RC', 'MT', 'NINJA', 'CMD', 'POWERSHELL'];
const definitionNames = ['SOUI_NATIVE_TOOLCHAIN', 'SOUI_LLVM_ARCHITECTURE', 'SOUI_LLVM_ROOT',
  'SOUI_MSVC142_ROOT', 'SOUI_WINDOWS_SDK_ROOT', 'SOUI_NINJA_EXECUTABLE', 'SOUI_WINDOWS_CMD_EXECUTABLE',
  'SOUI_LLVM_INPUT_IDENTITY', 'SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE', 'SOUI_TOOL_IDENTITY_BRIDGE',
  'SOUI_TOOL_IDENTITY_HELPER', 'SOUI_LLVM_BRIDGE_IDENTITY'];
const need = (ok, message) => { if (!ok) throw new Error(message); };
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const cmakePath = value => value.replaceAll('\\', '/');
function plain(filename, directory = false) {
  need(typeof filename === 'string' && path.isAbsolute(filename) && !/[;"\x00-\x1f]/.test(filename), 'An absolute plain LLVM input path is required');
  let cursor = path.resolve(filename), first = true;
  while (true) {
    const stat = fs.lstatSync(cursor);
    need(!stat.isSymbolicLink() && (first ? (directory ? stat.isDirectory() : stat.isFile()) : stat.isDirectory()), 'Linked or invalid LLVM input: ' + cursor);
    const parent = path.dirname(cursor); if (parent === cursor) break;
    cursor = parent; first = false;
  }
  return path.resolve(filename);
}
function pin(filename) {
  filename = plain(filename); const stat = fs.statSync(filename), fd = fs.openSync(filename, 'r');
  const hasher = crypto.createHash('sha256'), buffer = Buffer.alloc(1048576);
  try { let count; while ((count = fs.readSync(fd, buffer, 0, buffer.length, null)) !== 0) hasher.update(buffer.subarray(0, count)); }
  finally { fs.closeSync(fd); }
  return {path: cmakePath(filename), bytes: stat.size, sha256: hasher.digest('hex')};
}
function verifyPin(expected) {
  need(expected && Number.isSafeInteger(expected.bytes) && expected.bytes > 0 && /^[a-f0-9]{64}$/.test(expected.sha256 || ''), 'Invalid LLVM input pin');
  const actual = pin(expected.path);
  need(actual.bytes === expected.bytes && actual.sha256 === expected.sha256, 'LLVM input bytes changed: ' + expected.path);
  return actual;
}
function cacheValue(text, name, type) {
  const rows = text.split(/\r?\n/).filter(row => row.startsWith(name + ':'));
  need(rows.length === 1 && rows[0].startsWith(`${name}:${type}=`), 'Missing or ambiguous LLVM cache entry: ' + name);
  return rows[0].slice(name.length + type.length + 2);
}
function compilerCacheValue(text, name) {
  const rows = text.split(/\r?\n/).filter(row => row.startsWith(name + ':'));
  const prefix = [`${name}:FILEPATH=`, `${name}:STRING=`].find(value => rows[0]?.startsWith(value));
  need(rows.length === 1 && prefix, 'Missing or ambiguous LLVM cache entry: ' + name);
  return rows[0].slice(prefix.length);
}
function readRequest(filename, architecture, root) {
  need(['x86', 'x64'].includes(architecture), 'Invalid LLVM architecture');
  root = plain(root, true); filename = plain(filename);
  need(fs.statSync(filename).size <= 1048576, 'LLVM request exceeds 1 MiB');
  const requestPin = pin(filename), value = JSON.parse(fs.readFileSync(filename, 'utf8')), d = value.discovery;
  need(value.schema === 'soui.build-toolchain-request/v1' && value.profile === 'LLVM22MSVC142' && value.architecture === architecture &&
    d?.Profile === value.profile && d.Architecture === architecture && d.HostArchitecture === 'x64' &&
    d.RequestedClangVersion === '22.1.3' && d.MsvcVersion === '14.29.30133' && d.MsvcCompatibilityVersion === '19.29.30159' &&
    d.WindowsSdkVersion === '10.0.19041.0' && d.Target === (architecture === 'x64' ? 'x86_64-pc-windows-msvc' : 'i686-pc-windows-msvc') &&
    d.Generator === 'Ninja Multi-Config' && ['soui.llvm-profile-inputs/v2', 'soui.llvm-profile-inputs/v3'].includes(d.InputIdentitySchema) &&
    d.ActualCompilerVersion === null && d.GeneratorValidated === false && d.Qualified === false && d.WholeToolInventoryVerified === false,
  'LLVM request profile, architecture, or discovery scope differs');
  need(isDeepStrictEqual(Object.keys(d.ToolPins || {}).sort(), [...roles].sort()) && Array.isArray(d.RequiredInputPins) && d.RequiredInputPins.length === (d.InputIdentitySchema === 'soui.llvm-profile-inputs/v3' ? 18 : 17) &&
    Array.isArray(d.BridgePins) && d.BridgePins.length === 2 && isDeepStrictEqual(Object.keys(d.CMakeDefinitions || {}).sort(), [...definitionNames].sort()), 'Incomplete LLVM discovery inputs');
  const defs = d.CMakeDefinitions, bridge = cmakePath(path.join(root, 'tools/internal/Invoke-ToolFileIdentity.ps1')),
    helper = cmakePath(path.join(root, 'tools/internal/Tool-FileIdentity.ps1')), toolchain = cmakePath(path.join(root, 'cmake/toolchains/SouiLLVM22.cmake'));
  need(value.toolchain?.path && cmakePath(path.resolve(value.toolchain.path)) === toolchain &&
    defs.SOUI_TOOL_IDENTITY_BRIDGE === bridge && defs.SOUI_TOOL_IDENTITY_HELPER === helper &&
    d.BridgePins[0].path === bridge && d.BridgePins[1].path === helper, 'LLVM request must use the fixed product toolchain and identity siblings');
  const expectedRoles = {CLANG: `${defs.SOUI_LLVM_ROOT}/bin/clang-cl.exe`, LINKER: `${defs.SOUI_LLVM_ROOT}/bin/lld-link.exe`,
    ARCHIVER: `${defs.SOUI_LLVM_ROOT}/bin/llvm-lib.exe`, RC: `${defs.SOUI_WINDOWS_SDK_ROOT}/bin/10.0.19041.0/x64/rc.exe`,
    MT: `${defs.SOUI_WINDOWS_SDK_ROOT}/bin/10.0.19041.0/x64/mt.exe`, NINJA: defs.SOUI_NINJA_EXECUTABLE,
    CMD: defs.SOUI_WINDOWS_CMD_EXECUTABLE, POWERSHELL: defs.SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE};
  for (const role of roles) need(d.ToolPins[role]?.path === expectedRoles[role] && d.ToolPins[role].hostMachine === 34404, 'LLVM requested tool role differs: ' + role);
  need(d.NativeProcessPaths?.CMD === path.resolve(d.ToolPins.CMD.path), 'LLVM native child shell path differs from the exact requested CMD input');
  const includes = [`${defs.SOUI_LLVM_ROOT}/lib/clang/22/include`, `${defs.SOUI_MSVC142_ROOT}/include`,
    ...(d.InputIdentitySchema === 'soui.llvm-profile-inputs/v3' ? ['ucrt', 'shared', 'um', 'winrt'] : ['ucrt', 'shared', 'um']).map(name => `${defs.SOUI_WINDOWS_SDK_ROOT}/Include/10.0.19041.0/${name}`)];
  const libraries = [`${defs.SOUI_MSVC142_ROOT}/lib/${architecture}`,
    ...['ucrt', 'um'].map(name => `${defs.SOUI_WINDOWS_SDK_ROOT}/Lib/10.0.19041.0/${name}/${architecture}`)];
  need(path.basename(defs.SOUI_MSVC142_ROOT) === '14.29.30133' && isDeepStrictEqual(d.IncludeDirectories, includes) &&
    isDeepStrictEqual(d.LibraryDirectories, libraries), 'LLVM requested include/CRT roots differ');
  [...includes, ...libraries].forEach(filename => plain(filename, true));
  const sentinels = [`${includes[0]}/stddef.h`, ...['vector', 'optional', 'vcruntime.h'].map(name => `${includes[1]}/${name}`),
    `${includes[2]}/stdlib.h`, `${includes[3]}/sdkddkver.h`, `${includes[4]}/Windows.h`, ...(d.InputIdentitySchema === 'soui.llvm-profile-inputs/v3' ? [`${includes[5]}/EventToken.h`] : []),
    ...['msvcrt', 'msvcprt', 'vcruntime', 'libcmt', 'libcpmt', 'libvcruntime'].map(name => `${libraries[0]}/${name}.lib`),
    ...['ucrt', 'libucrt'].map(name => `${libraries[1]}/${name}.lib`), ...['kernel32', 'user32'].map(name => `${libraries[2]}/${name}.lib`)];
  need(isDeepStrictEqual(d.RequiredInputPins.map(item => item.path), sentinels), 'LLVM requested sentinel membership differs');
  const all = [...roles.map(role => d.ToolPins[role]), ...d.RequiredInputPins, ...d.BridgePins, value.toolchain];
  all.forEach(verifyPin);
  need(new Set(all.map(item => item.path.toLowerCase())).size === all.length, 'Duplicate LLVM input paths');
  const bridgeLines = ['soui.llvm-tool-bridge-inputs/v1', defs.SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE, bridge, helper,
    ...[d.ToolPins.POWERSHELL, ...d.BridgePins].map(item => `${item.path}|${item.bytes}|${item.sha256}`)];
  need(d.BridgeIdentity === digest(bridgeLines.join('\n')) && defs.SOUI_LLVM_BRIDGE_IDENTITY === d.BridgeIdentity &&
    defs.SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE === d.ToolPins.POWERSHELL.path, 'LLVM bridge identity differs');
  const identityLines = [d.InputIdentitySchema, value.profile, '22.1.3', '19.29.30159', '10.0.19041.0',
    architecture, d.Target, defs.SOUI_LLVM_ROOT, defs.SOUI_MSVC142_ROOT, defs.SOUI_WINDOWS_SDK_ROOT,
    defs.SOUI_NINJA_EXECUTABLE, defs.SOUI_WINDOWS_CMD_EXECUTABLE, defs.SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE, bridge, helper, d.BridgeIdentity,
    ...[...roles.map(role => d.ToolPins[role]), ...d.RequiredInputPins, ...d.BridgePins].map(item => `${item.path}|${item.bytes}|${item.sha256}`)];
  need(d.InputIdentity === digest(identityLines.join('\n')) && defs.SOUI_LLVM_INPUT_IDENTITY === d.InputIdentity &&
    defs.SOUI_NATIVE_TOOLCHAIN === value.profile && defs.SOUI_LLVM_ARCHITECTURE === architecture &&
    defs.SOUI_NINJA_EXECUTABLE === d.ToolPins.NINJA.path && defs.SOUI_WINDOWS_CMD_EXECUTABLE === d.ToolPins.CMD.path,
  'LLVM whole requested input identity differs');
  return {value, pin: requestPin, root, inputIdentity: d.InputIdentity, bridgeIdentity: d.BridgeIdentity};
}
function sameProfile(left, right) {
  need(isDeepStrictEqual(left.value, right.value), 'LLVM build and measurement requests differ');
}
// Each request remains validated against its own root. Only owner paths and the
// identities derived from those paths may differ for identical relocated inputs.
function sameRelocatedProfile(left, right) {
  const normalized = request => {
    need(request?.pin?.path && request.root && request.value, 'Missing independently owned LLVM request');
    const current = readRequest(request.pin.path, request.value.architecture, request.root);
    need(isDeepStrictEqual(current, request), 'LLVM owned request changed before profile comparison');
    const value = JSON.parse(JSON.stringify(current.value)), d = value.discovery, defs = d.CMakeDefinitions;
    const bridge = 'tools/internal/Invoke-ToolFileIdentity.ps1', helper = 'tools/internal/Tool-FileIdentity.ps1';
    value.toolchain.path = 'cmake/toolchains/SouiLLVM22.cmake';
    d.BridgePins[0].path = bridge; d.BridgePins[1].path = helper;
    defs.SOUI_TOOL_IDENTITY_BRIDGE = bridge; defs.SOUI_TOOL_IDENTITY_HELPER = helper;
    // readRequest already re-derived and checked each original complete identity.
    d.InputIdentity = null; defs.SOUI_LLVM_INPUT_IDENTITY = null;
    d.BridgeIdentity = null; defs.SOUI_LLVM_BRIDGE_IDENTITY = null;
    return value;
  };
  need(isDeepStrictEqual(normalized(left), normalized(right)), 'Relocated LLVM project and validation-tool profiles differ');
}
function toolPath(observed, expected, request) {
  plain(observed); verifyPin(expected);
  if (cmakePath(path.resolve(observed)) === expected.path) return {observed, expected: expected.path, physicalAlias: false};
  const d = request.value.discovery, helper = d.BridgePins[1];
  // Only the existing explicit PowerShell prerequisite is used. No PATH or hash-only alias fallback.
  const env = {...process.env};
  for (const name of ['NODE_OPTIONS', 'NODE_PATH', 'NODE_REPL_EXTERNAL_MODULE']) delete env[name];
  const result = cp.spawnSync(d.ToolPins.POWERSHELL.path, ['-NoProfile', '-NonInteractive', '-File', d.BridgePins[0].path,
    '-ObservedPath', observed, '-ExpectedPath', expected.path, '-ExpectedBytes', String(expected.bytes), '-ExpectedSha256', expected.sha256,
    '-HelperPath', helper.path, '-HelperBytes', String(helper.bytes), '-HelperSha256', helper.sha256],
  {windowsHide: true, encoding: 'utf8', timeout: 30000, maxBuffer: 8192, env});
  const match = /^SOUI_LLVM_TOOL_FILE_IDENTITY_V1\|([a-f0-9]{64})\|([0-9]+)\|([a-f0-9]{8})\|([a-f0-9]{16})\r?\n$/.exec(result.stdout || '');
  need(!result.error && !result.signal && result.status === 0 && result.stderr === '' && match && match[1] === expected.sha256 && Number(match[2]) === expected.bytes,
    'LLVM observed tool is not the same physical input file: ' + observed);
  verifyPin(d.ToolPins.POWERSHELL); d.BridgePins.forEach(verifyPin);
  return {observed, expected: expected.path, physicalAlias: true, volumeSerial: match[3], fileIndex: match[4]};
}
function setValue(text, name) {
  const expression = new RegExp('^set\\(' + name + '\\s+(?:"([^"\\r\\n]*)"|([^\\s()]+))\\)', 'gm');
  const matches = [...text.matchAll(expression)]; need(matches.length === 1, 'Missing or ambiguous generated compiler field: ' + name);
  return matches[0][1] ?? matches[0][2];
}
function inspectCache(request, build, source, configuration, languages = ['CXX']) {
  build = plain(build, true); source = plain(source, true);
  const cachePin = pin(path.join(build, 'CMakeCache.txt')), text = fs.readFileSync(cachePin.path, 'utf8'), d = request.value.discovery;
  need(path.resolve(cacheValue(text, 'CMAKE_HOME_DIRECTORY', 'INTERNAL')).toLowerCase() === source.toLowerCase() &&
    path.resolve(cacheValue(text, 'CMAKE_CACHEFILE_DIR', 'INTERNAL')).toLowerCase() === build.toLowerCase(), 'LLVM cache source/build ownership differs');
  need(cacheValue(text, 'CMAKE_GENERATOR', 'INTERNAL') === 'Ninja Multi-Config' && cacheValue(text, 'CMAKE_GENERATOR_PLATFORM', 'INTERNAL') === '' &&
    cacheValue(text, 'CMAKE_GENERATOR_TOOLSET', 'INTERNAL') === '' && cacheValue(text, 'CMAKE_CONFIGURATION_TYPES', 'STRING') === 'Debug;Release;RelWithDebInfo' &&
    ['Debug', 'Release', 'RelWithDebInfo'].includes(configuration), 'LLVM generator/configuration differs; empty platform is not an architecture');
  for (const name of definitionNames) need(cacheValue(text, name, 'STRING') === d.CMakeDefinitions[name], 'LLVM configured request differs: ' + name);
  const cacheIdentity = digest(`${d.InputIdentity}\n${request.value.toolchain.sha256}\n${d.BridgeIdentity}\nNinja Multi-Config\nDebug;Release;RelWithDebInfo`);
  need(cacheValue(text, 'SOUI_LLVM_CACHE_IDENTITY', 'INTERNAL') === cacheIdentity, 'LLVM configured toolchain identity differs');
  const toolSlots = {CMAKE_C_COMPILER: 'CLANG', CMAKE_CXX_COMPILER: 'CLANG', CMAKE_LINKER: 'LINKER', CMAKE_LINKER_LLD: 'LINKER',
    CMAKE_AR: 'ARCHIVER', CMAKE_RC_COMPILER: 'RC', CMAKE_MT: 'MT', CMAKE_MAKE_PROGRAM: 'NINJA'};
  const aliases = Object.entries(toolSlots).map(([name, role]) => toolPath(
    name === 'CMAKE_C_COMPILER' || name === 'CMAKE_CXX_COMPILER' ? compilerCacheValue(text, name) : cacheValue(text, name, 'FILEPATH'),
    d.ToolPins[role], request));
  need(cacheValue(text, 'CMAKE_LINKER_TYPE', 'STRING') === 'LLD' && cacheValue(text, 'CMAKE_SYSTEM_VERSION', 'STRING') === '10.0.19041.0', 'LLVM linker/SDK binding differs');
  const compilerRoot = plain(path.join(build, 'CMakeFiles'), true), directories = fs.readdirSync(compilerRoot, {withFileTypes: true}).filter(item => item.isDirectory() && /^\d+\.\d+(?:\.\d+)?$/.test(item.name));
  const compilerRecords = [];
  for (const language of languages) {
    need(['C', 'CXX'].includes(language), 'Invalid configured LLVM language');
    const files = directories.map(item => path.join(compilerRoot, item.name, `CMake${language}Compiler.cmake`)).filter(filename => fs.existsSync(filename));
    need(files.length === 1, 'Exactly one generated LLVM compiler record is required: ' + language);
    const compilerPin = pin(files[0]); need(compilerPin.bytes <= 1048576, 'Generated compiler record is oversized');
    const compiler = fs.readFileSync(compilerPin.path, 'utf8'), prefix = `CMAKE_${language}_`;
    const expected = {COMPILER_ID: 'Clang', COMPILER_VERSION: '22.1.3', COMPILER_FRONTEND_VARIANT: 'MSVC', SIMULATE_ID: 'MSVC',
      SIMULATE_VERSION: '19.29', COMPILER_ARCHITECTURE_ID: d.Architecture === 'x64' ? 'x64' : 'X86', SIZEOF_DATA_PTR: d.Architecture === 'x64' ? '8' : '4', COMPILER_WORKS: 'TRUE'};
    for (const [key, value] of Object.entries(expected)) need(setValue(compiler, prefix + key) === value, 'Generated LLVM compiler identity differs: ' + key);
    aliases.push(toolPath(setValue(compiler, prefix + 'COMPILER'), d.ToolPins.CLANG, request));
    need(cacheValue(text, prefix + 'COMPILER_TARGET', 'STRING') === d.Target, 'LLVM target differs');
    compilerRecords.push({language, pin: compilerPin, configuredIdentity: expected});
  }
  return {profile: 'LLVM22MSVC142', architecture: d.Architecture, target: d.Target, targetPointerBytes: d.Architecture === 'x64' ? 8 : 4,
    inputIdentity: d.InputIdentity, bridgeIdentity: d.BridgeIdentity, cache: cachePin, compilerRecords, aliases,
    configuredCompilerVerified: true, compiledObjectsVerified: false, nativeClosureVerified: false, qualificationPassed: false};
}
module.exports = {readRequest, sameProfile, sameRelocatedProfile, inspectCache, verifyPin, pin, cacheValue, toolPath};
if (require.main === module) {
  try {
    const [command, filename, architecture, root, expectedDiscovery, ...rest] = process.argv.slice(2);
    need(command === 'request' && rest.length === 0, 'Usage: llvm-profile-inputs.cjs request ABS_REQUEST ARCH ABS_ROOT [ABS_DISCOVERY_JSON]');
    const request = readRequest(filename, architecture, root);
    if (expectedDiscovery !== undefined) { plain(expectedDiscovery); need(fs.statSync(expectedDiscovery).size <= 1048576, 'Discovery exceeds 1 MiB');
      need(isDeepStrictEqual(request.value.discovery, JSON.parse(fs.readFileSync(expectedDiscovery, 'utf8'))), 'Explicit LLVM discovery differs from the existing build request'); }
    console.log(JSON.stringify({pin: request.pin, profile: request.value.profile, inputIdentity: request.inputIdentity, bridgeIdentity: request.bridgeIdentity}));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
