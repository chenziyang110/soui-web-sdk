'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const profile = require('../../tools/internal/web-build-profile.cjs');

const distributionRoot = path.resolve(__dirname, '../..');
const MiB = 1024 * 1024;
const businessText = 'export const answer: number = 42;\n';

function fixture(t, parent = os.tmpdir()) {
  const owner = fs.realpathSync(parent);
  const base = fs.realpathSync(fs.mkdtempSync(path.join(owner, '.soui-web-profile-test-')));
  t.after(() => {
    assert.equal(path.dirname(base), owner, 'Cleanup is limited to this directly created temporary fixture');
    assert.ok(path.basename(base).startsWith('.soui-web-profile-test-'));
    assert.equal(require('../../tools/internal/delivery-identity.cjs').safePath(base), base);
    fs.rmSync(base, { recursive: true, force: true });
  });
  const root = path.join(base, 'app');
  const out = path.join(root, 'dist');
  const script = path.join(out, 'assets', 'main.js');
  const source = path.join(root, 'src', 'main.ts');
  function write(file, content) {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, content);
    return file;
  }
  write(source, businessText);
  write(script, '(function () { window.answer = 42; }());\n');
  return {
    base, root, out, script, source, write,
    output(relative, content) { return write(path.join(out, relative), content); },
    map(entries = [{ file: source, content: businessText }], extra = {}) {
      return {
        version: 3,
        file: path.basename(script),
        names: [],
        sources: entries.map(entry => entry.name === undefined
          ? path.relative(path.dirname(script), entry.file).replace(/\\/g, '/')
          : entry.name),
        sourcesContent: entries.map(entry => entry.content),
        mappings: 'AAAA',
        ...extra,
      };
    },
    inline(map) {
      return write(script, '(function () { window.answer = 42; }());\n' +
        '//# sourceMappingURL=data:application/json;base64,' +
        Buffer.from(JSON.stringify(map)).toString('base64') + '\n');
    },
    async mark(selected = 'distribution', outDir = 'dist') {
      const configuration = profile.buildProfile(selected);
      configuration.plugin.configResolved({ root, build: { outDir } });
      await configuration.plugin.closeBundle();
      return configuration.plugin;
    },
    verify(selected, options) { return profile.verifyDirectory(out, selected, options); },
    marker() { return path.join(out, profile.manifestName); },
  };
}

function sparse(file, bytes) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const descriptor = fs.openSync(file, 'w');
  try { fs.ftruncateSync(descriptor, bytes); }
  finally { fs.closeSync(descriptor); }
}

function linkOrSkip(t, target, link, type) {
  try { fs.symlinkSync(target, link, type); }
  catch (error) {
    if (['EPERM', 'EACCES', 'ENOSYS'].includes(error.code)) {
      t.skip('The platform does not permit creating this fixture symlink.');
      return false;
    }
    throw error;
  }
  return true;
}

test('profiles have an explicit distribution default and reject unknown values', () => {
  assert.equal(profile.manifestName, '.soui-web-profile.json');
  assert.equal(profile.resolveProfile(undefined), 'distribution');
  for (const selected of ['distribution', 'source-diagnostic'])
    assert.equal(profile.resolveProfile(selected), selected);
  for (const selected of ['', null, false, 0, {}, 'Distribution', 'diagnostic', '../distribution'])
    assert.throws(() => profile.resolveProfile(selected));
});

test('build settings keep profile selection independent from native configuration', () => {
  for (const [selected, build] of [
    ['distribution', { sourcemap: false, minify: true }],
    ['source-diagnostic', { sourcemap: 'inline', minify: false }],
  ]) {
    const result = profile.buildProfile(selected);
    assert.equal(result.profile, selected);
    assert.deepEqual(result.build, build);
    assert.equal(typeof result.plugin.configResolved, 'function');
    assert.equal(typeof result.plugin.closeBundle, 'function');
  }
});

test('buildProfile reads the process profile only when selection is omitted', () => {
  const old = process.env.SOUI_WEB_BUILD_PROFILE;
  try {
    process.env.SOUI_WEB_BUILD_PROFILE = 'source-diagnostic';
    assert.equal(profile.buildProfile().profile, 'source-diagnostic');
    assert.equal(profile.buildProfile('distribution').profile, 'distribution');
    assert.equal(profile.resolveProfile(undefined), 'distribution');
    process.env.SOUI_WEB_BUILD_PROFILE = 'unsupported';
    assert.throws(() => profile.buildProfile());
    delete process.env.SOUI_WEB_BUILD_PROFILE;
    assert.equal(profile.buildProfile().profile, 'distribution');
  } finally {
    if (old === undefined) delete process.env.SOUI_WEB_BUILD_PROFILE;
    else process.env.SOUI_WEB_BUILD_PROFILE = old;
  }
});

test('legacy distribution is strictly scanned and can require a marker', t => {
  const f = fixture(t);
  f.output('assets/style.css', 'body { color: black; }\n');
  f.output('THIRD-PARTY-NOTICES.txt', 'Fixture notice.\n');
  const result = f.verify('distribution');
  assert.equal(result.profile, 'distribution');
  assert.equal(result.marked, false);
  assert.equal(result.maps, 0);
  assert.equal(result.businessSources, 0);
  assert.throws(() => f.verify('distribution', { requireManifest: true }));
  assert.throws(() => f.verify('source-diagnostic'));
});

test('legal materials are scanned by default and only an explicit option excludes their top-level directory', t => {
  const f = fixture(t);
  f.output('LeGaL/sdk/source.ts', 'SDK-owned legal source attachment.\n');
  assert.throws(() => f.verify('distribution'));
  assert.throws(() => f.verify('distribution', { excludeLegal: false }));
  assert.equal(f.verify('distribution', { excludeLegal: true }).profile, 'distribution');
});

test('excludeLegal does not exempt nested output directories named legal', t => {
  const f = fixture(t);
  f.output('assets/legal/source.ts', businessText);
  assert.throws(() => f.verify('distribution', { excludeLegal: true }));
});

test('verification rejects non-boolean legal exclusion and manifest requirements', t => {
  const f = fixture(t);
  for (const value of [null, 0, 1, 'true', [], {}]) {
    assert.throws(() => f.verify('distribution', { excludeLegal: value }));
    assert.throws(() => f.verify('distribution', { requireManifest: value }));
  }
});

test('the plugin writes a distribution marker and verification enforces its profile', async t => {
  const f = fixture(t);
  f.output('assets/style.css', 'body { color: black; }\n');
  await f.mark();
  assert.equal(fs.existsSync(f.marker()), true);
  const result = f.verify('distribution', { requireManifest: true });
  assert.equal(result.profile, 'distribution');
  assert.equal(result.marked, true);
  assert.equal(result.maps, 0);
  assert.throws(() => f.verify('source-diagnostic'));
});

test('absolute output directories and business source paths resolve from emitted scripts', async t => {
  const f = fixture(t);
  f.inline(f.map());
  await f.mark('source-diagnostic', f.out);
  const result = f.verify('source-diagnostic', { requireManifest: true });
  assert.equal(result.profile, 'source-diagnostic');
  assert.equal(result.marked, true);
  assert.equal(result.maps, 1);
  assert.equal(result.businessSources, 1);
});

test('SDK source projection uses the managed SDK root after application relocation', async t => {
  // Use one volume so map sources remain relative on Windows as well as POSIX.
  const f = fixture(t, distributionRoot);
  const sdkSource = path.join(distributionRoot, 'sdk', 'web', 'src', 'protocol.ts');
  f.inline(f.map([
    { file: f.source, content: businessText },
    { file: sdkSource, content: fs.readFileSync(sdkSource, 'utf8') },
  ]));
  await f.mark('source-diagnostic');
  assert.equal(f.verify('source-diagnostic').businessSources, 2);
});

test('marked diagnostic output remains verifiable without source files on the target', async t => {
  const f = fixture(t);
  f.inline(f.map());
  await f.mark('source-diagnostic');
  fs.rmSync(path.join(f.root, 'src'), { recursive: true });
  assert.equal(f.verify('source-diagnostic').businessSources, 1);
});

test('a second closeBundle cannot replace an existing profile identity', async t => {
  const f = fixture(t);
  const plugin = await f.mark();
  const before = fs.readFileSync(f.marker());
  await assert.rejects(async () => plugin.closeBundle());
  assert.deepEqual(fs.readFileSync(f.marker()), before);
});

test('a fresh plugin cannot overwrite a marker produced by a prior build', async t => {
  const f = fixture(t);
  await f.mark();
  const before = fs.readFileSync(f.marker());
  await assert.rejects(() => f.mark());
  assert.deepEqual(fs.readFileSync(f.marker()), before);
});

test('the plugin requires resolved build configuration exactly once before finalization', async t => {
  const f = fixture(t);
  const plugin = profile.buildProfile('distribution').plugin;
  await assert.rejects(async () => plugin.closeBundle());
  plugin.configResolved({ root: f.root, build: { outDir: 'dist' } });
  assert.throws(() => plugin.configResolved({ root: f.root, build: { outDir: 'dist' } }));
});

for (const selected of ['distribution', 'source-diagnostic']) {
  for (const extension of ['js', 'css']) {
    test(selected + ' rejects new unmapped .' + extension + ' assets after marking', async t => {
      const f = fixture(t);
      if (selected === 'source-diagnostic') f.inline(f.map());
      await f.mark(selected);
      f.output('assets/unrecorded.' + extension,
        extension === 'css' ? 'body { color: black; }\n' : 'window.started = true;\n');
      assert.throws(() => f.verify(selected), /E_WEB_UNBOUND_SCRIPT/);
    });
  }
  test(selected + ' permits the classic entry writer to rewrite unmapped HTML', async t => {
    const f = fixture(t);
    f.output('index.html', '<html><body>Vite entry</body></html>\n');
    if (selected === 'source-diagnostic') f.inline(f.map());
    await f.mark(selected);
    f.output('index.html', '<html><body><script>System.import("./assets/main.js");</script></body></html>\n');
    assert.equal(f.verify(selected).marked, true);
  });
}

for (const extension of ['js', 'mjs', 'cjs', 'css']) {
  test('marker binds original .' + extension + ' assets to their exact bytes', async t => {
    const f = fixture(t);
    const asset = f.output('assets/extra.' + extension,
      extension === 'css' ? 'body { color: red; }' : 'window.extra = 1;');
    await f.mark();
    fs.appendFileSync(asset, '\n/* changed */\n');
    assert.throws(() => f.verify('distribution'));
  });
}

test('removing an original script or truncating the marker fails verification', async t => {
  const f = fixture(t);
  await f.mark();
  const original = fs.readFileSync(f.script);
  fs.unlinkSync(f.script);
  assert.throws(() => f.verify('distribution'));
  fs.writeFileSync(f.script, original);
  fs.writeFileSync(f.marker(), '{');
  assert.throws(() => f.verify('distribution'));
});

test('a marker with its script inventory removed cannot authorize existing output', async t => {
  const f = fixture(t);
  f.inline(f.map());
  await f.mark('source-diagnostic');
  const marker = JSON.parse(fs.readFileSync(f.marker(), 'utf8'));
  marker.scripts = [];
  fs.writeFileSync(f.marker(), JSON.stringify(marker));
  assert.throws(() => f.verify('source-diagnostic'));
});

test('recorded script paths cannot escape the output directory', async t => {
  const f = fixture(t);
  await f.mark();
  const marker = JSON.parse(fs.readFileSync(f.marker(), 'utf8'));
  marker.scripts[0].path = '../outside.js';
  fs.writeFileSync(f.marker(), JSON.stringify(marker));
  assert.throws(() => f.verify('distribution'));
});

test('recorded projection byte identities must match embedded business content', async t => {
  const f = fixture(t);
  f.inline(f.map());
  await f.mark('source-diagnostic');
  const marker = JSON.parse(fs.readFileSync(f.marker(), 'utf8'));
  const source = marker.scripts.find(script => script.map).map.sources.find(entry => entry.kind === 'business');
  source.bytes += 1;
  fs.writeFileSync(f.marker(), JSON.stringify(marker));
  assert.throws(() => f.verify('source-diagnostic'));
});

test('recorded business projection paths cannot contain parent traversal', async t => {
  const f = fixture(t);
  f.inline(f.map());
  await f.mark('source-diagnostic');
  const marker = JSON.parse(fs.readFileSync(f.marker(), 'utf8'));
  const source = marker.scripts.find(script => script.map).map.sources.find(entry => entry.kind === 'business');
  source.path = '../escaped.ts';
  fs.writeFileSync(f.marker(), JSON.stringify(marker));
  assert.throws(() => f.verify('source-diagnostic'));
});

test('new inline-mapped scripts are rejected after the marker was created', async t => {
  const f = fixture(t);
  f.inline(f.map());
  await f.mark('source-diagnostic');
  f.output('assets/unrecorded.js', fs.readFileSync(f.script));
  assert.throws(() => f.verify('source-diagnostic'));
});

test('diagnostic creation requires an inline map', async t => {
  const f = fixture(t);
  await assert.rejects(() => f.mark('source-diagnostic'));
});

test('diagnostic creation requires projected business source in addition to a third-party map', async t => {
  const f = fixture(t);
  f.inline(f.map([{ name: '../../node_modules/fixture/index.js', content: 'module.exports = 1;' }]));
  await assert.rejects(() => f.mark('source-diagnostic'));
});

test('third-party and explicit virtual map sources need no original file reads', async t => {
  const f = fixture(t);
  f.inline(f.map([
    { file: f.source, content: businessText },
    { name: '../../node_modules/nonexistent-fixture/index.js', content: 'export const thirdParty = 1;' },
    { name: '\0virtual:fixture', content: 'export const virtualValue = 1;' },
    { name: 'vite:fixture-helper', content: 'export const viteValue = 1;' },
  ]));
  await f.mark('source-diagnostic');
  assert.equal(f.verify('source-diagnostic').businessSources, 1);
});

test('legacy polyfill labels bind only matching current installed package content', async t => {
  const f = fixture(t);
  f.write(path.join(f.root, 'node_modules/core-js/internals/fixture.js'), 'module.exports = 1;\n');
  f.write(path.join(f.root, 'node_modules/systemjs/dist/s.min.js'), 'var System = {};\n');
  f.inline(f.map([{file: f.source, content: businessText},
    {name: '../../../../../core-js/internals/fixture.js', content: 'module.exports = 1;\n'},
    {name: '../../../../../systemjs/dist/s.min.js', content: 'var System = {};\n'}]));
  await f.mark('source-diagnostic');
  assert.equal(f.verify('source-diagnostic').thirdPartySources, 2);
});

test('Vite binary media source proxies remain virtual rather than business text', async t => {
  const f = fixture(t);
  const asset = f.write(path.join(f.root, 'src/picture.png'), Buffer.from([0x89, 0x50, 0x4e, 0x47]));
  f.inline(f.map([{file: f.source, content: businessText},
    {file: asset, content: 'export default "__VITE_ASSET__BkHwt_VB__"'}]));
  await f.mark('source-diagnostic');
  const checked = f.verify('source-diagnostic');
  assert.equal(checked.businessSources, 1);
  assert.equal(checked.virtualSources, 1);
});

test('a Vite media proxy cannot replace a TypeScript business original', async t => {
  const f = fixture(t);
  f.inline(f.map([{file: f.source, content: 'export default "__VITE_ASSET__BkHwt_VB__"'}]));
  await assert.rejects(() => f.mark('source-diagnostic'), /E_WEB_SOURCE_CONTENT/);
});

test('binary media cannot hide arbitrary map source text', async t => {
  const f = fixture(t);
  const asset = f.write(path.join(f.root, 'src/picture.png'), Buffer.from([0x89, 0x50, 0x4e, 0x47]));
  f.inline(f.map([{file: f.source, content: businessText}, {file: asset, content: 'arbitrary source'}]));
  await assert.rejects(() => f.mark('source-diagnostic'), /E_WEB_UTF8/);
});

for (const source of ['../../../../../core-js/internals/fixture.js',
  '../../../../../core-js/../private.js', '../../../../../unknown-package/index.js']) {
  test('legacy polyfill labels refuse mismatched, traversing or unknown package content: ' + source, async t => {
    const f = fixture(t);
    f.write(path.join(f.root, 'node_modules/core-js/internals/fixture.js'), 'real installed bytes');
    f.inline(f.map([{file: f.source, content: businessText}, {name: source, content: 'different content'}]));
    await assert.rejects(() => f.mark('source-diagnostic'));
  });
}

test('explicit app source roots preserve same-named files and enforce their scope', async t => {
  const f = fixture(t);
  const first = f.write(path.join(f.root, 'apps/one/main.ts'), businessText);
  const second = f.write(path.join(f.root, 'apps/two/main.ts'), 'export const second = true;\n');
  f.inline(f.map([{file: first, content: businessText},
    {file: second, content: 'export const second = true;\n'}]));
  const {plugin} = profile.buildProfile('source-diagnostic', {appSourceDirectories: ['apps/one', 'apps/two']});
  plugin.configResolved({root: f.root, build: {outDir: 'dist'}});
  const checked = plugin.closeBundle();
  assert.equal(checked.businessSources, 2);
  const marker = JSON.parse(fs.readFileSync(f.marker(), 'utf8'));
  assert.deepEqual(marker.scripts[0].map.sources.map(entry => entry.path), ['apps/one/main.ts', 'apps/two/main.ts']);
});

test('explicit app roots reject otherwise matching source from an unselected directory', async t => {
  const f = fixture(t);
  f.inline(f.map());
  const {plugin} = profile.buildProfile('source-diagnostic', {appSourceDirectories: ['apps/one']});
  plugin.configResolved({root: f.root, build: {outDir: 'dist'}});
  await assert.rejects(async () => plugin.closeBundle(), /E_WEB_SOURCE_OUTSIDE_ROOTS/);
});

for (const directories of [[], ['../escape'], ['/absolute'], ['src', 'SRC'], ['src\\nested'], ['.'], ['']]) {
  test('app source root configuration refuses invalid scope ' + JSON.stringify(directories), () => {
    assert.throws(() => profile.buildProfile('source-diagnostic', {appSourceDirectories: directories}));
  });
}

for (const source of ['//example.invalid/node_modules/main.ts',
  '../../node_modules/../../outside.ts', '..\\..\\node_modules\\main.ts']) {
  test('third-party classification rejects escaping or non-local path ' + source, async t => {
    const f = fixture(t);
    f.inline(f.map([{file: f.source, content: businessText}, {name: source, content: 'outside'}]));
    await assert.rejects(() => f.mark('source-diagnostic'), /E_WEB_SOURCE_(?:PATH|OUTSIDE_ROOTS)/);
  });
}

for (const extension of ['ts', 'tsx', 'jsx', 'vue', 'svelte']) {
  test('diagnostic creation checks original .' + extension + ' business content', async t => {
    const f = fixture(t);
    const source = f.write(path.join(f.root, 'src', 'view.' + extension), 'fixture original\n');
    f.inline(f.map([{ file: source, content: 'fixture mismatch\n' }]));
    await assert.rejects(() => f.mark('source-diagnostic'));
  });
}

test('matching source text outside app/src and SDK sources cannot become business projection', async t => {
  const f = fixture(t);
  const outside = f.write(path.join(f.root, 'shared.ts'), businessText);
  f.inline(f.map([{ file: outside, content: businessText }]));
  await assert.rejects(() => f.mark('source-diagnostic'));
});

test('missing business originals fail creation instead of trusting sourcesContent', async t => {
  const f = fixture(t);
  f.inline(f.map());
  fs.unlinkSync(f.source);
  await assert.rejects(() => f.mark('source-diagnostic'));
});

for (const source of ['https://example.invalid/src/main.ts', 'file:///missing/main.ts', '../../src/main.ts?raw']) {
  test('map source references reject URL and query access: ' + source, async t => {
    const f = fixture(t);
    f.inline(f.map([{ name: source, content: businessText }]));
    await assert.rejects(() => f.mark('source-diagnostic'));
  });
}

for (const extra of [
  { version: 2 },
  { sources: 'main.ts' },
  { sourcesContent: [] },
  { sourcesContent: [null] },
  { sourcesContent: [42] },
  { sourceRoot: 'https://example.invalid/source/' },
]) {
  test('diagnostic maps reject malformed or non-self-contained fields ' + JSON.stringify(extra), async t => {
    const f = fixture(t);
    f.inline(f.map(undefined, extra));
    await assert.rejects(() => f.mark('source-diagnostic'));
  });
}

for (const url of [
  'main.js.map',
  'https://example.invalid/main.js.map',
  'file:///missing/main.js.map',
  'data:application/json,%7B%22version%22%3A3%7D',
  'data:application/json;base64,@@@@',
  'data:application/json;base64,' + Buffer.from('{').toString('base64'),
]) {
  test('diagnostic creation rejects external or malformed map URL ' + url.slice(0, 65), async t => {
    const f = fixture(t);
    fs.writeFileSync(f.script, 'window.answer = 42;\n//# sourceMappingURL=' + url + '\n');
    await assert.rejects(() => f.mark('source-diagnostic'));
  });
}

for (const [relative, content] of [
  ['assets/comment.js', 'window.answer=1;\n//# sourceMappingURL=missing.map'],
  ['assets/comment.css', 'body{}\n/*# sourceMappingURL=missing.map */'],
  ['assets/embedded.js', 'window.map=' + JSON.stringify({ version: 3, sources: [], sourcesContent: [], names: [], mappings: '' }) + ';'],
]) {
  test('distribution rejects map exposure in ' + relative, async t => {
    const f = fixture(t);
    f.output(relative, content);
    assert.throws(() => f.verify('distribution'));
    await assert.rejects(() => f.mark('distribution'));
  });
}

test('inline HTML scripts cannot hide source maps outside the bound asset inventory', async t => {
  const f = fixture(t);
  f.output('index.html', '<script>window.map={sourcesContent:["business source"]};</script>');
  assert.throws(() => f.verify('distribution'));
  f.inline(f.map());
  await assert.rejects(() => f.mark('source-diagnostic'));
});

for (const selected of ['distribution', 'source-diagnostic']) {
  for (const [relative, content] of [
    ['diagnostic.json', JSON.stringify({ version: 3, sourcesContent: ['business source'] })],
    ['debug.txt', 'window.answer=1;\n//# sourceMappingURL=data:application/json;base64,e30=\n'],
    ['debug.svg', '<svg><script>window.map={sourcesContent:["business source"]};</script></svg>'],
  ]) {
    test(selected + ' rejects map exposure in UTF-8 public asset ' + relative, async t => {
      const f = fixture(t);
      if (selected === 'source-diagnostic') f.inline(f.map());
      f.output(relative, content);
      await assert.rejects(() => f.mark(selected), /E_WEB_NON_SCRIPT_MAP/);
    });
  }
}

test('binary public assets retain their bytes without text map inspection', async t => {
  const f = fixture(t);
  const bytes = Buffer.concat([Buffer.from([0xff, 0xfe, 0x00]), Buffer.from('sourcesContent:["binary bytes"]')]);
  const file = f.output('binary.dat', bytes);
  await f.mark('distribution');
  assert.equal(f.verify('distribution').marked, true);
  assert.deepEqual(fs.readFileSync(file), bytes);
});

for (const extension of ['map', 'ts', 'tsx', 'jsx', 'vue', 'svelte', 'TSX']) {
  test('all profiles reject standalone .' + extension + ' output', async t => {
    const f = fixture(t);
    f.output('assets/leaked.' + extension, 'fixture source');
    assert.throws(() => f.verify('distribution'));
    f.inline(f.map());
    await assert.rejects(() => f.mark('source-diagnostic'));
  });
}

test('output file symlinks are rejected even when their targets are inside output', t => {
  const f = fixture(t);
  if (!linkOrSkip(t, f.script, path.join(f.out, 'linked.js'), 'file')) return;
  assert.throws(() => f.verify('distribution'));
});

test('output directory symlinks cannot expose files outside the scanned tree', t => {
  const f = fixture(t);
  const external = path.join(f.base, 'external');
  f.write(path.join(external, 'hidden.js'), 'window.hidden = 1;');
  if (!linkOrSkip(t, external, path.join(f.out, 'linked'), 'junction')) return;
  assert.throws(() => f.verify('distribution'));
});

test('business source symlinks cannot escape the approved source roots', async t => {
  const f = fixture(t);
  const external = f.write(path.join(f.base, 'outside.ts'), businessText);
  fs.unlinkSync(f.source);
  if (!linkOrSkip(t, external, f.source, 'file')) return;
  f.inline(f.map());
  await assert.rejects(() => f.mark('source-diagnostic'));
});

test('an individual output file exceeding 16 MiB fails the resource budget', t => {
  const f = fixture(t);
  sparse(path.join(f.out, 'oversized.bin'), 16 * MiB + 1);
  assert.throws(() => f.verify('distribution'), /budget|size|limit/i);
});

test('aggregate output exceeding 128 MiB fails before large resources are loaded', t => {
  const f = fixture(t);
  for (let index = 0; index < 9; index++)
    sparse(path.join(f.out, 'large-' + index + '.bin'), 16 * MiB);
  assert.throws(() => f.verify('distribution'), /budget|size|limit/i);
});

test('the output inventory is bounded to 4096 files', t => {
  const f = fixture(t);
  for (let index = 0; index < 4096; index++)
    f.output('small/' + index + '.txt', 'x');
  assert.throws(() => f.verify('distribution'), /budget|file.*count|files|limit/i);
});

test('maps with more than 16384 sources fail their resource budget', async t => {
  const f = fixture(t);
  const entries = [{ file: f.source, content: businessText },
    ...Array.from({ length: 16384 }, (_, index) =>
      ({ name: 'virtual:fixture-' + index, content: '' }))];
  f.inline(f.map(entries));
  await assert.rejects(() => f.mark('source-diagnostic'));
});

test('map JSON nesting exceeding 32 levels is rejected before projection', async t => {
  const f = fixture(t);
  let nested = 'leaf';
  for (let index = 0; index < 34; index++) nested = { child: nested };
  f.inline(f.map(undefined, { nested }));
  await assert.rejects(() => f.mark('source-diagnostic'), /depth/i);
});

test('output traversal exceeding 32 directories is rejected', t => {
  const f = fixture(t);
  f.output(Array.from({ length: 34 }, () => 'deep').join('/') + '/end.txt', 'x');
  assert.throws(() => f.verify('distribution'), /depth/i);
});
