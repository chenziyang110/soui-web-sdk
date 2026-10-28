'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const childProcess = require('node:child_process');
const id = require('./delivery-identity.cjs');

// These are transport settings, not application memory limits. In particular,
// requestedDictionaryBytes does not describe encoder/decoder peak memory.
const definitions = Object.freeze({
  zip: {format: 'zip', requestedDictionaryBytes: null, solid: false,
    arguments: ['-tzip', '-mx=5', '-mmt=2'], extension: '.zip', suffix: '',
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
function executableSnapshot(executable) {
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
function observeTool(executable) {
  const observed = childProcess.spawnSync(executable, ['i'], {encoding: 'utf8', windowsHide: true,
    timeout: 15000, maxBuffer: 1024 * 1024});
  if (observed.error || observed.status !== 0) throw new Error('Cannot identify archive tool version');
  const version = /^7-Zip(?:\s+\([^)]*\))?\s+(\d+\.\d+(?:\.\d+)?)(?:\s|$)/m.exec(observed.stdout || '')?.[1];
  if (!version) throw new Error('Archive tool did not report a supported 7-Zip version');
  const libraries = new Map();
  let inLibraries = false;
  for (const line of (observed.stdout || '').split(/\r?\n/)) {
    if (line.trim() === 'Libs:') { inLibraries = true; continue; }
    if (!inLibraries) continue;
    if (!line.trim()) { if (libraries.size) break; continue; }
    if (/^\S.*:\s*$/.test(line)) break;
    const match = /^\s*\d+\s*:\s*[^:\r\n]+\s*:\s*(.+?)\s*$/.exec(line);
    if (!match) throw new Error('Cannot identify archive runtime library');
    const full = id.safePath(path.resolve(path.dirname(executable), match[1]));
    libraries.set(process.platform === 'win32' ? full.toLowerCase() : full, full);
    if (libraries.size > 32) throw new Error('Archive runtime library count exceeds budget');
  }
  // Older output and standalone tools can omit Libs. If the standard companion
  // is present, include it conservatively instead of recording only the loader.
  if (!libraries.size) {
    const companion = id.safePath(path.dirname(executable), '7z.dll');
    if (fs.existsSync(companion)) libraries.set(process.platform === 'win32' ? companion.toLowerCase() : companion, companion);
  }
  return {version, libraries: [...libraries.values()].sort((a, b) => a.localeCompare(b, 'en'))};
}
function runtimeSnapshots(executable, libraries) {
  let bytes = 0;
  return libraries.map(file => {
    const identity = executableSnapshot(file);
    bytes += identity.bytes;
    if (bytes > 512 * 1024 * 1024) throw new Error('Archive runtime libraries exceed byte budget');
    return {...identity, file: path.relative(path.dirname(executable), file).split(path.sep).join('/')};
  });
}
function toolIdentity(executable) {
  if (typeof executable !== 'string' || !executable) throw new Error('Archive tool path is required');
  executable = id.safePath(executable);
  const before = executableSnapshot(executable), observed = observeTool(executable);
  const runtimeFiles = runtimeSnapshots(executable, observed.libraries), repeated = observeTool(executable);
  if (observed.version !== repeated.version || JSON.stringify(observed.libraries) !== JSON.stringify(repeated.libraries) ||
      JSON.stringify(runtimeFiles) !== JSON.stringify(runtimeSnapshots(executable, repeated.libraries)))
    throw new Error('Archive runtime libraries changed during version observation');
  const after = executableSnapshot(executable);
  if (JSON.stringify(before) !== JSON.stringify(after)) throw new Error('Archive tool changed during version observation');
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
