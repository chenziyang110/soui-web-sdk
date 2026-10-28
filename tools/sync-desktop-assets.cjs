#!/usr/bin/env node
'use strict';

// Trusted immutable build inputs only; not a sandbox against hostile filesystem races.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const schema = 'soui.desktop.managed-assets/1';
const maximumFileBytes = 256 * 1024 * 1024;
const maximumTotalBytes = 1024 * 1024 * 1024;
const maximumManifestBytes = 64 * 1024 * 1024;
const validationFailures = new WeakSet();
const filesystemCodes = new Set([
  'EACCES', 'EAGAIN', 'EBADF', 'EBUSY', 'EEXIST', 'EFAULT', 'EFBIG', 'EINTR',
  'EINVAL', 'EIO', 'EISDIR', 'ELOOP', 'EMFILE', 'ENAMETOOLONG', 'ENFILE',
  'ENODEV', 'ENOENT', 'ENOMEM', 'ENOSPC', 'ENOSYS', 'ENOTDIR', 'ENOTEMPTY',
  'ENOTSUP', 'ENXIO', 'EOVERFLOW', 'EPERM', 'EROFS', 'ETXTBSY', 'EXDEV',
  'ERR_INVALID_ARG_TYPE', 'ERR_INVALID_ARG_VALUE', 'ERR_OUT_OF_RANGE',
]);
const filesystemCalls = new Set([
  'open', 'read', 'write', 'close', 'mkdir', 'mkdtemp', 'rmdir', 'rename',
  'unlink', 'lstat', 'stat', 'scandir', 'readdir', 'realpath',
]);

function fail(message) {
  const error = new Error(message);
  validationFailures.add(error);
  throw error;
}
function ownValue(error, name) {
  try {
    const field = Object.getOwnPropertyDescriptor(error, name);
    return field && Object.hasOwn(field, 'value') ? field.value : undefined;
  } catch { return undefined; }
}
function syncFailure(error, syncPhase, workspace, fixedMessage) {
  const code = ownValue(error, 'code');
  const syscall = ownValue(error, 'syscall');
  const errno = ownValue(error, 'errno');
  const safeCode = filesystemCodes.has(code) ? code : undefined;
  // Only locally authored validation messages are safe to keep verbatim.
  const reason = validationFailures.has(error) ? ownValue(error, 'message') :
    safeCode ? `Filesystem operation failed (${safeCode}).` : 'Filesystem operation failed.';
  const failure = new Error(fixedMessage === undefined ?
    `${reason}${workspace ? ` Generated workspace ${path.basename(workspace)} retained; stop builds and inspect it for manual recovery.` : ''}` : fixedMessage);
  failure.syncPhase = syncPhase; // Internal fixed literals, never error-supplied input.
  if (safeCode !== undefined) failure.code = safeCode;
  if (filesystemCalls.has(syscall)) failure.syscall = syscall;
  if (Number.isInteger(errno) && errno >= -2147483648 && errno <= 2147483647) failure.errno = errno;
  // Do not copy native message, path/dest, stack, cause, or arbitrary properties.
  return failure;
}
function portable(name) {
  return name.length > 0 && Buffer.byteLength(name) <= 240 &&
    !/[<>:"/\\|?*\x00-\x1f\x7f-\x9f]/.test(name) && !/[. ]$/.test(name) &&
    name !== '.' && name !== '..' && name === name.normalize('NFC') &&
    !/^(?:con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³])(?:\.|$)/i.test(name);
}
function statIfPresent(file) {
  try { return fs.lstatSync(file); } catch (error) {
    if (error.code === 'ENOENT') return null;
    throw error;
  }
}
function directory(input) {
  const absolute = path.resolve(input);
  let current = path.parse(absolute).root;
  for (const segment of absolute.slice(current.length).split(path.sep).filter(Boolean)) {
    current = path.join(current, segment);
    const stat = fs.lstatSync(current);
    if (stat.isSymbolicLink() || !stat.isDirectory()) fail('Directory ancestors must be ordinary directories.');
  }
  return fs.realpathSync(absolute);
}
function within(parent, child) {
  const relative = path.relative(parent, child);
  return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
}
function hashFile(file, size) {
  const hash = crypto.createHash('sha256');
  const fd = fs.openSync(file, 'r');
  try {
    const bytes = Buffer.alloc(64 * 1024);
    let remaining = size;
    while (remaining > 0) {
      const count = fs.readSync(fd, bytes, 0, Math.min(bytes.length, remaining), null);
      if (!count) fail('Asset changed while reading.');
      hash.update(bytes.subarray(0, count));
      remaining -= count;
    }
    if (fs.readSync(fd, bytes, 0, 1, null)) fail('Asset changed while reading.');
    return hash.digest('hex');
  } finally { fs.closeSync(fd); }
}
function copyBounded(source, target, expected) {
  const stat = fs.lstatSync(source);
  if (!stat.isFile() || stat.isSymbolicLink() || stat.nlink !== 1 || stat.size !== expected.size)
    fail('Input changed before copy.');
  const input = fs.openSync(source, 'r');
  let output;
  try {
    output = fs.openSync(target, 'wx', 0o644);
    const bytes = Buffer.alloc(64 * 1024);
    const hash = crypto.createHash('sha256');
    let remaining = expected.size;
    while (remaining > 0) {
      const count = fs.readSync(input, bytes, 0, Math.min(bytes.length, remaining), null);
      if (!count) fail('Input changed during copy.');
      hash.update(bytes.subarray(0, count));
      let written = 0;
      while (written < count) {
        const result = fs.writeSync(output, bytes, written, count - written, null);
        if (!result) fail('Could not finish asset copy.');
        written += result;
      }
      remaining -= count;
    }
    if (fs.readSync(input, bytes, 0, 1, null) || hash.digest('hex') !== expected.sha256)
      fail('Input changed during copy.');
  } finally {
    try { if (output !== undefined) fs.closeSync(output); }
    finally { fs.closeSync(input); }
  }
}
function inventory(root) {
  const files = [], directories = [];
  let count = 0, total = 0;
  const rootStat = fs.lstatSync(root);
  if (!rootStat.isDirectory() || rootStat.isSymbolicLink()) fail('Assets must be an ordinary directory.');
  function walk(relative, depth) {
    if (depth > 64) fail('Asset directory depth exceeds 64.');
    const seen = new Set();
    for (const name of fs.readdirSync(path.join(root, relative)).sort()) {
      if (++count > 10000) fail('Asset entry limit exceeded (10000).');
      if (!portable(name) || seen.has(name.toLowerCase())) fail('Unsafe or case-colliding asset name.');
      seen.add(name.toLowerCase());
      const item = relative ? `${relative}/${name}` : name;
      if (Buffer.byteLength(item) > 4096) fail('Asset relative path is too long.');
      const file = path.join(root, item), stat = fs.lstatSync(file);
      if (stat.isSymbolicLink() || (!stat.isFile() && !stat.isDirectory())) fail('Asset links and special files are forbidden.');
      if (stat.isDirectory()) {
        directories.push(item);
        walk(item, depth + 1);
      } else {
        total += stat.size;
        if (stat.nlink !== 1) fail('Hard-linked assets are forbidden.');
        if (stat.size > maximumFileBytes || total > maximumTotalBytes) fail('Asset byte limit exceeded (256 MiB/file, 1 GiB total).');
        files.push({ path: item, size: stat.size, sha256: hashFile(file, stat.size) });
      }
    }
  }
  walk('', 0);
  if (!files.some(file => file.path === 'index.html')) fail('Assets require root index.html.');
  return { files, directories };
}
function same(a, b) { return JSON.stringify(a) === JSON.stringify(b); }
function readManifest(file) {
  const stat = fs.lstatSync(file);
  if (!stat.isFile() || stat.isSymbolicLink() || stat.nlink !== 1 || stat.size > maximumManifestBytes)
    fail('Invalid ownership manifest.');
  // Bound the read even if the file grows after lstat.
  const fd = fs.openSync(file, 'r');
  try {
    const bytes = Buffer.alloc(stat.size + 1);
    let count = 0;
    while (count < bytes.length) {
      const read = fs.readSync(fd, bytes, count, bytes.length - count, null);
      if (!read) break;
      count += read;
    }
    if (count !== stat.size) fail('Ownership manifest changed while reading.');
    try { return JSON.parse(bytes.subarray(0, count).toString('utf8')); }
    catch { fail('Invalid ownership manifest JSON.'); }
  } finally { fs.closeSync(fd); }
}
function removeVerifiedTree(root, expected) {
  if (!same(inventory(root), expected)) fail('Backup changed; retained for manual recovery.');
  // Only delete the exact, fully verified owned inventory. No recursive rm.
  for (const file of expected.files) fs.unlinkSync(path.join(root, file.path));
  for (const dir of [...expected.directories].reverse()) fs.rmdirSync(path.join(root, dir));
  fs.rmdirSync(root);
}
function syncAssets(input, output) {
  let lock, workspace;
  let syncPhase = 'validate-source';
  try {
    const source = directory(input);
    const sourceIdentity = crypto.createHash('sha256').update(source).digest('hex');
    syncPhase = 'validate-output';
    const absolute = path.resolve(output), basename = path.basename(absolute);
    if (!portable(basename) || Buffer.byteLength(basename) > 180) fail('Invalid output directory name.');
    const parent = directory(path.dirname(absolute));
    const target = path.join(parent, basename);
    if (within(source, target) || within(target, source)) fail('Input and output must not overlap.');
    const manifestPath = `${target}.soui-assets.json`;
    const lockPath = `${target}.soui-assets.lock`;
    syncPhase = 'acquire-lock';
    const lockFd = fs.openSync(lockPath, 'wx');
    lock = lockPath;
    fs.closeSync(lockFd);
    syncPhase = 'inspect-output';
    const exists = statIfPresent(target);
    const owned = statIfPresent(manifestPath);
    if (Boolean(exists) !== Boolean(owned)) fail('Refusing to adopt an existing output or orphan ownership manifest; choose a new output.');
    let previous;
    if (exists) {
      syncPhase = 'read-ownership';
      const manifest = readManifest(manifestPath);
      if (!manifest || manifest.schema !== schema || manifest.sourceIdentity !== sourceIdentity) fail('Ownership schema or source identity does not match.');
      syncPhase = 'inspect-previous';
      previous = inventory(target);
      if (!same(manifest.inventory, previous)) fail('Managed output contains edits or extra entries; nothing was deleted.');
    }
    syncPhase = 'inspect-source';
    const next = inventory(source);
    if (previous && same(previous, next)) return { changed: false, files: next.files.length };
    syncPhase = 'create-workspace';
    workspace = fs.mkdtempSync(path.join(parent, `.${basename}.soui-tmp-`));
    const fresh = path.join(workspace, 'new');
    const freshManifest = path.join(workspace, 'new.json');
    syncPhase = 'create-tree';
    fs.mkdirSync(fresh);
    for (const dir of next.directories) fs.mkdirSync(path.join(fresh, dir));
    syncPhase = 'copy-assets';
    for (const file of next.files) {
      copyBounded(path.join(source, file.path), path.join(fresh, file.path), file);
    }
    syncPhase = 'verify-tree';
    if (!same(inventory(fresh), next)) fail('Input changed during copy.');
    syncPhase = 'write-manifest';
    fs.writeFileSync(freshManifest, JSON.stringify({ schema, sourceIdentity, inventory: next }) + '\n', { flag: 'wx' });
    // There is no portable atomic two-path swap. A failure preserves the workspace
    // and any old tree/manifest for manual recovery; never silently delete backups.
    if (previous) {
      syncPhase = 'verify-previous';
      if (!same(inventory(target), previous)) fail('Managed output changed before replacement.');
      syncPhase = 'backup-tree';
      fs.renameSync(target, path.join(workspace, 'previous'));
      syncPhase = 'backup-manifest';
      fs.renameSync(manifestPath, path.join(workspace, 'previous.json'));
    }
    syncPhase = 'publish-tree';
    fs.renameSync(fresh, target);
    syncPhase = 'publish-manifest';
    fs.renameSync(freshManifest, manifestPath);
    if (previous) {
      syncPhase = 'remove-backup-tree';
      removeVerifiedTree(path.join(workspace, 'previous'), previous);
      syncPhase = 'remove-backup-manifest';
      fs.unlinkSync(path.join(workspace, 'previous.json'));
    }
    syncPhase = 'remove-workspace';
    fs.rmdirSync(workspace);
    workspace = null;
    return { changed: true, files: next.files.length };
  } catch (error) {
    throw syncFailure(error, syncPhase, workspace);
  } finally {
    if (lock) {
      try { fs.unlinkSync(lock); } catch (error) {
        throw syncFailure(error, 'release-lock', undefined,
          'Could not remove owned synchronization lock; inspect it before retrying.');
      }
    }
  }
}
function parseArgs(args) {
  const result = {};
  for (let i = 0; i < args.length; i += 2) {
    const name = args[i];
    if (!['--input', '--output'].includes(name) || result[name.slice(2)] !== undefined ||
        !args[i + 1] || args[i + 1].startsWith('--')) fail('Expected unique --input and --output options.');
    result[name.slice(2)] = args[i + 1];
  }
  if (!result.input || !result.output) fail('Both --input and --output are required.');
  return result;
}
if (require.main === module) {
  try {
    const args = parseArgs(process.argv.slice(2));
    const result = syncAssets(args.input, args.output);
    console.log(`Desktop assets ${result.changed ? 'synchronized' : 'unchanged'} (${result.files} files).`);
  } catch (error) {
    console.error(`Desktop asset sync failed: ${error.message}`);
    process.exitCode = 1;
  }
}
module.exports = { syncAssets, parseArgs };
