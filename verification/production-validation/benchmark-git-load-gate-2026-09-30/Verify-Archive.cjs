'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const root = __dirname;
const archive = path.join(root, 'raw');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function list(directory, prefix = '') {
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
    assert(!entry.isSymbolicLink());
    const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.isDirectory()) return list(path.join(directory, entry.name), relative);
    assert(entry.isFile());
    return [relative];
  });
}
const files = list(archive).sort().map(relative => {
  const bytes = fs.readFileSync(path.join(archive, ...relative.split('/')));
  return {path: relative, bytes: bytes.length, sha256: sha(bytes)};
});
const command = process.argv[2] || 'verify';
assert(['create', 'verify'].includes(command));
const current = {schema: 'soui.benchmark-live-load-raw/v1', files};
const manifest = path.join(root, 'raw-manifest.json');
if (command === 'create') fs.writeFileSync(manifest, JSON.stringify(current, null, 2) + '\n', {flag: 'wx'});
else {
  assert.deepEqual(JSON.parse(fs.readFileSync(manifest, 'utf8')), current);
  const originalOption = process.argv.indexOf('--originals');
  const originalRoot = originalOption < 0 ? null : process.argv[originalOption + 1];
  if (originalOption >= 0) assert(originalRoot && path.isAbsolute(originalRoot));
  const repo = path.resolve(root, '../../..');
  const prefix = path.relative(repo, archive).split(path.sep).join('/') + '/';
  for (const file of files) {
    if (originalRoot) {
      const bytes = fs.readFileSync(path.join(originalRoot, ...file.path.split('/')));
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
