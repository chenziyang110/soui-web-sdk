'use strict';

const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const identity = require('./internal/delivery-identity.cjs');

const prefix = 'third_party/sources/soui/';
const [sourceRootArg, outputArg] = process.argv.slice(2);
if (!sourceRootArg || !outputArg) {
  console.error('Usage: node tools/Stage-SouiSource.cjs SOURCE_ROOT OUTPUT_UNDER_ROOT_OUT');
  process.exit(2);
}

const sourceRoot = identity.safePath(path.resolve(sourceRootArg));
const output = identity.safePath(path.resolve(outputArg));
const allowed = path.join(sourceRoot, 'out') + path.sep;
if (!output.toLowerCase().startsWith(allowed.toLowerCase()) || fs.existsSync(output)) {
  throw new Error('SOUI stage must be a new directory under the source out/ directory');
}

let entries;
if (fs.existsSync(path.join(sourceRoot, '.git'))) {
  const names = cp.execFileSync('git', ['-C', sourceRoot, 'ls-files', '--cached', '--others', '--exclude-standard', '-z', '--', 'third_party/sources/soui'],
    {windowsHide: true, maxBuffer: 16 * 1024 * 1024}).toString('utf8');
  entries = [...new Set(names.split('\0').filter(name => name.startsWith(prefix)))].sort().map(name => ({path: name}));
} else {
  const manifest = identity.readJson(identity.safePath(sourceRoot, 'source-manifest.json'));
  if (manifest.schema !== 'soui.full-source/v2') throw new Error('A Git-free SOUI build requires the full-source v2 manifest');
  entries = manifest.files.filter(file => file.path.startsWith(prefix));
}
if (!entries.length) throw new Error('No SOUI source files were found');

fs.mkdirSync(output);
for (const entry of entries) {
  const file = identity.safePath(sourceRoot, entry.path);
  if (!fs.statSync(file).isFile()) throw new Error(`SOUI source entry is not a file: ${entry.path}`);
  const data = fs.readFileSync(file);
  if (entry.sha256) {
    const canonical = data.length === entry.bytes && identity.hash(data) === entry.sha256
      ? data : identity.normalize(data, entry.normalization);
    if (canonical.length !== entry.bytes || identity.hash(canonical) !== entry.sha256) {
      throw new Error(`SOUI source differs from full-source manifest: ${entry.path}`);
    }
  }
  const relative = entry.path.slice(prefix.length);
  const target = identity.safePath(output, relative);
  fs.mkdirSync(path.dirname(target), {recursive: true});
  fs.writeFileSync(target, data, {flag: 'wx'});
}
console.log(output);
