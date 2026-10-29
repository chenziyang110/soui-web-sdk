'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const childProcess = require('node:child_process');
const {isUtf8} = require('node:buffer');
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
  for (const relative of ['tools/Package.ps1', 'tools/Package-Applications.ps1', 'tools/internal/compression-profiles.cjs',
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
    fs.mkdirSync(path.join(stage,'resources/\\u4e2d\\u6587'),{recursive:true});
    fs.writeFileSync(path.join(stage,'resources/\\u4e2d\\u6587/\\u8bf4\\u660e.txt'),'Unicode filename payload');
    const payload=id.snapshot(stage,id.walk(stage));
    const report={schema:'soui.application-size/v1',version:'0.5.0',target,architecture,configuration,
      rendererBackend:'miniblink',webTarget:null,buildId:'fixture-'+target,sourceSha256:'a'.repeat(64),
      payload,categories:{kernel:188416},releaseReady:false};
    console.log(id.encoded(report).replace(/[\\u0080-\\uffff]/g,value=>'\\\\u'+value.charCodeAt(0).toString(16).padStart(4,'0')));
  `);
  // Replace only the upstream source staging boundary. The real Package entry
  // still chooses the version/path, invokes 7-Zip, checks it and writes SHA256.
  put('tools/internal/source-archive.cjs', `
    const fs=require('node:fs'),path=require('node:path');
    const [command,stage]=process.argv.slice(2);
    if(command!=='stage')throw Error('unexpected fixture command');
    fs.mkdirSync(path.join(stage,'third_party/\\u4e2d\\u6587'),{recursive:true});
    fs.writeFileSync(path.join(stage,'VERSION'),'0.5.0\\n');
    fs.writeFileSync(path.join(stage,'third_party/\\u4e2d\\u6587/\\u8bf4\\u660e.md'),'source fixture bytes');
    console.log(JSON.stringify({version:'0.5.0'}));
  `);
  const readback = put('zip-readback.ps1', `
    param([string]$Archive,[string]$Destination,[int]$CodePage)
    $ErrorActionPreference='Stop'
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $base=[IO.Path]::GetFullPath($Destination)
    if([IO.Directory]::Exists($base)){throw 'Readback destination already exists'}
    [IO.Directory]::CreateDirectory($base)|Out-Null
    $zip=[IO.Compression.ZipFile]::Open($Archive,[IO.Compression.ZipArchiveMode]::Read,[Text.Encoding]::GetEncoding($CodePage))
    try {
      foreach($entry in $zip.Entries){
        $target=[IO.Path]::GetFullPath((Join-Path $base $entry.FullName))
        if(-not $target.StartsWith($base+'\\',[StringComparison]::OrdinalIgnoreCase)){throw 'Readback path escaped destination'}
        if($entry.FullName.EndsWith('/')){[IO.Directory]::CreateDirectory($target)|Out-Null;continue}
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
      }
    } finally {$zip.Dispose()}
  `);
  const run = (script, extra) => childProcess.spawnSync(powershell,
    ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', script, ...extra],
    {encoding: 'utf8', timeout: 120000, windowsHide: true, env: {...process.env,
      PATH: `${path.dirname(executable || '.')}${path.delimiter}${process.env.PATH}`,
      ...(process.platform === 'win32' ? {PSModulePath: path.join(process.env.SystemRoot || 'C:/Windows',
        'System32/WindowsPowerShell/v1.0/Modules')} : {})}});
  return {root, put, run: extra => run(path.join(root, 'tools/Package-Applications.ps1'), extra),
    runSource: extra => run(path.join(root, 'tools/Package.ps1'), extra),
    readback: (archive, destination, codePage) => {
      const result = run(readback, ['-Archive', archive, '-Destination', destination, '-CodePage', String(codePage)]);
      assert.equal(result.status, 0, result.stderr || result.stdout);
    }};
}
function extract(archive, destination) {
  const result = childProcess.spawnSync(executable, ['x', archive, `-o${destination}`, '-y'], {encoding: 'utf8', timeout: 60000, windowsHide: true});
  assert.equal(result.status, 0, result.stderr || result.stdout);
}

// Parse the physical ZIP headers: 7-Zip readback alone can hide the ACP bug by
// honoring Unicode Path extra fields even when ordinary filename bytes are not UTF-8.
function assertUtf8Zip(archive, expectedNames) {
  const body = fs.readFileSync(archive), end = body.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
  assert.ok(end >= 0, 'ZIP end of central directory is missing');
  assert.equal(end + 22 + body.readUInt16LE(end + 20), body.length);
  assert.equal(body.readUInt16LE(end + 4), 0, 'fixture ZIP must be single-disk');
  assert.equal(body.readUInt16LE(end + 6), 0);
  const count = body.readUInt16LE(end + 10), names = [];
  assert.equal(body.readUInt16LE(end + 8), count);
  const directory = body.readUInt32LE(end + 16);
  let cursor = directory;
  for (let index = 0; index < count; ++index) {
    assert.equal(body.readUInt32LE(cursor), 0x02014b50);
    const flags = body.readUInt16LE(cursor + 8), length = body.readUInt16LE(cursor + 28);
    const name = body.subarray(cursor + 46, cursor + 46 + length);
    assert.ok(isUtf8(name), 'central filename must contain valid UTF-8 bytes');
    const decoded = name.toString('utf8');
    if (/[^\x00-\x7f]/.test(decoded)) assert.equal(flags & 0x0800, 0x0800, 'central UTF-8 flag is missing');
    const local = body.readUInt32LE(cursor + 42);
    assert.equal(body.readUInt32LE(local), 0x04034b50);
    assert.equal(body.readUInt16LE(local + 6) & 0x0800, flags & 0x0800, 'local UTF-8 flag differs');
    assert.equal(body.readUInt16LE(local + 26), name.length);
    assert.deepEqual(body.subarray(local + 30, local + 30 + name.length), name, 'local filename differs');
    if (!decoded.endsWith('/')) names.push(decoded);
    cursor += 46 + length + body.readUInt16LE(cursor + 30) + body.readUInt16LE(cursor + 32);
  }
  assert.equal(cursor, directory + body.readUInt32LE(end + 12));
  assert.deepEqual(names.sort(), [...expectedNames].sort(), 'ZIP file set differs from staged payload');
}

test('default ZIP preserves compression settings and selects UTF-8 filenames explicitly', () => {
  assert.deepEqual(profiles.resolve(), {profile: 'zip', format: 'zip', requestedDictionaryBytes: null, solid: false,
    arguments: ['-tzip', '-mx=5', '-mmt=2', '-mcu=on'], extension: '.zip', suffix: '', compression: '7-Zip ZIP -mx=5 -mmt=2'});
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
      if (expected.format === 'zip') assertUtf8Zip(archive, report.payload.files.map(file => file.path));
      if (!selection) {
        // A reader using an unrelated legacy code page must obey ZIP bit 11.
        for (const codePage of [437, 1252]) {
          const readback = path.join(f.root, `ps5-${codePage}-${target}`);
          f.readback(archive, readback, codePage);
          assert.deepEqual(id.snapshot(readback, id.walk(readback)), report.payload);
        }
      }
      const listing = childProcess.spawnSync(executable, ['l', '-slt', archive], {encoding: 'utf8'});
      assert.equal(listing.status, 0, listing.stderr);
      assert.match(listing.stdout, new RegExp(`Type = ${expected.format}`));
      assert.equal(report.releaseReady, false);
    }
  });
}

for (const mode of ['default', 'explicit-source-roots']) {
  test(`public source ZIP ${mode} preserves Unicode names through PS5 legacy-code-page readback`, {skip: !executable || process.platform !== 'win32'}, t => {
    const f = fixture(t), workspace = path.join(f.root, 'external');
    const extra = mode === 'default' ? [] : ['-SourceRootsBinding', f.put('binding.json', '{}'), '-PackageWorkspace', workspace];
    if (mode !== 'default') f.put('VERSION', 'tool-host-other\n');
    const result = f.runSource(extra);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const archive = path.join(mode === 'default' ? f.root : workspace, 'dist/soui-sdk-0.5.0-full-source.zip');
    const expected = [{path: 'VERSION', body: '0.5.0\n'}, {path: 'third_party/\u4e2d\u6587/\u8bf4\u660e.md', body: 'source fixture bytes'}];
    assertUtf8Zip(archive, expected.map(file => file.path));
    assert.equal(fs.readFileSync(archive + '.sha256', 'utf8'), `${id.hash(fs.readFileSync(archive))}  ${path.basename(archive)}${os.EOL}`);
    for (const codePage of [437, 1252]) {
      const destination = path.join(f.root, `source-ps5-${codePage}`);
      f.readback(archive, destination, codePage);
      assert.deepEqual(id.walk(destination), expected.map(file => file.path).sort());
      for (const file of expected) assert.deepEqual(fs.readFileSync(id.safePath(destination, file.path)), Buffer.from(file.body));
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
