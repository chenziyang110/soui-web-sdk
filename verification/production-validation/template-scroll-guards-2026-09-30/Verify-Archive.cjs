'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const root = __dirname;
const raw = path.join(root, 'raw');
const manifestFile = path.join(root, 'raw-manifest.json');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function list(directory, prefix = '') {
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
    assert(!entry.isSymbolicLink(), entry.name);
    const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.isDirectory()) return list(path.join(directory, entry.name), relative);
    assert(entry.isFile(), relative);
    return [relative];
  });
}
const files = list(raw).sort().map(relative => {
  const bytes = fs.readFileSync(path.join(raw, ...relative.split('/')));
  return {path: relative, bytes: bytes.length, sha256: sha(bytes)};
});
const command = process.argv[2] || 'verify';
assert(['create', 'verify'].includes(command));
const current = {schema: 'soui.template-scroll-guards-raw/v1', files};
if (command === 'create') {
  fs.writeFileSync(manifestFile, JSON.stringify(current, null, 2) + '\n', {flag: 'wx'});
} else {
  assert.deepEqual(JSON.parse(fs.readFileSync(manifestFile, 'utf8')), current);
  const originals = {};
  for (const group of ['template', 'capture']) {
    const option = process.argv.indexOf(`--${group}-originals`);
    if (option >= 0) {
      originals[group] = process.argv[option + 1];
      assert(originals[group] && path.isAbsolute(originals[group]));
    }
  }
  const repo = path.resolve(root, '../../..');
  const prefix = path.relative(repo, raw).split(path.sep).join('/') + '/';
  for (const file of files) {
    const [group, ...relative] = file.path.split('/');
    assert(['template', 'capture'].includes(group), file.path);
    if (originals[group]) {
      const bytes = fs.readFileSync(path.join(originals[group], ...relative));
      assert.equal(bytes.length, file.bytes, file.path);
      assert.equal(sha(bytes), file.sha256, file.path);
    }
    if (process.argv.includes('--index')) {
      const result = cp.spawnSync('git', ['show', `:${prefix}${file.path}`],
        {cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024});
      assert.equal(result.status, 0, file.path);
      assert.equal(result.stdout.length, file.bytes, file.path);
      assert.equal(sha(result.stdout), file.sha256, file.path);
    }
  }
}
console.log(JSON.stringify({command, files: files.length, passed: true}));
