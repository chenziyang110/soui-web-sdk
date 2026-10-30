'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {isUtf8} = require('node:buffer');
const identity = require('./delivery-identity.cjs');

const manifestName = '.soui-web-target.json';
const schema = 'soui.web-target/v1';
const fileLimit = 16 * 1024 * 1024;
const totalLimit = 128 * 1024 * 1024;
const countLimit = 4096;
const need = (condition, code) => { if (!condition) throw new Error(code); };
const plain = value => value && typeof value === 'object' && !Array.isArray(value);
const exact = (value, keys) => plain(value) && Object.keys(value).sort().join(',') === keys.slice().sort().join(',');
const named = value => typeof value === 'string' && /^[a-z][a-z0-9-]{0,47}$/.test(value);
const digest = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);

function relative(root, value) {
  need(typeof value === 'string' && value.length > 0 && value.length <= 512 && value.split('/').length <= 32,
    'E_WEB_TARGET_PATH');
  return identity.safePath(root, value);
}
function read(file, budget = {bytes: 0}) {
  identity.safePath(file);
  const fd = fs.openSync(file, 'r');
  try {
    const before = fs.fstatSync(fd);
    need(before.isFile() && before.nlink === 1, 'E_WEB_TARGET_REGULAR_FILE');
    need(before.size <= fileLimit && (budget.bytes += before.size) <= totalLimit, 'E_WEB_TARGET_BUDGET');
    const bytes = Buffer.alloc(before.size + 1);
    let offset = 0, count;
    while (offset < bytes.length && (count = fs.readSync(fd, bytes, offset, bytes.length - offset, null))) offset += count;
    const after = fs.fstatSync(fd);
    need(offset === before.size && ['dev', 'ino', 'size', 'mtimeMs', 'ctimeMs', 'nlink'].every(key => before[key] === after[key]),
      'E_WEB_TARGET_FILE_CHANGED');
    return bytes.subarray(0, offset);
  } finally { fs.closeSync(fd); }
}
function json(bytes) {
  need(isUtf8(bytes), 'E_WEB_TARGET_UTF8');
  const text = bytes.toString('utf8').replace(/^\uFEFF/, '');
  let depth = 0, quoted = false, escaped = false;
  for (const character of text) {
    if (quoted) {
      if (escaped) escaped = false;
      else if (character === '\\') escaped = true;
      else if (character === '"') quoted = false;
    } else if (character === '"') quoted = true;
    else if (character === '{' || character === '[') need(++depth <= 32, 'E_WEB_TARGET_JSON_DEPTH');
    else if (character === '}' || character === ']') --depth;
  }
  try { return JSON.parse(text); } catch { throw new Error('E_WEB_TARGET_JSON'); }
}
function validateTarget(root, value, checkFiles) {
  need(exact(value, ['name', 'entry', 'publicAssets']) && named(value.name), 'E_WEB_TARGET_CONFIG');
  relative(root, value.name);
  const entry = relative(root, value.entry);
  const segments = value.entry.split('/');
  need(segments[0] === 'web' && segments.at(-1) === 'index.html' &&
    !segments.slice(1, -1).some(part => ['dist', 'public', 'node_modules', '.vite'].includes(part.toLowerCase())),
  'E_WEB_TARGET_ENTRY');
  need(Array.isArray(value.publicAssets) && value.publicAssets.length <= countLimit, 'E_WEB_TARGET_ASSETS');
  const seen = new Set(), budget = {bytes: 0};
  for (const asset of value.publicAssets) {
    const file = relative(path.join(root, 'web/public'), asset);
    const key = asset.toLowerCase();
    need(!seen.has(key) && !['index.html', manifestName, '.soui-web-profile.json'].includes(key) &&
      !['legal', '.vite'].includes(key.split('/')[0]), 'E_WEB_TARGET_ASSET_COLLISION');
    for (const prior of seen) need(!key.startsWith(prior + '/') && !prior.startsWith(key + '/'), 'E_WEB_TARGET_ASSET_COLLISION');
    seen.add(key);
    if (checkFiles) read(file, budget);
  }
  if (checkFiles) read(entry);
  return {name: value.name, entry: value.entry, outDir: 'web/dist/' + value.name, publicAssets: [...value.publicAssets]};
}
function resolve(projectRoot) {
  const root = identity.safePath(projectRoot);
  const file = identity.safePath(root, 'project.json');
  const project = fs.existsSync(file) ? json(read(file)) : {};
  need(plain(project), 'E_WEB_TARGET_PROJECT');
  if (!Object.hasOwn(project, 'webTargets')) return {configured: false, default: null, configurationSha256: null,
    targets: [{name: null, entry: 'web/index.html', outDir: 'web/dist', publicAssets: []}]};
  const config = project.webTargets;
  need(exact(config, ['default', 'targets']) && named(config.default) && Array.isArray(config.targets) &&
    config.targets.length > 0 && config.targets.length <= 32, 'E_WEB_TARGET_CONFIG');
  const targets = config.targets.map(target => validateTarget(root, target, true));
  need(new Set(targets.map(target => target.name)).size === targets.length &&
    new Set(targets.map(target => target.entry.toLowerCase())).size === targets.length &&
    targets.some(target => target.name === config.default), 'E_WEB_TARGET_DUPLICATE');
  const normalized = {default: config.default, targets: targets.map(({outDir, ...target}) => target)};
  return {configured: true, default: config.default,
    configurationSha256: identity.hash(identity.encoded(normalized)), targets};
}
function selection(projectRoot, targetName) {
  const config = resolve(projectRoot);
  if (!config.configured) {
    need(targetName === undefined, 'E_WEB_TARGET_UNCONFIGURED');
    return null;
  }
  const selected = config.targets.find(target => target.name === (targetName ?? config.default));
  need(selected, 'E_WEB_TARGET_UNKNOWN');
  return {...selected, configurationSha256: config.configurationSha256};
}
function inventory(directory, excludeLegal) {
  const root = identity.safePath(directory), budget = {bytes: 0};
  need(fs.statSync(root).isDirectory(), 'E_WEB_TARGET_DIRECTORY');
  let count = 0;
  const names = identity.walk(root, '', name => {
    const file = relative(root, name), stat = fs.statSync(file);
    if (excludeLegal && name.toLowerCase() === 'legal' && stat.isDirectory()) return true;
    if (name === manifestName) return true;
    if (!stat.isDirectory()) need(++count <= countLimit, 'E_WEB_TARGET_FILE_COUNT');
    return false;
  });
  const seen = new Set();
  return names.map(name => {
    need(!seen.has(name.toLowerCase()), 'E_WEB_TARGET_FILE_COLLISION'); seen.add(name.toLowerCase());
    const bytes = read(relative(root, name), budget);
    return {path: name, bytes: bytes.length, sha256: identity.hash(bytes)};
  });
}
function validateSelection(value) {
  need(plain(value) && named(value.name) && digest(value.configurationSha256), 'E_WEB_TARGET_SELECTION');
}
function createManifest(directory, selected) {
  validateSelection(selected);
  validateTarget(directory, {name: selected.name, entry: selected.entry, publicAssets: selected.publicAssets}, false);
  const files = inventory(directory, false);
  need(files.some(file => file.path === 'index.html') && files.some(file => file.path === '.soui-web-profile.json'),
    'E_WEB_TARGET_OUTPUT');
  for (const name of selected.publicAssets) need(files.some(file => file.path === name), 'E_WEB_TARGET_ASSET_MISSING');
  const manifest = {schema, target: selected.name, entry: selected.entry, publicAssets: selected.publicAssets,
    configurationSha256: selected.configurationSha256, files};
  fs.writeFileSync(identity.safePath(directory, manifestName), identity.encoded(manifest));
  return verifyTargetDirectory(directory, selected, {requireManifest: true});
}
function verifyTargetDirectory(directory, expected, options = {}) {
  need(plain(options) && Object.keys(options).every(key => ['requireManifest', 'excludeLegal'].includes(key)) &&
    Object.values(options).every(value => typeof value === 'boolean'), 'E_WEB_TARGET_OPTIONS');
  const marker = identity.safePath(directory, manifestName);
  if (!fs.existsSync(marker)) {
    need(!expected && !options.requireManifest, 'E_WEB_TARGET_MANIFEST_REQUIRED');
    return {targetManifest: false, webTarget: null, configurationSha256: null};
  }
  const manifest = json(read(marker));
  need(exact(manifest, ['schema', 'target', 'entry', 'publicAssets', 'configurationSha256', 'files']) &&
    manifest.schema === schema && digest(manifest.configurationSha256), 'E_WEB_TARGET_MANIFEST');
  validateTarget(directory, {name: manifest.target, entry: manifest.entry, publicAssets: manifest.publicAssets}, false);
  if (expected !== undefined && expected !== null) {
    if (typeof expected === 'string') need(expected === manifest.target, 'E_WEB_TARGET_MISMATCH');
    else {
      validateSelection(expected);
      need(expected.name === manifest.target && expected.configurationSha256 === manifest.configurationSha256 &&
        (expected.entry === undefined || expected.entry === manifest.entry) &&
        (expected.publicAssets === undefined || JSON.stringify(expected.publicAssets) === JSON.stringify(manifest.publicAssets)),
      'E_WEB_TARGET_MISMATCH');
    }
  }
  need(Array.isArray(manifest.files) && manifest.files.length > 0 && manifest.files.length <= countLimit, 'E_WEB_TARGET_MANIFEST');
  for (const file of manifest.files) need(exact(file, ['path', 'bytes', 'sha256']), 'E_WEB_TARGET_MANIFEST');
  identity.validateFiles(manifest.files);
  need(manifest.files.some(file => file.path === 'index.html') &&
    manifest.files.some(file => file.path === '.soui-web-profile.json') &&
    manifest.publicAssets.every(name => manifest.files.some(file => file.path === name)), 'E_WEB_TARGET_OUTPUT');
  const current = inventory(directory, options.excludeLegal === true);
  need(JSON.stringify(current) === JSON.stringify(manifest.files), 'E_WEB_TARGET_FILE_IDENTITY');
  return {targetManifest: true, webTarget: manifest.target, configurationSha256: manifest.configurationSha256};
}
function viteSelection(config) {
  const root = process.env.SOUI_WEB_PROJECT_ROOT ?? path.resolve(config.root ?? process.cwd(), '..');
  // Ordinary SDK/demo Vite builds have no project.json and keep their original configuration.
  if (!fs.existsSync(identity.safePath(root, 'project.json'))) {
    need(!process.env.SOUI_WEB_TARGET && !process.env.SOUI_WEB_PROJECT_ROOT, 'E_WEB_TARGET_PROJECT');
    return null;
  }
  const selected = selection(root, process.env.SOUI_WEB_TARGET);
  return selected ? {root: identity.safePath(root), ...selected} : null;
}
function buildAll(projectRoot) {
  const root = identity.safePath(projectRoot), config = resolve(root);
  for (const target of config.targets) {
    const env = {...process.env};
    delete env.SOUI_WEB_TARGET; delete env.SOUI_WEB_PROJECT_ROOT;
    if (config.configured) { env.SOUI_WEB_TARGET = target.name; env.SOUI_WEB_PROJECT_ROOT = root; }
    // npm's Windows launcher is a batch file. Arguments here are fixed literals;
    // the project path travels through cwd/environment, never a shell expression.
    const run = process.platform === 'win32'
      ? spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/s', '/c', 'npm run build'], {cwd: path.join(root, 'web'), env, stdio: ['inherit', 2, 2]})
      : spawnSync('npm', ['run', 'build'], {cwd: path.join(root, 'web'), env, stdio: ['inherit', 2, 2]});
    if (run.error) throw run.error;
    need(run.status === 0, 'E_WEB_TARGET_BUILD_FAILED');
    if (config.configured) verifyTargetDirectory(path.join(root, target.outDir),
      {...target, configurationSha256: config.configurationSha256}, {requireManifest: true});
  }
  need(JSON.stringify(resolve(root)) === JSON.stringify(config), 'E_WEB_TARGET_CONFIG_CHANGED');
  return config;
}

module.exports = {manifestName, resolve, selection, createManifest, verifyTargetDirectory, viteSelection, buildAll, read};
if (require.main === module) {
  try {
    const [command, root, ...extra] = process.argv.slice(2);
    need(root && extra.length === 0, 'Usage: web-targets.cjs resolve|output|build PROJECT_ROOT');
    if (command === 'resolve') console.log(JSON.stringify(resolve(root)));
    else if (command === 'output') {
      const config = resolve(root), target = config.targets.find(item => item.name === config.default);
      console.log(identity.safePath(root, target.outDir));
    } else if (command === 'build') console.log(JSON.stringify(buildAll(root)));
    else throw new Error('Usage: web-targets.cjs resolve|output|build PROJECT_ROOT');
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
