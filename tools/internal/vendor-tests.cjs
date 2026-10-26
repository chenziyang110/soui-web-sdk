'use strict';
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const manifest = require('./vendor-test-names.json');
const need = (value, message) => { if (!value) throw new Error(message); };
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const comparable = value => process.platform === 'win32' ? value.toLowerCase() : value;

function directory(value) {
  need(typeof value === 'string' && path.isAbsolute(value), 'An absolute source/build root is required');
  const full = path.resolve(value), stat = fs.lstatSync(full);
  need(stat.isDirectory() && !stat.isSymbolicLink(), 'A plain source/build directory is required');
  return fs.realpathSync.native(full);
}
function cacheValue(text, name, type) {
  const rows = text.split(/\r?\n/).filter(row => row.startsWith(name + ':'));
  need(rows.length === 1 && rows[0].startsWith(`${name}:${type}=`), `Missing or ambiguous ${name}:${type} cache entry`);
  return rows[0].slice(name.length + type.length + 2);
}
function requiredSourcePaths(root) {
  root = directory(root);
  const privateRoot = path.join(root, 'third_party/sources/miniblink');
  if (!fs.existsSync(privateRoot)) return [];
  // An existing incomplete private tree is a failure, just as the CMake ON default is.
  directory(privateRoot);
  const inputs = require('./llvm-profile-inputs.cjs');
  const moduleFile = path.join(root, 'cmake/SouiVendorTests.cmake');
  inputs.pin(moduleFile);
  const text = fs.readFileSync(moduleFile, 'utf8');
  const blocks = [...text.matchAll(/^set\(_soui_vendor_required\s*\r?\n([\s\S]*?)^\)\s*$/gm)];
  need(blocks.length === 1, 'Missing or ambiguous vendor required source contract');
  const rows = blocks[0][1].split(/\r?\n/).filter(row => row.trim());
  need(rows.length === 30 && rows.every(row => /^\s*"third_party\/sources\/miniblink\/[A-Za-z0-9_./-]+"\s*$/.test(row)), 'Invalid exact 30 vendor source contract');
  const names = rows.map(row => row.trim().slice(1, -1)).sort();
  need(new Set(names).size === 30 && names.every(name => !name.split('/').includes('..')), 'Duplicate or escaping vendor source');
  return names.map(name => inputs.pin(path.join(root, name)));
}
function configuration(root, architecture, config, required = false, options = {}) {
  need(['x64', 'x86'].includes(architecture) && ['Debug', 'Release'].includes(config), 'Invalid native architecture/configuration');
  need(typeof required === 'boolean', 'Vendor requirement must be boolean');
  root = directory(root);
  need(!fs.existsSync(path.join(root, 'project.json')), 'Vendor tests require the source devkit root, not a generated app');
  const build = directory(path.join(root, 'out', architecture)), cache = path.join(build, 'CMakeCache.txt');
  const cacheStat = fs.lstatSync(cache);
  need(cacheStat.isFile() && !cacheStat.isSymbolicLink(), 'A plain CMakeCache.txt file is required');
  const raw = fs.readFileSync(cache), text = raw.toString('utf8');
  need(comparable(directory(cacheValue(text, 'CMAKE_HOME_DIRECTORY', 'INTERNAL'))) === comparable(root), 'CMake cache belongs to another source root');
  need(comparable(directory(cacheValue(text, 'CMAKE_CACHEFILE_DIR', 'INTERNAL'))) === comparable(build), 'CMake cache belongs to another build directory');
  const platform = cacheValue(text, 'CMAKE_GENERATOR_PLATFORM', 'INTERNAL').split(',')[0];
  let nativeToolchain;
  if (options.toolchainRequest !== undefined) {
    const inputs = require('./llvm-profile-inputs.cjs');
    const request = inputs.readRequest(options.toolchainRequest, architecture, root);
    nativeToolchain = {requestFile: request.pin, ...inputs.inspectCache(request, build, root, config, ['C', 'CXX'])};
  } else need(platform === (architecture === 'x64' ? 'x64' : 'Win32'), 'CMake cache architecture differs');
  need(cacheValue(text, 'CMAKE_CONFIGURATION_TYPES', 'STRING').split(';').includes(config), 'Requested configuration is not generated');
  need(cacheValue(text, 'SOUI_BUILD_TESTS', 'BOOL') === 'ON', 'SDK native tests must be configured ON');
  const mode = cacheValue(text, 'SOUI_BUILD_VENDOR_TESTS', 'BOOL');
  need(mode === 'ON' || mode === 'OFF', 'Vendor cache mode must be exactly ON or OFF');
  need(!required || mode === 'ON', 'Required vendor regressions are OFF; rebuild with -WithVendorTests -FreshConfigure');
  const selected = {root, build, cache, cacheSha256: hash(raw), architecture, configuration: config, mode};
  if (nativeToolchain) selected.nativeToolchain = nativeToolchain;
  return selected;
}
function registry(raw, mode) {
  need(mode === 'ON' || mode === 'OFF', 'Invalid vendor registry mode');
  const value = JSON.parse(raw);
  need(value?.kind === 'ctestInfo' && value.version?.major === 1 && value.version?.minor === 0 && Array.isArray(value.tests), 'Invalid CTest json-v1 registry');
  const names = value.tests.map(test => test?.name);
  need(names.length > 0 && names.every(name => typeof name === 'string' && name.length > 0), 'CTest registry is empty or invalid');
  need(new Set(names).size === names.length, 'Duplicate CTest test name');
  need(names.some(name => name.startsWith('sdk.native.')), 'SDK native tests are absent');
  const vendor = value.tests.filter(test => test.name.startsWith('kernel.native'));
  const actual = vendor.map(test => test.name).sort();
  const expected = manifest.names;
  const auxiliary = manifest.auxiliaryNames;
  need(Array.isArray(auxiliary) && auxiliary.length === 4 && new Set(auxiliary).size === 4 &&
    auxiliary.every(name => typeof name === 'string' && name.startsWith('scheduler.')) &&
    JSON.stringify(auxiliary) === JSON.stringify([...auxiliary].sort()), 'Invalid exact auxiliary vendor names');
  const actualAuxiliary = names.filter(name => name.startsWith('scheduler.')).sort();
  need(manifest.schema === 'soui.vendor-test-names/v1' && Array.isArray(expected) && expected.length === 66 &&
    new Set(expected).size === 66 && expected.every(name => name.startsWith('kernel.native.')) &&
    JSON.stringify(expected) === JSON.stringify([...expected].sort()), 'Invalid exact vendor names manifest');
  if (mode === 'ON') {
    need(JSON.stringify(actualAuxiliary) === JSON.stringify(auxiliary), 'Vendor ON registry lacks exact scheduler source contracts');
    need(JSON.stringify(actual) === JSON.stringify(expected), 'Vendor ON registry must contain all 66 exact names with no extra kernel.native tests');
    need(vendor.every(test => !(test.properties || []).some(property => property.name === 'DISABLED' && property.value)), 'A registered vendor test is disabled');
  }
  else need(actual.length === 0 && actualAuxiliary.length === 0, 'Vendor OFF cache conflicts with registered kernel.native tests');
  return {registeredVendorTests: actual.length, registeredNativeTests: names.length};
}
function inspect(root, architecture, config, ctest, required = false, options = {}) {
  const selected = configuration(root, architecture, config, required, options);
  need(typeof ctest === 'string' && path.isAbsolute(ctest) && fs.statSync(ctest).isFile(), 'An actual absolute CTest executable is required');
  const result = cp.spawnSync(ctest, ['--test-dir', selected.build, '-C', config, '--show-only=json-v1'],
    {cwd: selected.root, encoding: 'utf8', windowsHide: true, timeout: 30000, maxBuffer: 8 * 1024 * 1024});
  need(!result.error && !result.signal && result.status === 0, 'Actual CTest registry query failed: ' + (result.error?.message || result.stderr || result.status));
  const observed = registry(result.stdout, selected.mode);
  need(hash(fs.readFileSync(selected.cache)) === selected.cacheSha256, 'CMake cache changed during registry query');
  if (options.toolchainRequest !== undefined) configuration(root, architecture, config, required, options);
  return {schema: 'soui.vendor-test-registry/v1', ...selected, ...observed, scope: 'registry-only',
    privateAbiSourceComparison: selected.mode === 'ON' ? 'required' : 'not-evaluated',
    vendorRegressionPassed: null, qualificationPassed: false, releaseReady: false};
}
module.exports = {configuration, registry, inspect, requiredSourcePaths};
if (require.main === module) {
  try {
    if (process.argv[2] === 'required-source') {
      need(process.argv.length === 4, 'Usage: vendor-tests.cjs required-source ABS_ROOT');
      process.stdout.write(JSON.stringify(requiredSourcePaths(process.argv[3])) + '\n');
    } else {
    const [command, root, arch, config, ctest, requireText, ...extra] = process.argv.slice(2);
    need(command === 'inspect' && ['true', 'false'].includes(requireText) && (extra.length === 0 ||
      (extra.length === 2 && extra[0] === '--toolchain-request' && path.isAbsolute(extra[1]))),
    'Usage: vendor-tests.cjs inspect ABS_ROOT ARCH CONFIG ABS_CTEST true|false [--toolchain-request ABS_REQUEST]');
    process.stdout.write(JSON.stringify(inspect(root, arch, config, ctest, requireText === 'true',
      extra.length ? {toolchainRequest: extra[1]} : {})) + '\n');
    }
  } catch (error) { process.stderr.write(error.message + '\n'); process.exitCode = 1; }
}
