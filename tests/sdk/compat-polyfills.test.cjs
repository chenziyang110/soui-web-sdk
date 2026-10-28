'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const compat = require('../../tools/internal/compat-polyfills.cjs');
const identity = require('../../tools/internal/delivery-identity.cjs');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const patchFile = path.resolve(__dirname, '../../tools/internal/polyfills/object-create.chrome49.cjs');

// Pristine core-js 3.50.0 test input (MIT; third_party/licenses/core-js-3.50.0).
// Keep the actual upstream bytes so identity tests also detect stale fixtures.
const upstreamModule = `'use strict';
/* global ActiveXObject -- old IE, WSH */
var anObject = require('../internals/an-object');
var definePropertiesModule = require('../internals/object-define-properties');
var enumBugKeys = require('../internals/enum-bug-keys');
var hiddenKeys = require('../internals/hidden-keys');
var html = require('../internals/html');
var documentCreateElement = require('../internals/document-create-element');
var sharedKey = require('../internals/shared-key');

var GT = '>';
var LT = '<';
var PROTOTYPE = 'prototype';
var SCRIPT = 'script';
var IE_PROTO = sharedKey('IE_PROTO');

var EmptyConstructor = function () { /* empty */ };

var scriptTag = function (content) {
  return LT + SCRIPT + GT + content + LT + '/' + SCRIPT + GT;
};

// Create object with fake \`null\` prototype: use ActiveX Object with cleared prototype
var NullProtoObjectViaActiveX = function (activeXDocument) {
  activeXDocument.write(scriptTag(''));
  activeXDocument.close();
  var temp = activeXDocument.parentWindow.Object;
  // eslint-disable-next-line no-useless-assignment -- avoid memory leak
  activeXDocument = null;
  return temp;
};

// Create object with fake \`null\` prototype: use iframe Object with cleared prototype
var NullProtoObjectViaIFrame = function () {
  // Thrash, waste and sodomy: IE GC bug
  var iframe = documentCreateElement('iframe');
  var JS = 'java' + SCRIPT + ':';
  var iframeDocument;
  iframe.style.display = 'none';
  html.appendChild(iframe);
  // https://github.com/zloirock/core-js/issues/475
  iframe.src = String(JS);
  iframeDocument = iframe.contentWindow.document;
  iframeDocument.open();
  iframeDocument.write(scriptTag('document.F=Object'));
  iframeDocument.close();
  return iframeDocument.F;
};

// Check for document.domain and active x support
// No need to use active x approach when document.domain is not set
// see https://github.com/es-shims/es5-shim/issues/150
// variation of https://github.com/kitcambridge/es5-shim/commit/4f738ac066346
// avoid IE GC bug
var activeXDocument;
var NullProtoObject = function () {
  try {
    activeXDocument = new ActiveXObject('htmlfile');
  } catch (error) { /* ignore */ }
  NullProtoObject = typeof document != 'undefined'
    ? document.domain && activeXDocument
      ? NullProtoObjectViaActiveX(activeXDocument) // old IE
      : NullProtoObjectViaIFrame()
    : NullProtoObjectViaActiveX(activeXDocument); // WSH
  var length = enumBugKeys.length;
  while (length--) delete NullProtoObject[PROTOTYPE][enumBugKeys[length]];
  return NullProtoObject();
};

hiddenKeys[IE_PROTO] = true;

// \`Object.create\` method
// https://tc39.es/ecma262/#sec-object.create
// eslint-disable-next-line es/no-object-create -- safe
module.exports = Object.create || function create(O, Properties) {
  var result;
  if (O !== null) {
    EmptyConstructor[PROTOTYPE] = anObject(O);
    result = new EmptyConstructor();
    EmptyConstructor[PROTOTYPE] = null;
    // add "__proto__" for Object.getPrototypeOf polyfill
    result[IE_PROTO] = O;
  } else result = NullProtoObject();
  return Properties === undefined ? result : definePropertiesModule.f(result, Properties);
};
`;

function fixture(t) {
  const owner = fs.realpathSync(os.tmpdir());
  const base = fs.realpathSync(fs.mkdtempSync(path.join(owner, '.soui-compat-polyfills-test-')));
  t.after(() => {
    assert.equal(path.dirname(base), owner);
    assert.ok(path.basename(base).startsWith('.soui-compat-polyfills-test-'));
    assert.equal(identity.safePath(base), base);
    fs.rmSync(base, {recursive: true, force: true});
  });
  const root = path.join(base, 'web');
  const modules = path.join(root, 'node_modules');
  function write(file, value) {
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, typeof value === 'string' || Buffer.isBuffer(value) ? value : JSON.stringify(value));
    return file;
  }
  function core(directory) {
    const manifest = write(path.join(directory, 'package.json'), {name: 'core-js', version: '3.50.0'});
    const module = write(path.join(directory, 'internals/object-create.js'), upstreamModule);
    return {directory, manifest, module};
  }
  write(path.join(root, 'package.json'), {private: true});
  const plugin = path.join(modules, '@vitejs/plugin-legacy');
  const pluginManifest = write(path.join(plugin, 'package.json'), {name: '@vitejs/plugin-legacy', version: '6.1.1',
    exports: {'.': {import: './dist/index.mjs', require: './dist/index.cjs'}}});
  write(path.join(plugin, 'dist/index.cjs'), 'module.exports = {};\n');
  write(path.join(plugin, 'dist/index.mjs'), 'export default {};\n');
  const system = path.join(modules, 'systemjs');
  const systemManifest = write(path.join(system, 'package.json'), {name: 'systemjs', version: '6.15.1'});
  const systemEntry = write(path.join(system, 'dist/s.min.js'), '/* unchanged SystemJS fixture */\n');
  const original = core(path.join(modules, 'core-js'));
  return {base, root, modules, plugin, pluginManifest, original, systemManifest, systemEntry, write, core,
    prepare: () => compat.prepareCompatibilityPolyfills(root)};
}

function linkOrSkip(t, target, link, type) {
  try { fs.symlinkSync(target, link, type); }
  catch (error) {
    if (['EPERM', 'EACCES', 'ENOSYS'].includes(error.code)) { t.skip('Creating this link is unavailable.'); return false; }
    throw error;
  }
  return true;
}

test('adapts the exact upstream module once and retains the original Object.create semantics', t => {
  const f = fixture(t);
  assert.equal(Buffer.byteLength(upstreamModule), 3137);
  assert.equal(hash(upstreamModule), 'd953e1f674fd17fbc5c2f71dd196f12c0d225c583dcfad22ace48b2855509b74');
  const unrelated = f.write(path.join(f.original.directory, 'internals/other.js'), 'unrelated\n');
  const prepared = f.prepare();
  const first = fs.readFileSync(f.original.module);
  assert.equal(first.length, 356);
  assert.equal(hash(first), '78415ccff6be150c98035b73f8c7d2277cb6cf263b35aaa26ed1cc1dd91a7ead');
  const before = fs.statSync(f.original.module);
  assert.deepEqual(f.prepare().receipt, prepared.receipt);
  const after = fs.statSync(f.original.module);
  for (const key of ['ino', 'size', 'mtimeMs', 'ctimeMs'])
    assert.equal(after[key], before[key], 'Repeated builds do not rewrite the dependency');
  assert.equal(fs.readFileSync(unrelated, 'utf8'), 'unrelated\n');
  const context = {module: {exports: undefined}, Object};
  vm.runInNewContext(first.toString('utf8'), context);
  assert.equal(context.module.exports, Object.create);
  const value = context.module.exports(null, {answer: {value: 42, enumerable: true}});
  assert.equal(Object.getPrototypeOf(value), null);
  assert.deepEqual(Object.keys(value), ['answer']);
  assert.equal(value.answer, 42);
  assert.throws(() => vm.runInNewContext(first.toString('utf8'), {module: {}, Object: {}}),
    /SOUI_CHROME49_OBJECT_CREATE_REQUIRED/);
  assert.doesNotThrow(() => prepared.plugin.buildStart());
});

test('uses plugin-legacy own nested core-js rather than the project top-level package', t => {
  const f = fixture(t);
  const nested = f.core(path.join(f.plugin, 'node_modules/core-js'));
  f.write(f.original.manifest, {name: 'core-js', version: 'unsupported-top-level'});
  f.prepare();
  assert.equal(hash(fs.readFileSync(nested.module)), '78415ccff6be150c98035b73f8c7d2277cb6cf263b35aaa26ed1cc1dd91a7ead');
  assert.equal(fs.readFileSync(f.original.module, 'utf8'), upstreamModule);
});

test('emits a deterministic plain JSON receipt without absolute paths or source maps', t => {
  const f = fixture(t);
  const {plugin, receipt} = f.prepare();
  const assets = [];
  plugin.generateBundle.call({emitFile: asset => assets.push(asset)}, {}, {});
  assert.equal(assets.length, 1);
  assert.equal(assets[0].type, 'asset');
  assert.equal(assets[0].fileName, compat.receiptName);
  assert.deepEqual(JSON.parse(assets[0].source), receipt);
  assert.equal(receipt.adaptation.upstream.bytes, 3137);
  assert.equal(receipt.adaptation.replacement.bytes, 356);
  assert.doesNotMatch(assets[0].source, /[A-Za-z]:[\\/]|\\\\|sourcesContent|sourceMappingURL|(?:\.\.\/)/);
  assert.throws(() => plugin.generateBundle.call({emitFile: () => assert.fail()}, {},
    {[compat.receiptName.toUpperCase()]: {type: 'asset'}}), /E_COMPAT_RECEIPT_COLLISION/);
});

test('fails before mutation for unsupported dependency versions, names, entries and module contents', async t => {
  for (const [name, change, error] of [
    ['plugin version', f => f.write(f.pluginManifest, {name: '@vitejs/plugin-legacy', version: '6.1.2'}), 'E_COMPAT_PLUGIN_VERSION'],
    ['plugin name', f => f.write(f.pluginManifest, {name: 'other', version: '6.1.1'}), 'E_COMPAT_PLUGIN_VERSION'],
    ['plugin import entry', f => {
      const value = JSON.parse(fs.readFileSync(f.pluginManifest)); value.exports['.'].import = './other.mjs';
      f.write(f.pluginManifest, value);
    }, 'E_COMPAT_PLUGIN_ENTRY'],
    ['core version', f => f.write(f.original.manifest, {name: 'core-js', version: '3.50.1'}), 'E_COMPAT_CORE_JS_VERSION'],
    ['core name', f => f.write(f.original.manifest, {name: 'other', version: '3.50.0'}), 'E_COMPAT_CORE_JS_VERSION'],
    ['SystemJS version', f => f.write(f.systemManifest, {name: 'systemjs', version: '6.15.2'}), 'E_COMPAT_SYSTEM_JS_VERSION'],
    ['unrecognized source', f => f.write(f.original.module, upstreamModule.replace('var GT', 'let GT')), 'E_COMPAT_MODULE_IDENTITY'],
    ['CRLF upstream', f => f.write(f.original.module, upstreamModule.replace(/\n/g, '\r\n')), 'E_COMPAT_MODULE_IDENTITY'],
    ['oversized source', f => f.write(f.original.module, 'x'.repeat(65536)), 'E_COMPAT_MODULE_IDENTITY'],
  ]) await t.test(name, sub => {
    const f = fixture(sub); change(f);
    const before = fs.readFileSync(f.original.module);
    assert.throws(f.prepare, new RegExp(error));
    assert.deepEqual(fs.readFileSync(f.original.module), before);
  });
});

test('refuses parent dependency fallback outside this project node_modules', t => {
  const f = fixture(t);
  const external = path.join(f.base, 'node_modules/core-js');
  fs.mkdirSync(path.dirname(external), {recursive: true});
  fs.renameSync(f.original.directory, external);
  assert.throws(f.prepare, /E_COMPAT_DEPENDENCY_OUTSIDE_PROJECT/);
  assert.equal(fs.readFileSync(path.join(external, 'internals/object-create.js'), 'utf8'), upstreamModule);
});

test('refuses package export redirection to another core-js module', t => {
  const f = fixture(t);
  f.write(f.original.manifest, {name: 'core-js', version: '3.50.0', exports: {
    './package.json': './package.json', './internals/object-create.js': './other.js'}});
  f.write(path.join(f.original.directory, 'other.js'), upstreamModule);
  assert.throws(f.prepare, /E_COMPAT_MODULE_PATH/);
  assert.equal(fs.readFileSync(f.original.module, 'utf8'), upstreamModule);
});

test('refuses symlinked dependency files and hardlinked module or package files', async t => {
  for (const kind of ['module symlink', 'module hardlink', 'package hardlink', 'plugin entry hardlink'])
    await t.test(kind, sub => {
      const f = fixture(sub);
      const file = kind === 'package hardlink' ? f.original.manifest : kind === 'plugin entry hardlink'
        ? path.join(f.plugin, 'dist/index.cjs') : f.original.module;
      const outside = path.join(f.base, 'outside-file');
      fs.renameSync(file, outside);
      if (kind === 'module symlink') { if (!linkOrSkip(sub, outside, file, 'file')) return; }
      else fs.linkSync(outside, file);
      assert.throws(f.prepare, /E_COMPAT_(?:LINKED_PATH|REGULAR_FILE|DEPENDENCY_OUTSIDE_PROJECT)/);
      assert.equal(fs.readFileSync(outside, 'utf8'), kind.includes('module') ? upstreamModule
        : kind === 'package hardlink' ? JSON.stringify({name: 'core-js', version: '3.50.0'}) : 'module.exports = {};\n');
    });
});

test('refuses junctions at dependency and project ancestor boundaries', async t => {
  for (const kind of ['dependency', 'project']) await t.test(kind, sub => {
    const f = fixture(sub);
    const real = kind === 'dependency' ? f.original.directory : f.root;
    const destination = path.join(f.base, kind + '-actual');
    fs.renameSync(real, destination);
    if (!linkOrSkip(sub, destination, real, 'junction')) return;
    assert.throws(f.prepare, /E_COMPAT_LINKED_PATH/);
    assert.equal(fs.readFileSync(path.join(destination, kind === 'dependency'
      ? 'internals/object-create.js' : 'node_modules/core-js/internals/object-create.js'), 'utf8'), upstreamModule);
  });
});

test('refuses a dependency junction even when its target is inside the same node_modules', t => {
  const f = fixture(t);
  const target = path.join(f.modules, '.owned-cache/core-js');
  fs.mkdirSync(path.dirname(target), {recursive: true});
  fs.renameSync(f.original.directory, target);
  if (!linkOrSkip(t, target, f.original.directory, 'junction')) return;
  assert.throws(f.prepare, /E_COMPAT_LINKED_PATH/);
  assert.equal(fs.readFileSync(path.join(target, 'internals/object-create.js'), 'utf8'), upstreamModule);
});

test('revalidates dependency identity at build start', t => {
  const f = fixture(t);
  const {plugin} = f.prepare();
  f.write(f.original.module, 'changed after configuration\n');
  assert.throws(() => plugin.buildStart(), /E_COMPAT_MODULE_IDENTITY/);
});

test('revalidates the actual SystemJS body at build start', t => {
  const f = fixture(t);
  const {plugin, receipt} = f.prepare();
  assert.equal(receipt.systemJs.version, '6.15.1');
  assert.equal(receipt.systemJs.sha256, hash(fs.readFileSync(f.systemEntry)));
  f.write(f.systemEntry, 'modified SystemJS\n');
  assert.throws(() => plugin.buildStart(), /E_COMPAT_SYSTEM_JS_CHANGED/);
});

test('normalizes only the controlled replacement checkout and rejects unknown replacement source', async t => {
  for (const corrupt of [false, true]) await t.test(corrupt ? 'unknown replacement' : 'CRLF checkout', sub => {
    const f = fixture(sub);
    const helper = path.join(f.base, 'tools/compat-polyfills.cjs');
    f.write(helper, fs.readFileSync(path.resolve(__dirname, '../../tools/internal/compat-polyfills.cjs')));
    const bytes = fs.readFileSync(patchFile, 'utf8').replace(/\r\n/g, '\n');
    f.write(path.join(f.base, 'tools/polyfills/object-create.chrome49.cjs'),
      corrupt ? bytes.replace('var nativeCreate', 'let nativeCreate') : bytes.replace(/\n/g, '\r\n'));
    const local = require(helper);
    if (corrupt) {
      assert.throws(() => local.prepareCompatibilityPolyfills(f.root), /E_COMPAT_REPLACEMENT_IDENTITY/);
      assert.equal(fs.readFileSync(f.original.module, 'utf8'), upstreamModule);
    } else {
      local.prepareCompatibilityPolyfills(f.root);
      assert.equal(hash(fs.readFileSync(f.original.module)), '78415ccff6be150c98035b73f8c7d2277cb6cf263b35aaa26ed1cc1dd91a7ead');
    }
  });
});
