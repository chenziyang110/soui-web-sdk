'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {createRequire} = require('node:module');
const {isUtf8} = require('node:buffer');

const receiptName = '.soui-compat-polyfills.json';
const upstream = {bytes: 3137, sha256: 'd953e1f674fd17fbc5c2f71dd196f12c0d225c583dcfad22ace48b2855509b74'};
const replacement = {bytes: 356, sha256: '78415ccff6be150c98035b73f8c7d2277cb6cf263b35aaa26ed1cc1dd91a7ead'};
const need = (condition, code) => { if (!condition) throw new Error(code); };
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const same = (left, right) => ['dev', 'ino', 'size', 'mtimeMs', 'ctimeMs', 'nlink'].every(key => left[key] === right[key]);

function inside(root, file) {
  const relative = path.relative(root, file);
  need(relative && !relative.startsWith('..' + path.sep) && relative !== '..' && !path.isAbsolute(relative),
    'E_COMPAT_DEPENDENCY_OUTSIDE_PROJECT');
}

// Check every ancestor, including the project and its parents. Node represents
// Windows junctions as symbolic links; realpath also rejects redirected aliases.
function plainPath(file, directory = false) {
  const full = path.resolve(file);
  let cursor = full;
  while (true) {
    const stat = fs.lstatSync(cursor);
    need(!stat.isSymbolicLink(), 'E_COMPAT_LINKED_PATH');
    if (cursor === full) need(directory ? stat.isDirectory() : stat.isFile() && stat.nlink === 1,
      'E_COMPAT_REGULAR_FILE');
    else need(stat.isDirectory(), 'E_COMPAT_DIRECTORY');
    const parent = path.dirname(cursor);
    if (parent === cursor) break;
    cursor = parent;
  }
  need(path.relative(full, fs.realpathSync.native(full)) === '', 'E_COMPAT_LINKED_PATH');
  return full;
}

function read(file, limit = 16384) {
  const full = plainPath(file);
  const descriptor = fs.openSync(full, 'r');
  try {
    const before = fs.fstatSync(descriptor);
    need(before.isFile() && before.nlink === 1 && before.size <= limit, 'E_COMPAT_FILE_BUDGET');
    const bytes = Buffer.alloc(before.size + 1);
    let offset = 0, count;
    while (offset < bytes.length && (count = fs.readSync(descriptor, bytes, offset, bytes.length - offset, null))) offset += count;
    need(offset === before.size && same(before, fs.fstatSync(descriptor)) && same(before, fs.lstatSync(full)),
      'E_COMPAT_FILE_CHANGED');
    return bytes.subarray(0, offset);
  } finally { fs.closeSync(descriptor); }
}

function packageInfo(file) {
  const bytes = read(file);
  need(isUtf8(bytes), 'E_COMPAT_PACKAGE_JSON');
  let value;
  try { value = JSON.parse(bytes.toString('utf8')); } catch { throw new Error('E_COMPAT_PACKAGE_JSON'); }
  need(value && typeof value === 'object' && !Array.isArray(value), 'E_COMPAT_PACKAGE_JSON');
  return value;
}

function ownedPackage(requester, name, modules) {
  // require.resolve canonicalizes symlinks. Inspect the first package candidate
  // in the resolver's own search order before that canonicalization can hide a
  // junction pointing elsewhere, including elsewhere in this node_modules.
  const search = requester.resolve.paths(name);
  need(Array.isArray(search), 'E_COMPAT_DEPENDENCY_RESOLUTION');
  for (const directory of search) {
    const candidate = path.join(directory, name);
    try { fs.lstatSync(candidate); }
    catch (error) { if (['ENOENT', 'ENOTDIR'].includes(error.code)) continue; throw error; }
    inside(modules, candidate);
    return plainPath(candidate, true);
  }
  throw new Error('E_COMPAT_DEPENDENCY_MISSING; run npm ci in this Web project to restore its locked dependencies');
}

function resolveDependencies(webRoot) {
  need(typeof webRoot === 'string' && path.isAbsolute(webRoot), 'E_COMPAT_PROJECT_ROOT');
  const root = plainPath(webRoot, true);
  const modules = plainPath(path.join(root, 'node_modules'), true);
  const localRequire = createRequire(path.join(root, 'package.json'));
  const pluginRoot = ownedPackage(localRequire, '@vitejs/plugin-legacy', modules);
  const plugin = packageInfo(path.join(pluginRoot, 'package.json'));
  need(plugin.name === '@vitejs/plugin-legacy' && plugin.version === '6.1.1', 'E_COMPAT_PLUGIN_VERSION');
  need(plugin.exports?.['.']?.import === './dist/index.mjs' && plugin.exports['.'].require === './dist/index.cjs',
    'E_COMPAT_PLUGIN_ENTRY');
  const entry = localRequire.resolve('@vitejs/plugin-legacy');
  need(entry === path.join(pluginRoot, 'dist/index.cjs'), 'E_COMPAT_PLUGIN_ENTRY');
  plainPath(entry);
  const importEntry = plainPath(path.join(pluginRoot, 'dist/index.mjs'));
  // plugin-legacy starts a separate Vite build rooted beside this ESM entry.
  // A parent build's resolve.alias cannot replace its CommonJS core-js module.
  const pluginRequire = createRequire(importEntry);
  const coreRoot = ownedPackage(pluginRequire, 'core-js', modules);
  const corePackage = pluginRequire.resolve('core-js/package.json');
  const module = pluginRequire.resolve('core-js/internals/object-create.js');
  inside(modules, corePackage);
  inside(modules, module);
  need(corePackage === path.join(coreRoot, 'package.json'), 'E_COMPAT_MODULE_PATH');
  const core = packageInfo(corePackage);
  need(core.name === 'core-js' && core.version === '3.50.0', 'E_COMPAT_CORE_JS_VERSION');
  need(path.relative(path.dirname(corePackage), module) === path.join('internals', 'object-create.js'),
    'E_COMPAT_MODULE_PATH');
  plainPath(module);
  const systemRoot = ownedPackage(pluginRequire, 'systemjs', modules);
  const systemPackage = pluginRequire.resolve('systemjs/package.json');
  const systemEntry = pluginRequire.resolve('systemjs/dist/s.min.js');
  need(systemPackage === path.join(systemRoot, 'package.json') && systemEntry === path.join(systemRoot, 'dist/s.min.js'),
    'E_COMPAT_SYSTEM_JS_PATH');
  const system = packageInfo(systemPackage);
  need(system.name === 'systemjs' && system.version === '6.15.1', 'E_COMPAT_SYSTEM_JS_VERSION');
  const systemBytes = read(systemEntry, 128 * 1024);
  return {module, systemJs: {version: system.version, file: 'dist/s.min.js', bytes: systemBytes.length, sha256: hash(systemBytes)}};
}

function replacementBytes() {
  const bytes = read(path.join(__dirname, 'polyfills/object-create.chrome49.cjs'));
  need(isUtf8(bytes), 'E_COMPAT_REPLACEMENT_IDENTITY');
  // Git may check this controlled source out with CRLF. The dependency always
  // receives the same LF bytes, so npm and direct Vite builds have one identity.
  const normalized = Buffer.from(bytes.toString('utf8').replace(/\r\n/g, '\n'));
  need(normalized.length === replacement.bytes && hash(normalized) === replacement.sha256,
    'E_COMPAT_REPLACEMENT_IDENTITY');
  return normalized;
}

function adapt(module, bytes) {
  plainPath(module);
  const descriptor = fs.openSync(module, 'r+');
  try {
    const before = fs.fstatSync(descriptor);
    need(before.isFile() && before.nlink === 1 && before.size <= upstream.bytes,
      'E_COMPAT_MODULE_IDENTITY; run npm ci in this Web project to restore its locked dependencies');
    const current = Buffer.alloc(before.size + 1);
    let offset = 0, count;
    while (offset < current.length && (count = fs.readSync(descriptor, current, offset, current.length - offset, null))) offset += count;
    need(offset === before.size && same(before, fs.fstatSync(descriptor)) && same(before, fs.lstatSync(module)),
      'E_COMPAT_FILE_CHANGED');
    const digest = hash(current.subarray(0, offset));
    if (offset === replacement.bytes && digest === replacement.sha256) return;
    need(offset === upstream.bytes && digest === upstream.sha256,
      'E_COMPAT_MODULE_IDENTITY; run npm ci in this Web project to restore its locked dependencies');
    offset = 0;
    while (offset < bytes.length) {
      count = fs.writeSync(descriptor, bytes, offset, bytes.length - offset, offset);
      need(count > 0, 'E_COMPAT_MODULE_WRITE');
      offset += count;
    }
    fs.ftruncateSync(descriptor, bytes.length);
    fs.fsyncSync(descriptor);
  } finally { fs.closeSync(descriptor); }
  const actual = read(module);
  need(actual.length === replacement.bytes && hash(actual) === replacement.sha256, 'E_COMPAT_MODULE_WRITE');
}

function prepareCompatibilityPolyfills(webRoot) {
  const dependency = resolveDependencies(webRoot);
  adapt(dependency.module, replacementBytes());
  const receipt = {
    schema: 'soui.compat-polyfills/v1', target: 'Chrome 49',
    pluginLegacy: {version: '6.1.1'}, coreJs: {version: '3.50.0'}, systemJs: dependency.systemJs,
    adaptation: {module: 'core-js/internals/object-create.js', upstream, replacement,
      requires: 'native Object.create'},
  };
  const plugin = {name: 'soui-compat-polyfills', apply: 'build', enforce: 'post',
    buildStart() {
      const current = resolveDependencies(webRoot);
      need(JSON.stringify(current.systemJs) === JSON.stringify(dependency.systemJs), 'E_COMPAT_SYSTEM_JS_CHANGED');
      const actual = read(current.module);
      need(actual.length === replacement.bytes && hash(actual) === replacement.sha256, 'E_COMPAT_MODULE_IDENTITY');
    },
    generateBundle(_options, bundle) {
      need(!Object.keys(bundle).some(name => name.toLowerCase() === receiptName), 'E_COMPAT_RECEIPT_COLLISION');
      this.emitFile({type: 'asset', fileName: receiptName, source: JSON.stringify(receipt, null, 2) + '\n'});
    },
  };
  return {receipt, plugin};
}

module.exports = {prepareCompatibilityPolyfills, receiptName};
