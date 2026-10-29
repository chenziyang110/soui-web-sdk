'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const childProcess = require('node:child_process');
const id = require('./delivery-identity.cjs');
const deadlineFile = require('./deadline-file-pin.cjs');

// These are transport settings, not application memory limits. In particular,
// requestedDictionaryBytes does not describe encoder/decoder peak memory.
const definitions = Object.freeze({
  zip: {format: 'zip', requestedDictionaryBytes: null, solid: false,
    // Write filename bytes and ZIP bit 11 explicitly, independent of host ACP.
    arguments: ['-tzip', '-mx=5', '-mmt=2', '-mcu=on'], extension: '.zip', suffix: '',
    compression: '7-Zip ZIP -mx=5 -mmt=2'},
  '7z-balanced': {format: '7z', requestedDictionaryBytes: 32 * 1024 * 1024, solid: true,
    arguments: ['-t7z', '-m0=LZMA2', '-mx=7', '-md=32m', '-ms=on', '-mmt=2'], extension: '.7z', suffix: '-7z-balanced',
    compression: '7-Zip 7z -m0=LZMA2 -mx=7 -md=32m -ms=on -mmt=2'},
  '7z-small': {format: '7z', requestedDictionaryBytes: 64 * 1024 * 1024, solid: true,
    arguments: ['-t7z', '-m0=LZMA2', '-mx=9', '-md=64m', '-ms=on', '-mmt=2'], extension: '.7z', suffix: '-7z-small',
    compression: '7-Zip 7z -m0=LZMA2 -mx=9 -md=64m -ms=on -mmt=2'},
});
function resolve(profile = 'zip') {
  if (typeof profile !== 'string' || !Object.hasOwn(definitions, profile)) throw new Error('Unknown compression profile');
  return {profile, ...definitions[profile], arguments: [...definitions[profile].arguments]};
}
function executableSnapshot(executable, deadline) {
  if (deadline !== undefined) {
    const observed = deadlineFile.pin(executable, {deadline, minBytes: 1, maxBytes: 128 * 1024 * 1024});
    return {file: path.basename(observed.path), bytes: observed.bytes, sha256: observed.sha256};
  }
  executable = id.safePath(executable);
  const before = fs.lstatSync(executable);
  if (!before.isFile() || before.nlink !== 1 || before.size < 1 || before.size > 128 * 1024 * 1024)
    throw new Error('Archive tool must be an ordinary bounded executable');
  const descriptor = fs.openSync(executable, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    const opened = fs.fstatSync(descriptor);
    if (opened.ino !== before.ino || opened.dev !== before.dev || !opened.isFile() || opened.nlink !== 1)
      throw new Error('Archive tool changed before hashing');
    const hash = crypto.createHash('sha256'), buffer = Buffer.allocUnsafe(128 * 1024);
    let length, bytes = 0;
    while ((length = fs.readSync(descriptor, buffer, 0, buffer.length, null)) > 0) {
      bytes += length;
      if (bytes > 128 * 1024 * 1024) throw new Error('Archive tool exceeds byte budget');
      hash.update(buffer.subarray(0, length));
    }
    const after = fs.fstatSync(descriptor), current = fs.lstatSync(id.safePath(executable));
    if (bytes !== before.size || after.size !== before.size || after.mtimeMs !== before.mtimeMs ||
        current.ino !== before.ino || current.dev !== before.dev || current.mtimeMs !== before.mtimeMs || current.nlink !== 1)
      throw new Error('Archive tool changed during hashing');
    return {file: path.basename(executable), bytes, sha256: hash.digest('hex')};
  } finally { fs.closeSync(descriptor); }
}
function observeTool(executable, deadline) {
  // Optional absolute monotonic milliseconds, shared with the owning operation.
  const timeout = deadline === undefined ? 15000 : Math.min(15000, Math.ceil(deadlineFile.check(deadline)));
  if (!(timeout > 0)) throw new Error('Aggregate archive tool deadline exceeded');
  const observed = childProcess.spawnSync(executable, ['i'], {encoding: 'utf8', windowsHide: true,
    timeout, maxBuffer: 1024 * 1024});
  if (observed.error || observed.status !== 0 || observed.signal) {
    if (deadline === undefined) throw new Error('Cannot identify archive tool version');
    const failure = observed.error || new Error('Cannot identify archive tool version');
    failure.archiveToolObservation = {executable, arguments: ['i'], timeoutMs: timeout, exitCode: observed.status,
      signal: observed.signal || null, stdout: observed.stdout || '', stderr: observed.stderr || ''};
    throw failure;
  }
  if (deadline !== undefined) deadlineFile.check(deadline);
  const version = /^7-Zip(?:\s+\([^)]*\))?\s+(\d+\.\d+(?:\.\d+)?)(?:\s|$)/m.exec(observed.stdout || '')?.[1];
  if (!version) throw new Error('Archive tool did not report a supported 7-Zip version');
  const libraries = new Map();
  let inLibraries = false;
  for (const line of (observed.stdout || '').split(/\r?\n/)) {
    if (deadline !== undefined) deadlineFile.check(deadline);
    if (line.trim() === 'Libs:') { inLibraries = true; continue; }
    if (!inLibraries) continue;
    if (!line.trim()) { if (libraries.size) break; continue; }
    if (/^\S.*:\s*$/.test(line)) break;
    const match = /^\s*\d+\s*:\s*[^:\r\n]+\s*:\s*(.+?)\s*$/.exec(line);
    if (!match) throw new Error('Cannot identify archive runtime library');
    const full = id.safePath(path.resolve(path.dirname(executable), match[1]), '', deadline === undefined ? undefined : () => deadlineFile.check(deadline));
    libraries.set(process.platform === 'win32' ? full.toLowerCase() : full, full);
    if (libraries.size > 32) throw new Error('Archive runtime library count exceeds budget');
  }
  // Older output and standalone tools can omit Libs. If the standard companion
  // is present, include it conservatively instead of recording only the loader.
  if (!libraries.size) {
    const companion = id.safePath(path.dirname(executable), '7z.dll', deadline === undefined ? undefined : () => deadlineFile.check(deadline));
    const exists = deadline === undefined ? fs.existsSync(companion) : deadlineFile.io(deadline, () => fs.existsSync(companion));
    if (exists) libraries.set(process.platform === 'win32' ? companion.toLowerCase() : companion, companion);
  }
  return {version, libraries: [...libraries.values()].sort((a, b) => a.localeCompare(b, 'en'))};
}
function runtimeSnapshots(executable, libraries, deadline) {
  let bytes = 0;
  return libraries.map(file => {
    const identity = executableSnapshot(file, deadline);
    bytes += identity.bytes;
    if (bytes > 512 * 1024 * 1024) throw new Error('Archive runtime libraries exceed byte budget');
    return {...identity, file: path.relative(path.dirname(executable), file).split(path.sep).join('/')};
  });
}
function toolIdentity(executable, {deadline} = {}) {
  if (typeof executable !== 'string' || !executable) throw new Error('Archive tool path is required');
  if (deadline !== undefined && (!Number.isFinite(deadline) || deadline <= 0)) throw new Error('Invalid archive tool deadline');
  executable = id.safePath(executable, '', deadline === undefined ? undefined : () => deadlineFile.check(deadline));
  const before = executableSnapshot(executable, deadline), observed = observeTool(executable, deadline);
  const runtimeFiles = runtimeSnapshots(executable, observed.libraries, deadline), repeated = observeTool(executable, deadline);
  if (observed.version !== repeated.version || JSON.stringify(observed.libraries) !== JSON.stringify(repeated.libraries) ||
      JSON.stringify(runtimeFiles) !== JSON.stringify(runtimeSnapshots(executable, repeated.libraries, deadline)))
    throw new Error('Archive runtime libraries changed during version observation');
  const after = executableSnapshot(executable, deadline);
  if (JSON.stringify(before) !== JSON.stringify(after)) throw new Error('Archive tool changed during version observation');
  if (deadline !== undefined) deadlineFile.check(deadline);
  return {...after, version: observed.version, runtimeFiles};
}
module.exports = {resolve, toolIdentity};

if (require.main === module) {
  try {
    const [command, ...argv] = process.argv.slice(2), options = {};
    if (command !== 'describe') throw new Error('Expected compression-profiles describe');
    while (argv.length) {
      const flag = argv.shift(), value = argv.shift();
      if (!['--profile', '--tool'].includes(flag) || !value || Object.hasOwn(options, flag)) throw new Error('Invalid compression profile arguments');
      options[flag] = value;
    }
    const result = resolve(options['--profile']);
    if (options['--tool']) result.toolIdentity = toolIdentity(options['--tool']);
    console.log(id.encoded(result).replace(/[\u0080-\uffff]/g, value => '\\u' + value.charCodeAt(0).toString(16).padStart(4, '0')));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
