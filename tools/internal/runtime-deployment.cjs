'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const id = require('./delivery-identity.cjs');

const receiptName = '.soui-runtime.json';
const manifestName = 'tools/internal/runtime-files.json';
const componentNames = ['crt', 'soui-core', 'soui-gdi', 'soui-resources', 'miniblink'];
const architectures = ['x86', 'x64'];
const backends = ['miniblink', 'webview2', 'auto'];
const externalWebView2 = Object.freeze({deployment: 'external', selection: 'installed-or-explicit-fixed',
  required: false, fallback: 'miniblink', download: false});
const maxRuntimeBytes = 512 * 1024 * 1024;
const fail = message => { throw new Error(`E_RUNTIME: ${message}`); };
const object = value => value && typeof value === 'object' && !Array.isArray(value);
function keys(value, required, optional = []) {
  if (!object(value) || required.some(key => !Object.hasOwn(value, key)) ||
      Object.keys(value).some(key => !required.includes(key) && !optional.includes(key))) fail('invalid object fields');
}
function names(value, allowed, label) {
  if (!Array.isArray(value) || !value.length || value.some(name => typeof name !== 'string' || !allowed.includes(name)) ||
      new Set(value).size !== value.length) fail(`invalid ${label}`);
}
function location(file) {
  keys(file, ['source', 'path', 'bytes', 'sha256', 'component']);
  if (typeof file.source !== 'string' || typeof file.path !== 'string' || !/^[^/]+\.dll$/i.test(file.path) ||
      !Number.isSafeInteger(file.bytes) || file.bytes < 1 || file.bytes > maxRuntimeBytes || typeof file.sha256 !== 'string' ||
      !/^[a-f0-9]{64}$/.test(file.sha256) || !componentNames.includes(file.component)) fail('invalid runtime file');
  id.safePath(process.cwd(), file.source);
  id.safePath(process.cwd(), file.path);
}
function assertArchitecture(data, architecture, name) {
  if (data.length < 64 || data.toString('ascii', 0, 2) !== 'MZ') fail(`invalid PE runtime: ${name}`);
  const offset = data.readUInt32LE(0x3c);
  if (offset > data.length - 6 || data.toString('ascii', offset, offset + 4) !== 'PE\0\0' ||
      data.readUInt16LE(offset + 4) !== (architecture === 'x64' ? 0x8664 : 0x14c)) fail(`runtime architecture mismatch: ${name}`);
}
function checkedFile(root, file, architecture, source = file.source) {
  const full = id.safePath(root, source);
  const stat = fs.statSync(full);
  if (!stat.isFile() || stat.nlink !== 1) fail(`runtime is not a regular unlinked file: ${source}`);
  if (stat.size !== file.bytes || stat.size > maxRuntimeBytes) fail(`runtime identity mismatch: ${source}`);
  const data = fs.readFileSync(full);
  if (data.length !== file.bytes || id.hash(data) !== file.sha256) fail(`runtime identity mismatch: ${source}`);
  assertArchitecture(data, architecture, source);
  return full;
}
function closure(selected, definitions) {
  const result = new Set(), active = new Set();
  function visit(name) {
    if (active.has(name)) fail('component dependency cycle');
    if (result.has(name)) return;
    active.add(name);
    for (const dependency of definitions[name].dependsOn) visit(dependency);
    active.delete(name);
    result.add(name);
  }
  selected.forEach(visit);
  return [...result].sort();
}
function readManifest(distribution) {
  const full = id.safePath(distribution, manifestName);
  const stat = fs.statSync(full);
  if (!stat.isFile() || stat.nlink !== 1 || stat.size > 1024 * 1024) fail('runtime manifest is not a regular bounded file');
  const manifest = id.readJson(full);
  keys(manifest, ['schema', 'components', 'backends', 'architectures']);
  if (manifest.schema !== 'soui.runtime-files/v1') fail('unsupported runtime manifest');
  keys(manifest.components, componentNames);
  for (const definition of Object.values(manifest.components)) {
    keys(definition, ['dependsOn']);
    if (!Array.isArray(definition.dependsOn) || definition.dependsOn.some(name => !componentNames.includes(name)) ||
        new Set(definition.dependsOn).size !== definition.dependsOn.length) fail('invalid dependencies');
  }
  closure(componentNames, manifest.components);
  keys(manifest.backends, ['miniblink', 'webview2'], ['auto']);
  for (const backend of Object.keys(manifest.backends)) {
    const definition = manifest.backends[backend];
    keys(definition, ['default', 'required', 'allowed']);
    for (const field of ['default', 'required', 'allowed']) names(definition[field], componentNames, `${backend} ${field}`);
    if ([...definition.default, ...definition.required].some(name => !definition.allowed.includes(name)) ||
        closure(definition.allowed, manifest.components).some(name => !definition.allowed.includes(name))) fail('inconsistent backend components');
  }
  keys(manifest.architectures, architectures);
  for (const architecture of architectures) {
    const files = manifest.architectures[architecture];
    if (!Array.isArray(files) || !files.length || files.length > 1000) fail('invalid architecture file list');
    const paths = new Set(), sources = new Set();
    for (const file of files) {
      location(file);
      if (!file.source.startsWith(`third_party/prebuilt/${architecture}/`) || !/\.dll$/i.test(file.source) ||
          path.posix.basename(file.source).toLowerCase() !== file.path.toLowerCase() ||
          paths.has(file.path.toLowerCase()) || sources.has(file.source.toLowerCase())) fail('duplicate or invalid runtime source/path');
      paths.add(file.path.toLowerCase()); sources.add(file.source.toLowerCase());
    }
    if (componentNames.some(name => !files.some(file => file.component === name))) fail('component has no files');
  }
  return {manifest, manifestSha256: id.hash(id.encoded(manifest))};
}

function resolve(distribution, architecture, backend = 'miniblink', options = {}) {
  keys(options, [], ['projectRoot', 'components', 'kernelSource', 'kernelSha256', 'adoptLegacy']);
  if (options.adoptLegacy !== undefined && typeof options.adoptLegacy !== 'boolean') fail('adoptLegacy must be boolean');
  for (const field of ['projectRoot', 'kernelSource', 'kernelSha256']) {
    if (options[field] !== undefined && typeof options[field] !== 'string') fail(`invalid ${field}`);
  }
  distribution = id.safePath(distribution);
  if (!architectures.includes(architecture) || !backends.includes(backend)) fail('unsupported architecture/backend');
  const {manifest, manifestSha256} = readManifest(distribution);
  const definition = manifest.backends[backend];
  if (!definition) fail('SDK manifest does not provide requested backend');
  let selected = options.components;
  if (selected === undefined && options.projectRoot) {
    const project = id.safePath(options.projectRoot, 'project.json');
    if (fs.existsSync(project)) {
      if (fs.statSync(project).size > 1024 * 1024) fail('project configuration exceeds size limit');
      const config = id.readJson(project);
      if (!object(config)) fail('invalid project configuration');
      selected = config.runtimeComponents;
    }
  }
  if (selected === undefined) selected = definition.default;
  names(selected, definition.allowed, 'runtimeComponents');
  const components = closure([...selected, ...definition.required], manifest.components);
  const files = manifest.architectures[architecture].filter(file => components.includes(file.component))
    .map(file => ({...file})).sort((a, b) => a.path.localeCompare(b.path, 'en'));
  const overrides = new Map();
  let kernel;
  if (options.kernelSource || options.kernelSha256) {
    if (backend !== 'miniblink') fail('kernel override requires miniblink');
    const original = files.find(file => file.path === 'node.dll');
    const source = options.kernelSource ? id.safePath(options.kernelSource) : id.safePath(distribution, original.source);
    const ordinary = id.safePath(distribution, original.source);
    if (source.toLowerCase() !== ordinary.toLowerCase()) {
      if (typeof options.kernelSha256 !== 'string' || !/^[a-f0-9]{64}$/.test(options.kernelSha256)) fail('external kernel requires explicit SHA-256');
      const stat = fs.statSync(source);
      if (!stat.isFile() || stat.nlink !== 1 || stat.size > maxRuntimeBytes) fail('external kernel is not a regular bounded file');
      const data = fs.readFileSync(source);
      if (id.hash(data) !== options.kernelSha256) fail('external kernel identity mismatch');
      assertArchitecture(data, architecture, 'node.dll');
      original.bytes = data.length; original.sha256 = options.kernelSha256;
      overrides.set(original.path, source);
      kernel = {external: true, sha256: options.kernelSha256};
    } else if (options.kernelSha256 && options.kernelSha256 !== original.sha256) fail('kernel approval does not match ordinary runtime');
  }
  for (const file of files) {
    const override = overrides.get(file.path);
    checkedFile(override ? path.dirname(override) : distribution, file, architecture, override ? path.basename(override) : file.source);
  }
  const plan = {schema: 'soui.runtime/v1', architecture, backend, components, manifestSha256, files,
    ...(backend === 'auto' ? {externalWebView2: {...externalWebView2}} : {}), ...(kernel ? {kernel} : {})};
  Object.defineProperties(plan, {distribution: {value: distribution}, sourceOverrides: {value: overrides}});
  return plan;
}
function validateReceipt(receipt) {
  keys(receipt, ['schema', 'architecture', 'backend', 'components', 'manifestSha256', 'files'], ['kernel', 'externalWebView2']);
  if (receipt.schema !== 'soui.runtime/v1' || !architectures.includes(receipt.architecture) || !backends.includes(receipt.backend) ||
      typeof receipt.manifestSha256 !== 'string' || !/^[a-f0-9]{64}$/.test(receipt.manifestSha256)) fail('invalid runtime receipt');
  names(receipt.components, componentNames, 'receipt components');
  if (!Array.isArray(receipt.files) || !receipt.files.length || receipt.files.length > 1000) fail('invalid receipt files');
  const paths = new Set();
  for (const file of receipt.files) {
    location(file);
    if (!file.source.startsWith(`third_party/prebuilt/${receipt.architecture}/`) || !receipt.components.includes(file.component) ||
        path.posix.basename(file.source).toLowerCase() !== file.path.toLowerCase() ||
        paths.has(file.path.toLowerCase())) fail('invalid receipt path or component');
    paths.add(file.path.toLowerCase());
  }
  if (receipt.kernel) {
    keys(receipt.kernel, ['external', 'sha256']);
    if (receipt.kernel.external !== true || receipt.backend !== 'miniblink' ||
        receipt.files.find(file => file.path === 'node.dll')?.sha256 !== receipt.kernel.sha256) fail('invalid receipt kernel');
  }
  if (receipt.backend === 'webview2' && (receipt.components.some(name => name !== 'crt') || receipt.files.some(file => file.component !== 'crt')))
    fail('invalid modern runtime components');
  if (['miniblink', 'auto'].includes(receipt.backend) && ['crt', 'soui-core', 'miniblink'].some(name => !receipt.components.includes(name)))
    fail('missing required runtime component');
  if (receipt.backend === 'auto') {
    if (id.encoded(receipt.externalWebView2) !== id.encoded(externalWebView2)) fail('invalid external WebView2 dependency');
  } else if (receipt.externalWebView2 !== undefined) fail('external WebView2 declaration requires auto');
  if (receipt.components.some(name => !receipt.files.some(file => file.component === name))) fail('receipt component has no files');
  return receipt;
}
function readReceipt(application) {
  const file = id.safePath(application, receiptName);
  if (!fs.existsSync(file)) return null;
  const stat = fs.statSync(file);
  if (!stat.isFile() || stat.nlink !== 1 || stat.size > 1024 * 1024) fail('invalid runtime receipt file');
  return validateReceipt(id.readJson(file));
}
function verifyDirectory(application, expected) {
  application = id.safePath(application);
  const receipt = readReceipt(application);
  if (!receipt) fail('runtime receipt missing');
  if (expected && id.encoded(receipt) !== id.encoded(validateReceipt(expected))) fail('runtime receipt does not match expected plan');
  for (const file of receipt.files) checkedFile(application, file, receipt.architecture, file.path);
  return receipt;
}

function deploy(distribution, application, architecture, backend = 'miniblink', options = {}) {
  const plan = resolve(distribution, architecture, backend, options);
  application = id.safePath(application);
  const old = readReceipt(application);
  if (old?.kernel?.external && !plan.kernel?.external)
    fail('paired kernel transition requires explicit clean deployment/approval');
  const previous = new Map((old?.files || []).map(file => [file.path.toLowerCase(), file]));
  const selected = new Map(plan.files.map(file => [file.path.toLowerCase(), file]));
  const affected = [...new Set([...previous.keys(), ...selected.keys()])];
  const initial = new Map();
  // Preflight all conflicts before staging or touching the existing application.
  for (const name of affected) {
    const file = previous.get(name) || selected.get(name);
    const full = id.safePath(application, file.path);
    if (!fs.existsSync(full)) { initial.set(name, null); continue; }
    const stat = fs.statSync(full);
    if (!stat.isFile() || stat.nlink !== 1 || stat.size > maxRuntimeBytes) fail(`runtime destination is not a regular bounded file: ${file.path}`);
    const data = fs.readFileSync(full), current = {bytes: data.length, sha256: id.hash(data)};
    initial.set(name, current);
    const expected = previous.get(name) || selected.get(name);
    if ((!options.adoptLegacy || old) && (current.bytes !== expected.bytes || current.sha256 !== expected.sha256)) fail(`unowned or modified runtime conflict: ${file.path}`);
  }
  // Match the former copy_if_different behavior for loaded DLLs. Source and
  // destination identities have already been checked; an unchanged complete
  // deployment needs no filesystem writes or renames.
  if (old && id.encoded(old) === id.encoded(plan) && affected.every(name => initial.get(name))) {
    if (id.encoded(readReceipt(application)) !== id.encoded(old)) fail('runtime receipt changed during deployment');
    return plan;
  }
  fs.mkdirSync(application, {recursive: true});
  const token = crypto.randomUUID();
  const stage = id.safePath(application, `.runtime-new-${token}`);
  const backup = id.safePath(application, `.runtime-old-${token}`);
  const moved = [], installed = [];
  try {
    fs.mkdirSync(stage); fs.mkdirSync(backup);
    for (const file of plan.files) {
      const source = plan.sourceOverrides.get(file.path) || id.safePath(plan.distribution, file.source);
      fs.copyFileSync(source, id.safePath(stage, file.path), fs.constants.COPYFILE_EXCL);
    }
    fs.writeFileSync(id.safePath(stage, receiptName), id.encoded(plan), {flag: 'wx'});
    verifyDirectory(stage, plan);
    // Reject concurrent source/destination mutations before publication.
    for (const name of affected) {
      const file = previous.get(name) || selected.get(name), full = id.safePath(application, file.path);
      const exists = fs.existsSync(full), expected = initial.get(name);
      if (exists !== !!expected) fail(`runtime destination changed during deployment: ${file.path}`);
      if (exists && (!fs.statSync(full).isFile() || fs.statSync(full).size !== expected.bytes ||
          id.hash(fs.readFileSync(full)) !== expected.sha256)) fail(`runtime destination changed during deployment: ${file.path}`);
    }
    const currentReceipt = readReceipt(application);
    if (id.encoded(currentReceipt) !== id.encoded(old)) fail('runtime receipt changed during deployment');
    for (const name of [...affected.map(name => (previous.get(name) || selected.get(name)).path), receiptName]) {
      const full = id.safePath(application, name);
      if (fs.existsSync(full)) { fs.renameSync(full, id.safePath(backup, name)); moved.push(name); }
    }
    for (const name of [...plan.files.map(file => file.path), receiptName]) {
      fs.renameSync(id.safePath(stage, name), id.safePath(application, name)); installed.push(name);
    }
    verifyDirectory(application, plan);
  } catch (error) {
    for (const name of installed.reverse()) fs.unlinkSync(id.safePath(application, name));
    for (const name of moved.reverse()) fs.renameSync(id.safePath(backup, name), id.safePath(application, name));
    throw error;
  } finally {
    if (fs.existsSync(stage)) fs.rmSync(id.safePath(application, path.basename(stage)), {recursive: true});
    // A failed rollback must retain backup files for recovery.
    if (fs.existsSync(backup) && fs.readdirSync(backup).length === 0) fs.rmdirSync(backup);
  }
  if (fs.existsSync(backup)) fs.rmSync(id.safePath(application, path.basename(backup)), {recursive: true});
  return plan;
}

module.exports = {resolve, deploy, verifyDirectory, validateReceipt, receiptName};
if (require.main === module) {
  try {
    const [command, ...argv] = process.argv.slice(2), args = {};
    const allowed = ['sdk-root', 'application', 'architecture', 'backend', 'project-root', 'kernel-source', 'kernel-sha256', 'adopt-legacy'];
    if (command !== 'deploy') fail('usage: runtime-deployment.cjs deploy --sdk-root SDK --application APP --architecture x64 --backend miniblink [--project-root PROJECT] [--adopt-legacy]');
    for (let n = 0; n < argv.length; ++n) {
      const name = argv[n].replace(/^--/, '');
      if (!argv[n].startsWith('--') || !allowed.includes(name) || Object.hasOwn(args, name)) fail('invalid command argument');
      args[name] = name === 'adopt-legacy' ? true : argv[++n];
      if (args[name] === undefined) fail('missing command argument value');
    }
    if (['sdk-root', 'application', 'architecture', 'backend'].some(name => !args[name])) fail('missing command argument');
    console.log(JSON.stringify(deploy(args['sdk-root'], args.application, args.architecture, args.backend,
      {projectRoot: args['project-root'], kernelSource: args['kernel-source'], kernelSha256: args['kernel-sha256'], adoptLegacy: args['adopt-legacy']})));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
