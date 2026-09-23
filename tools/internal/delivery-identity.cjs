'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {isUtf8} = require('node:buffer');
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const encoded = value => JSON.stringify(value, null, 2) + '\n';
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const skipped = new Set(['out', 'dist', 'dist-legacy', 'node_modules', '.git', '.vs']);

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
function source(root) {
  const generated = fs.existsSync(safePath(root, 'project.json'));
  const roots = ['sdk', 'cmake', 'tools', 'third_party/include', 'third_party/prebuilt', 'third_party/sources/zlib',
    ...(generated ? ['app', 'web', 'contracts', 'resources'] : ['templates', 'demos', 'tests', 'benchmarks'])];
  const names = roots.filter(name => fs.existsSync(safePath(root, name)))
    .flatMap(name => walk(root, name, (relative, leaf) => skipped.has(leaf) ||
      relative === 'demos/workbench/web/benchmarks/electron-smoothness/source-navigation.log'));
  for (const name of ['VERSION', 'CMakeLists.txt', 'project.json', 'sdk-lock.json']) {
    if (fs.existsSync(safePath(root, name))) names.push(name);
  }
  return snapshot(root, names, true);
}
const diagnostics = new Set(['smoke.log', 'webview-smoke.log', 'source-navigation.log', 'gpu-present-trace.txt']);
function payload(root, target) {
  safePath(root, `${target}.exe`);
  const names = walk(root, '', name => diagnostics.has(name) || name === `${target}.pdb`);
  for (const required of [`${target}.exe`, 'node.dll', 'resources/index.html']) {
    if (!names.includes(required)) throw new Error(`Runtime file missing: ${required}`);
  }
  for (const name of names) {
    if (name !== `${target}.exe` && !/^[^/]+\.dll$/i.test(name) && !name.startsWith('resources/')) {
      throw new Error(`Unknown runtime file (keep application data outside build output): ${name}`);
    }
  }
  return snapshot(root, names);
}
function verifyPayload(root, target, expected) {
  if (!expected || !Array.isArray(expected.files) || hash(encoded(expected.files)) !== expected.sha256) throw new Error('Invalid payload evidence');
  const current = payload(root, target);
  if (current.sha256 !== expected.sha256) throw new Error(`Runtime payload changed: ${target}`);
  return current;
}
module.exports = {hash, encoded, readJson, safePath, walk, skipped, normalization, normalize, validateFiles,
  snapshot, source, payload, verifyPayload};
