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
function run(command, args, cwd) {
  const result = spawnSync(command, args, { cwd, stdio: 'inherit', shell: false });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed (${result.signal || result.status}).`);
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
  const cache = readCache(path.join(build, 'CMakeCache.txt'));
  if (fs.realpathSync(cache.CMAKE_HOME_DIRECTORY || '') !== fs.realpathSync(source))
    throw new Error('Build directory does not belong to this source checkout.');
  if (cache.SOUI_BUILD_DESKTOP !== 'ON' || cache.SOUI_BUILD_TESTS !== 'ON')
    throw new Error('A desktop build with SOUI_BUILD_TESTS=ON is required.');
  if (!cache.CMAKE_CONFIGURATION_TYPES && cache.CMAKE_BUILD_TYPE !== config)
    throw new Error('Requested configuration does not match the single-config build.');
  if (fs.existsSync(output)) throw new Error('Output already exists; choose a new directory.');
  // Non-recursive mkdir reserves a new destination without ever replacing data.
  fs.mkdirSync(output);
  const stage = path.join(output, 'sdk');
  const completed = [];
  try {
    execute('cmake', ['--build', build, '--config', config], source);
    completed.push('build');
    execute('ctest', ['--test-dir', build, '-C', config, '--output-on-failure', '--no-tests=error'], source);
    completed.push('registered-tests');
    execute('cmake', ['--install', build, '--config', config, '--prefix', stage], source);
    completed.push('install');
    const target = identity(stage);
    const files = filesIn(stage);
    if (!files.some(file => file.path === 'include/soui/desktop/app.h') ||
        !files.some(file => /(?:^|\/)(?:lib)?soui_desktop\.(?:a|lib)$/.test(file.path)))
      throw new Error('Installed desktop header or static library is missing.');
    const contentHash = hash(JSON.stringify(files));
    const manifest = {
      schema: 1, kind: 'soui-desktop-development-snapshot', ...target,
      configuration: config, contentSha256: contentHash, releaseReady: false,
      validation: { build: 'passed', registeredTests: 'passed', nativeGui: 'not-certified', targetRuntime: 'not-certified' },
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
module.exports = { parseArgs, readCache, filesIn, identity, packageDesktop };
