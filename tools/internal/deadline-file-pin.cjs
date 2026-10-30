'use strict';
// Stable streamed pins for one owning operation's absolute performance.now()
// deadline. Never reset the budget between files, metadata I/O or hash chunks.
const fs = require('node:fs');
const crypto = require('node:crypto');
const {performance} = require('node:perf_hooks');
const id = require('./delivery-identity.cjs');
function check(deadline) {
  if (!Number.isFinite(deadline) || deadline <= 0) throw new TypeError('Invalid absolute monotonic deadline');
  const remaining = deadline - performance.now();
  if (remaining <= 0) throw Object.assign(new Error('Aggregate extraction deadline exceeded'), {code: 'ETIMEDOUT'});
  return remaining;
}
function io(deadline, operation) {check(deadline); const result = operation(); check(deadline); return result;}
function safePath(root, name, deadline) {return id.safePath(root, name, () => check(deadline));}
function pin(file, {deadline, maxBytes = Number.MAX_SAFE_INTEGER, minBytes = 0} = {}) {
  check(deadline);
  if (!Number.isSafeInteger(maxBytes) || !Number.isSafeInteger(minBytes) || minBytes < 0 || maxBytes < minBytes)
    throw new TypeError('Invalid file pin byte bounds');
  const full = safePath(file, '', deadline), before = io(deadline, () => fs.lstatSync(full, {bigint: true}));
  if (!before.isFile() || before.nlink !== 1n || before.size < BigInt(minBytes) || before.size > BigInt(maxBytes))
    throw new Error('File pin requires a plain bounded single-link file');
  const fields = ['dev', 'ino', 'size', 'mtimeNs', 'ctimeNs'];
  const same = value => value.isFile() && value.nlink === 1n && fields.every(key => value[key] === before[key]);
  let descriptor, result, failure;
  try {
    // Keep the descriptor even if open itself consumed the remaining deadline.
    check(deadline); descriptor = fs.openSync(full, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0)); check(deadline);
    if (!same(io(deadline, () => fs.fstatSync(descriptor, {bigint: true})))) throw new Error('File changed before hashing');
    const hash = crypto.createHash('sha256'), chunk = Buffer.allocUnsafe(128 * 1024); let bytes = 0;
    for (;;) {
      const count = io(deadline, () => fs.readSync(descriptor, chunk, 0, chunk.length, null));
      if (!count) break;
      bytes += count;
      if (BigInt(bytes) > before.size || bytes > maxBytes) throw new Error('File changed or exceeds byte budget during hashing');
      check(deadline); hash.update(chunk.subarray(0, count)); check(deadline);
    }
    const after = io(deadline, () => fs.fstatSync(descriptor, {bigint: true}));
    const current = io(deadline, () => fs.lstatSync(safePath(full, '', deadline), {bigint: true}));
    if (BigInt(bytes) !== before.size || !same(after) || !same(current)) throw new Error('File changed during hashing');
    result = {path: full, bytes, sha256: io(deadline, () => hash.digest('hex'))};
  } catch (error) {failure = error;}
  // Closing an owned handle is failure cleanup and must still occur on expiry.
  if (descriptor !== undefined) {
    try {fs.closeSync(descriptor);}
    catch (error) {
      if (failure) failure.closeFailure = {message: error.message, code: error.code || null};
      else failure = error;
    }
  }
  if (failure) throw failure;
  check(deadline); return result;
}
module.exports = {check, io, safePath, pin};
