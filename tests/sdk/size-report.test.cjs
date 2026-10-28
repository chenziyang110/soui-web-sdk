'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const {test} = require('node:test');
const {summarize} = require('../../tools/internal/size-report.cjs');
const tool = path.resolve(__dirname, '../../tools/internal/size-report.cjs');
const file = (name, bytes, content = name) => ({path: name, bytes, sha256: crypto.createHash('sha256').update(content).digest('hex')});
const report = (files, extra = {}) => ({schema: 'soui.application-size/v1', version: '0.5.0', target: 'Example',
  architecture: 'x64', configuration: 'Release', rendererBackend: 'miniblink', webTarget: null,
  payload: {bytes: files.reduce((sum, file) => sum + file.bytes, 0), files}, ...extra});

test('old payload input works without baseline, with deterministic categories and ranking', () => {
  const files = [file('resources/hero.png', 40), file('resources/script.js', 30), file('resources/style.css', 2),
    file('resources/font.woff2', 3), file('resources/index.html', 5), file('resources/licenses/component.txt', 7),
    file('app.exe', 60), file('resources/a.svg', 40), file('resources/app.js.map', 4), file('resources/item.json', 1),
    file('resources/movie.mp4', 8), file('resources/file.unknown', 9)];
  const result = summarize({files});
  assert.equal(result.bytes, 209);
  assert.equal(result.fileCount, 12);
  assert.deepEqual(result.categories.images, {bytes: 80, fileCount: 2});
  assert.deepEqual(result.categories.javascript, {bytes: 30, fileCount: 1});
  assert.deepEqual(result.categories.legal, {bytes: 7, fileCount: 1});
  assert.equal(result.categories.css.bytes, 2);
  assert.equal(result.categories.fonts.bytes, 3);
  assert.equal(result.categories.html.bytes, 5);
  assert.equal(result.categories.native.bytes, 60);
  assert.equal(result.categories.sourceMaps.bytes, 4);
  assert.equal(result.categories.data.bytes, 1);
  assert.equal(result.categories.media.bytes, 8);
  assert.equal(result.categories.other.bytes, 9);
  assert.deepEqual(result.topFiles.slice(0, 3).map(file => file.path), ['app.exe', 'resources/a.svg', 'resources/hero.png']);
  assert.equal(result.comparison, null);
  assert.equal(result.budget, null);
  assert.deepEqual(result, summarize({files: [...files].reverse()}));
  assert.equal(summarize({files: []}).bytes, 0);
});

test('duplicates require equal hash and bytes and never merge by basename', () => {
  const input = report([file('setup/node.dll', 100, 'kernel-a'), file('uninstall/node.dll', 100, 'kernel-b'),
    file('resources/a.png', 30, 'same-image'), file('resources/copy.png', 30, 'same-image'),
    file('resources/inconsistent-size.png', 31, 'same-image')]);
  const result = summarize(input);
  assert.equal(result.duplicates.length, 1);
  assert.deepEqual(result.duplicates[0].paths, ['resources/a.png', 'resources/copy.png']);
  assert.equal(result.duplicates[0].potentialUncompressedBytes, 30);
  assert.equal(result.potentialDuplicateBytes, 30);
  assert.equal(result.bytes, 291);
});

test('baseline compares complete current manifests, including versions and content-only changes', () => {
  const previous = report([file('resources/removed.png', 10), file('resources/stable.js', 30),
    file('resources/changed.js', 5, 'old'), file('node.dll', 20)]);
  const current = report([file('resources/added.png', 12), file('resources/stable.js', 30),
    file('resources/changed.js', 5, 'new'), file('node.dll', 25)], {version: '0.6.0', sizeAnalysis: {old: true}});
  const result = summarize(current, {baseline: previous});
  assert.equal(result.comparison.deltaBytes, 7);
  assert.equal(result.comparison.baseline.version, '0.5.0');
  assert.deepEqual(result.comparison.added.map(file => file.path), ['resources/added.png']);
  assert.deepEqual(result.comparison.removed.map(file => file.path), ['resources/removed.png']);
  assert.deepEqual(result.comparison.changed.map(file => [file.path, file.deltaBytes]), [['node.dll', 5], ['resources/changed.js', 0]]);
  assert.deepEqual(result, summarize({...current, payload: {...current.payload, files: [...current.payload.files].reverse()}}, {baseline: {...previous, payload: {...previous.payload, files: [...previous.payload.files].reverse()}}}));
});

test('baseline rejects identity mismatch and missing native identity, but accepts aliases', () => {
  const input = report([file('app.exe', 1)]);
  for (const [key, value] of [['target', 'Other'], ['architecture', 'x86'], ['configuration', 'Debug'], ['rendererBackend', 'webview2'], ['webTarget', 'pe']]) {
    assert.throws(() => summarize(input, {baseline: {...input, [key]: value}}), new RegExp(`Incomparable size baseline: ${key}`));
  }
  assert.throws(() => summarize(input, {baseline: {...input, rendererBackend: undefined}}), /rendererBackend/);
  assert.throws(() => summarize(input, {baseline: {...input, target: null}}), /target/);
  const aliased = {...input, architecture: undefined, configuration: undefined, rendererBackend: undefined, arch: 'x64', config: 'Release', backend: 'miniblink'};
  assert.equal(summarize(input, {baseline: aliased}).comparison.deltaBytes, 0);
  assert.throws(() => summarize({...input, arch: 'x86'}), /Conflicting/);
});

test('manifest validation rejects unsafe Windows names, collisions, malformed bytes and hashes', () => {
  for (const name of ['../escape', '/absolute', 'folder\\file.png', 'C:/file', 'resources/./file', 'resources//file', 'resources/file:stream', 'resources/NUL.txt', 'resources/file.']) {
    assert.throws(() => summarize({files: [file(name, 1)]}), /Invalid path/);
  }
  assert.throws(() => summarize({files: [file('resources/A.png', 1), file('resources/a.png', 1)]}), /Duplicate/);
  for (const bytes of [-1, 0.5, Number.MAX_SAFE_INTEGER + 1, '1', null, NaN]) {
    assert.throws(() => summarize({files: [{...file('app.exe', 1), bytes}]}), /Invalid size manifest/);
  }
  for (const sha256 of ['a', 'g'.repeat(64), 'A'.repeat(64), null, 4]) {
    assert.throws(() => summarize({files: [{...file('app.exe', 1), sha256}]}), /Invalid size manifest/);
  }
  for (const input of [null, [], {}, {payload: null}, {files: null}, {files: [null]}, {files: [{path: ''}]}]) assert.throws(() => summarize(input));
  assert.throws(() => summarize(report([file('app.exe', 2)], {payload: {files: [file('app.exe', 2)], bytes: 3}})), /total/);
  assert.throws(() => summarize({files: [file('one.bin', Number.MAX_SAFE_INTEGER), file('two.bin', 1)]}), /safe integer/);
});

test('budgets only impose explicit limits and growth requires a comparable baseline', () => {
  const input = report([file('app.exe', 11)]), baseline = report([file('app.exe', 8)]);
  assert.deepEqual(summarize(input, {budget: {totalBytes: 11}}).budget, {limits: {totalBytes: 11}, passed: true, violations: []});
  const result = summarize(input, {baseline, budget: {growthBytes: 2, totalBytes: 10}});
  assert.equal(result.budget.passed, false);
  assert.deepEqual(result.budget.violations, [{metric: 'totalBytes', actual: 11, limit: 10}, {metric: 'growthBytes', actual: 3, limit: 2}]);
  assert.deepEqual(result, summarize(input, {baseline, budget: {totalBytes: 10, growthBytes: 2}}));
  assert.throws(() => summarize(input, {budget: {growthBytes: 1}}), /requires a comparable baseline/);
  for (const budget of [null, [], {totalBytes: -1}, {growthBytes: 0.1}, {totalBytes: '10'}, {readingLimitBytes: 100}]) assert.throws(() => summarize(input, {budget}), /budget permits/);
  assert.equal(summarize({files: [file('node.dll', 100 * 1024 * 1024)]}).budget, null);
});

test('CLI emits a reviewable analysis and returns nonzero for budget failure and malformed inputs', t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-size-report-'));
  t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
  const save = (name, value) => { const full = path.join(directory, name); fs.writeFileSync(full, JSON.stringify(value)); return full; };
  const input = save('current.json', report([file('app.exe', 12)]));
  const baseline = save('baseline.json', report([file('app.exe', 10)]));
  const budget = save('budget.json', {totalBytes: 11, growthBytes: 1});
  const run = args => spawnSync(process.execPath, [tool, ...args], {encoding: 'utf8'});
  const failed = run(['--report', input, '--baseline', baseline, '--budget', budget]);
  assert.equal(failed.status, 1, failed.stderr);
  assert.equal(JSON.parse(failed.stdout).budget.passed, false);
  assert.equal(run(['--report', input]).status, 0);
  const malformed = save('bad.json', {files: [file('../outside', 1)]});
  assert.equal(run(['--report', malformed]).status, 1);
  for (const args of [[], ['--report'], ['--unknown', input], ['--report', input, '--report', input]]) assert.equal(run(args).status, 1);
});

for (const mutation of ['truncate', 'rewrite', 'replace']) test(`CLI rejects valid JSON changed during ${mutation}`, t => {
  const temporary = fs.realpathSync(os.tmpdir());
  const directory = fs.mkdtempSync(path.join(temporary, 'soui-size-report-race-'));
  t.after(() => {
    assert.equal(path.dirname(fs.realpathSync(directory)), temporary);
    fs.rmSync(directory, {recursive: true, force: true});
  });
  const input = path.join(directory, 'report.json'), marker = path.join(directory, 'mutation.txt');
  const content = JSON.stringify(report([file('app.exe', 1)]));
  fs.writeFileSync(input, content + (mutation === 'truncate' ? ' '.repeat(128) : ''));
  const hook = path.join(directory, 'race.cjs');
  // Intercept only this child's report read. Every mutation changes a real file
  // after its initial fstat; all resulting JSON remains valid and bounded.
  fs.writeFileSync(hook, `
    const fs = require('node:fs'), path = require('node:path');
    const input = ${JSON.stringify(input)}, marker = ${JSON.stringify(marker)};
    const mutation = ${JSON.stringify(mutation)}, original = fs.readFileSync(input, 'utf8');
    const changed = original.replaceAll('"bytes":1', '"bytes":2');
    if (changed.length !== original.length) throw Error('Race fixture length changed');
    const open = fs.openSync, read = fs.readSync;
    let inputFd, applied = false;
    fs.openSync = function (file, ...args) {
      const fd = open.call(this, file, ...args);
      if (typeof file === 'string' && path.resolve(file) === input) inputFd = fd;
      return fd;
    };
    fs.readSync = function (fd, ...args) {
      if (fd === inputFd && !applied) {
        applied = true;
        if (mutation === 'truncate') fs.truncateSync(input, original.trimEnd().length);
        else if (mutation === 'replace') {
          fs.renameSync(input, input + '.old');
          fs.writeFileSync(input, changed);
        } else {
          const before = fs.statSync(input);
          fs.writeFileSync(input, changed);
          fs.utimesSync(input, before.atime, new Date(before.mtimeMs + 2000));
        }
        fs.writeFileSync(marker, 'applied');
      }
      return read.call(this, fd, ...args);
    };
  `);
  const result = spawnSync(process.execPath, ['--require', hook, tool, '--report', input],
    {encoding: 'utf8', windowsHide: true});
  assert.equal(fs.readFileSync(marker, 'utf8'), 'applied');
  assert.equal(result.status, 1, result.stdout + result.stderr);
  assert.equal(result.stdout, '');
  assert.match(result.stderr, /JSON changed during read/);
});

test('PowerShell packaging preserves archive identity and rejects a failed budget before archiving', {
  skip: process.platform !== 'win32' || !fs.existsSync(path.join(process.env.ProgramFiles || '', '7-Zip/7z.exe'))
}, t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-size-package-'));
  t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
  function fixture(name) {
    const root = path.join(directory, name), internal = path.join(root, 'tools/internal');
    fs.mkdirSync(internal, {recursive: true});
    fs.writeFileSync(path.join(root, 'VERSION'), '0.5.0\n');
    fs.copyFileSync(path.resolve(__dirname, '../../tools/Package-Applications.ps1'), path.join(root, 'tools/Package-Applications.ps1'));
    for (const name of ['size-report.cjs', 'delivery-identity.cjs', 'compression-profiles.cjs']) fs.copyFileSync(path.resolve(__dirname, '../../tools/internal', name), path.join(internal, name));
    // This fixture exercises the real packaging entry point with synthetic staging;
    // it is not native build or startup evidence.
    fs.writeFileSync(path.join(internal, 'build-evidence.cjs'), `
      const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
      const [, architecture, configuration, target, stage] = process.argv.slice(2);
      const files = [{path: target + '.exe', bytes: 4}, {path: 'resources/index.html', bytes: 7}];
      for (const file of files) {
        const data = Buffer.alloc(file.bytes, 'x'), full = path.join(stage, file.path);
        fs.mkdirSync(path.dirname(full), {recursive: true}); fs.writeFileSync(full, data);
        file.sha256 = crypto.createHash('sha256').update(data).digest('hex');
      }
      console.log(JSON.stringify({schema: 'soui.application-size/v1', version: '0.5.0', target, architecture, configuration,
        rendererBackend: 'webview2', webTarget: null, payload: {files, bytes: 11},
        categories: {executable: 4, kernel: 0, libraries: 0, web: 7}, releaseReady: false}));
    `);
    return root;
  }
  const run = (root, args = []) => spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    path.join(root, 'tools/Package-Applications.ps1'), '-Profile', 'modern', ...args], {
    encoding: 'utf8', windowsHide: true,
    // Bundled Node runtimes may inherit PowerShell 7 modules. PS 5 must resolve its
    // own standard modules rather than importing an incompatible Utility module.
    env: {...process.env, PSModulePath: path.join(process.env.SystemRoot || 'C:/Windows', 'System32/WindowsPowerShell/v1.0/Modules')}
  });
  const first = fixture('normal'), success = run(first);
  assert.equal(success.status, 0, success.stdout + success.stderr);
  const archive = path.join(first, 'dist/SouiModernStarter-0.5.0-windows-x64.zip');
  const result = JSON.parse(fs.readFileSync(archive + '.json', 'utf8'));
  assert.equal(result.schema, 'soui.application-size/v1');
  assert.deepEqual(result.categories, {executable: 4, kernel: 0, libraries: 0, web: 7});
  assert.equal(result.archive.bytes, fs.statSync(archive).size);
  assert.equal(result.archive.sha256, crypto.createHash('sha256').update(fs.readFileSync(archive)).digest('hex'));
  assert.equal(result.sizeAnalysis.bytes, 11);
  assert.equal(result.sizeAnalysis.budget, null);
  const baselines = path.join(directory, 'baselines'); fs.mkdirSync(baselines);
  fs.copyFileSync(archive + '.json', path.join(baselines, 'SouiModernStarter.json'));
  const budget = path.join(directory, 'budget.json'); fs.writeFileSync(budget, JSON.stringify({totalBytes: 11, growthBytes: 0}));
  const compared = fixture('compared'), comparison = run(compared, ['-SizeBaselineDirectory', baselines, '-SizeBudgetPath', budget]);
  assert.equal(comparison.status, 0, comparison.stdout + comparison.stderr);
  const comparedReport = JSON.parse(fs.readFileSync(path.join(compared, 'dist', path.basename(archive) + '.json'), 'utf8'));
  assert.equal(comparedReport.sizeAnalysis.comparison.deltaBytes, 0);
  assert.equal(comparedReport.sizeAnalysis.budget.passed, true);
  fs.writeFileSync(budget, JSON.stringify({totalBytes: 10}));
  const over = fixture('over'), failed = run(over, ['-SizeBudgetPath', budget]);
  assert.notEqual(failed.status, 0);
  assert.equal(fs.readdirSync(path.join(over, 'dist')).length, 0);
  const analysisFile = fs.readdirSync(path.join(over, 'out/package')).find(name => name.endsWith('.size-analysis.json'));
  assert.ok(analysisFile);
  assert.equal(JSON.parse(fs.readFileSync(path.join(over, 'out/package', analysisFile), 'utf8')).budget.passed, false);
});
