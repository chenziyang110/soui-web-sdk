'use strict';

// Pure JavaScript regression: canonical loader/publication code, surrounding UI
// and URL adapters are doubles. No browser, business JS, network or native code.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const override = path.join(root, 'sdk/resources/devtools/miniblink/overrides/front_end');
const sourceMapBytes = fs.readFileSync(path.join(override, 'sdk/SourceMap.js'));
const mappingBytes = fs.readFileSync(path.join(override, 'bindings/CompilerScriptMapping.js'));
const hash = body => crypto.createHash('sha256').update(body).digest('hex');
const encode = payload => 'data:application/json;charset=utf-8;base64,' +
  Buffer.from(typeof payload === 'string' ? payload : JSON.stringify(payload)).toString('base64');
const minimal = () => ({version: 3, file: 'bundle.js', names: [],
  sources: ['src/a.ts'], sourcesContent: ['const a = 1;\n'], mappings: 'AAAA'});
const sources = (count, prefix) => ({version: 3, file: 'bundle.js', names: [],
  sources: Array.from({length: count}, (_, i) => prefix + i + '.ts'),
  sourcesContent: Array(count).fill('const a = 1;\n'), mappings: 'AAAA'});

function realm() {
  const wi = {}, forbidden = {network: 0};
  const context = vm.createContext({WebInspector: wi, TextDecoder, Uint8Array, atob, btoa,
    console: {assert: value => assert.ok(value), error: () => assert.fail('unexpected raw log')}});
  const noNetwork = () => { ++forbidden.network; throw new Error('unexpected URL load'); };
  context.XMLHttpRequest = noNetwork;
  wi.ResourceLoader = {loadUsingTargetUA: noNetwork};
  wi.ParsedURL = {
    completeURL(base, relative) { try { return new URL(relative, base).href; } catch { return null; } },
    splitURLIntoPathComponents(url) {
      const parts = url.split('/');
      parts.peekLast = () => parts[parts.length - 1];
      return parts;
    },
  };
  wi.StaticContentProvider = function(_type, content) { this._content = content; };
  wi.StaticContentProvider.prototype.requestContent = function(callback) { callback(this._content); };
  wi.resourceTypes = {Script: 'script'};
  wi.Workspace = {Events: {UISourceCodeAdded: 'added'}};
  wi.DebuggerModel = {Events: {GlobalObjectCleared: 'reset'}};
  wi.Script = {Events: {SourceMapURLAdded: 'mapadded'}};
  wi.projectTypes = {Service: 'service'};
  wi.ContentProviderBasedProjectDelegate = function() {
    this.addContentProvider = (parent, name) => parent + '/' + name;
    this.removeFile = () => {};
    this.reset = () => {};
  };
  vm.runInContext('Array.prototype.peekLast = function() { return this[this.length - 1]; };', context);
  vm.runInContext(sourceMapBytes.toString('utf8'), context, {timeout: 5000});
  vm.runInContext(mappingBytes.toString('utf8'), context, {timeout: 5000});
  return {wi, forbidden};
}

function load(r, value, state = r.wi.SourceMap._createInlineState(), base = 'file:///C:/fixture/assets/bundle.js') {
  let calls = 0, result;
  r.wi.SourceMap.load(typeof value === 'string' ? value : encode(value), base,
    model => { ++calls; result = model; }, state);
  assert.equal(calls, 1);
  assert.equal(r.forbidden.network, 0);
  return {result, state, status: r.wi.SourceMap._inlineStatus(state)};
}

function rejected(payload, error) {
  const value = load(realm(), payload);
  assert.equal(value.result, null);
  assert.equal(value.status.lastError, error);
  assert.equal(value.status.logicalBytes, 0);
  assert.equal(value.status.records, 0);
}

function consumer() {
  const r = realm(), wi = r.wi, scripts = new Map(), files = new Map(), bound = new Set();
  const target = {resourceTreeModel: {inspectedPageURL: () => 'file:///C:/fixture/index.html'}};
  const model = {target: () => target, scriptForId: id => scripts.get(id),
    addEventListener() {}, removeEventListener() {},
    createRawLocation: (script, line, column) => ({scriptId: script.scriptId, line, column})};
  const workspace = {addEventListener() {}, removeEventListener() {}, uiSourceCode: () => ({})};
  const mapping = {hasMappingForURL: () => false, uiSourceCodeForURL: url => files.get(url)};
  const delegate = {_target: target, _contentProviders: Object.create(null)};
  delegate.removeFile = url => { delete delegate._contentProviders[url]; files.delete(url); };
  const project = {_projectDelegate: delegate, id: () => 'owned-project'};
  const network = {_projectDelegates: {'owned-project': delegate}, addFileForURL(url, provider) {
    const ui = {project: () => project, path: () => url,
      uiLocation: (line, column) => ({url, line, column})};
    files.set(url, ui); delegate._contentProviders[url] = provider; return ui;
  }};
  const binding = {pushSourceMapping() {}, updateLocations() {},
    setSourceMapping(_target, ui, owner) { if (owner) bound.add(ui); else bound.delete(ui); }};
  const c = new wi.CompilerScriptMapping(model, workspace, mapping, network, binding);
  function add(id, payload, name = 'bundle-' + id + '.js') {
    const script = {scriptId: id, sourceURL: 'file:///C:/fixture/assets/' + name,
      sourceMapURL: typeof payload === 'string' ? payload : encode(payload),
      target: () => target, isContentScript: () => false,
      addEventListener() {}, removeEventListener() {}};
    scripts.set(id, script); c.addScript(script); return script;
  }
  return {r, c, add, files, bound};
}

test('131072 mapping segments load; segment 131073 fails closed', () => {
  const p = minimal(); p.mappings = Array(131072).fill('A').join(',');
  assert.ok(load(realm(), p).result);
  p.mappings += ',A'; rejected(p, 'E_INLINE_SEGMENTS');
});

test('512 source records load; source 513 fails closed', () => {
  assert.equal(load(realm(), sources(512, 'src/limit')).result.sources().length, 512);
  rejected(sources(513, 'src/over'), 'E_INLINE_MAP');
});

test('actual publication algorithm binds 512 UIs; UI 513 is refused and reset releases them', () => {
  const h = consumer(); h.add('a', sources(256, 'src/a')); h.add('b', sources(256, 'src/b'));
  assert.equal(h.bound.size, 512); assert.equal(h.files.size, 512);
  assert.equal(h.c._inlineSources.size, 512);
  const before = h.r.wi.SourceMap._inlineStatus(h.c._inlineState);
  assert.equal(before.bindingRecords, 512); assert.equal(before.bindingLogicalBytes, 512 * 256);
  h.add('over', sources(1, 'src/over'));
  assert.equal(h.c._sourceMapForScriptId.over, undefined);
  assert.equal(h.c._inlineBindingError, 'E_INLINE_CONTENT_IDENTITY');
  assert.equal(h.bound.size, 512); assert.equal(h.files.size, 512);
  assert.equal(h.r.wi.SourceMap._inlineStatus(h.c._inlineState).bindingRecords, 512);
  h.c._debuggerReset();
  const after = h.r.wi.SourceMap._inlineStatus(h.c._inlineState);
  assert.equal(after.bindingRecords, 0); assert.equal(after.logicalBytes, 0);
  assert.equal(h.files.size, 0); assert.equal(h.bound.size, 0);
});

test('unchanged 16MiB total charge rejects another large map without corrupting retained models', () => {
  const r = realm(), state = r.wi.SourceMap._createInlineState(), p = minimal();
  p.mappings = Array(131072).fill('A').join(',');
  let accepted = 0;
  for (let i = 0; i < 8; ++i) {
    const before = r.wi.SourceMap._inlineStatus(state);
    const v = load(r, p, state, 'file:///C:/fixture/assets/large-' + i + '.js');
    if (v.result) { ++accepted; continue; }
    assert.equal(v.status.lastError, 'E_INLINE_BUDGET');
    assert.equal(v.status.logicalBytes, before.logicalBytes);
    assert.equal(v.status.records, accepted); break;
  }
  assert.ok(accepted > 0 && accepted < 8);
  assert.ok(r.wi.SourceMap._inlineStatus(state).logicalBytes <= 16 * 1024 * 1024);
  r.wi.SourceMap._beginInlineReset(state); r.wi.SourceMap._endInlineReset(state);
  assert.equal(r.wi.SourceMap._inlineStatus(state).logicalBytes, 0);
});

test('URL-byte, path, flat-JSON, VLQ and coordinate guards remain effective', () => {
  rejected('data:application/json;base64,' + 'A'.repeat(1048576), 'E_INLINE_URL');
  // Header bounds still run before decoding; maximum admitted URL encodes <1MiB.
  const bad = [
    [p => { p.sources = ['https://remote/a.ts']; }, 'E_INLINE_LABEL'],
    [p => { p.sources = ['src/a%2fts']; }, 'E_INLINE_LABEL'],
    [p => { p.sources = ['a/../same.ts', 'same.ts']; p.sourcesContent = ['x', 'x']; }, 'E_INLINE_SOURCE_URL'],
    [p => { p.sections = []; }, 'E_INLINE_FIELDS'],
    [p => { p.mappings = 'gggggggA'; }, 'E_INLINE_VLQ'],
    [p => { p.mappings = 'ACAA'; }, 'E_INLINE_COORDINATE'],
    [p => { p.mappings = 'AAAAA'; }, 'E_INLINE_NAME_INDEX'],
  ];
  for (const [mutate, code] of bad) { const p = minimal(); mutate(p); rejected(p, code); }
  rejected(encode('{"version":3,"version":3}'), 'E_INLINE_DUPLICATE_KEY');
});

const args = process.argv.slice(2);
if (args.length) {
  assert.equal(args.length, 2, 'only --fixture-root <absolute resources> is supported');
  assert.equal(args[0], '--fixture-root'); assert.ok(path.isAbsolute(args[1]));
  const fixture = path.resolve(args[1]);
  test('explicit frozen legacy fixture parses and publishes all 311 sources under original total budget', t => {
    const h = consumer();
    const rows = [
      ['polyfills-legacy-Debvrl7F.js', 1349891, 'a68f083f0f9e1139d75a37d4557d259b9e4e4d7b40a25fd94f20a39d6032546e', 286, 60974],
      ['index-legacy-BKAuK8HG.js', 1400562, '69fb0e562d8b514328069694f72652bf859eca6925be80f5eb5ed168e47493bb', 24, 77525],
      ['lazy-legacy-M3Z3UHHp.js', 2687, '409999cda4b127b8a6a0337407efa1225bf6d93626b82f9393614d7168a08f41', 1, 65],
    ];
    let expected = 0;
    for (const [name, bytes, sha256, count, segments] of rows) {
      const file = path.join(fixture, 'assets', name), before = fs.readFileSync(file);
      assert.equal(before.length, bytes); assert.equal(hash(before), sha256);
      const matches = [...before.toString('utf8').matchAll(/^\/\/# sourceMappingURL=(\S+)\s*$/gm)];
      assert.equal(matches.length, 1);
      const payload = JSON.parse(Buffer.from(matches[0][1].split(',')[1], 'base64').toString('utf8'));
      assert.equal(payload.sources.length, count);
      assert.equal(payload.mappings.split(/[;,]/).filter(Boolean).length, segments);
      h.add(name, matches[0][1], name); expected += count;
      const model = h.c._sourceMapForScriptId[name]; assert.ok(model, name + ' must publish');
      assert.equal(model.sources().length, count);
      for (let i = 0; i < count; ++i) {
        const url = new URL(payload.sources[i], 'file:///C:/fixture/assets/' + name).href;
        assert.equal(model.sourceContent(url), payload.sourcesContent[i]);
      }
      assert.equal(h.bound.size, expected);
      assert.equal(hash(fs.readFileSync(file)), sha256, 'fixture remains unchanged');
      t.diagnostic(JSON.stringify({name, bytes, sha256, sources: count, segments}));
    }
    const status = h.r.wi.SourceMap._inlineStatus(h.c._inlineState);
    assert.equal(status.bindingRecords, 311); assert.equal(status.records, 3);
    assert.ok(status.logicalBytes <= 16 * 1024 * 1024);
    t.diagnostic(JSON.stringify({logicalBytes: status.logicalBytes, bindingRecords: status.bindingRecords,
      sourceMapSha256: hash(sourceMapBytes), publicationSha256: hash(mappingBytes),
      actualBrowser: false, businessJavaScriptExecuted: false}));
    h.c._debuggerReset();
    assert.equal(h.r.wi.SourceMap._inlineStatus(h.c._inlineState).logicalBytes, 0);
    assert.equal(h.files.size, 0); assert.equal(h.bound.size, 0);
  });
}
