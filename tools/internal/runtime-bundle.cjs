'use strict';
// Content-addressed transport only. Applications still run from independent,
// ordinary directories after restore; no DLL lookup or ABI sharing is implied.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const id = require('./delivery-identity.cjs');
const versioning = require('./sdk-version.cjs');

const SCHEMA = 'soui.runtime-bundle/v1';
const LIMITS = Object.freeze({applications: 128, files: 100000, manifestBytes: 32 * 1024 * 1024,
  fileBytes: 2 * 1024 * 1024 * 1024, totalBytes: 64 * 1024 * 1024 * 1024, depth: 32});
const sha = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const fail = message => { throw new Error(message); };
function keys(value, expected, label) {
  if (!object(value) || Object.keys(value).sort().join(',') !== [...expected].sort().join(','))
    fail(`Invalid ${label} fields`);
}
function boundedJson(value) {
  let nodes = 0, stringBytes = 0;
  const pending = [[value, 0]];
  while (pending.length) {
    const [entry, depth] = pending.pop();
    if (++nodes > LIMITS.files * 32 || depth > LIMITS.depth) fail('JSON exceeds structural budget');
    if (typeof entry === 'string') {
      stringBytes += Buffer.byteLength(entry, 'utf8');
      if (stringBytes > LIMITS.manifestBytes) fail('JSON exceeds byte budget');
      continue;
    }
    if (entry === null || typeof entry === 'boolean') continue;
    if (typeof entry === 'number' && Number.isFinite(entry)) continue;
    if (!object(entry) && !Array.isArray(entry)) fail('Expected plain JSON value');
    if (Object.getPrototypeOf(entry) !== (Array.isArray(entry) ? Array.prototype : Object.prototype) &&
        Object.getPrototypeOf(entry) !== null) fail('Expected plain JSON object');
    const names = Object.keys(entry);
    if (names.length + nodes + pending.length > LIMITS.files * 32) fail('JSON exceeds structural budget');
    for (const name of names) {
      stringBytes += Buffer.byteLength(name, 'utf8');
      if (stringBytes > LIMITS.manifestBytes) fail('JSON exceeds byte budget');
      pending.push([entry[name], depth + 1]);
    }
  }
}
function jsonBytes(value) {
  boundedJson(value);
  const encoded = Buffer.from(id.encoded(value), 'utf8');
  if (encoded.length > LIMITS.manifestBytes) fail('JSON exceeds byte budget');
  return encoded;
}
function jsonFile(file) {
  file = id.safePath(file);
  const stat = regular(file);
  if (stat.size > LIMITS.manifestBytes) fail('Invalid JSON file or byte budget');
  const descriptor = fs.openSync(file, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0)), chunks = [];
  let bytes = 0;
  try {
    const opened = fs.fstatSync(descriptor);
    if (opened.dev !== stat.dev || opened.ino !== stat.ino) fail('JSON file changed before reading');
    while (true) {
      const buffer = Buffer.allocUnsafe(128 * 1024), length = fs.readSync(descriptor, buffer, 0, buffer.length, null);
      if (length === 0) break;
      bytes += length;
      if (bytes > LIMITS.manifestBytes) fail('JSON exceeds byte budget');
      chunks.push(buffer.subarray(0, length));
    }
    const after = regular(file);
    if (after.dev !== stat.dev || after.ino !== stat.ino || after.size !== bytes || after.mtimeMs !== stat.mtimeMs)
      fail('JSON file changed during reading');
  } finally { fs.closeSync(descriptor); }
  const result = JSON.parse(Buffer.concat(chunks, bytes).toString('utf8').replace(/^\uFEFF/, ''));
  boundedJson(result);
  return result;
}
function safeRelative(name) {
  if (typeof name !== 'string' || !name || name.length > 2048 || path.isAbsolute(name)) fail('Invalid logical path');
  id.safePath(process.cwd(), name);
  return name;
}
function appName(name) {
  safeRelative(name);
  if (name.includes('/') || name.length > 128) fail('Invalid application name');
  return name;
}
function size(value, maximum = LIMITS.fileBytes) {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) fail('Invalid byte count or byte budget');
  return value;
}
function total(left, right) { return size(left + right, LIMITS.totalBytes); }
function validatePaths(files) {
  if (!Array.isArray(files) || files.length === 0 || files.length > LIMITS.files) fail('Invalid file count');
  const names = new Set(), directories = new Map();
  for (const entry of files) {
    keys(entry, ['path', 'bytes', 'sha256'], 'payload file');
    safeRelative(entry.path);
    size(entry.bytes);
    if (!sha(entry.sha256)) fail('Invalid file hash');
    const normalized = entry.path.toLowerCase();
    if (names.has(normalized)) fail('Duplicate or case-colliding file path');
    names.add(normalized);
    const parts = entry.path.split('/');
    for (let length = 1; length < parts.length; length++) {
      const prefix = parts.slice(0, length).join('/'), folded = prefix.toLowerCase();
      if (directories.has(folded) && directories.get(folded) !== prefix) fail('Case-colliding directory path');
      directories.set(folded, prefix);
    }
  }
  for (const name of names) {
    const parts = name.split('/');
    for (let length = 1; length < parts.length; length++)
      if (names.has(parts.slice(0, length).join('/'))) fail('File/directory path prefix conflict');
  }
}
function validateReport(report) {
  if (!object(report) || report.schema !== 'soui.application-size/v1' ||
      !versioning.isVersion(report.version) || !['x86', 'x64'].includes(report.architecture) ||
      !['Debug', 'Release'].includes(report.configuration) ||
      !['miniblink', 'webview2', 'auto'].includes(report.rendererBackend) ||
      typeof report.target !== 'string' || !/^[A-Za-z][A-Za-z0-9_]{1,63}$/.test(report.target) ||
      typeof report.buildId !== 'string' || !report.buildId || report.buildId.length > 128 ||
      !sha(report.sourceSha256) || report.releaseReady !== false) fail('Invalid application report identity');
  keys(report.payload, ['sha256', 'bytes', 'files'], 'source payload');
  validatePaths(report.payload.files);
  const bytes = report.payload.files.reduce((sum, file) => total(sum, file.bytes), 0);
  if (report.payload.bytes !== bytes || !sha(report.payload.sha256) ||
      id.hash(id.encoded(report.payload.files)) !== report.payload.sha256) fail('Source payload snapshot differs');
  for (const required of [`${report.target}.exe`, ...(report.rendererBackend === 'auto' ?
    ['resources/web-modern/index.html', 'resources/web-compat/index.html', '.soui-runtime.json'] : ['resources/index.html']),
    ...(['miniblink', 'auto'].includes(report.rendererBackend) ? ['node.dll'] : [])])
    if (!report.payload.files.some(file => file.path === required)) fail(`Application file missing: ${required}`);
}
function summarize(applications, blobs) {
  const logicalBytes = applications.reduce((sum, app) => total(sum, app.report.payload.bytes), 0);
  const fileCount = applications.reduce((sum, app) => sum + app.report.payload.files.length, 0);
  if (fileCount > LIMITS.files) fail('Bundle exceeds file count budget');
  const uniqueBytes = blobs.reduce((sum, blob) => total(sum, blob.bytes), 0);
  return {logicalBytes, uniqueBytes, savedUncompressedBytes: logicalBytes - uniqueBytes, fileCount, blobCount: blobs.length};
}
function validateManifest(manifest) {
  keys(manifest, ['schema', 'applications', 'blobs', 'totals'], 'bundle manifest');
  if (manifest.schema !== SCHEMA) fail('Unknown runtime bundle schema');
  if (!Array.isArray(manifest.applications) || manifest.applications.length === 0 ||
      manifest.applications.length > LIMITS.applications) fail('Invalid application count');
  const seen = new Set(), needed = new Map();
  let fileCount = 0;
  for (const app of manifest.applications) {
    keys(app, ['name', 'report'], 'application');
    appName(app.name);
    if (seen.has(app.name.toLowerCase())) fail('Duplicate or case-colliding application');
    seen.add(app.name.toLowerCase());
    validateReport(app.report);
    fileCount += app.report.payload.files.length;
    if (fileCount > LIMITS.files) fail('Bundle exceeds file count budget');
    for (const file of app.report.payload.files) {
      if (needed.has(file.sha256) && needed.get(file.sha256) !== file.bytes) fail('Content hash has conflicting byte counts');
      needed.set(file.sha256, file.bytes);
    }
  }
  if (!Array.isArray(manifest.blobs) || manifest.blobs.length !== needed.size) fail('Blob inventory differs');
  const blobNames = new Set();
  for (const blob of manifest.blobs) {
    keys(blob, ['sha256', 'bytes'], 'blob');
    size(blob.bytes);
    if (!sha(blob.sha256) || blobNames.has(blob.sha256) || needed.get(blob.sha256) !== blob.bytes)
      fail('Invalid, duplicate, extra, or inconsistent blob');
    blobNames.add(blob.sha256);
  }
  const totals = summarize(manifest.applications, manifest.blobs);
  keys(manifest.totals, Object.keys(totals), 'totals');
  for (const [key, value] of Object.entries(totals)) if (manifest.totals[key] !== value) fail('Bundle totals differ');
  return manifest;
}
function regular(file) {
  file = id.safePath(file);
  const stat = fs.lstatSync(file);
  if (!stat.isFile() || stat.nlink !== 1) fail(`Expected ordinary file: ${file}`);
  return stat;
}
function fingerprint(file) {
  const before = regular(file);
  size(before.size);
  const descriptor = fs.openSync(file, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    const opened = fs.fstatSync(descriptor);
    if (!opened.isFile() || opened.nlink !== 1 || opened.dev !== before.dev || opened.ino !== before.ino)
      fail('File changed before reading');
    const digest = crypto.createHash('sha256'), buffer = Buffer.allocUnsafe(128 * 1024);
    let bytes = 0, length;
    while ((length = fs.readSync(descriptor, buffer, 0, buffer.length, null)) > 0) {
      bytes = size(bytes + length);
      digest.update(buffer.subarray(0, length));
    }
    const after = fs.fstatSync(descriptor), current = regular(file);
    if (bytes !== before.size || after.size !== before.size || after.mtimeMs !== before.mtimeMs ||
        current.dev !== before.dev || current.ino !== before.ino || current.mtimeMs !== before.mtimeMs)
      fail('File changed during reading');
    return {bytes, sha256: digest.digest('hex')};
  } finally { fs.closeSync(descriptor); }
}
function expectFile(file, expected) {
  const actual = fingerprint(file);
  if (actual.bytes !== expected.bytes || actual.sha256 !== expected.sha256) fail(`File content differs: ${file}`);
}
function inventory(root) {
  root = id.safePath(root);
  if (!fs.lstatSync(root).isDirectory()) fail('Expected directory');
  const files = [], directories = [], pending = [''];
  while (pending.length) {
    const parent = pending.pop(), directory = id.safePath(root, parent);
    for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
      const name = parent ? `${parent}/${entry.name}` : entry.name;
      safeRelative(name);
      const full = id.safePath(root, name), stat = fs.lstatSync(full);
      if (stat.isDirectory()) { directories.push(name); pending.push(name); }
      else if (stat.isFile() && stat.nlink === 1) files.push(name);
      else fail(`Linked or unsupported file: ${name}`);
      if (files.length + directories.length > LIMITS.files * 4) fail('Directory exceeds entry budget');
    }
  }
  return {files: files.sort(), directories: directories.sort()};
}
function expectedDirectories(files) {
  const result = new Set();
  for (const name of files) {
    const parts = name.split('/');
    for (let length = 1; length < parts.length; length++) result.add(parts.slice(0, length).join('/'));
  }
  return [...result].sort();
}
function exactInventory(root, files) {
  const actual = inventory(root), expected = [...files].sort();
  if (JSON.stringify(actual.files) !== JSON.stringify(expected) ||
      JSON.stringify(actual.directories) !== JSON.stringify(expectedDirectories(expected))) fail('Directory inventory differs');
}
function checkApplication(root, report) {
  exactInventory(root, report.payload.files.map(file => file.path));
  for (const file of report.payload.files) expectFile(id.safePath(root, file.path), file);
}
function checkBundle(root) {
  root = id.safePath(root);
  const manifestFile = id.safePath(root, 'manifest.json');
  if (regular(manifestFile).size > LIMITS.manifestBytes) fail('Bundle manifest exceeds byte budget');
  const manifestIdentity = fingerprint(manifestFile);
  const manifest = validateManifest(jsonFile(manifestFile));
  exactInventory(root, ['manifest.json', ...manifest.blobs.map(blob => `blobs/${blob.sha256}`)]);
  for (const blob of manifest.blobs) expectFile(id.safePath(root, `blobs/${blob.sha256}`), blob);
  expectFile(manifestFile, manifestIdentity);
  return {manifest, manifestIdentity};
}
function verify(bundle) {
  const {manifest} = checkBundle(bundle);
  return {schema: SCHEMA, verified: true, ...manifest.totals,
    applications: manifest.applications.map(app => ({name: app.name, target: app.report.target,
      architecture: app.report.architecture, rendererBackend: app.report.rendererBackend,
      payloadSha256: app.report.payload.sha256}))};
}
function destinationPath(destination) {
  destination = id.safePath(destination);
  if (fs.existsSync(destination)) fail('Destination already exists');
  const parent = path.dirname(destination);
  if (!fs.statSync(parent).isDirectory()) fail('Destination parent must exist');
  return destination;
}
function within(parent, child) {
  const relative = path.relative(parent, child);
  return relative === '' || (!relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative));
}
function transactionalDirectory(destination, work) {
  destination = destinationPath(destination);
  const parent = path.dirname(destination), prefix = `.soui-bundle-${crypto.randomUUID()}-`;
  const stage = fs.mkdtempSync(path.join(parent, prefix));
  try {
    const result = work(stage);
    // Recheck after all reads and writes, immediately before publishing. Never
    // merge with a user directory or copy partial results into a final name.
    destinationPath(destination);
    fs.renameSync(stage, destination);
    return result;
  } finally {
    if (fs.existsSync(stage)) {
      if (path.dirname(stage) !== parent || !path.basename(stage).startsWith(prefix)) fail('Invalid cleanup ownership');
      id.safePath(stage);
      fs.rmSync(stage, {recursive: true, force: true});
    }
  }
}
function pack(output, applications) {
  output = destinationPath(output);
  if (!Array.isArray(applications) || applications.length === 0 || applications.length > LIMITS.applications)
    fail('Invalid application count');
  // Bound the complete input before cloning reports. Many individually valid
  // reports must not multiply the metadata budget by the application count.
  jsonBytes(applications);
  const inputs = applications.map(app => {
    keys(app, ['name', 'root', 'report'], 'pack application');
    if (typeof app.root !== 'string') fail('Invalid application root');
    const root = id.safePath(app.root);
    if (within(root, output)) fail('Bundle output must be outside source applications');
    return {root, name: app.name, report: JSON.parse(jsonBytes(app.report).toString('utf8'))};
  });
  const blobs = new Map();
  for (const app of inputs) {
    validateReport(app.report);
    for (const file of app.report.payload.files) {
      const previous = blobs.get(file.sha256);
      if (previous && previous.bytes !== file.bytes) fail('Content hash has conflicting byte counts');
      if (!previous) blobs.set(file.sha256, {sha256: file.sha256, bytes: file.bytes});
    }
  }
  const manifest = {schema: SCHEMA, applications: inputs.map(({name, report}) => ({name, report})),
    blobs: [...blobs.values()].sort((a, b) => a.sha256.localeCompare(b.sha256))};
  manifest.totals = summarize(manifest.applications, manifest.blobs);
  validateManifest(manifest);
  const manifestBytes = jsonBytes(manifest);
  for (const app of inputs) checkApplication(app.root, app.report);
  return transactionalDirectory(output, stage => {
    fs.mkdirSync(path.join(stage, 'blobs'));
    const copied = new Set();
    for (const app of inputs) for (const file of app.report.payload.files) {
      if (!copied.has(file.sha256)) {
        const from = id.safePath(app.root, file.path), to = id.safePath(stage, `blobs/${file.sha256}`);
        expectFile(from, file);
        fs.copyFileSync(from, to, fs.constants.COPYFILE_EXCL);
        expectFile(to, file);
        copied.add(file.sha256);
      }
    }
    fs.writeFileSync(path.join(stage, 'manifest.json'), manifestBytes, {flag: 'wx'});
    // Capture-time validation plus a final source scan rejects stale snapshots,
    // new files and changes that occur while the bundle is assembled.
    for (const app of inputs) checkApplication(app.root, app.report);
    const result = verify(stage);
    return {...result, output};
  });
}
function restore(bundle, destination) {
  bundle = id.safePath(bundle);
  destination = destinationPath(destination);
  if (within(bundle, destination)) fail('Restore destination must be outside bundle');
  const {manifest, manifestIdentity} = checkBundle(bundle);
  return transactionalDirectory(destination, stage => {
    for (const app of manifest.applications) for (const file of app.report.payload.files) {
      const source = id.safePath(bundle, `blobs/${file.sha256}`), target = id.safePath(stage, `${app.name}/${file.path}`);
      expectFile(source, file);
      fs.mkdirSync(path.dirname(target), {recursive: true});
      fs.copyFileSync(source, target, fs.constants.COPYFILE_EXCL);
      expectFile(target, file);
    }
    for (const app of manifest.applications) checkApplication(id.safePath(stage, app.name), app.report);
    exactInventory(stage, manifest.applications.flatMap(app => app.report.payload.files.map(file => `${app.name}/${file.path}`)));
    checkBundle(bundle);
    expectFile(id.safePath(bundle, 'manifest.json'), manifestIdentity);
    return {schema: SCHEMA, restored: true, output: destination, ...manifest.totals};
  });
}
module.exports = {pack, verify, restore};

if (require.main === module) {
  try {
    const [command, ...args] = process.argv.slice(2), flags = {};
    if (!['pack', 'verify', 'restore'].includes(command)) fail('Invalid bundle command');
    while (args.length) {
      const flag = args.shift(), value = args.shift();
      if (!['--output', '--applications', '--bundle'].includes(flag) || !value || Object.hasOwn(flags, flag))
        fail('Invalid bundle arguments');
      flags[flag] = value;
    }
    const expected = command === 'pack' ? ['--output', '--applications'] : command === 'verify' ? ['--bundle'] : ['--bundle', '--output'];
    if (Object.keys(flags).sort().join(',') !== expected.sort().join(',')) fail('Missing or unexpected bundle arguments');
    let result;
    if (command === 'pack') {
      const file = id.safePath(flags['--applications']), config = jsonFile(file);
      if (!Array.isArray(config)) fail('Applications configuration must be an array');
      const apps = config.map(app => {
        keys(app, ['name', 'root', 'reportFile'], 'CLI application');
        if (typeof app.root !== 'string' || typeof app.reportFile !== 'string') fail('Invalid CLI source paths');
        return {name: app.name, root: path.resolve(path.dirname(file), app.root),
          report: jsonFile(path.resolve(path.dirname(file), app.reportFile))};
      });
      result = pack(flags['--output'], apps);
    } else if (command === 'verify') result = verify(flags['--bundle']);
    else result = restore(flags['--bundle'], flags['--output']);
    console.log(id.encoded(result).replace(/[\u0080-\uffff]/g, value => '\\u' + value.charCodeAt(0).toString(16).padStart(4, '0')));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
