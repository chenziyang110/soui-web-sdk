'use strict';
// Offline, opt-in build/deployment only. Pins must come from the native
// application's approved configuration; this tool never creates trust pins.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {isUtf8} = require('node:buffer');
const identity = require('./delivery-identity.cjs');

const limits = Object.freeze({files: 1024, fileBytes: 1024 * 1024, totalBytes: 16 * 1024 * 1024,
  manifestBytes: 512 * 1024, kernelBytes: 256 * 1024 * 1024, licenses: 256,
  pathBytes: 256, segmentBytes: 128, pathDepth: 8, directories: 2048});
const approvalSchema = 'soui.devtools-package-approval/v1';
const packageSchema = 'soui.devtools-package/v1';
const manifestName = 'approved-manifest.json';
const headerName = 'soui_devtools_package.h';
const frontendSubdir = 'devtools/package/frontend';
const pin = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const encoded = value => Buffer.from(JSON.stringify(value, null, 2) + '\n');
const need = (value, code) => { if (!value) throw new Error(code); };
const object = value => value && typeof value === 'object' && !Array.isArray(value);
const exact = (value, keys) => object(value) && Object.keys(value).sort().join(',') === keys.sort().join(',');
const version = value => typeof value === 'string' && value.length > 0 && value.length <= 128 && /^[A-Za-z0-9][A-Za-z0-9._+() -]*$/.test(value);
const comparable = value => process.platform === 'win32' ? value.toLowerCase() : value;

function relativeName(value, layoutDepth = 0) {
  need(typeof value === 'string' && value.length > 0 && Buffer.byteLength(value) <= limits.pathBytes + layoutDepth * 16,
    'E_DEVTOOLS_PATH');
  const parts = value.split('/');
  need(parts.length <= limits.pathDepth + layoutDepth && parts.every(part => part.length > 0 && part.length <= limits.segmentBytes &&
    part !== '.' && part !== '..' && /^[A-Za-z0-9_.-]+$/.test(part) && !part.endsWith('.') &&
    !/^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part)), 'E_DEVTOOLS_PATH');
  return value;
}
function safeExisting(file, directory) {
  const absolute = identity.safePath(file);
  const stat = fs.lstatSync(absolute, {bigint: true});
  need(!stat.isSymbolicLink() && (directory ? stat.isDirectory() : stat.isFile()), 'E_DEVTOOLS_PLAIN_PATH');
  need(comparable(fs.realpathSync.native(absolute)) === comparable(absolute), 'E_DEVTOOLS_PATH_ALIAS');
  return {absolute, stat};
}
function rootPath(value, create = false) {
  need(typeof value === 'string' && path.isAbsolute(value) && value.length <= 2048, 'E_DEVTOOLS_ROOT');
  const absolute = identity.safePath(value);
  if (create) fs.mkdirSync(absolute, {recursive: true});
  safeExisting(absolute, true);
  // Reject linked ancestors too, including directory junctions. Stable body
  // identities and complete post-copy hashes below detect concurrent changes.
  for (let current = absolute; ; current = path.dirname(current)) {
    safeExisting(current, true);
    if (path.dirname(current) === current) break;
  }
  return absolute;
}
function inRoot(root, name) { return identity.safePath(root, relativeName(name, 2)); }
function sameFile(before, after) {
  return ['dev', 'ino', 'size', 'mtimeNs', 'ctimeNs', 'nlink'].every(key => before[key] === after[key]);
}
function inspectFile(file, maximum, options = {}) {
  const {absolute, stat: named} = safeExisting(file, false);
  const fd = fs.openSync(absolute, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  let destination;
  try {
    const before = fs.fstatSync(fd, {bigint: true});
    need(before.isFile() && before.nlink === 1n && sameFile(before, named), 'E_DEVTOOLS_FILE_IDENTITY');
    need(before.size >= 0n && before.size <= BigInt(maximum), 'E_DEVTOOLS_FILE_BUDGET');
    const bytes = Number(before.size);
    if (options.bytes !== undefined) need(bytes === options.bytes, 'E_DEVTOOLS_FILE_SIZE');
    let architecture;
    if (options.kernel) {
      need(bytes >= 64, 'E_DEVTOOLS_KERNEL_PE');
      const dos = Buffer.alloc(64);
      need(fs.readSync(fd, dos, 0, dos.length, 0) === dos.length && dos[0] === 0x4d && dos[1] === 0x5a,
        'E_DEVTOOLS_KERNEL_PE');
      const offset = dos.readUInt32LE(0x3c), pe = Buffer.alloc(6);
      need(offset >= 64 && offset <= bytes - pe.length && fs.readSync(fd, pe, 0, pe.length, offset) === pe.length &&
        pe.subarray(0, 4).equals(Buffer.from([0x50, 0x45, 0, 0])), 'E_DEVTOOLS_KERNEL_PE');
      architecture = ({332: 'x86', 34404: 'x64'})[pe.readUInt16LE(4)];
      need(architecture && architecture === options.architecture, 'E_DEVTOOLS_KERNEL_ARCHITECTURE');
    }
    if (options.copy) {
      fs.mkdirSync(path.dirname(options.copy), {recursive: true});
      rootPath(path.dirname(options.copy));
      destination = fs.openSync(options.copy, 'wx');
    }
    const digest = crypto.createHash('sha256'), block = Buffer.alloc(65536), chunks = [];
    let offset = 0;
    while (offset < bytes) {
      const count = fs.readSync(fd, block, 0, Math.min(block.length, bytes - offset), offset);
      need(count > 0, 'E_DEVTOOLS_FILE_CHANGED');
      const data = block.subarray(0, count);
      digest.update(data);
      if (options.body) chunks.push(Buffer.from(data));
      if (destination !== undefined) {
        for (let written = 0; written < count;) {
          const amount = fs.writeSync(destination, data, written, count - written, offset + written);
          need(amount > 0, 'E_DEVTOOLS_COPY'); written += amount;
        }
      }
      offset += count;
    }
    need(fs.readSync(fd, block, 0, 1, bytes) === 0, 'E_DEVTOOLS_FILE_CHANGED');
    const sha256 = digest.digest('hex');
    if (options.sha256 !== undefined) need(sha256 === options.sha256, 'E_DEVTOOLS_FILE_DIGEST');
    need(sameFile(before, fs.fstatSync(fd, {bigint: true})) &&
      sameFile(before, safeExisting(absolute, false).stat), 'E_DEVTOOLS_FILE_CHANGED');
    if (destination !== undefined) fs.fsyncSync(destination);
    return {bytes, sha256, ...(architecture ? {architecture} : {}), ...(options.body ? {body: Buffer.concat(chunks, bytes)} : {})};
  } finally {
    if (destination !== undefined) fs.closeSync(destination);
    fs.closeSync(fd);
  }
}
function parseJson(body) {
  need(isUtf8(body), 'E_DEVTOOLS_JSON_UTF8');
  const source = body.toString('utf8'), stack = [];
  // Bound structure and reject duplicate keys before JSON.parse can silently
  // pick a different interpretation of an externally approved document.
  for (let index = 0; index < source.length; ++index) {
    const ch = source[index];
    if (ch === '"') {
      const begin = index++;
      for (; index < source.length; ++index) {
        if (source[index] === '\\') ++index;
        else if (source[index] === '"') break;
      }
      const top = stack.at(-1);
      if (top?.keys && top.key) {
        let key;
        try { key = JSON.parse(source.slice(begin, index + 1)); } catch { throw new Error('E_DEVTOOLS_JSON'); }
        need(!top.keys.has(key), 'E_DEVTOOLS_JSON_DUPLICATE'); top.keys.add(key); top.key = false;
      }
    } else if (ch === '{' || ch === '[') {
      stack.push(ch === '{' ? {keys: new Set(), key: true} : {});
      need(stack.length <= 32, 'E_DEVTOOLS_JSON_DEPTH');
    } else if (ch === '}' || ch === ']') stack.pop();
    else if (ch === ',' && stack.at(-1)?.keys) stack.at(-1).key = true;
  }
  try { return JSON.parse(source); } catch { throw new Error('E_DEVTOOLS_JSON'); }
}
function roster(value, maximum = limits.files) {
  need(Array.isArray(value) && value.length > 0 && value.length <= maximum, 'E_DEVTOOLS_MANIFEST');
  const seen = new Set(); let total = 0;
  for (const row of value) {
    need(exact(row, ['path', 'bytes', 'sha256']) && pin(row.sha256) && Number.isSafeInteger(row.bytes) &&
      row.bytes >= 0 && row.bytes <= limits.fileBytes, 'E_DEVTOOLS_MANIFEST_ROW');
    relativeName(row.path);
    need(!seen.has(row.path.toLowerCase()), 'E_DEVTOOLS_CASE_ALIAS'); seen.add(row.path.toLowerCase());
    total += row.bytes; need(total <= limits.totalBytes, 'E_DEVTOOLS_TOTAL_BUDGET');
  }
  return value;
}
function common(options) {
  need(object(options) && [options.kernelSha256, options.manifestSha256, options.approvalSha256].every(pin) &&
    ['x86', 'x64'].includes(options.architecture) && version(options.sdkVersion), 'E_DEVTOOLS_CONFIGURATION');
}
function approvalValue(value, options) {
  need(exact(value, ['schema', 'backend', 'kernel', 'frontend', 'licenses']) && value.schema === approvalSchema &&
    value.backend === 'miniblink' && exact(value.kernel, ['path', 'sha256', 'version']) &&
    exact(value.frontend, ['manifest', 'sha256', 'version']) && version(value.kernel.version) && version(value.frontend.version),
    'E_DEVTOOLS_APPROVAL');
  relativeName(value.kernel.path);
  need(value.kernel.sha256 === options.kernelSha256 && value.frontend.sha256 === options.manifestSha256 &&
    value.frontend.manifest === manifestName && (!options.kernelRelative || value.kernel.path === options.kernelRelative),
    'E_DEVTOOLS_APPROVAL_PAIR');
  roster(value.licenses, limits.licenses);
  need(value.licenses.every(row => row.bytes > 0), 'E_DEVTOOLS_LEGAL_SOURCE');
  return value;
}
function walk(root) {
  rootPath(root);
  const files = [], names = new Set(); let directories = 0;
  function visit(relative) {
    const current = relative ? inRoot(root, relative) : root;
    safeExisting(current, true);
    need(++directories <= limits.directories, 'E_DEVTOOLS_DIRECTORY_BUDGET');
    const children = fs.readdirSync(current);
    need(children.length <= limits.files + limits.licenses + 8, 'E_DEVTOOLS_FILE_COUNT');
    for (const child of children.sort()) {
      const next = relative ? relative + '/' + child : child;
      relativeName(next, 2);
      need(!names.has(next.toLowerCase()), 'E_DEVTOOLS_CASE_ALIAS'); names.add(next.toLowerCase());
      const found = safeExisting(inRoot(root, next), fs.lstatSync(inRoot(root, next)).isDirectory());
      if (found.stat.isDirectory()) visit(next);
      else { files.push(next); need(files.length <= limits.files + limits.licenses + 8, 'E_DEVTOOLS_FILE_COUNT'); }
    }
  }
  visit(''); return files.sort();
}
function closedTree(root, names) {
  const expected = [...names].sort();
  need(new Set(expected.map(name => name.toLowerCase())).size === expected.length, 'E_DEVTOOLS_PACKAGE_OVERLAP');
  need(JSON.stringify(walk(root)) === JSON.stringify(expected), 'E_DEVTOOLS_PACKAGE_CLOSURE');
}
function disconnected(first, second) {
  for (const [base, candidate] of [[first, second], [second, first]]) {
    const rel = path.relative(base, candidate);
    need(rel && (rel === '..' || rel.startsWith('..' + path.sep) || path.isAbsolute(rel)), 'E_DEVTOOLS_OVERLAPPING_ROOTS');
  }
}
function sources(options) {
  common(options); relativeName(options.kernelRelative); relativeName(options.approvalRelative);
  const root = rootPath(options.frontendRoot);
  const manifest = inspectFile(inRoot(root, manifestName), limits.manifestBytes,
    {sha256: options.manifestSha256, body: true});
  const members = roster(parseJson(manifest.body));
  need(members.some(row => row.path === 'front_end/inspector.html') &&
    !members.some(row => row.path.toLowerCase() === manifestName), 'E_DEVTOOLS_FRONTEND_ENTRY');
  const approvalFile = inspectFile(inRoot(root, options.approvalRelative), limits.manifestBytes,
    {sha256: options.approvalSha256, body: true});
  const approval = approvalValue(parseJson(approvalFile.body), options);
  const names = [manifestName, options.kernelRelative, options.approvalRelative,
    ...members.map(row => row.path), ...approval.licenses.map(row => row.path)];
  closedTree(root, names);
  inspectFile(inRoot(root, options.kernelRelative), limits.kernelBytes,
    {sha256: options.kernelSha256, kernel: true, architecture: options.architecture});
  for (const row of [...members, ...approval.licenses]) inspectFile(inRoot(root, row.path), limits.fileBytes, row);
  return {root, manifest, members, approval, approvalFile, names};
}
function fileRows(materials, kernel, prepared) {
  return [
    {path: 'frontend/' + manifestName, bytes: materials.manifest.bytes, sha256: materials.manifest.sha256},
    ...materials.members.map(row => ({...row, path: 'frontend/' + row.path})),
    ...materials.approval.licenses.map(row => ({...row, path: 'legal/' + row.path})),
    ...(prepared ? [{path: 'kernel/node.dll', bytes: kernel.bytes, sha256: kernel.sha256},
      {path: 'approval.json', bytes: materials.approvalFile.bytes, sha256: materials.approvalFile.sha256}] : []),
  ].sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
}
function metadata(materials, kernel, options, prepared) {
  return {schema: packageSchema, mode: prepared ? 'prepared' : 'delivery', backend: 'miniblink',
    sdk_version: options.sdkVersion, architecture: options.architecture,
    kernel: {sha256: options.kernelSha256, version: materials.approval.kernel.version},
    frontend: {manifest_sha256: options.manifestSha256, version: materials.approval.frontend.version},
    approval_sha256: options.approvalSha256, files: fileRows(materials, kernel, prepared)};
}
function put(file, body) { fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, body, {flag: 'wx'}); }
function copy(root, name, stage, target, maximum, expected = {}) {
  return inspectFile(inRoot(root, name), maximum, {...expected, copy: inRoot(stage, target)});
}
function verifyRows(root, rows) {
  for (const row of rows) inspectFile(inRoot(root, row.path), row.path === 'kernel/node.dll' ? limits.kernelBytes :
    row.path === 'approval.json' || row.path === 'frontend/' + manifestName ? limits.manifestBytes : limits.fileBytes, row);
  closedTree(root, [...rows.map(row => row.path), 'package.json']);
}
function managedExisting(root) {
  const data = parseJson(inspectFile(inRoot(root, 'package.json'), limits.manifestBytes, {body: true}).body);
  need(exact(data, ['schema', 'mode', 'backend', 'sdk_version', 'architecture', 'kernel', 'frontend', 'approval_sha256', 'files']) &&
    data.schema === packageSchema && ['prepared', 'delivery'].includes(data.mode) && Array.isArray(data.files) &&
    data.files.length > 0 && data.files.length <= limits.files + limits.licenses + 4, 'E_DEVTOOLS_MANAGED_DESTINATION');
  const names = new Set();
  for (const row of data.files) {
    need(exact(row, ['path', 'bytes', 'sha256']) && pin(row.sha256) && Number.isSafeInteger(row.bytes) && row.bytes >= 0,
      'E_DEVTOOLS_MANAGED_DESTINATION');
    relativeName(row.path, 2); need(!names.has(row.path.toLowerCase()), 'E_DEVTOOLS_CASE_ALIAS'); names.add(row.path.toLowerCase());
  }
  verifyRows(root, data.files);
}
function removeOwned(container, owned) {
  need(path.dirname(owned) === container && /^\.(?:new|old)-[a-f0-9-]{36}$/.test(path.basename(owned)), 'E_DEVTOOLS_DELETE_SCOPE');
  rootPath(container); walk(owned); fs.rmSync(owned, {recursive: true});
}
function transaction(container, write) {
  const suffix = crypto.randomUUID(), stage = path.join(container, '.new-' + suffix), backup = path.join(container, '.old-' + suffix);
  const destination = path.join(container, 'package');
  fs.mkdirSync(stage);
  let moved = false, published = false;
  try {
    write(stage);
    if (fs.existsSync(destination)) { managedExisting(destination); fs.renameSync(destination, backup); moved = true; }
    try { fs.renameSync(stage, destination); published = true; }
    catch (error) {
      if (moved && !fs.existsSync(destination)) fs.renameSync(backup, destination);
      throw error;
    }
    if (moved) removeOwned(container, backup);
  } finally {
    if (!published && fs.existsSync(stage)) removeOwned(container, stage);
  }
  return destination;
}
function locked(container, body) {
  const file = path.join(container, '.devtools-package.lock');
  let fd;
  try { fd = fs.openSync(file, 'wx'); } catch (error) {
    if (error.code === 'EEXIST') throw new Error('E_DEVTOOLS_BUSY'); throw error;
  }
  const held = fs.fstatSync(fd, {bigint: true});
  try { return body(); }
  finally {
    fs.closeSync(fd);
    const named = fs.lstatSync(file, {bigint: true});
    need(named.dev === held.dev && named.ino === held.ino && !named.isSymbolicLink(), 'E_DEVTOOLS_LOCK_IDENTITY');
    fs.unlinkSync(file);
  }
}
function nativeHeader(materials, options) {
  return Buffer.from('#pragma once\n// Generated from independently approved offline package pins. No permission is granted.\n' +
    'namespace soui::sdk::devtools::package {\n' +
    'inline constexpr char kernel_sha256[] = "' + options.kernelSha256 + '";\n' +
    'inline constexpr char frontend_manifest_sha256[] = "' + options.manifestSha256 + '";\n' +
    'inline constexpr char kernel_version[] = "' + materials.approval.kernel.version + '";\n' +
    'inline constexpr char frontend_version[] = "' + materials.approval.frontend.version + '";\n' +
    'inline constexpr wchar_t frontend_subdir[] = L"' + frontendSubdir + '";\n' +
    '} // namespace soui::sdk::devtools::package\n');
}
function prepare(options) {
  const materials = sources(options);
  const prospective = identity.safePath(options.output);
  need(path.isAbsolute(options.output), 'E_DEVTOOLS_OUTPUT'); disconnected(materials.root, prospective);
  const output = rootPath(options.output, true);
  return locked(output, () => {
    const destination = transaction(output, stage => {
      const kernel = copy(materials.root, options.kernelRelative, stage, 'kernel/node.dll', limits.kernelBytes,
        {sha256: options.kernelSha256, kernel: true, architecture: options.architecture});
      copy(materials.root, manifestName, stage, 'frontend/' + manifestName, limits.manifestBytes,
        {bytes: materials.manifest.bytes, sha256: options.manifestSha256});
      for (const row of materials.members) copy(materials.root, row.path, stage, 'frontend/' + row.path, limits.fileBytes, row);
      for (const row of materials.approval.licenses) copy(materials.root, row.path, stage, 'legal/' + row.path, limits.fileBytes, row);
      copy(materials.root, options.approvalRelative, stage, 'approval.json', limits.manifestBytes,
        {bytes: materials.approvalFile.bytes, sha256: options.approvalSha256});
      const meta = metadata(materials, kernel, options, true);
      put(inRoot(stage, 'package.json'), encoded(meta)); verifyRows(stage, meta.files); closedTree(materials.root, materials.names);
    });
    const header = path.join(output, headerName), body = nativeHeader(materials, options);
    if (!fs.existsSync(header) || !inspectFile(header, limits.manifestBytes, {body: true}).body.equals(body)) {
      const temporary = path.join(output, '.header-' + crypto.randomUUID());
      try { fs.writeFileSync(temporary, body, {flag: 'wx'}); fs.renameSync(temporary, header); }
      finally { if (fs.existsSync(temporary)) fs.unlinkSync(temporary); }
    }
    return {schema: packageSchema, prepared: true, architecture: options.architecture,
      kernel_sha256: options.kernelSha256, frontend_manifest_sha256: options.manifestSha256,
      approval_sha256: options.approvalSha256, files: materials.names.length};
  });
}
function prepared(options) {
  common(options);
  const root = path.join(rootPath(options.preparedRoot), 'package'); rootPath(root);
  const manifest = inspectFile(inRoot(root, 'frontend/' + manifestName), limits.manifestBytes,
    {sha256: options.manifestSha256, body: true});
  const members = roster(parseJson(manifest.body));
  need(members.some(row => row.path === 'front_end/inspector.html'), 'E_DEVTOOLS_FRONTEND_ENTRY');
  const approvalFile = inspectFile(inRoot(root, 'approval.json'), limits.manifestBytes,
    {sha256: options.approvalSha256, body: true});
  const approval = approvalValue(parseJson(approvalFile.body), options);
  const kernel = inspectFile(inRoot(root, 'kernel/node.dll'), limits.kernelBytes,
    {sha256: options.kernelSha256, kernel: true, architecture: options.architecture});
  const materials = {root, manifest, members, approval, approvalFile};
  const meta = metadata(materials, kernel, options, true);
  need(inspectFile(inRoot(root, 'package.json'), limits.manifestBytes, {body: true}).body.equals(encoded(meta)),
    'E_DEVTOOLS_PREPARED_METADATA');
  verifyRows(root, meta.files);
  return {materials, kernel};
}
function deploy(options) {
  const {materials, kernel} = prepared(options);
  const application = rootPath(options.applicationOutput);
  disconnected(materials.root, application);
  // The normal runtime deployment copies the selected, verified kernel before
  // this step. Never overwrite or transact application files outside devtools.
  inspectFile(path.join(application, 'node.dll'), limits.kernelBytes,
    {sha256: options.kernelSha256, kernel: true, architecture: options.architecture});
  const container = rootPath(path.join(application, 'devtools'), true);
  return locked(container, () => {
    transaction(container, stage => {
      copy(materials.root, 'frontend/' + manifestName, stage, 'frontend/' + manifestName, limits.manifestBytes,
        {bytes: materials.manifest.bytes, sha256: options.manifestSha256});
      for (const row of materials.members) copy(materials.root, 'frontend/' + row.path, stage, 'frontend/' + row.path, limits.fileBytes, row);
      for (const row of materials.approval.licenses) copy(materials.root, 'legal/' + row.path, stage, 'legal/' + row.path, limits.fileBytes, row);
      const meta = metadata(materials, kernel, options, false);
      put(inRoot(stage, 'package.json'), encoded(meta)); verifyRows(stage, meta.files);
      inspectFile(path.join(application, 'node.dll'), limits.kernelBytes, {sha256: options.kernelSha256});
    });
    return {schema: packageSchema, deployed: true, architecture: options.architecture,
      kernel_sha256: options.kernelSha256, frontend_manifest_sha256: options.manifestSha256,
      approval_sha256: options.approvalSha256, files: fileRows(materials, kernel, false).length};
  });
}
function verifyDelivery(applicationOutput) {
  const application = rootPath(applicationOutput);
  const root = rootPath(path.join(application, 'devtools', 'package'));
  const captured = inspectFile(inRoot(root, 'package.json'), limits.manifestBytes, {body: true});
  const data = parseJson(captured.body);
  need(exact(data, ['schema', 'mode', 'backend', 'sdk_version', 'architecture', 'kernel', 'frontend', 'approval_sha256', 'files']) &&
    data.schema === packageSchema && data.mode === 'delivery' && data.backend === 'miniblink' && version(data.sdk_version) &&
    ['x86', 'x64'].includes(data.architecture) && exact(data.kernel, ['sha256', 'version']) && pin(data.kernel.sha256) &&
    version(data.kernel.version) && exact(data.frontend, ['manifest_sha256', 'version']) &&
    pin(data.frontend.manifest_sha256) && version(data.frontend.version) && pin(data.approval_sha256) &&
    Array.isArray(data.files) && data.files.length > 0 && data.files.length <= limits.files + limits.licenses + 1,
    'E_DEVTOOLS_DELIVERY_METADATA');
  const names = new Set();
  for (const row of data.files) {
    need(exact(row, ['path', 'bytes', 'sha256']) && pin(row.sha256) && Number.isSafeInteger(row.bytes) && row.bytes >= 0,
      'E_DEVTOOLS_DELIVERY_METADATA');
    relativeName(row.path, 2);
    need(!names.has(row.path.toLowerCase()), 'E_DEVTOOLS_CASE_ALIAS'); names.add(row.path.toLowerCase());
  }
  const manifest = inspectFile(inRoot(root, 'frontend/' + manifestName), limits.manifestBytes,
    {sha256: data.frontend.manifest_sha256, body: true});
  const members = roster(parseJson(manifest.body));
  need(members.some(row => row.path === 'front_end/inspector.html') &&
    !members.some(row => row.path.toLowerCase() === manifestName), 'E_DEVTOOLS_FRONTEND_ENTRY');
  // Legal materials have their own bounded roster; an entry in the frontend
  // manifest cannot authorize an extra delivery file or a legal-file change.
  const legal = data.files.filter(row => row.path.startsWith('legal/'))
    .map(row => ({path: row.path.slice(6), bytes: row.bytes, sha256: row.sha256}));
  roster(legal, limits.licenses);
  need(legal.every(row => row.bytes > 0), 'E_DEVTOOLS_LEGAL_SOURCE');
  const expected = [{path: 'frontend/' + manifestName, bytes: manifest.bytes, sha256: manifest.sha256},
    ...members.map(row => ({...row, path: 'frontend/' + row.path})),
    ...legal.map(row => ({...row, path: 'legal/' + row.path}))];
  const records = rows => rows.map(row => [row.path, row.bytes, row.sha256]).sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0);
  need(JSON.stringify(records(data.files)) === JSON.stringify(records(expected)), 'E_DEVTOOLS_DELIVERY_CLOSURE');
  verifyRows(root, expected);
  inspectFile(path.join(application, 'node.dll'), limits.kernelBytes,
    {sha256: data.kernel.sha256, kernel: true, architecture: data.architecture});
  // This verifies delivery consistency only. Metadata cannot issue a native
  // permission or replace the independently compiled application pins.
  const files = [{path: 'devtools/package/package.json', bytes: captured.bytes, sha256: captured.sha256},
    ...expected.map(row => ({...row, path: 'devtools/package/' + row.path}))]
    .sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
  return {metadata: data, paths: files.map(row => row.path), files};
}
function cli(argv) {
  const mode = argv.shift(); need(['prepare', 'deploy'].includes(mode), 'E_DEVTOOLS_COMMAND');
  const allowed = mode === 'prepare' ? ['frontend-root', 'kernel-relative', 'kernel-sha256', 'manifest-sha256',
    'approval-relative', 'approval-sha256', 'architecture', 'sdk-version', 'output'] : ['prepared-root', 'application-output',
    'kernel-sha256', 'manifest-sha256', 'approval-sha256', 'architecture', 'sdk-version'];
  const result = {};
  while (argv.length) {
    const option = argv.shift(); need(option?.startsWith('--') && allowed.includes(option.slice(2)), 'E_DEVTOOLS_ARGUMENT');
    const key = option.slice(2).replace(/-([a-z0-9])/g, (_, letter) => letter.toUpperCase());
    need(!Object.hasOwn(result, key) && argv.length && !argv[0].startsWith('--'), 'E_DEVTOOLS_ARGUMENT');
    result[key] = argv.shift();
  }
  need(Object.keys(result).length === allowed.length, 'E_DEVTOOLS_ARGUMENT');
  return (mode === 'prepare' ? prepare : deploy)(result);
}
function reported(operation, options) {
  try { return operation(options); }
  catch (error) {
    if (/^E_DEVTOOLS_[A-Z_]+$/.test(error.message)) throw error;
    throw new Error('E_DEVTOOLS_IO');
  }
}
module.exports = {prepare: options => reported(prepare, options), deploy: options => reported(deploy, options),
  verifyDelivery: applicationOutput => reported(verifyDelivery, applicationOutput), limits};
if (require.main === module) {
  try { console.log(JSON.stringify(cli(process.argv.slice(2)))); }
  catch (error) {
    // Do not print file paths, raw JSON, hashes derived from rejected input,
    // source contents, or platform exception text.
    console.error(/^E_DEVTOOLS_[A-Z_]+$/.test(error.message) ? error.message : 'E_DEVTOOLS_IO');
    process.exitCode = 1;
  }
}
