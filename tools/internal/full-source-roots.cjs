'use strict';
const fs = require('node:fs');
const path = require('node:path');
const id = require('./delivery-identity.cjs');
const schema = 'soui.full-source-roots/v1';
const publicPrefixes = ['third_party/sources/soui', '.github', 'experiments/webview2-consumer', 'third_party/patches/miniblink'];
const sdkSchema = 'soui.sdk-renderer-v2-abi-raw-source-manifest/v1';
const kernelSchema = 'soui.full-kernel-composed-source/v1';
const reserved = 'source-manifest.json';
function keys(value, expected, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      JSON.stringify(Object.keys(value).sort()) !== JSON.stringify([...expected].sort())) throw new Error(`Invalid ${label} fields`);
}
function regular(file) {
  file = id.safePath(file);
  if (!fs.statSync(file).isFile()) throw new Error(`Not a regular source file: ${file}`);
  return file;
}
function pinned(pin) {
  keys(pin, ['path', 'bytes', 'sha256'], 'manifest pin');
  if (typeof pin.path !== 'string' || !path.isAbsolute(pin.path) || !Number.isSafeInteger(pin.bytes) || pin.bytes < 0 ||
      !/^[a-f0-9]{64}$/.test(pin.sha256)) throw new Error('Invalid source manifest pin');
  const data = fs.readFileSync(regular(pin.path));
  if (data.length !== pin.bytes || id.hash(data) !== pin.sha256) throw new Error('Source manifest pin differs');
  return data;
}
function contained(root, child) {
  const relative = path.relative(root, child);
  return relative === '' || (!relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative));
}
function exact(root, rows, omitManifest = false) {
  const actual = id.walk(root).filter(name => !(omitManifest && name === reserved)).sort();
  const expected = rows.map(row => row.path).sort();
  if (JSON.stringify(actual) !== JSON.stringify(expected)) throw new Error('Exact source file set differs from manifest');
}
function sourceContent(root, row) {
  const raw = fs.readFileSync(regular(id.safePath(root, row.path)));
  const matches = bytes => bytes.length === row.bytes && id.hash(bytes) === row.sha256;
  if (matches(raw)) return raw;
  const normalized = id.normalize(raw, row.normalization);
  if (!matches(normalized)) throw new Error(`Source missing or modified: ${row.path}`);
  return normalized;
}
function rawRows(role, manifest, root) {
  const generic = manifest.schema === 'soui.raw-source/v1';
  if (generic) {
    keys(manifest, ['schema', 'sourceOnly', 'root', 'files'], 'raw-source manifest');
    if (manifest.sourceOnly !== true || path.resolve(manifest.root) !== root) throw new Error('Invalid raw source owner');
  } else if (role === 'sdk') {
    if (manifest.schema !== sdkSchema || manifest.sourceOnly !== true || path.resolve(manifest.root) !== root) throw new Error('Invalid SDK raw source manifest');
  } else if (manifest.schema !== kernelSchema || manifest.sourceOnly !== true || manifest.executionEnabled !== false ||
      path.resolve(manifest.copySourceRoot) !== root) throw new Error('Invalid kernel raw source manifest');
  if (!Array.isArray(manifest.files)) throw new Error('Missing raw source rows');
  const rows = manifest.files.map(row => ({path: generic ? row.path : row.relative, bytes: !generic && role === 'sdk' ? row.pin?.bytes : row.bytes,
    sha256: !generic && role === 'sdk' ? row.pin?.sha256 : row.sha256, normalization: 'none'}));
  id.validateFiles(rows);
  for (const row of rows) {
    if (id.skipped.has(row.path.split('/')[0].toLowerCase())) throw new Error(`Private/generated root refused: ${row.path}`);
    if (role === 'sdk' && /^third_party\/sources\/(miniblink|soui)(\/|$)/i.test(row.path)) throw new Error('SDK raw input already contains a full native source root');
  }
  exact(root, rows);
  for (const row of rows) sourceContent(root, row);
  return rows;
}
function prepare(bindingFile, output) {
  const binding = id.readJson(regular(bindingFile));
  keys(binding, ['schema', 'sdk', 'miniblink', 'publicSource'], 'source-roots binding');
  if (binding.schema !== schema) throw new Error('Invalid source-roots schema');
  output = id.safePath(output);
  if (fs.existsSync(output)) throw new Error('Source stage already exists');
  const inputs = [];
  for (const role of ['sdk', 'miniblink', 'publicSource']) {
    const entry = binding[role];
    keys(entry, ['root', 'manifest'], `${role} source root`);
    if (typeof entry.root !== 'string' || !path.isAbsolute(entry.root)) throw new Error('Source root must be absolute');
    const root = id.safePath(entry.root);
    if (!fs.statSync(root).isDirectory()) throw new Error('Source root must be a directory');
    // publicSource is a repository locator. Only its four fixed subtrees are
    // payload inputs; unrelated private out directories are never selected.
    if (role !== 'publicSource' && (contained(root, output) || contained(output, root))) throw new Error('Source output overlaps an input root');
    if (role !== 'publicSource' && inputs.some(other => contained(root, other.root) || contained(other.root, root))) throw new Error('Source input roots overlap');
    const manifest = JSON.parse(pinned(entry.manifest).toString('utf8').replace(/^\uFEFF/, ''));
    inputs.push({role, root, pin: entry.manifest, manifest});
  }
  const sdk = inputs[0], kernel = inputs[1], prior = inputs[2];
  for (const prefix of publicPrefixes) {
    const root = id.safePath(prior.root, prefix);
    if ([output, sdk.root, kernel.root].some(other => contained(root, other) || contained(other, root))) throw new Error('Public-source subtree overlaps another input/output');
  }
  sdk.rows = rawRows('sdk', sdk.manifest, sdk.root);
  kernel.rows = rawRows('miniblink', kernel.manifest, kernel.root);
  const versionRow = sdk.rows.find(row => row.path === 'VERSION');
  if (!versionRow) throw new Error('SDK raw source is missing VERSION');
  const version = sourceContent(sdk.root, versionRow).toString('utf8').trim();
  if (prior.manifest.schema !== 'soui.full-source/v2' || prior.manifest.releaseReady !== false || prior.manifest.version !== version) throw new Error('Invalid public-source manifest/version');
  id.validateFiles(prior.manifest.files);
  prior.rows = prior.manifest.files.filter(row => publicPrefixes.some(prefix => row.path.startsWith(`${prefix}/`)));
  if (!prior.rows.some(row => row.path === 'third_party/sources/soui/SOURCE-MANIFEST.json')) throw new Error('Missing standard SOUI source manifest');
  for (const row of prior.rows) {
    if (!['lf', 'none'].includes(row.normalization)) throw new Error('Invalid public source normalization');
    sourceContent(prior.root, row);
  }
  function checkPublic() {
    for (const prefix of publicPrefixes) {
      const expected = prior.rows.filter(row => row.path.startsWith(`${prefix}/`)).map(row => ({path: row.path.slice(prefix.length + 1)}));
      if (!expected.length && !fs.existsSync(id.safePath(prior.root, prefix))) continue;
      exact(id.safePath(prior.root, prefix), expected);
    }
  }
  checkPublic();
  const selected = new Map();
  function add(input, row, name) {
    if (selected.has(name.toLowerCase())) throw new Error(`Conflicting full-source member: ${name}`);
    selected.set(name.toLowerCase(), {input, row, name});
  }
  for (const row of sdk.rows) if (row.path !== reserved) add(sdk, row, row.path);
  for (const row of kernel.rows) add(kernel, row, `third_party/sources/miniblink/${row.path}`);
  for (const row of prior.rows) add(prior, row, row.path);
  const entries = [...selected.values()].sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
  const files = entries.map(entry => ({...entry.row, path: entry.name}));
  id.validateFiles(files);
  const manifest = {schema: 'soui.full-source/v2', version, releaseReady: false,
    identity: 'Explicit pinned source roots; current raw SDK and complete kernel; canonical public SOUI/build-chain rows.',
    sourceComposition: {schema: 'soui.full-source-composition/v1', sourceOnly: true, currentBinaryProvenance: null,
      historicalPrebuiltBinaries: true, excludedHistoricalEvidence: 'verification/** from the prior full-source manifest',
      inputs: inputs.map(input => ({role: input.role, manifest: {bytes: input.pin.bytes, sha256: input.pin.sha256}, files: input.rows.length}))}, files};
  function recheck() {
    for (const input of inputs) {
      pinned(input.pin);
      if (input !== prior) exact(input.root, input.rows);
      for (const row of input.rows) sourceContent(input.root, row);
    }
    checkPublic();
  }
  return {manifest, read: name => {
    const entry = selected.get(name.toLowerCase());
    if (!entry || entry.name !== name) throw new Error('Undeclared full-source member');
    return sourceContent(entry.input.root, entry.row);
  }, recheck};
}
module.exports = {prepare, exact};
