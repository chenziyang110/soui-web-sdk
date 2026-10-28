'use strict';
// Source metadata only. Never configures, builds, executes fixtures or replaces binaries.
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
function git(args) {
  return cp.execFileSync('git', ['-C', root, ...args], {windowsHide: true, maxBuffer: 32 * 1024 * 1024});
}
function pin(bytes) {
  return {bytes: bytes.length, sha256: crypto.createHash('sha256').update(bytes).digest('hex')};
}
function main() {
  const [base, name, patchName, scope, ...extra] = process.argv.slice(2);
  if (extra.length || !/^[a-f0-9]{40}$/.test(base || '') ||
      !/^w[0-9]{2}(?:-[a-z0-9]+)*$/.test(name || '') ||
      !/^[0-9]{4}-[a-z0-9-]+\.patch$/.test(patchName || '') || !scope || scope.length > 600)
    throw new Error('Usage: input-ui-source-delta.cjs BASE40 wNN-name NNNN-name.patch SCOPE');
  if (git(['rev-parse', `${base}^{commit}`]).toString().trim() !== base) throw new Error('Base must be an exact commit');
  git(['diff', '--quiet', '--', '.', ':(exclude)source-manifest.json']);
  const roots = ['third_party/sources/miniblink', 'third_party/include/miniblink'];
  const files = git(['diff', '--cached', '--name-only', '-z', base, '--', ...roots])
      .toString('utf8').split('\0').filter(Boolean).sort();
  if (!files.length) throw new Error('No staged kernel delta');
  const patchPath = `third_party/patches/miniblink/${patchName}`;
  const manifestPath = `third_party/manifests/kernel-input-ui-${name}-source.json`;
  for (const file of [patchPath, manifestPath])
    if (fs.existsSync(path.join(root, file))) throw new Error(`Refusing to replace historical delta: ${file}`);
  const tree = new Set(git(['ls-tree', '-r', '--name-only', '-z', base, '--', ...roots]).toString('utf8').split('\0'));
  const rows = files.map(file => ({path: file,
    before: tree.has(file) ? pin(git(['show', `${base}:${file}`])) : {absent: true},
    after: pin(git(['show', `:${file}`]))}));
  const patch = git(['diff', '--cached', '--binary', '--no-ext-diff', base, '--', ...roots]);
  const manifest = {schema: 'soui.kernel-source-delta/v1',
    sdkVersion: fs.readFileSync(path.join(root, 'VERSION'), 'utf8').trim(),
    status: 'source-only-unreleased-candidate', scope, baseCommit: base,
    sourceIdentityScope: 'Only listed staged source delta; historical import and prebuilt identities unchanged',
    patch: {path: patchPath, ...pin(patch)}, files: rows,
    validation: {finalSourceCompiled: false, finalTestsExecuted: false,
      reversePatchCheckPerformed: false, forwardReplayExecuted: false},
    candidateBinaryIdentity: null, releaseReady: false};
  fs.writeFileSync(path.join(root, patchPath), patch);
  fs.writeFileSync(path.join(root, manifestPath), JSON.stringify(manifest, null, 2) + '\n');
  const attributes = path.join(root, '.gitattributes');
  fs.appendFileSync(attributes, `\n${patchPath} -text -eol whitespace=cr-at-eol,-blank-at-eof,-blank-at-eol\n`);
  // Check the exact byte patch against the current tree; no replay or execution.
  git(['apply', '--reverse', '--check', '--', patchPath]);
  manifest.validation.reversePatchCheckPerformed = true;
  fs.writeFileSync(path.join(root, manifestPath), JSON.stringify(manifest, null, 2) + '\n');
  process.stdout.write(JSON.stringify({manifestPath, patch: manifest.patch, files: rows.length}) + '\n');
}
try { main(); } catch (error) { process.stderr.write(error.message + '\n'); process.exitCode = 1; }
