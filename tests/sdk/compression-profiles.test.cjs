'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const childProcess = require('node:child_process');
const profiles = require('../../tools/internal/compression-profiles.cjs');
const bundle = require('../../tools/internal/runtime-bundle.cjs');
const id = require('../../tools/internal/delivery-identity.cjs');
const sdkRoot = path.resolve(__dirname, '../..');
const powershell = process.platform === 'win32' ? 'powershell.exe' : 'pwsh';
const found = childProcess.spawnSync(process.platform === 'win32' ? 'where.exe' : 'which', [process.platform === 'win32' ? '7z.exe' : '7z'], {encoding: 'utf8'});
const executable = (found.stdout || '').split(/\r?\n/).find(file => file && fs.existsSync(file)) ||
  (process.env.ProgramFiles && fs.existsSync(path.join(process.env.ProgramFiles, '7-Zip/7z.exe')) ? path.join(process.env.ProgramFiles, '7-Zip/7z.exe') : null);

function fixture(t) {
  const parent = fs.realpathSync(os.tmpdir()), root = fs.mkdtempSync(path.join(parent, 'soui-compression-test-'));
  t.after(() => {
    assert.equal(path.dirname(root), parent);
    assert.ok(path.basename(root).startsWith('soui-compression-test-'));
    fs.rmSync(id.safePath(root), {recursive: true, force: true});
  });
  const put = (name, content) => {
    const file = id.safePath(root, name);
    fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, content); return file;
  };
  put('VERSION', '0.5.0\n');
  for (const relative of ['tools/Package-Applications.ps1', 'tools/internal/compression-profiles.cjs',
    'tools/internal/runtime-bundle.cjs', 'tools/internal/delivery-identity.cjs', 'tools/internal/sdk-version.cjs', 'tools/internal/size-report.cjs']) {
    const target = id.safePath(root, relative);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.copyFileSync(path.join(sdkRoot, relative), target);
  }
  put('tools/internal/build-evidence.cjs', `
    const fs=require('node:fs'),path=require('node:path'),id=require('./delivery-identity.cjs');
    const [command,architecture,configuration,target,stage]=process.argv.slice(2);
    if(command!=='stage-app')throw Error('unexpected fixture command');
    fs.mkdirSync(path.join(stage,'resources'),{recursive:true});
    fs.writeFileSync(path.join(stage,target+'.exe'),'test executable '+target);
    fs.writeFileSync(path.join(stage,'node.dll'),Buffer.from('repeated runtime bytes '.repeat(8192)));
    fs.writeFileSync(path.join(stage,'resources/index.html'),'<html>same fixture</html>');
    const payload=id.snapshot(stage,id.walk(stage));
    console.log(id.encoded({schema:'soui.application-size/v1',version:'0.5.0',target,architecture,configuration,
      rendererBackend:'miniblink',webTarget:null,buildId:'fixture-'+target,sourceSha256:'a'.repeat(64),
      payload,categories:{kernel:188416},releaseReady:false}));
  `);
  return {root, put, run: extra => childProcess.spawnSync(powershell,
    ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.join(root, 'tools/Package-Applications.ps1'), ...extra],
    {encoding: 'utf8', timeout: 120000, windowsHide: true, env: {...process.env,
      PATH: `${path.dirname(executable || '.')}${path.delimiter}${process.env.PATH}`,
      ...(process.platform === 'win32' ? {PSModulePath: path.join(process.env.SystemRoot || 'C:/Windows',
        'System32/WindowsPowerShell/v1.0/Modules')} : {})}})};
}
function extract(archive, destination) {
  const result = childProcess.spawnSync(executable, ['x', archive, `-o${destination}`, '-y'], {encoding: 'utf8', timeout: 60000, windowsHide: true});
  assert.equal(result.status, 0, result.stderr || result.stdout);
}

test('default ZIP definition preserves the exact original arguments, extension and compression text', () => {
  assert.deepEqual(profiles.resolve(), {profile: 'zip', format: 'zip', requestedDictionaryBytes: null, solid: false,
    arguments: ['-tzip', '-mx=5', '-mmt=2'], extension: '.zip', suffix: '', compression: '7-Zip ZIP -mx=5 -mmt=2'});
  assert.deepEqual(profiles.resolve(), profiles.resolve('zip'));
});
test('7z profiles explicitly select LZMA2, dictionary, solid mode and two threads', () => {
  for (const [name, level, dictionary] of [['7z-balanced', 7, 32], ['7z-small', 9, 64]]) {
    const profile = profiles.resolve(name);
    assert.deepEqual(profile.arguments, ['-t7z', '-m0=LZMA2', `-mx=${level}`, `-md=${dictionary}m`, '-ms=on', '-mmt=2']);
    assert.equal(profile.requestedDictionaryBytes, dictionary * 1024 * 1024);
    assert.equal(profile.solid, true);
    assert.equal(profile.format, '7z');
    assert.equal(profile.suffix, `-${name}`);
    assert.equal(profile.extension, '.7z');
  }
  const mutated = profiles.resolve('7z-small'); mutated.arguments.push('-md=1g');
  assert.equal(profiles.resolve('7z-small').arguments.length, 6);
});
test('invalid names and non-string profile selections are rejected', () => {
  for (const invalid of [null, [], {}, '', '7z', 'ZIP', 'zip;touch-file', '__proto__', 'constructor'])
    assert.throws(() => profiles.resolve(invalid), /profile/);
});
test('tool identity records the actual observed 7-Zip version and executable content', {skip: !executable}, () => {
  const identity = profiles.toolIdentity(executable);
  assert.equal(identity.file, path.basename(executable));
  assert.equal(identity.sha256, id.hash(fs.readFileSync(executable)));
  assert.equal(identity.bytes, fs.statSync(executable).size);
  assert.match(identity.version, /^\d+\.\d+(?:\.\d+)?$/);
  assert.equal(Object.hasOwn(identity, 'path'), false);
  const companion = path.join(path.dirname(executable), '7z.dll');
  if (fs.existsSync(companion)) {
    assert.deepEqual(identity.runtimeFiles.find(file => file.file === '7z.dll'), {
      file: '7z.dll', bytes: fs.statSync(companion).size, sha256: id.hash(fs.readFileSync(companion))});
  } else assert.ok(Array.isArray(identity.runtimeFiles));
});
test('tool identity rejects missing files, version failures and changing executables', t => {
  const f = fixture(t), fake = f.put('fake-tool.exe', 'fixture executable bytes');
  assert.throws(() => profiles.toolIdentity(path.join(f.root, 'missing.exe')));
  const original = childProcess.spawnSync;
  try {
    childProcess.spawnSync = () => ({status: 1, stdout: '7-Zip 25.01 (x64)'});
    assert.throws(() => profiles.toolIdentity(fake), /identify/);
    childProcess.spawnSync = () => ({status: 0, stdout: 'unrelated command'});
    assert.throws(() => profiles.toolIdentity(fake), /version/);
    childProcess.spawnSync = () => { fs.appendFileSync(fake, 'changed'); return {status: 0, stdout: '7-Zip 25.01 (x64)'}; };
    assert.throws(() => profiles.toolIdentity(fake), /changed/);
    const codec = f.put('codecs/codec.dll', 'observed codec bytes');
    const libraryOutput = `7-Zip 25.01 (x64)\nLibs:\n 0 : 25.01 : ${codec}\n\nFormats:\n`;
    childProcess.spawnSync = () => ({status: 0, stdout: libraryOutput});
    assert.deepEqual(profiles.toolIdentity(fake).runtimeFiles, [{file: 'codecs/codec.dll',
      bytes: fs.statSync(codec).size, sha256: id.hash(fs.readFileSync(codec))}]);
    let observations = 0;
    childProcess.spawnSync = () => {
      if (++observations === 2) fs.appendFileSync(codec, 'changed library');
      return {status: 0, stdout: libraryOutput};
    };
    assert.throws(() => profiles.toolIdentity(fake), /runtime libraries changed/);
  } finally { childProcess.spawnSync = original; }
});
test('profile CLI provides ASCII JSON and rejects unknown, duplicate or incomplete arguments', {skip: !executable}, () => {
  const script = path.join(sdkRoot, 'tools/internal/compression-profiles.cjs');
  const run = args => childProcess.spawnSync(process.execPath, [script, ...args], {encoding: 'utf8'});
  const result = run(['describe', '--profile', '7z-balanced', '--tool', executable]);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(JSON.parse(result.stdout).requestedDictionaryBytes, 33554432);
  assert.match(JSON.parse(result.stdout).toolIdentity.sha256, /^[a-f0-9]{64}$/);
  for (const args of [['describe', '--profile', '7z'], ['describe', '--profile'], ['describe', '--unknown', 'x'],
    ['describe', '--profile', 'zip', '--profile', 'zip'], ['resolve']]) assert.notEqual(run(args).status, 0);
});

for (const selection of [null, 'zip', '7z-balanced', '7z-small']) {
  test(`actual PowerShell packaging ${selection || 'implicit ZIP'} keeps application bytes and reports exact arguments`, {skip: !executable || process.platform !== 'win32'}, t => {
    const f = fixture(t), expected = profiles.resolve(selection || 'zip');
    const extra = selection ? ['-CompressionProfile', selection] : [];
    const result = f.run(extra);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    for (const target of ['SouiStarter', 'SouiWorkbench']) {
      const archive = path.join(f.root, `dist/${target}-0.5.0-windows-x64${expected.suffix}${expected.extension}`);
      assert.ok(fs.existsSync(archive));
      const report = id.readJson(archive + '.json'), metadata = report.archive;
      assert.equal(metadata.compression, expected.compression);
      assert.equal(metadata.profile, expected.profile);
      assert.equal(metadata.format, expected.format);
      assert.equal(metadata.requestedDictionaryBytes, expected.requestedDictionaryBytes);
      assert.equal(metadata.solid, expected.solid);
      assert.deepEqual(metadata.exactArguments, ['a', ...expected.arguments, archive, '*']);
      assert.equal(metadata.toolIdentity.sha256, id.hash(fs.readFileSync(executable)));
      const destination = path.join(f.root, `decoded-${target}`);
      extract(archive, destination);
      assert.deepEqual(id.snapshot(destination, id.walk(destination)), report.payload);
      const listing = childProcess.spawnSync(executable, ['l', '-slt', archive], {encoding: 'utf8'});
      assert.equal(listing.status, 0, listing.stderr);
      assert.match(listing.stdout, new RegExp(`Type = ${expected.format}`));
      assert.equal(report.releaseReady, false);
    }
  });
}
for (const selection of ['zip', '7z-balanced', '7z-small']) {
  test(`actual PowerShell Bundle ${selection} remains verifiable and restores complete independent applications`, {skip: !executable || process.platform !== 'win32'}, t => {
    const f = fixture(t), expected = profiles.resolve(selection), result = f.run(['-Bundle', '-CompressionProfile', selection]);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const archive = path.join(f.root, `dist/SouiApplications-0.5.0-windows-x64-compatibility.bundle${expected.suffix}${expected.extension}`);
    const report = id.readJson(archive + '.json');
    assert.equal(report.archive.compression, expected.compression);
    assert.equal(report.archive.profile, expected.profile);
    assert.deepEqual(report.archive.exactArguments, ['a', ...expected.arguments, archive, '*']);
    assert.equal(report.archive.toolIdentity.sha256, id.hash(fs.readFileSync(executable)));
    const decoded = path.join(f.root, 'decoded-bundle');
    extract(archive, decoded);
    assert.equal(bundle.verify(decoded).verified, true);
    const restored = path.join(f.root, 'restored');
    assert.equal(bundle.restore(decoded, restored).restored, true);
    const manifest = id.readJson(path.join(decoded, 'manifest.json'));
    for (const app of manifest.applications)
      assert.deepEqual(id.snapshot(path.join(restored, app.name), id.walk(path.join(restored, app.name))), app.report.payload);
  });
}
test('archive conflict and size budget rejection preserve existing files and never emit a success report', {skip: !executable || process.platform !== 'win32'}, t => {
  const f = fixture(t), archive = f.put('dist/SouiStarter-0.5.0-windows-x64-7z-small.7z', 'user-owned');
  const conflict = f.run(['-CompressionProfile', '7z-small']);
  assert.notEqual(conflict.status, 0);
  assert.equal(fs.readFileSync(archive, 'utf8'), 'user-owned');
  assert.equal(fs.existsSync(archive + '.json'), false);
  const budget = f.put('budget.json', JSON.stringify({totalBytes: 1}));
  const rejected = f.run(['-CompressionProfile', '7z-balanced', '-SizeBudgetPath', budget]);
  assert.notEqual(rejected.status, 0);
  assert.equal(fs.existsSync(path.join(f.root, 'dist/SouiStarter-0.5.0-windows-x64-7z-balanced.7z')), false);
});
test('legacy positional architecture/profile/baseline/budget arguments retain their original bindings', {skip: !executable || process.platform !== 'win32'}, t => {
  const f = fixture(t), first = f.run([]);
  assert.equal(first.status, 0, first.stderr || first.stdout);
  for (const target of ['SouiStarter', 'SouiWorkbench']) {
    const archive = path.join(f.root, `dist/${target}-0.5.0-windows-x64.zip`);
    f.put(`baselines/${target}.json`, fs.readFileSync(archive + '.json'));
    for (const file of [archive, archive + '.json', archive + '.sha256']) fs.unlinkSync(file);
  }
  const budget = f.put('budget.json', JSON.stringify({totalBytes: 1024 * 1024, growthBytes: 0}));
  const result = f.run(['x64', 'compatibility', path.join(f.root, 'baselines'), budget]);
  assert.equal(result.status, 0, result.stderr || result.stdout);
  for (const target of ['SouiStarter', 'SouiWorkbench']) {
    const report = id.readJson(path.join(f.root, `dist/${target}-0.5.0-windows-x64.zip.json`));
    assert.equal(report.archive.profile, 'zip');
    assert.equal(report.sizeAnalysis.budget.passed, true);
    assert.equal(report.sizeAnalysis.comparison.deltaBytes, 0);
  }
});
