'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {isUtf8} = require('node:buffer');
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const encoded = value => JSON.stringify(value, null, 2) + '\n';
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const skipped = new Set(['out', 'dist', 'dist-legacy', 'dist-react', 'dist-vue', 'node_modules', '.git', '.vs']);

function safePath(root, name = '') {
  if (name && (typeof name !== 'string' || name.includes('\\') || name.includes(':') ||
      name.split('/').some(p => !p || p === '.' || p === '..' || /[<>"|?*\x00-\x1f]|[. ]$/.test(p) ||
        /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(p)))) throw new Error(`Invalid path: ${name}`);
  const full = path.resolve(root, name);
  let cursor = full;
  while (true) {
    try {
      if (fs.lstatSync(cursor).isSymbolicLink()) throw new Error(`Linked path: ${cursor}`);
    } catch (error) { if (error.code !== 'ENOENT') throw error; }
    const parent = path.dirname(cursor);
    if (parent === cursor) break;
    cursor = parent;
  }
  return full;
}

function walk(root, relative = '', exclude = () => false) {
  const full = safePath(root, relative);
  if (fs.statSync(full).isFile()) return [relative];
  return fs.readdirSync(full).sort().flatMap(name => {
    const next = relative ? `${relative}/${name}` : name;
    return exclude(next, name) ? [] : walk(root, next, exclude);
  });
}
function normalization(name, data) {
  if (name === 'sdk/resources/devtools/miniblink/baseline-manifest.json' ||
      name.startsWith('sdk/resources/devtools/miniblink/baseline/')) return 'none';
  if (name === 'LICENSE.md' || name.startsWith('third_party/licenses/')) return 'none';
  if (/\.(dll|lib|exe|pdb|png|jpe?g|gif|ico|zip|7z|pdf|woff2?|ttf|mp[34]|wav|bin)$/i.test(name)) return 'none';
  const legacyText = /\.(c|cc|cpp|cxx|h|hpp|hxx|inl|inc|tlh|idl|def|rc|cmake|txt|props|targets|vcxproj|filters|xml|sln|cmd|bat|ps1)$/i.test(name);
  return !data.includes(0) && (legacyText || isUtf8(data)) ? 'lf' : 'none';
}
function normalize(data, mode) {
  if (mode === 'none') return data;
  if (mode !== 'lf') throw new Error('Invalid source normalization');
  // Latin-1 preserves all other bytes, including legacy source encodings.
  return Buffer.from(data.toString('latin1').replace(/\r\n/g, '\n'), 'latin1');
}
function validateFiles(files) {
  if (!Array.isArray(files) || !files.length) throw new Error('Missing file manifest');
  const seen = new Set();
  for (const file of files) {
    safePath(process.cwd(), file.path);
    if (!file.path || seen.has(file.path.toLowerCase()) || !Number.isSafeInteger(file.bytes) || file.bytes < 0 ||
        !/^[a-f0-9]{64}$/.test(file.sha256)) throw new Error(`Invalid or duplicate file: ${file.path}`);
    seen.add(file.path.toLowerCase());
  }
}
function snapshot(root, names, source = false) {
  const files = names.sort().map(name => {
    const raw = fs.readFileSync(safePath(root, name));
    const mode = source ? normalization(name, raw) : 'none';
    const data = normalize(raw, mode);
    return {path: name, bytes: data.length, sha256: hash(data), ...(source ? {normalization: mode} : {})};
  });
  validateFiles(files);
  return {sha256: hash(encoded(files)), bytes: files.reduce((n, f) => n + f.bytes, 0), files};
}
function excludeSource(relative, leaf) {
  return skipped.has(leaf) ||
    relative === 'demos/workbench/web/benchmarks/electron-smoothness/source-navigation.log';
}
function sourceInventory(root) {
  const generated = fs.existsSync(safePath(root, 'project.json'));
  const roots = ['sdk', 'cmake', 'tools', 'third_party/include', 'third_party/prebuilt', 'third_party/sources/zlib',
    'third_party/licenses',
    ...(generated ? ['app', 'web', 'contracts', 'resources'] : ['templates', 'demos', 'tests', 'benchmarks'])];
  const inventoryRoots = roots.filter(name => fs.existsSync(safePath(root, name)));
  const names = inventoryRoots.flatMap(name => walk(root, name, excludeSource));
  for (const name of ['VERSION', 'CMakeLists.txt', 'LICENSE.md', 'THIRD_PARTY_NOTICES.md', 'project.json', 'sdk-lock.json']) {
    if (fs.existsSync(safePath(root, name))) { names.push(name); inventoryRoots.push(name); }
  }
  return {names, inventoryRoots};
}
function source(root) {
  return snapshot(root, sourceInventory(root).names, true);
}
function sourceSnapshot(root) {
  root = safePath(root);
  const inventory = sourceInventory(root);
  return {...snapshot(root, inventory.names, true), root,
    inventoryMode: 'sdk-source', inventoryRoots: inventory.inventoryRoots};
}
const diagnostics = new Set(['smoke.log', 'webview-smoke.log', 'source-navigation.log', 'gpu-present-trace.txt']);
function validateApplicationFiles(files = [], expectedFiles) {
  if (!Array.isArray(files) || files.length > 64) throw new Error('Application file count exceeds 64 rows');
  if (!files.length) return [];
  validateFiles(files);
  let bytes = 0;
  const rows = files.map(file => {
    if (Object.keys(file).sort().join(',') !== 'bytes,path,sha256') throw new Error('Application files require exact path/bytes/sha256 rows');
    const name = file.path.toLowerCase();
    if (name === 'resources' || name.startsWith('resources/') || name === 'devtools' || name.startsWith('devtools/') ||
        /\.(exe|dll)$/i.test(name) || name.split('/').at(-1) === '.soui-runtime.json')
      throw new Error('Application files cannot authorize resources, DevTools or native runtime files');
    bytes += file.bytes;
    if (bytes > 16 * 1024 * 1024) throw new Error('Application file bytes exceed 16 MiB');
    return {path: file.path, bytes: file.bytes, sha256: file.sha256};
  }).sort((left, right) => left.path < right.path ? -1 : left.path > right.path ? 1 : 0);
  if (expectedFiles !== undefined) {
    validateFiles(expectedFiles);
    const expected = new Map(expectedFiles.map(file => [file.path, file]));
    for (const row of rows) {
      const file = expected.get(row.path);
      if (!file || file.bytes !== row.bytes || file.sha256 !== row.sha256) throw new Error('Application file descriptor is not an exact payload subset');
    }
  }
  return rows;
}
function payload(root, target, backend = 'miniblink', applicationFiles = []) {
  if (!['miniblink', 'webview2', 'auto'].includes(backend)) throw new Error('Unknown renderer backend');
  safePath(root, `${target}.exe`);
  const applicationRows = validateApplicationFiles(applicationFiles);
  const applicationNames = new Set(applicationRows.map(file => file.path));
  for (const file of applicationRows) {
    const status = fs.lstatSync(safePath(root, file.path));
    if (!status.isFile() || status.nlink !== 1 || status.size !== file.bytes) throw new Error('Application file is not a plain exact-size file');
  }
  const names = walk(root, '', name => diagnostics.has(name) || name === `${target}.pdb`);
  for (const required of [`${target}.exe`, ...(backend === 'auto' ?
    ['resources/web-modern/index.html', 'resources/web-compat/index.html', '.soui-runtime.json'] : ['resources/index.html']),
    ...(['miniblink', 'auto'].includes(backend) ? ['node.dll'] : [])]) {
    if (!names.includes(required)) throw new Error(`Runtime file missing: ${required}`);
  }
  if (backend === 'auto') require('./deploy-resources.cjs').verifyAutoDirectory(root);
  let devtools;
  if (names.some(name => /^devtools\//i.test(name))) {
    if (backend !== 'miniblink') throw new Error('Offline MiniBlink DevTools package cannot belong to another renderer');
    // Lazy import: the package validator also uses this module's path/hash
    // primitives. Validation proves payload consistency, never permission.
    devtools = require('./devtools-package.cjs').verifyDelivery(path.resolve(root));
  }
  const approved = new Set(devtools ? devtools.paths : []);
  let runtime, runtimeReceiptSha256;
  if (names.includes('.soui-runtime.json')) {
    runtimeReceiptSha256 = hash(fs.readFileSync(safePath(root, '.soui-runtime.json')));
    runtime = require('./runtime-deployment.cjs').verifyDirectory(path.resolve(root));
    if (runtime.backend !== backend) throw new Error('Runtime deployment belongs to another renderer');
  }
  for (const name of names) {
    if (name !== `${target}.exe` && name !== '.soui-runtime.json' && !/^[^/]+\.dll$/i.test(name) && !name.startsWith('resources/') && !approved.has(name) && !applicationNames.has(name)) {
      throw new Error(`Unknown runtime file (keep application data outside build output): ${name}`);
    }
  }
  const result = snapshot(root, names);
  if (applicationRows.length) {
    validateApplicationFiles(applicationRows, result.files);
    result.applicationFiles = applicationRows;
  }
  if (runtime) {
    const files = new Map(result.files.map(file => [file.path, file]));
    for (const expected of runtime.files) {
      const current = files.get(expected.path);
      if (!current || current.bytes !== expected.bytes || current.sha256 !== expected.sha256)
        throw new Error('Runtime deployment changed during payload capture');
    }
    if (files.get('.soui-runtime.json')?.sha256 !== runtimeReceiptSha256)
      throw new Error('Runtime receipt changed during payload capture');
  }
  if (devtools) {
    const files = new Map(result.files.map(file => [file.path, file]));
    for (const verified of [...devtools.files, {path: 'node.dll', sha256: devtools.metadata.kernel.sha256}]) {
      const current = files.get(verified.path);
      if (!current || current.sha256 !== verified.sha256 ||
          (verified.bytes !== undefined && current.bytes !== verified.bytes))
        throw new Error('DevTools package changed during payload capture');
    }
  }
  return result;
}
function verifyPayload(root, target, expected, backend = 'miniblink') {
  if (!expected || !Array.isArray(expected.files) || hash(encoded(expected.files)) !== expected.sha256) throw new Error('Invalid payload evidence');
  const applicationFiles = validateApplicationFiles(expected.applicationFiles === undefined ? [] : expected.applicationFiles, expected.files);
  const current = payload(root, target, backend, applicationFiles);
  if (current.sha256 !== expected.sha256) throw new Error(`Runtime payload changed: ${target}`);
  return current;
}
module.exports = {hash, encoded, readJson, safePath, walk, skipped, normalization, normalize, validateFiles, validateApplicationFiles,
  snapshot, excludeSource, sourceInventory, source, sourceSnapshot, payload, verifyPayload};
