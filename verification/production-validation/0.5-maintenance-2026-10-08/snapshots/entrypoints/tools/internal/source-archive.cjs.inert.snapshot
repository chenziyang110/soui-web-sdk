'use strict';
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const id = require('./delivery-identity.cjs');
const manifestName = 'source-manifest.json';
const schema = 'soui.full-source/v2';
function git(root, args) {
  return cp.execFileSync('git', ['-C', root, ...args], {encoding: 'utf8', windowsHide: true, maxBuffer: 32 * 1024 * 1024});
}
function tracked(root) {
  if (!fs.existsSync(path.join(root, '.git'))) return null;
  return git(root, ['ls-files', '--eol', '-z']).split('\0').filter(Boolean).map(row => {
    const tab = row.indexOf('\t');
    const flags = row.slice(0, tab);
    return {path: row.slice(tab + 1), normalization: !/attr\/-text(?:\s|$)/.test(flags) && /^i\/(lf|crlf|mixed)\s/.test(flags) ? 'lf' : 'none'};
  }).filter(file => file.path !== manifestName).sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
}
function unpackedFiles(root, files) {
  const frozenCaches = new Set();
  for (const file of files) {
    let prefix = '';
    for (const part of file.path.split('/')) {
      prefix = prefix ? `${prefix}/${part}` : part;
      if (id.skipped.has(part)) frozenCaches.add(prefix);
    }
  }
  return id.walk(root, '', (name, leaf) => {
    if (name === manifestName) return true;
    if (!id.skipped.has(leaf)) return false;
    // Git can track immutable evidence under output-like names. Scan that
    // entire subtree so undeclared descendants cannot hide as nested caches.
    for (let ancestor = name; ancestor;) {
      if (frozenCaches.has(ancestor)) return false;
      const slash = ancestor.lastIndexOf('/');
      ancestor = slash < 0 ? '' : ancestor.slice(0, slash);
    }
    id.safePath(root, name); // An excluded cache entry must not be a link.
    return true;
  });
}
function checkFileSet(root, files) {
  const entries = tracked(root);
  const expected = files.map(f => f.path).sort();
  let names;
  if (entries) {
    names = entries.map(f => f.path).sort();
    const extra = git(root, ['ls-files', '--others', '--exclude-standard', '-z']).split('\0')
      .filter(name => name && name !== manifestName);
    if (extra.length) throw new Error(`Untracked source files: ${extra.slice(0, 5).join(', ')}`);
  } else {
    names = unpackedFiles(root, files).sort();
  }
  if (JSON.stringify(names) !== JSON.stringify(expected)) throw new Error('Source file set differs from manifest');
}
function create(root) {
  const entries = tracked(root);
  if (!entries || !entries.length) throw new Error('Manifest generation requires a Git checkout with added source files');
  checkFileSet(root, entries);
  git(root, ['diff', '--quiet', '--', '.', ':(exclude)source-manifest.json']);
  const index = new Map(git(root, ['ls-files', '--stage', '-z']).split('\0').filter(Boolean).map(row => {
    const [mode, oid, stage] = row.slice(0, row.indexOf('\t')).split(' ');
    if (!['100644', '100755'].includes(mode) || stage !== '0') throw new Error('Linked, nested or unmerged source entry');
    return [row.slice(row.indexOf('\t') + 1), oid];
  }));
  const oids = entries.map(file => index.get(file.path));
  const sizes = cp.execFileSync('git', ['-C', root, 'cat-file', '--batch-check'],
    {input: oids.join('\n') + '\n', encoding: 'utf8', maxBuffer: 16 * 1024 * 1024, windowsHide: true})
    .trim().split('\n').map(row => Number(row.split(' ')[2]));
  const files = [];
  // Read canonical index blobs in bounded batches instead of normalizing already-normalized text twice.
  for (let start = 0; start < entries.length;) {
    let end = start, bytes = 0;
    do { bytes += sizes[end++]; } while (end < entries.length && bytes + sizes[end] < 8 * 1024 * 1024 && end - start < 512);
    const data = cp.execFileSync('git', ['-C', root, 'cat-file', '--batch'], {input: oids.slice(start, end).join('\n') + '\n',
      maxBuffer: bytes + (end - start) * 128 + 1024, windowsHide: true});
    let offset = 0;
    for (let i = start; i < end; ++i) {
      const newline = data.indexOf(10, offset);
      const header = data.subarray(offset, newline).toString('ascii').split(' ');
      if (header[0] !== oids[i] || header[1] !== 'blob' || Number(header[2]) !== sizes[i]) throw new Error('Unexpected Git source blob');
      offset = newline + 1;
      files.push({...entries[i], bytes: sizes[i], sha256: id.hash(data.subarray(offset, offset + sizes[i]))});
      offset += sizes[i] + 1;
    }
    start = end;
  }
  id.validateFiles(files);
  const manifest = {schema, version: fs.readFileSync(path.join(root, 'VERSION'), 'utf8').trim(), releaseReady: false,
    identity: 'Git index blob bytes; checkout CRLF tolerated for text; binaries unchanged.', files};
  fs.writeFileSync(path.join(root, manifestName), id.encoded(manifest));
  return manifest;
}
function content(root, file, format) {
  const full = id.safePath(root, file.path);
  if (!fs.existsSync(full)) throw new Error(`Missing source file: ${file.path}`);
  const raw = fs.readFileSync(full);
  const matches = data => data.length === file.bytes && id.hash(data) === file.sha256;
  // Exact canonical blobs can legitimately contain mixed line endings; preserve them as-is.
  if (matches(raw)) return raw;
  const normalized = id.normalize(raw, format === schema ? file.normalization : 'none');
  if (!matches(normalized)) throw new Error(`Source missing or modified: ${file.path}`);
  return normalized;
}
function verify(root) {
  const manifest = id.readJson(id.safePath(root, manifestName));
  if (![schema, 'soui.full-source/v1'].includes(manifest.schema) || manifest.releaseReady !== false ||
      manifest.version !== fs.readFileSync(id.safePath(root, 'VERSION'), 'utf8').trim()) throw new Error('Invalid source manifest/version');
  id.validateFiles(manifest.files);
  if (manifest.schema === schema) checkFileSet(root, manifest.files);
  for (const file of manifest.files) {
    if (manifest.schema === schema && !['lf', 'none'].includes(file.normalization)) throw new Error('Invalid source normalization');
    content(root, file, manifest.schema);
  }
  return manifest;
}
function stage(root, output) {
  const manifest = verify(root);
  if (manifest.schema !== schema) throw new Error('New source archives require a v2 manifest');
  output = id.safePath(output);
  if (fs.existsSync(output)) throw new Error('Source stage already exists');
  for (const file of manifest.files) {
    const target = id.safePath(output, file.path);
    const data = content(root, file, manifest.schema);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.writeFileSync(target, data, {flag: 'wx'});
  }
  fs.writeFileSync(path.join(output, manifestName), id.encoded(manifest), {flag: 'wx'});
  verify(output);
  return {stage: output, files: manifest.files.length, version: manifest.version, releaseReady: false};
}
module.exports = {create, verify, stage};
if (require.main === module) {
  try {
    const [command = 'create', output] = process.argv.slice(2);
    const root = path.resolve(__dirname, '../..');
    if (!['create', 'verify', 'stage'].includes(command) || (command === 'stage' && !output)) throw new Error('Usage: source-archive.cjs create|verify|stage OUTPUT');
    const result = command === 'stage' ? stage(root, output) : (command === 'verify' ? verify(root) : create(root));
    console.log(id.encoded({status: command, version: result.version, files: Array.isArray(result.files) ? result.files.length : result.files, releaseReady: false}));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
