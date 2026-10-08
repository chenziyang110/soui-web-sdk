'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');

// Immutable backups + a write-ahead journal make recovery independent of the
// interrupted process's call stack. No business paths are accepted by this layer.
module.exports = function transactions({safePath, validateManifest, verifyFiles, hash, encoded, sdkId}) {
  const directory = '.soui-sdk-transaction';
  const exists = file => fs.existsSync(file);
  const same = (a, b) => a === b;
  function currentLock(file) {
    if (!exists(file)) return null;
    const bytes = fs.readFileSync(file), text = bytes.toString('utf8');
    if (!bytes.equals(Buffer.from(text, 'utf8'))) throw new Error('SDK recovery conflict: lock is not UTF-8');
    return text;
  }
  function durable(file, bytes) {
    const fd = fs.openSync(file, 'wx');
    try { fs.writeFileSync(fd, bytes); fs.fsyncSync(fd); } finally { fs.closeSync(fd); }
  }
  function copy(source, destination) {
    fs.mkdirSync(path.dirname(destination), {recursive: true});
    durable(destination, fs.readFileSync(source));
  }
  function read(file) {
    if (!fs.statSync(file).isFile() || fs.statSync(file).size > 16 * 1024 * 1024) throw new Error('Invalid transaction record');
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  }
  function lock(text) {
    if (typeof text !== 'string') throw new Error('Invalid transaction lock');
    const value = JSON.parse(text.replace(/^\uFEFF/, ''));
    if (value.schema !== 'soui.sdk-lock/v1' || value.manifestSha256 !== hash(encoded(value.manifest))) {
      throw new Error('Invalid transaction lock identity');
    }
    validateManifest(value.manifest);
    return value.manifest;
  }
  function assertNoLegacy(project) {
    if (exists(project) && fs.readdirSync(project).some(name => name.startsWith('.sdk-install-'))) {
      throw new Error('Legacy SDK transaction needs explicit recovery from its original package; preserve .sdk-install-*');
    }
  }
  function assertSettled(project) {
    assertNoLegacy(project);
    if (exists(safePath(project, directory))) throw new Error('Pending SDK transaction; run recover --project DIR');
  }
  function owner() { return {pid: process.pid, host: os.hostname(), nonce: crypto.randomUUID()}; }
  function claims(root) {
    const parent = safePath(root, 'claims');
    const names = fs.readdirSync(parent).filter(name => /^\d{8}$/.test(name)).sort();
    if (!names.length || names.length > 10000 || names.some((name, index) => name !== String(index).padStart(8, '0'))) {
      throw new Error('Invalid SDK transaction ownership history');
    }
    const name = names[names.length - 1];
    const value = read(safePath(root, `claims/${name}/owner.json`));
    if (!Number.isSafeInteger(value.pid) || value.pid < 1 || typeof value.host !== 'string' ||
        typeof value.nonce !== 'string' || !/^[a-f0-9-]{36}$/.test(value.nonce)) throw new Error('Invalid SDK transaction owner');
    return {name, value, next: String(names.length).padStart(8, '0')};
  }
  function dead(value) {
    if (value.host !== os.hostname()) return false;
    try { process.kill(value.pid, 0); return false; }
    catch (error) { if (error.code === 'ESRCH') return true; return false; }
  }
  function cleanPrivate(project, target) {
    // The caller supplies only an independently created or renamed private tree.
    const relative = path.relative(project, target).split(path.sep).join('/');
    safePath(project, relative);
    if (!relative.startsWith('.soui-sdk-') || relative.includes('/')) throw new Error('Unsafe transaction cleanup');
    fs.rmSync(target, {recursive: true});
  }
  function acquire(project, create) {
    project = safePath(project);
    assertNoLegacy(project);
    if (!exists(project)) { if (!create) return null; fs.mkdirSync(project, {recursive: true}); }
    const root = safePath(project, directory);
    const value = owner();
    if (!exists(root)) {
      if (!create) return null;
      const stage = fs.mkdtempSync(path.join(project, '.soui-sdk-prepare-'));
      try {
        const claim = safePath(stage, 'claims/00000000');
        fs.mkdirSync(claim, {recursive: true});
        durable(safePath(claim, 'owner.json'), encoded(value));
        // Both directories are nonempty. Rename cannot replace another claim.
        fs.renameSync(stage, root);
      } catch (error) {
        if (exists(stage)) cleanPrivate(project, stage);
        throw new Error(`SDK transaction is busy or could not be acquired: ${error.message}`);
      }
    } else {
      const prior = claims(root);
      if (!dead(prior.value)) throw new Error('SDK transaction is busy (live, foreign or uncertain owner)');
      const stage = safePath(root, `claims/pending-${value.nonce}`);
      fs.mkdirSync(stage);
      durable(safePath(stage, 'owner.json'), encoded(value));
      try { fs.renameSync(stage, safePath(root, `claims/${prior.next}`)); }
      catch (error) { throw new Error(`SDK recovery ownership conflict: ${error.message}`); }
    }
    const context = {project, root, nonce: value.nonce};
    owned(context);
    return context;
  }
  function owned(context) {
    if (claims(context.root).value.nonce !== context.nonce) throw new Error('SDK transaction ownership changed');
  }
  function retire(context) {
    owned(context);
    const retired = safePath(context.project, `.soui-sdk-retired-${crypto.randomUUID()}`);
    fs.renameSync(context.root, retired);
    cleanPrivate(context.project, retired);
  }
  function entries(previous, next) {
    const files = new Map();
    const folded = new Map();
    for (const [side, manifest] of [['previous', previous], ['next', next]]) {
      for (const file of manifest?.files || []) {
        const key = file.path.toLowerCase();
        if (folded.has(key) && folded.get(key) !== file.path) throw new Error('SDK cross-version case-only path change');
        folded.set(key, file.path);
        if (!files.has(file.path)) files.set(file.path, {});
        files.get(file.path)[side] = file;
      }
    }
    return [...files].map(([name, sides]) => ({name, ...sides}));
  }
  function matches(file, expected) {
    if (!expected || !exists(file) || !fs.statSync(file).isFile()) return false;
    const bytes = fs.readFileSync(file);
    return bytes.length === expected.bytes && hash(bytes) === expected.sha256;
  }
  function checkTarget(context, entry) {
    const file = safePath(context.project, entry.name);
    if (exists(file) && !matches(file, entry.previous) && !matches(file, entry.next)) {
      throw new Error(`SDK recovery conflict: ${entry.name}`);
    }
    return file;
  }
  function publish(context, source, destination) {
    owned(context);
    const temporary = safePath(context.root, `publish-${crypto.randomUUID()}`);
    copy(source, temporary);
    fs.mkdirSync(path.dirname(destination), {recursive: true});
    fs.renameSync(temporary, destination);
  }
  function prepare(context, previousText, nextText, packageRoot) {
    owned(context);
    const previous = previousText === null ? null : lock(previousText), next = lock(nextText);
    const all = entries(previous, next);
    for (const entry of all) {
      if (entry.previous) copy(safePath(context.project, entry.name), safePath(context.root, `old/${entry.name}`));
      if (entry.next) copy(safePath(packageRoot, `payload/${entry.name}`), safePath(context.root, `new/${entry.name}`));
    }
    if (previous) verifyFiles(safePath(context.root, 'old'), previous);
    verifyFiles(safePath(context.root, 'new'), next);
    if (previous) verifyFiles(context.project, previous);
    const lockFile = safePath(context.project, 'sdk-lock.json');
    if (currentLock(lockFile) !== previousText) throw new Error('SDK lock changed during preparation');
    for (const entry of all) {
      if (!entry.previous && exists(safePath(context.project, entry.name))) throw new Error(`SDK installation collision: ${entry.name}`);
    }
    durable(safePath(context.root, 'next-lock.json'), nextText);
    const journal = {schema: 'soui.sdk-install-transaction/v1', id: sdkId, previousLock: previousText, nextLock: nextText};
    durable(safePath(context.root, 'journal.pending'), encoded(journal));
    fs.renameSync(safePath(context.root, 'journal.pending'), safePath(context.root, 'journal.json'));
    return {previous, next, all, previousText};
  }
  function apply(context, plan) {
    for (const entry of plan.all) {
      const target = safePath(context.project, entry.name);
      if (entry.previous ? !matches(target, entry.previous) : exists(target)) {
        throw new Error(`SDK installation collision or modification: ${entry.name}`);
      }
      if (entry.next) publish(context, safePath(context.root, `new/${entry.name}`), target);
      else { owned(context); fs.unlinkSync(target); }
    }
    verifyFiles(context.project, plan.next);
    if (currentLock(safePath(context.project, 'sdk-lock.json')) !== plan.previousText) throw new Error('SDK lock changed during installation');
    publish(context, safePath(context.root, 'next-lock.json'), safePath(context.project, 'sdk-lock.json'));
  }
  function recoverOwned(context) {
    owned(context);
    const journalFile = safePath(context.root, 'journal.json');
    if (!exists(journalFile)) { retire(context); return 'preparation-discarded'; }
    const journal = read(journalFile);
    if (journal.schema !== 'soui.sdk-install-transaction/v1' || journal.id !== sdkId) throw new Error('Invalid SDK transaction journal');
    const previous = journal.previousLock === null ? null : lock(journal.previousLock);
    const next = lock(journal.nextLock), all = entries(previous, next);
    const lockFile = safePath(context.project, 'sdk-lock.json');
    const currentText = currentLock(lockFile);
    if (same(currentText, journal.nextLock)) {
      verifyFiles(context.project, next);
      retire(context); return 'committed';
    }
    if (!same(currentText, journal.previousLock)) throw new Error('SDK recovery conflict: sdk-lock.json');
    // Validate the WHOLE recovery plan before any rollback write.
    if (previous) verifyFiles(safePath(context.root, 'old'), previous);
    verifyFiles(safePath(context.root, 'new'), next);
    all.forEach(entry => checkTarget(context, entry));
    for (const entry of all) {
      const target = checkTarget(context, entry);
      if (entry.previous) {
        if (!matches(target, entry.previous)) publish(context, safePath(context.root, `old/${entry.name}`), target);
      } else if (exists(target)) { owned(context); fs.unlinkSync(target); }
    }
    if (previous) verifyFiles(context.project, previous);
    if (currentLock(lockFile) !== journal.previousLock) throw new Error('SDK lock changed during recovery');
    retire(context); return 'rolled-back';
  }
  function recover(project) {
    const context = acquire(project, false);
    return {status: context ? recoverOwned(context) : 'no-pending-transaction', releaseReady: false};
  }
  return {assertSettled, acquire, prepare, apply, recoverOwned, recover};
};
