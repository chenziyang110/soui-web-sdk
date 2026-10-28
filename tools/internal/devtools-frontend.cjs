'use strict';

// Source composition only: this never approves a package, verifies a kernel,
// changes native policy, or runs frontend/native code.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {isUtf8} = require('node:buffer');
const identity = require('./delivery-identity.cjs');
const {limits} = require('./devtools-package.cjs');

const schema = 'soui.miniblink-inspector-composition/v1';
const sourceSchema = 'soui.miniblink-inspector-overrides/v1';
const manifestName = 'approved-manifest.json';
const receiptName = 'composition-receipt.json';
const memberCount = 650;
const overrideNames = Object.freeze([
  'front_end/bindings/CompilerScriptMapping.js',
  'front_end/devtools.js',
  'front_end/host/InspectorFrontendHost.js',
  'front_end/sdk/DebuggerModel.js',
  'front_end/sdk/InspectorBackend.js',
  'front_end/sdk/SourceMap.js',
  'front_end/sources/SourcesPanel.js',
]);
const hash = body => crypto.createHash('sha256').update(body).digest('hex');
const encoded = value => Buffer.from(JSON.stringify(value, null, 2) + '\n');
const pin = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const need = (condition, code) => { if (!condition) throw new Error('E_DEVTOOLS_FRONTEND_' + code); };
const object = value => value && typeof value === 'object' && !Array.isArray(value);
const exact = (value, keys) => object(value) && Object.keys(value).sort().join(',') === [...keys].sort().join(',');
const comparable = value => process.platform === 'win32' ? value.toLowerCase() : value;

function relativeName(value, layoutDepth = 0) {
  need(typeof value === 'string' && value.length > 0 && Buffer.byteLength(value) <= limits.pathBytes + layoutDepth * 16, 'PATH');
  const parts = value.split('/');
  need(parts.length <= limits.pathDepth + layoutDepth && parts.every(part => part.length > 0 && part.length <= limits.segmentBytes &&
    part !== '.' && part !== '..' && /^[A-Za-z0-9_.-]+$/.test(part) && !part.endsWith('.') &&
    !/^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part)), 'PATH');
  return value;
}

function absoluteName(value) {
  need(typeof value === 'string' && path.isAbsolute(value) && value.length <= 2048 &&
    !(process.platform === 'win32' && /^[\\/]{2}/.test(value)), 'ROOT');
  return identity.safePath(value);
}

function existing(value, directory) {
  const absolute = absoluteName(value);
  const stat = fs.lstatSync(absolute, {bigint: true});
  need(!stat.isSymbolicLink() && (directory ? stat.isDirectory() : stat.isFile()), 'PLAIN_PATH');
  need(comparable(fs.realpathSync.native(absolute)) === comparable(absolute), 'PATH_ALIAS');
  return {absolute, stat};
}

function sameFile(first, second) {
  return ['dev', 'ino', 'size', 'mtimeNs', 'ctimeNs', 'nlink'].every(key => first[key] === second[key]);
}

function capture(file, maximum, expected) {
  const named = existing(file, false);
  const fd = fs.openSync(named.absolute, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    const before = fs.fstatSync(fd, {bigint: true});
    need(before.isFile() && before.nlink === 1n && sameFile(before, named.stat), 'FILE_IDENTITY');
    need(before.size >= 0n && before.size <= BigInt(maximum), 'FILE_BUDGET');
    const body = Buffer.alloc(Number(before.size));
    for (let offset = 0; offset < body.length;) {
      const amount = fs.readSync(fd, body, offset, body.length - offset, offset);
      need(amount > 0, 'FILE_CHANGED'); offset += amount;
    }
    need(fs.readSync(fd, Buffer.alloc(1), 0, 1, body.length) === 0, 'FILE_CHANGED');
    const result = {bytes: body.length, sha256: hash(body)};
    if (expected) {
      need((expected.bytes === undefined || result.bytes === expected.bytes) &&
        result.sha256 === expected.sha256, 'FILE_DIGEST');
    }
    need(sameFile(before, fs.fstatSync(fd, {bigint: true})) &&
      sameFile(before, existing(file, false).stat), 'FILE_CHANGED');
    return {...result, body, absolute: named.absolute};
  } finally { fs.closeSync(fd); }
}

function parseJson(body) {
  need(isUtf8(body), 'JSON_UTF8');
  const source = body.toString('utf8'), stack = [];
  let nodes = 0;
  // Bound nesting and reject duplicate object keys before JSON.parse can select
  // a different interpretation of the explicitly pinned source declaration.
  for (let index = 0; index < source.length; ++index) {
    const ch = source[index];
    if (ch === '"') {
      const begin = index;
      for (++index; index < source.length; ++index) {
        if (source[index] === '\\') ++index;
        else if (source[index] === '"') break;
      }
      let next = index + 1;
      while (next < source.length && /\s/.test(source[next])) ++next;
      if (source[next] === ':' && stack.at(-1)?.keys) {
        let key;
        try { key = JSON.parse(source.slice(begin, index + 1)); }
        catch { throw new Error('E_DEVTOOLS_FRONTEND_JSON'); }
        const keys = stack.at(-1).keys;
        need(!keys.has(key), 'JSON_DUPLICATE'); keys.add(key);
      }
      need(++nodes <= 32768, 'JSON_NODES');
    } else if (ch === '{' || ch === '[') {
      stack.push(ch === '{' ? {keys: new Set()} : {});
      need(stack.length <= 32 && ++nodes <= 32768, 'JSON_DEPTH');
    } else if (ch === '}' || ch === ']') stack.pop();
  }
  try { return JSON.parse(source); }
  catch { throw new Error('E_DEVTOOLS_FRONTEND_JSON'); }
}

function roster(value) {
  need(Array.isArray(value) && value.length === memberCount, 'MEMBER_COUNT');
  const seen = new Set(); let total = 0, previous = '';
  for (const row of value) {
    need(exact(row, ['path', 'bytes', 'sha256']) && pin(row.sha256) && Number.isSafeInteger(row.bytes) &&
      row.bytes >= 0 && row.bytes <= limits.fileBytes, 'MANIFEST_ROW');
    relativeName(row.path);
    need(row.path > previous && !seen.has(row.path.toLowerCase()), 'CASE_ALIAS');
    previous = row.path; seen.add(row.path.toLowerCase());
    total += row.bytes; need(total <= limits.totalBytes, 'TOTAL_BUDGET');
  }
  need(seen.has('front_end/inspector.html') && !seen.has(manifestName), 'ENTRY');
  return value;
}

function walk(root, layoutDepth = 0) {
  existing(root, true);
  const names = [], seen = new Set(); let directories = 0;
  function visit(prefix) {
    const current = prefix ? identity.safePath(root, prefix) : root;
    existing(current, true); need(++directories <= limits.directories, 'DIRECTORY_BUDGET');
    const children = fs.readdirSync(current);
    need(children.length <= limits.files + 8, 'FILE_COUNT');
    for (const child of children.sort()) {
      const name = relativeName(prefix ? prefix + '/' + child : child, layoutDepth);
      need(!seen.has(name.toLowerCase()), 'CASE_ALIAS'); seen.add(name.toLowerCase());
      const file = identity.safePath(root, name), stat = fs.lstatSync(file);
      need(!stat.isSymbolicLink(), 'PLAIN_PATH');
      if (stat.isDirectory()) visit(name);
      else { need(stat.isFile(), 'PLAIN_PATH'); names.push(name); need(names.length <= limits.files, 'FILE_COUNT'); }
    }
  }
  visit(''); return names.sort();
}

function closedTree(root, names, layoutDepth = 0) {
  need(JSON.stringify(walk(root, layoutDepth)) === JSON.stringify([...names].sort()), 'CLOSURE');
}

function disconnected(first, second) {
  for (const [base, candidate] of [[first, second], [second, first]]) {
    const relative = path.relative(base, candidate);
    need(relative && (relative === '..' || relative.startsWith('..' + path.sep) || path.isAbsolute(relative)), 'OVERLAP');
  }
}

function put(file, body) {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  existing(path.dirname(file), true);
  const fd = fs.openSync(file, 'wx');
  try { fs.writeFileSync(fd, body); fs.fsyncSync(fd); }
  finally { fs.closeSync(fd); }
}

function compose(options) {
  need(exact(options, ['baselineRoot', 'baselineManifest', 'baselineManifestSha256', 'sourceRoot',
    'sourceSha256', 'kernelSha256', 'output']) &&
    [options.baselineManifestSha256, options.sourceSha256, options.kernelSha256].every(pin), 'CONFIGURATION');
  const baseline = existing(options.baselineRoot, true).absolute;
  const sourceRoot = existing(options.sourceRoot, true).absolute;
  const output = absoluteName(options.output), manifestFile = absoluteName(options.baselineManifest);
  existing(path.dirname(output), true);
  need(!fs.existsSync(output), 'OUTPUT_EXISTS');
  disconnected(output, baseline); disconnected(output, sourceRoot); disconnected(output, manifestFile);
  const manifest = capture(manifestFile, limits.manifestBytes, {sha256: options.baselineManifestSha256});
  const members = roster(parseJson(manifest.body));
  closedTree(baseline, members.map(row => row.path));
  const source = capture(identity.safePath(sourceRoot, 'source.json'), limits.manifestBytes, {sha256: options.sourceSha256});
  const declaration = parseJson(source.body);
  need(exact(declaration, ['schema', 'base', 'files']) && declaration.schema === sourceSchema &&
    exact(declaration.base, ['manifestSha256', 'members']) && declaration.base.members === memberCount &&
    declaration.base.manifestSha256 === options.baselineManifestSha256 &&
    Array.isArray(declaration.files) && declaration.files.length === overrideNames.length, 'SOURCE');
  const membersByName = new Map(members.map(row => [row.path, row]));
  const replacements = new Map(), captured = [manifest, source];
  for (let index = 0; index < declaration.files.length; ++index) {
    const row = declaration.files[index], name = overrideNames[index];
    need(exact(row, ['path', 'bytes', 'sha256', 'baseSha256']) && row.path === 'overrides/' + name &&
      Number.isSafeInteger(row.bytes) && row.bytes >= 0 && row.bytes <= limits.fileBytes &&
      pin(row.sha256) && pin(row.baseSha256) && membersByName.get(name)?.sha256 === row.baseSha256, 'OVERRIDE');
    relativeName(row.path);
    const file = capture(identity.safePath(sourceRoot, row.path), limits.fileBytes, row);
    replacements.set(name, file); captured.push(file);
  }
  closedTree(identity.safePath(sourceRoot, 'overrides'), overrideNames);
  const payload = new Map(), finalMembers = []; let totalBytes = 0;
  for (const row of members) {
    const original = capture(identity.safePath(baseline, row.path), limits.fileBytes, row);
    captured.push(original);
    const selected = replacements.get(row.path) || original;
    totalBytes += selected.bytes; need(totalBytes <= limits.totalBytes, 'TOTAL_BUDGET');
    payload.set(row.path, selected.body);
    finalMembers.push({path: row.path, bytes: selected.bytes, sha256: selected.sha256});
  }
  const finalManifest = encoded(finalMembers);
  need(finalManifest.length <= limits.manifestBytes, 'MANIFEST_BUDGET');
  // No output is created until all source bytes, declarations and budgets pass.
  fs.mkdirSync(output);
  existing(output, true);
  const payloadRoot = path.join(output, 'payload'); fs.mkdirSync(payloadRoot);
  for (const [name, body] of payload) put(identity.safePath(payloadRoot, name), body);
  put(path.join(output, manifestName), finalManifest);
  capture(path.join(output, manifestName), limits.manifestBytes, {bytes: finalManifest.length, sha256: hash(finalManifest)});
  closedTree(payloadRoot, finalMembers.map(row => row.path));
  for (const row of finalMembers) capture(identity.safePath(payloadRoot, row.path), limits.fileBytes, row);
  // A later source edit cannot silently publish a successful receipt for an
  // earlier mix of inputs. Failures retain their new directory for inspection.
  closedTree(baseline, members.map(row => row.path));
  closedTree(identity.safePath(sourceRoot, 'overrides'), overrideNames);
  for (const file of captured) capture(file.absolute, file.body.length, file);
  const receipt = {
    schema,
    baseline: {manifestSha256: manifest.sha256, members: memberCount},
    source: {sha256: source.sha256, overrides: overrideNames.length},
    requestedKernelSha256: options.kernelSha256,
    kernelVerified: false,
    frontend: {manifest: manifestName, manifestSha256: hash(finalManifest), members: memberCount, totalBytes},
    overrides: declaration.files.map((row, index) => ({path: overrideNames[index],
      bytes: row.bytes, sha256: row.sha256, baseSha256: row.baseSha256})),
    approvalCreated: false,
    nativePolicyChanged: false,
    runtimeVerified: false,
  };
  const receiptBody = encoded(receipt);
  need(receiptBody.length <= limits.manifestBytes, 'RECEIPT_BUDGET');
  put(path.join(output, receiptName), receiptBody);
  capture(path.join(output, receiptName), limits.manifestBytes, {bytes: receiptBody.length, sha256: hash(receiptBody)});
  closedTree(output, [manifestName, receiptName, ...finalMembers.map(row => 'payload/' + row.path)], 1);
  return {schema, output, payloadRoot, manifestSha256: receipt.frontend.manifestSha256,
    sourceSha256: source.sha256, requestedKernelSha256: options.kernelSha256, members: memberCount,
    receiptSha256: hash(encoded(receipt))};
}

function reported(options) {
  try { return compose(options); }
  catch (error) {
    if (/^E_DEVTOOLS_FRONTEND_[A-Z_]+$/.test(error.message)) throw error;
    throw new Error('E_DEVTOOLS_FRONTEND_IO');
  }
}

function cli(argv) {
  need(argv.shift() === 'compose', 'COMMAND');
  const names = ['baseline-root', 'baseline-manifest', 'baseline-manifest-sha256', 'source-root',
    'source-sha256', 'kernel-sha256', 'output'], options = {};
  while (argv.length) {
    const option = argv.shift();
    need(option?.startsWith('--') && names.includes(option.slice(2)) && argv.length &&
      !argv[0].startsWith('--'), 'ARGUMENT');
    const key = option.slice(2).replace(/-([a-z0-9])/g, (_, letter) => letter.toUpperCase());
    need(!Object.hasOwn(options, key), 'ARGUMENT'); options[key] = argv.shift();
  }
  need(Object.keys(options).length === names.length, 'ARGUMENT');
  return reported(options);
}

module.exports = {compose: reported};
if (require.main === module) {
  try { console.log(JSON.stringify(cli(process.argv.slice(2)))); }
  catch (error) {
    console.error(/^E_DEVTOOLS_FRONTEND_[A-Z_]+$/.test(error.message) ? error.message : 'E_DEVTOOLS_FRONTEND_IO');
    process.exitCode = 1;
  }
}
