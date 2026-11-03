#!/usr/bin/env node
'use strict';

// SDK development snapshot only. This does not build an application installer,
// install system dependencies, sign binaries, or assert native GUI acceptance.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const configurations = ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'];
const matrixFixture = 'soui_platform_matrix_groups';
const matrixSetupTests = [
  'sdk.portable.platform-matrix-cli',
  'sdk.portable.platform-matrix-policy',
  'sdk.portable.platform-matrix-cmake-aliases',
  'sdk.portable.platform-matrix-cmake-cross',
  'sdk.portable.platform-matrix-cmake-windows',
  'sdk.portable.platform-matrix-cmake-invalid',
  'sdk.portable.platform-matrix-cmake-macos',
  'sdk.portable.platform-matrix-cmake-dependencies',
  'sdk.portable.platform-matrix-cmake-slices',
  'sdk.portable.platform-matrix-probe-native',
  'sdk.portable.platform-matrix-probe-mismatch',
  'sdk.portable.platform-matrix-linux-cross',
  'sdk.portable.platform-matrix-linux-os',
  'sdk.portable.platform-matrix-linux-arch',
  'sdk.portable.platform-matrix-abi',
];
const requiredTests = [
  'sdk.portable.integration', 'sdk.portable.primitives', 'sdk.portable.installed-consumer',
  'sdk.portable.platform-matrix', 'sdk.desktop.contract', 'sdk.desktop.preload',
  'sdk.desktop.package-tool', 'sdk.desktop.assets', 'sdk.desktop.asset-executor',
  ...matrixSetupTests,
];
function parseArgs(args) {
  const options = { configuration: 'Release' };
  const names = { '--build-dir': 'build', '--output': 'output', '--config': 'configuration' };
  const seen = new Set();
  for (let i = 0; i < args.length; ++i) {
    const key = names[args[i]];
    if (!key || !args[i + 1] || args[i + 1].startsWith('--') || seen.has(key))
      throw new Error(`Invalid or repeated option: ${args[i]}`);
    seen.add(key);
    options[key] = args[++i];
  }
  if (!options.build || !options.output || !configurations.includes(options.configuration))
    throw new Error('Usage: node tools/package-desktop.cjs --build-dir DIR --output NEW_DIR [--config Release|Debug|RelWithDebInfo|MinSizeRel]');
  return options;
}
function run(command, args, cwd, capture = false) {
  const result = spawnSync(command, args, {
    cwd, stdio: capture ? ['ignore', 'pipe', 'inherit'] : 'inherit',
    encoding: 'utf8', maxBuffer: 16 * 1024 * 1024, shell: false,
  });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed (${result.signal || result.status}).`);
  return result.stdout;
}
function readCache(file) {
  const values = {};
  for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    const match = line.match(/^([^#/:][^:]*):[^=]+=(.*)$/);
    if (match) values[match[1]] = match[2];
  }
  return values;
}
function hash(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}
function validateCache(build, source, config) {
  const cache = readCache(path.join(build, 'CMakeCache.txt'));
  if (!cache.CMAKE_HOME_DIRECTORY ||
      fs.realpathSync(cache.CMAKE_HOME_DIRECTORY) !== fs.realpathSync(source))
    throw new Error('Build directory does not belong to this source checkout.');
  if (cache.SOUI_BUILD_DESKTOP !== 'ON' || cache.SOUI_BUILD_TESTS !== 'ON')
    throw new Error('A desktop build with SOUI_BUILD_TESTS=ON is required.');
  if (cache.CMAKE_CONFIGURATION_TYPES) {
    if (!cache.CMAKE_CONFIGURATION_TYPES.split(';').includes(config))
      throw new Error('Requested configuration is not available in this multi-config build.');
  } else if (cache.CMAKE_BUILD_TYPE !== config) {
    throw new Error('Requested configuration does not match the single-config build.');
  }
}
function validateTests(text) {
  let listing;
  try { listing = JSON.parse(text); } catch { throw new Error('CTest registration is not valid JSON.'); }
  if (!listing || listing.kind !== 'ctestInfo' || !Array.isArray(listing.tests))
    throw new Error('CTest registration is incomplete.');
  const registered = new Map();
  for (const name of requiredTests) {
    const matches = listing.tests.filter(test => test.name === name);
    if (matches.length !== 1 || !Array.isArray(matches[0].command) || !matches[0].command.length ||
        !Array.isArray(matches[0].properties) ||
        matches[0].properties.some(property => property.name === 'DISABLED' &&
          ![false, 0, '0', 'FALSE', 'OFF'].includes(property.value)))
      throw new Error(`Required CTest is missing, disabled or invalid: ${name}. Reconfigure this checkout.`);
    registered.set(name, matches[0]);
  }
  function hasFixture(test, property) {
    const entries = test.properties.filter(item => item.name === property);
    if (!entries.length) return false;
    if (entries.length !== 1) return null;
    const value = entries[0].value;
    const fixtures = Array.isArray(value) ? value : typeof value === 'string' ? value.split(';') : null;
    if (!fixtures || !fixtures.every(item => typeof item === 'string')) return null;
    return fixtures.includes(matrixFixture);
  }
  for (const name of [...matrixSetupTests, 'sdk.portable.platform-matrix']) {
    const test = registered.get(name);
    const setup = matrixSetupTests.includes(name);
    if (hasFixture(test, 'FIXTURES_SETUP') !== setup || hasFixture(test, 'FIXTURES_REQUIRED') !== !setup)
      throw new Error(`Required platform matrix fixture is missing or invalid: ${name}. Reconfigure this checkout.`);
  }
  return requiredTests.slice();
}
function filesIn(directory, relative = '') {
  const files = [];
  for (const name of fs.readdirSync(path.join(directory, relative)).sort()) {
    const local = relative ? `${relative}/${name}` : name;
    const filename = path.join(directory, local);
    const stat = fs.lstatSync(filename);
    if (stat.isSymbolicLink()) throw new Error(`Symlink not allowed in package: ${local}`);
    if (stat.isDirectory()) files.push(...filesIn(directory, local));
    else if (stat.isFile()) files.push({ path: local, bytes: stat.size, sha256: hash(fs.readFileSync(filename)) });
    else throw new Error(`Special file not allowed in package: ${local}`);
  }
  return files;
}
function identity(stage) {
  const runtime = fs.readFileSync(path.join(stage, 'lib/cmake/SouiRuntime/SouiRuntimeConfig.cmake'), 'utf8');
  const desktop = fs.readFileSync(path.join(stage, 'lib/cmake/SouiDesktop/SouiDesktopConfig.cmake'), 'utf8');
  const versionText = fs.readFileSync(path.join(stage, 'include/soui/version.h'), 'utf8');
  function extract(text, pattern, name) {
    const match = text.match(pattern);
    if (!match) throw new Error(`Missing or invalid installed package ${name}.`);
    return match[1];
  }
  const os = extract(runtime, /set\(SouiRuntime_TARGET_OS "(linux|windows|macos)"\)/, 'OS');
  const architecture = extract(runtime, /set\(SouiRuntime_TARGET_ARCH "(x86|x64|arm32|arm64|universal2)"\)/, 'architecture');
  const backend = extract(desktop, /set\(SouiDesktop_BACKEND "(webkitgtk|webview2|wkwebview)"\)/, 'backend');
  const version = extract(versionText, /^#define SOUI_SDK_VERSION_STRING "([0-9]+\.[0-9]+\.[0-9]+(?:[-+][a-zA-Z0-9.-]+)?)"/m, 'version');
  if ({ linux: 'webkitgtk', windows: 'webview2', macos: 'wkwebview' }[os] !== backend)
    throw new Error('Installed OS/backend mismatch.');
  if (os === 'macos' && !['x64', 'arm64', 'universal2'].includes(architecture) ||
      os !== 'macos' && architecture === 'universal2' || os === 'windows' && architecture === 'arm32')
    throw new Error('Unsupported installed desktop architecture.');
  return { version, os, architecture, backend };
}
function packageDesktop(options, execute = run, source = root) {
  const build = fs.realpathSync(path.resolve(options.build));
  const output = path.resolve(options.output);
  const config = options.configuration;
  if (!configurations.includes(config)) throw new Error('Invalid build configuration.');
  validateCache(build, source, config);
  if (fs.existsSync(output)) throw new Error('Output already exists; choose a new directory.');
  // Non-recursive mkdir reserves a new destination without ever replacing data.
  fs.mkdirSync(output);
  const stage = path.join(output, 'sdk');
  const completed = [];
  try {
    execute('cmake', ['--build', build, '--config', config], source);
    completed.push('build');
    // Building may regenerate CMake. Revalidate the actual cache and required
    // registration rather than accepting an unrelated surviving test as proof.
    validateCache(build, source, config);
    const tested = validateTests(execute('ctest',
      ['--test-dir', build, '-C', config, '--show-only=json-v1'], source, true));
    execute('ctest', ['--test-dir', build, '-C', config, '--output-on-failure', '--no-tests=error'], source);
    completed.push('registered-tests');
    execute('cmake', ['--install', build, '--config', config, '--prefix', stage], source);
    completed.push('install');
    const target = identity(stage);
    if (target.version !== fs.readFileSync(path.join(source, 'VERSION'), 'utf8').trim())
      throw new Error('Installed package version does not match source VERSION.');
    const files = filesIn(stage);
    if (!files.some(file => file.path === 'include/soui/desktop/app.h') ||
        !['desktop', 'runtime'].every(component =>
          files.some(file => file.path === `lib/${config}/${target.os === 'windows' ? '' : 'lib'}soui_${component}.${target.os === 'windows' ? 'lib' : 'a'}`)))
      throw new Error('Installed desktop header or matching configuration static library is missing.');
    const contentHash = hash(JSON.stringify(files));
    const manifest = {
      schema: 1, kind: 'soui-desktop-development-snapshot', ...target,
      configuration: config, contentSha256: contentHash, releaseReady: false,
      validation: { build: 'passed', registeredTests: 'passed', requiredTests: tested,
        nativeGui: 'not-certified', targetRuntime: 'not-certified' },
      files,
    };
    fs.writeFileSync(path.join(stage, 'development-manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`, { flag: 'wx' });
    const name = `soui-desktop-${target.version}-dev-${target.os}-${target.architecture}-${config}-${contentHash.slice(0, 12)}.zip`;
    const archive = path.join(output, name);
    execute('cmake', ['-E', 'tar', 'cf', archive, '--format=zip', 'sdk'], output);
    const archiveHash = hash(fs.readFileSync(archive));
    fs.writeFileSync(`${archive}.sha256`, `${archiveHash}  ${name}\n`, { flag: 'wx' });
    return { archive, sha256: archiveHash, manifest };
  } catch (error) {
    fs.writeFileSync(path.join(output, 'FAILED.json'), `${JSON.stringify({ completed, error: error.message }, null, 2)}\n`);
    throw error;
  }
}
if (require.main === module) {
  try {
    const result = packageDesktop(parseArgs(process.argv.slice(2)));
    console.log(`Development SDK snapshot: ${result.archive}\nSHA-256: ${result.sha256}\nNot a release or native GUI certification.`);
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
module.exports = { parseArgs, readCache, filesIn, identity, validateTests, requiredTests,
  matrixFixture, matrixSetupTests, packageDesktop };
