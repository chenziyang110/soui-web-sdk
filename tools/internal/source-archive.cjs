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
    names = id.walk(root, '', (name, leaf) => name === manifestName || id.skipped.has(leaf)).sort();
  }
  if (JSON.stringify(names) !== JSON.stringify(expected)) throw new Error('Source file set differs from manifest');
}
function create(root) {
  const entries = tracked(root);
  if (!entries || !entries.length) throw new Error('Manifest generation requires a Git checkout with added source files');
  checkFileSet(root, entries);
  const files = entries.map(file => {
    const data = id.normalize(fs.readFileSync(id.safePath(root, file.path)), file.normalization);
    return {...file, bytes: data.length, sha256: id.hash(data)};
  });
  id.validateFiles(files);
  const manifest = {schema, version: fs.readFileSync(path.join(root, 'VERSION'), 'utf8').trim(), releaseReady: false,
    identity: 'Tracked source; text CRLF normalized to LF; binaries unchanged.', files};
  fs.writeFileSync(path.join(root, manifestName), id.encoded(manifest));
  return manifest;
}
function verify(root) {
  const manifest = id.readJson(id.safePath(root, manifestName));
  if (![schema, 'soui.full-source/v1'].includes(manifest.schema) || manifest.releaseReady !== false ||
      manifest.version !== fs.readFileSync(id.safePath(root, 'VERSION'), 'utf8').trim()) throw new Error('Invalid source manifest/version');
  id.validateFiles(manifest.files);
  if (manifest.schema === schema) checkFileSet(root, manifest.files);
  for (const file of manifest.files) {
    const full = id.safePath(root, file.path);
    if (!fs.existsSync(full)) throw new Error(`Missing source file: ${file.path}`);
    const data = id.normalize(fs.readFileSync(full), manifest.schema === schema ? file.normalization : 'none');
    if (data.length !== file.bytes || id.hash(data) !== file.sha256) throw new Error(`Source missing or modified: ${file.path}`);
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
    const data = id.normalize(fs.readFileSync(id.safePath(root, file.path)), file.normalization);
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
