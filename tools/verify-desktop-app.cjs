#!/usr/bin/env node
'use strict';

// Read-only consistency check. A manifest and its hashes do not authenticate an app.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { TextDecoder } = require('node:util');
const { inspectBinary, noticeLayout, parseArgs: parseStageArgs } = require('./stage-desktop-app.cjs');
const schema = 'soui.desktop.application-verification/1';
const maximumFileBytes = 256 * 1024 * 1024;
const maximumTotalBytes = 1536 * 1024 * 1024;
const maximumManifestBytes = 64 * 1024 * 1024;
const maximumFiles = 10016;
function fail(code) { const error = new Error(code); error.code = code; throw error; }
function object(value, keys) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).length !== keys.length || keys.some(key => !Object.hasOwn(value, key))) fail('invalid_manifest');
}
function portableName(name) {
  return name.length > 0 && Buffer.byteLength(name, 'utf8') <= 240 && name === name.normalize('NFC') &&
    !/[<>:"/\\|?*\x00-\x1f\x7f-\x9f]/.test(name) && !/[. ]$/.test(name) && name !== '.' && name !== '..' &&
    !/^(?:con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³])(?:\.|$)/i.test(name);
}
function validRelative(value) {
  return typeof value === 'string' && Buffer.byteLength(value) <= 4096 && value.split('/').every(portableName);
}
function safeRoot(input) {
  if (typeof input !== 'string' || !input || input.includes('\0')) fail('invalid_input');
  const root = path.resolve(input), prefix = path.parse(root).root;
  let current = prefix;
  for (const part of root.slice(prefix.length).split(path.sep).filter(Boolean)) {
    current = path.join(current, part);
    if (fs.lstatSync(current).isSymbolicLink()) fail('symlink_not_allowed');
  }
  if (!fs.lstatSync(root).isDirectory()) fail('invalid_input');
  return root;
}
function openedFile(file, limit, consume) {
  const descriptor = fs.openSync(file, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    const before = fs.fstatSync(descriptor);
    if (!before.isFile() || before.size > limit) fail('file_limit_or_type');
    const result = consume(descriptor, before);
    const after = fs.fstatSync(descriptor);
    if (before.size !== after.size || before.mtimeMs !== after.mtimeMs || before.ctimeMs !== after.ctimeMs) fail('input_changed');
    return result;
  } finally { fs.closeSync(descriptor); }
}
function readManifest(file) {
  return openedFile(file, maximumManifestBytes, (descriptor, stat) => {
    const data = Buffer.alloc(stat.size + 1);
    let count = 0, read;
    while (count < data.length && (read = fs.readSync(descriptor, data, count, data.length - count, null)) > 0) count += read;
    if (count !== stat.size) fail('input_changed');
    try { return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(data.subarray(0, count))); }
    catch { fail('invalid_manifest_json'); }
  });
}
function hashFile(file, expected) {
  return openedFile(file, maximumFileBytes, (descriptor, stat) => {
    if (stat.size !== expected.size) fail('size_mismatch');
    const hash = crypto.createHash('sha256'), buffer = Buffer.alloc(64 * 1024);
    let total = 0, count;
    while ((count = fs.readSync(descriptor, buffer, 0, buffer.length, null)) > 0) {
      total += count;
      if (total > expected.size) fail('input_changed');
      hash.update(buffer.subarray(0, count));
    }
    if (total !== expected.size || hash.digest('hex') !== expected.sha256) fail('hash_mismatch');
    return stat.mode;
  });
}
function expectationsValid(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).some(key => !['platform', 'arch'].includes(key))) fail('invalid_expectations');
  if (value.platform !== undefined && !['windows', 'linux', 'macos'].includes(value.platform)) fail('invalid_expectations');
  if (value.arch !== undefined && !['x86', 'x64', 'arm32', 'arm64', 'universal2'].includes(value.arch)) fail('invalid_expectations');
}
function verifyStage(input, expectations = {}) {
  try {
    expectationsValid(expectations);
    const root = safeRoot(input), manifestPath = path.join(root, 'application-manifest.json');
    const stat = fs.lstatSync(manifestPath);
    if (!stat.isFile() || stat.isSymbolicLink()) fail('invalid_manifest_file');
    const manifest = readManifest(manifestPath);
    const diagnosticsPresent = !!manifest && Object.hasOwn(manifest, 'diagnostics');
    const noticesPresent = manifest?.schema === 'soui.desktop.application-stage/2';
    object(manifest, ['schema', 'name', 'applicationVersion', 'identifier', 'platform', 'architecture', 'executableFormat',
      'executable', 'assets', 'minimumOSVersion', 'developmentOnly', 'signed', 'dependenciesBundled', 'nativeRuntimeVerified',
      ...(diagnosticsPresent ? ['diagnostics'] : []), ...(noticesPresent ? ['notices'] : []), 'requiredRuntime', 'files']);
    if (!['soui.desktop.application-stage/1', 'soui.desktop.application-stage/2'].includes(manifest.schema) ||
        (noticesPresent && !diagnosticsPresent) || manifest.developmentOnly !== true ||
        manifest.signed !== false || manifest.dependenciesBundled !== false || manifest.nativeRuntimeVerified !== false) fail('invalid_manifest');
    for (const key of ['name', 'applicationVersion', 'platform', 'architecture'])
      if (typeof manifest[key] !== 'string' || manifest[key].length > 240) fail('invalid_manifest');
    if (manifest.identifier !== null && (typeof manifest.identifier !== 'string' || manifest.identifier.length > 4096)) fail('invalid_manifest');
    const args = ['--binary', 'input', '--assets', 'assets', '--output', 'output', '--name', manifest.name,
      '--version', manifest.applicationVersion, '--platform', manifest.platform, '--arch', manifest.architecture];
    if (manifest.identifier !== null) args.push('--identifier', manifest.identifier);
    try { parseStageArgs(args); } catch { fail('invalid_manifest_identity'); }
    if ((expectations.platform && expectations.platform !== manifest.platform) ||
        (expectations.arch && expectations.arch !== manifest.architecture)) fail('expected_identity_mismatch');
    const mac = manifest.platform === 'macos', linux = manifest.platform === 'linux';
    const executable = mac ? `${manifest.name}.app/Contents/MacOS/${manifest.name}` :
      `${manifest.name}${manifest.platform === 'windows' ? '.exe' : ''}`;
    const assets = mac ? `${manifest.name}.app/Contents/Resources/web` : 'web';
    const doctor = manifest.platform === 'windows' ? 'doctor.ps1' : 'doctor.sh';
    if (manifest.executable !== executable || manifest.assets !== assets ||
        manifest.executableFormat !== ({ linux: 'ELF', windows: 'PE', macos: 'Mach-O' })[manifest.platform]) fail('invalid_layout');
    if ((!mac && manifest.minimumOSVersion !== null) || (mac && (typeof manifest.minimumOSVersion !== 'string' ||
        !/^\d{1,5}\.\d{1,5}(?:\.\d{1,5})?$/.test(manifest.minimumOSVersion)))) fail('invalid_manifest_identity');
    if (diagnosticsPresent) {
      object(manifest.diagnostics, ['entry', 'requiresNode', 'executesApplication', 'successMeans']);
      if (manifest.diagnostics.entry !== doctor || manifest.diagnostics.requiresNode !== false ||
          manifest.diagnostics.executesApplication !== false ||
          manifest.diagnostics.successMeans !== 'no-known-blocker-not-runtime-acceptance') fail('invalid_diagnostics');
    }
    if (!Array.isArray(manifest.requiredRuntime) || manifest.requiredRuntime.length < 1 || manifest.requiredRuntime.length > 32 ||
        manifest.requiredRuntime.some(value => typeof value !== 'string' || value.length > 4096 || /[\x00-\x1f\x7f]/.test(value))) fail('invalid_runtime_metadata');
    if (!Array.isArray(manifest.files) || !manifest.files.length || manifest.files.length > maximumFiles) fail('file_count_limit');
    const mandatory = new Set([executable, `${assets}/index.html`, 'DEPENDENCIES.txt']);
    const noticePaths = noticesPresent ? noticeLayout(manifest.platform, manifest.name).map(item => item.path) : [];
    if (noticesPresent) {
      object(manifest.notices, ['schema', 'files']);
      if (manifest.notices.schema !== 'soui.desktop.notices/1' || !Array.isArray(manifest.notices.files) ||
          manifest.notices.files.length !== noticePaths.length ||
          noticePaths.some((value, index) => manifest.notices.files[index] !== value)) fail('invalid_notices');
      for (const relative of noticePaths) mandatory.add(relative);
    }
    if (diagnosticsPresent) mandatory.add(doctor);
    if (linux) mandatory.add('launch.sh');
    if (mac) mandatory.add(`${manifest.name}.app/Contents/Info.plist`);
    const expected = new Map(), directories = new Set(), foldedPaths = new Map();
    let bytes = 0;
    for (const file of manifest.files) {
      object(file, ['path', 'size', 'sha256']);
      if (!validRelative(file.path) || (!mandatory.has(file.path) && !file.path.startsWith(`${assets}/`))) fail('unsafe_manifest_path');
      if (!Number.isSafeInteger(file.size) || file.size < 0 || file.size > maximumFileBytes ||
          typeof file.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(file.sha256)) fail('invalid_file_record');
      if (noticePaths.includes(file.path) && (file.size < 1 || file.size > 16 * 1024 * 1024)) fail('invalid_notice_file');
      if (expected.has(file.path)) fail('duplicate_path');
      bytes += file.size; if (bytes > maximumTotalBytes) fail('total_size_limit');
      expected.set(file.path, file);
      const parts = file.path.split('/');
      for (let index = 1; index <= parts.length; ++index) {
        const partial = parts.slice(0, index).join('/'), folded = partial.toLowerCase();
        if (foldedPaths.has(folded) && foldedPaths.get(folded) !== partial) fail('case_collision');
        foldedPaths.set(folded, partial);
        if (index < parts.length) directories.add(partial);
      }
    }
    for (const required of mandatory) if (!expected.has(required)) fail('missing_required_file');
    for (const directory of directories) if (expected.has(directory)) fail('file_directory_collision');
    const found = new Set(); let entries = 0;
    function walk(directory, prefix, depth) {
      if (depth > 70) fail('directory_depth_limit');
      const stream = fs.opendirSync(directory);
      try {
        let entry;
        while ((entry = stream.readSync())) {
          if (++entries > 20064 || !portableName(entry.name)) fail('unsafe_inventory');
          const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
          const full = path.join(directory, entry.name), info = fs.lstatSync(full);
          if (info.isSymbolicLink()) fail('symlink_not_allowed');
          if (info.isDirectory()) {
            if (!directories.has(relative)) fail('unexpected_directory');
            walk(full, relative, depth + 1);
          } else if (info.isFile()) {
            if (relative !== 'application-manifest.json' && !expected.has(relative)) fail('unexpected_file');
            found.add(relative);
          } else fail('special_file_not_allowed');
        }
      } finally { stream.closeSync(); }
    }
    walk(root, '', 0);
    for (const relative of expected.keys()) if (!found.has(relative)) fail('missing_file');
    const permissionsChecked = process.platform !== 'win32' && manifest.platform !== 'windows';
    for (const [relative, file] of expected) {
      const mode = hashFile(path.join(root, ...relative.split('/')), file);
      if (permissionsChecked && [executable, ...(diagnosticsPresent ? [doctor] : []), ...(linux ? ['launch.sh'] : [])].includes(relative) && !(mode & 0o100)) fail('executable_permission_missing');
    }
    let identity;
    try { identity = inspectBinary(path.join(root, ...executable.split('/'))); } catch { fail('invalid_executable'); }
    if (identity.platform !== manifest.platform || identity.arch !== manifest.architecture || identity.format !== manifest.executableFormat ||
        (identity.minimumOSVersion || null) !== manifest.minimumOSVersion) fail('binary_identity_mismatch');
    return { schema, ok: true, code: 'integrity_consistent', platform: manifest.platform, architecture: manifest.architecture,
      checkedFiles: expected.size, checkedBytes: bytes, diagnosticsPresent, noticeSetVerified: noticesPresent,
      stageSchema: manifest.schema, executablePermissionsChecked: permissionsChecked,
      authenticityVerified: false, nativeRuntimeVerified: false };
  } catch (error) {
    if (typeof error.code === 'string' && /^[a-z][a-z_]+$/.test(error.code)) throw error;
    fail('unreadable_or_changed_input');
  }
}
function parseArgs(args) {
  const options = {};
  for (let index = 0; index < args.length; index += 2) {
    const name = args[index].slice(2);
    if (!args[index].startsWith('--') || !['input', 'platform', 'arch'].includes(name) ||
        Object.hasOwn(options, name) || !args[index + 1] || args[index + 1].startsWith('--')) fail('invalid_arguments');
    options[name] = args[index + 1];
  }
  if (!options.input) fail('invalid_arguments');
  const { input, ...expectations } = options; expectationsValid(expectations);
  return { input, ...expectations };
}
if (require.main === module) {
  try { const { input, ...expected } = parseArgs(process.argv.slice(2)); console.log(JSON.stringify(verifyStage(input, expected))); }
  catch (error) {
    console.log(JSON.stringify({ schema, ok: false, code: error.code || 'verification_failed', authenticityVerified: false, nativeRuntimeVerified: false }));
    process.exitCode = 1;
  }
}
module.exports = { verifyStage, parseArgs };
