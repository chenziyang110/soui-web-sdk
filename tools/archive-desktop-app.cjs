#!/usr/bin/env node
'use strict';
// Stored ZIP32 only. No extraction, execution, external command or authenticity claim.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { verifyStage, parseArgs: parseVerifyArgs } = require('./verify-desktop-app.cjs');
const schema = 'soui.desktop.application-delivery/1';
const maxZip = 0xffffffff, chunkSize = 64 * 1024;
const crcTable = Array.from({ length: 256 }, (_, value) => {
  for (let bit = 0; bit < 8; ++bit) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  return value >>> 0;
});
function crcUpdate(crc, bytes) {
  for (const byte of bytes) crc = (crc >>> 8) ^ crcTable[(crc ^ byte) & 255];
  return crc >>> 0;
}
function fail(code) { const error = new Error(code); error.code = code; throw error; }
function safePath(input) {
  if (typeof input !== 'string' || !input || input.includes('\0')) fail('invalid_path');
  const absolute = path.resolve(input), root = path.parse(absolute).root;
  let current = root;
  for (const part of absolute.slice(root.length).split(path.sep).filter(Boolean)) {
    current = path.join(current, part);
    if (fs.lstatSync(current).isSymbolicLink()) fail('symlink_not_allowed');
  }
  return absolute;
}
function within(parent, child) {
  const relative = path.relative(parent, child);
  return !relative || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
}
function portableName(name) {
  return name && Buffer.byteLength(name) <= 240 && name === name.normalize('NFC') &&
    !/[<>:"/\\|?*\x00-\x1f\x7f-\x9f]/.test(name) && !/[. ]$/.test(name) && name !== '.' && name !== '..' &&
    !/^(con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³])(?:\.|$)/i.test(name);
}
function readBounded(file, limit) {
  const descriptor = fs.openSync(file, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    const stat = fs.fstatSync(descriptor);
    if (!stat.isFile() || stat.size > limit) fail('input_limit_or_type');
    const data = Buffer.alloc(stat.size + 1);
    let count = 0, read;
    while (count < data.length && (read = fs.readSync(descriptor, data, count, data.length - count, null))) count += read;
    if (count !== stat.size) fail('input_changed');
    return data.subarray(0, count);
  } finally { fs.closeSync(descriptor); }
}
function localHeader(entry) {
  const header = Buffer.alloc(30);
  header.writeUInt32LE(0x04034b50, 0); header.writeUInt16LE(20, 4);
  header.writeUInt16LE(0x800, 6); header.writeUInt16LE(0x21, 12); // UTF-8, stored, 1980-01-01.
  header.writeUInt32LE(entry.crc || 0, 14); header.writeUInt32LE(entry.size, 18);
  header.writeUInt32LE(entry.size, 22); header.writeUInt16LE(entry.name.length, 26);
  return header;
}
function centralHeader(entry) {
  const header = Buffer.alloc(46);
  header.writeUInt32LE(0x02014b50, 0); header.writeUInt16LE(0x314, 4); // Unix creator.
  header.writeUInt16LE(20, 6); header.writeUInt16LE(0x800, 8); header.writeUInt16LE(0x21, 14);
  header.writeUInt32LE(entry.crc, 16); header.writeUInt32LE(entry.size, 20);
  header.writeUInt32LE(entry.size, 24); header.writeUInt16LE(entry.name.length, 28);
  const mode = (entry.directory ? 0o040000 : 0o100000) | entry.mode;
  header.writeUInt32LE(((mode << 16) | (entry.directory ? 0x10 : 0)) >>> 0, 38);
  header.writeUInt32LE(entry.offset, 42);
  return header;
}
function endHeader(count, size, offset) {
  const header = Buffer.alloc(22);
  header.writeUInt32LE(0x06054b50, 0); header.writeUInt16LE(count, 8); header.writeUInt16LE(count, 10);
  header.writeUInt32LE(size, 12); header.writeUInt32LE(offset, 16);
  return header;
}
function writeAll(descriptor, bytes, position) {
  let written = 0;
  while (written < bytes.length) {
    const count = fs.writeSync(descriptor, bytes, written, bytes.length - written, position + written);
    if (!count) fail('archive_write_failed');
    written += count;
  }
}
function sameRead(descriptor, expected, position) {
  const actual = Buffer.alloc(expected.length);
  if (fs.readSync(descriptor, actual, 0, actual.length, position) !== actual.length || !actual.equals(expected)) fail('archive_header_mismatch');
}
function hashPayload(descriptor, size, position, consume) {
  const buffer = Buffer.alloc(chunkSize), hash = crypto.createHash('sha256');
  let count = 0, crc = 0xffffffff;
  while (count < size) {
    const wanted = Math.min(buffer.length, size - count);
    const read = fs.readSync(descriptor, buffer, 0, wanted, position === null ? null : position + count);
    if (!read) fail('input_changed');
    const bytes = buffer.subarray(0, read);
    hash.update(bytes); crc = crcUpdate(crc, bytes);
    if (consume) consume(bytes, count);
    count += read;
  }
  return { sha256: hash.digest('hex'), crc: (crc ^ 0xffffffff) >>> 0 };
}
function writeZip(file, entries) {
  const descriptor = fs.openSync(file, 'wx', 0o644);
  let offset = 0;
  const append = bytes => {
    if (offset + bytes.length > maxZip) fail('zip32_limit');
    writeAll(descriptor, bytes, offset); offset += bytes.length;
  };
  try {
    for (const entry of entries) {
      entry.offset = offset; append(localHeader(entry)); append(entry.name);
      if (entry.directory) { entry.crc = 0; continue; }
      const source = fs.openSync(entry.source, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
      try {
        const before = fs.fstatSync(source);
        if (!before.isFile() || before.size !== entry.size || (before.mode & 0o777) !== entry.sourceMode) fail('input_changed');
        const result = hashPayload(source, entry.size, 0, bytes => append(bytes));
        const after = fs.fstatSync(source);
        if (result.sha256 !== entry.sha256 || before.size !== after.size || before.mtimeMs !== after.mtimeMs || before.ctimeMs !== after.ctimeMs) fail('input_changed');
        entry.crc = result.crc;
        writeAll(descriptor, localHeader(entry), entry.offset);
      } finally { fs.closeSync(source); }
    }
    const centralOffset = offset;
    for (const entry of entries) { append(centralHeader(entry)); append(entry.name); }
    const end = endHeader(entries.length, offset - centralOffset, centralOffset); append(end);
    return { centralOffset, end, size: offset };
  } finally { fs.closeSync(descriptor); }
}
function verifyZip(file, entries, layout) {
  const descriptor = fs.openSync(file, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    if (fs.fstatSync(descriptor).size !== layout.size) fail('archive_size_mismatch');
    let position = 0;
    for (const entry of entries) {
      if (position !== entry.offset) fail('archive_layout_mismatch');
      const local = localHeader(entry); sameRead(descriptor, local, position); position += local.length;
      sameRead(descriptor, entry.name, position); position += entry.name.length;
      if (!entry.directory) {
        const payload = hashPayload(descriptor, entry.size, position);
        if (payload.sha256 !== entry.sha256 || payload.crc !== entry.crc) fail('archive_payload_mismatch');
      }
      position += entry.size;
    }
    if (position !== layout.centralOffset) fail('archive_layout_mismatch');
    for (const entry of entries) {
      const central = centralHeader(entry); sameRead(descriptor, central, position); position += central.length;
      sameRead(descriptor, entry.name, position); position += entry.name.length;
    }
    sameRead(descriptor, layout.end, position); position += layout.end.length;
    if (position !== layout.size) fail('archive_layout_mismatch');
    return hashPayload(descriptor, layout.size, 0).sha256;
  } finally { fs.closeSync(descriptor); }
}
function archiveStage(input, output, expectations = {}) {
  try {
    const root = safePath(input), destination = path.resolve(output);
    safePath(path.dirname(destination));
    if (!portableName(path.basename(destination))) fail('invalid_output');
    if (fs.existsSync(destination)) fail('output_exists');
    if (within(root, destination) || within(destination, root)) fail('overlapping_paths');
    verifyStage(root, expectations);
    const manifestPath = path.join(root, 'application-manifest.json');
    const manifestBytes = readBounded(manifestPath, 64 * 1024 * 1024);
    const manifest = JSON.parse(manifestBytes.toString('utf8'));
    if (process.platform === 'win32' && manifest.platform !== 'windows') fail('unix_permissions_unavailable');
    // Recheck the exact manifest snapshot before deriving any archive paths.
    verifyStage(root, expectations);
    if (!readBounded(manifestPath, 64 * 1024 * 1024).equals(manifestBytes)) fail('input_changed');
    const archiveRoot = `${manifest.name}-${manifest.applicationVersion}-${manifest.platform}-${manifest.architecture}`;
    if (!portableName(archiveRoot)) fail('invalid_archive_root');
    const directories = new Set([archiveRoot]), entries = [];
    const files = [...manifest.files, { path: 'application-manifest.json', size: manifestBytes.length,
      sha256: crypto.createHash('sha256').update(manifestBytes).digest('hex') }];
    for (const file of files) {
      const source = path.join(root, ...file.path.split('/'));
      safePath(source);
      const stat = fs.lstatSync(source);
      if (!stat.isFile()) fail('input_changed');
      const relative = `${archiveRoot}/${file.path}`;
      const parts = relative.split('/');
      for (let index = 1; index < parts.length; ++index) directories.add(parts.slice(0, index).join('/'));
      const sourceMode = stat.mode & 0o777;
      entries.push({ name: Buffer.from(relative), source, sourceMode, mode: sourceMode,
        size: file.size, sha256: file.sha256, directory: false });
    }
    for (const directory of directories) entries.push({ name: Buffer.from(`${directory}/`), mode: 0o755, size: 0, directory: true, crc: 0 });
    entries.sort((left, right) => Buffer.compare(left.name, right.name));
    if (entries.length >= 0xffff || entries.some(entry => entry.name.length > 0xffff)) fail('zip32_limit');
    const projected = entries.reduce((sum, entry) => sum + entry.size + 76 + 2 * entry.name.length, 22);
    if (projected > maxZip) fail('zip32_limit');
    fs.mkdirSync(destination); // No recursive cleanup or overwrite, even on failure.
    const filename = `${archiveRoot}.zip`, archive = path.join(destination, filename);
    const layout = writeZip(archive, entries);
    const sha256 = verifyZip(archive, entries, layout);
    verifyStage(root, expectations);
    if (!readBounded(manifestPath, 64 * 1024 * 1024).equals(manifestBytes)) fail('input_changed');
    fs.writeFileSync(path.join(destination, `${filename}.sha256`), `${sha256}  ${filename}\n`, { flag: 'wx', mode: 0o644 });
    const delivery = { schema, name: manifest.name, applicationVersion: manifest.applicationVersion,
      platform: manifest.platform, architecture: manifest.architecture, format: 'zip32-store', archive: filename,
      archiveRoot, size: layout.size, sha256, payloadFiles: files.length, archiveEntries: entries.length,
      integrityVerified: true, authenticityVerified: false, nativeRuntimeVerified: false, signed: false };
    fs.writeFileSync(path.join(destination, 'delivery-manifest.json'), JSON.stringify(delivery, null, 2) + '\n', { flag: 'wx', mode: 0o644 });
    return delivery;
  } catch (error) {
    if (typeof error.code === 'string' && /^[a-z][a-z_]+$/.test(error.code)) throw error;
    fail('archive_io_or_changed_input');
  }
}
function parseArgs(args) {
  const options = {};
  for (let index = 0; index < args.length; index += 2) {
    const key = args[index].slice(2);
    if (!args[index].startsWith('--') || !['input', 'output', 'platform', 'arch'].includes(key) ||
        Object.hasOwn(options, key) || !args[index + 1] || args[index + 1].startsWith('--')) fail('invalid_arguments');
    options[key] = args[index + 1];
  }
  if (!options.input || !options.output) fail('invalid_arguments');
  parseVerifyArgs(['--input', options.input, ...(options.platform ? ['--platform', options.platform] : []), ...(options.arch ? ['--arch', options.arch] : [])]);
  return options;
}
if (require.main === module) {
  try { const { input, output, ...expected } = parseArgs(process.argv.slice(2)); console.log(JSON.stringify(archiveStage(input, output, expected))); }
  catch (error) { console.log(JSON.stringify({ schema, ok: false, code: error.code || 'archive_failed', authenticityVerified: false, nativeRuntimeVerified: false })); process.exitCode = 1; }
}
module.exports = { archiveStage, parseArgs };
