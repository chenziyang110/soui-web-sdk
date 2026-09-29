'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const archive = path.join(root, 'raw');
const manifestFile = path.join(root, 'raw-manifest.json');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function list(directory, prefix = '') {
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
    assert.equal(entry.isSymbolicLink(), false);
    const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
    return entry.isDirectory() ? list(path.join(directory, entry.name), relative) : [relative];
  });
}
function snapshot() {
  return list(archive).sort().map(relative => {
    const bytes = fs.readFileSync(path.join(archive, ...relative.split('/')));
    return {path: relative, bytes: bytes.length, sha256: sha(bytes)};
  });
}
const command = process.argv[2] || 'verify';
assert(['create', 'verify'].includes(command));
const current = {schema: 'soui.template-scrollbar-raw/v1', files: snapshot()};
if (command === 'create') {
  fs.writeFileSync(manifestFile, JSON.stringify(current, null, 2) + '\n', {flag: 'wx'});
} else {
  assert.deepEqual(JSON.parse(fs.readFileSync(manifestFile, 'utf8')), current);
  if (process.argv.includes('--index')) {
    const repo = path.resolve(root, '../../..');
    const prefix = 'verification/production-validation/template-scrollbar-2026-09-30/raw/';
    for (const file of current.files) {
      const result = cp.spawnSync('git', ['show', `:${prefix}${file.path}`],
        {cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024});
      assert.equal(result.status, 0, file.path);
      assert.equal(result.stdout.length, file.bytes, file.path);
      assert.equal(sha(result.stdout), file.sha256, file.path);
    }
  }
  const originalOption = process.argv.indexOf('--originals');
  if (originalOption !== -1) {
    const originalRoot = process.argv[originalOption + 1];
    assert.ok(originalRoot && path.isAbsolute(originalRoot));
    for (const file of current.files) {
      let original;
      const build = /^build-(x64|x86)\/(.+)$/.exec(file.path);
      const smoke = /^smoke-stage-(x64|x86)\/(.+)$/.exec(file.path);
      if (build) original = path.join(originalRoot, 'generated/gen-idle-attempt1/out', build[1], build[2]);
      else if (smoke) original = path.join(originalRoot, 'staged', `${smoke[1]}-smoke-attempt1`, smoke[2]);
      else original = path.join(originalRoot, ...file.path.split('/'));
      const bytes = fs.readFileSync(original);
      assert.equal(bytes.length, file.bytes, file.path);
      assert.equal(sha(bytes), file.sha256, file.path);
    }
  }
}
process.stdout.write(JSON.stringify({command, files: current.files.length, passed: true}) + '\n');
