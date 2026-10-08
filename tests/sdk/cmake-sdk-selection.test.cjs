'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const sdk = require('../../tools/sdk/soui-sdk.cjs');

const repository = path.resolve(__dirname, '../..');
const helper = path.join(repository, 'tools/internal/CMake-Configure.ps1');
const guard = path.join(repository, 'cmake/SouiWindowsSdk.cmake');
const windows = process.platform === 'win32';
const quote = value => `'${value.replace(/'/g, "''")}'`;
function write(file, text) {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, text);
}
function fixture(t) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-cmake-sdk-'));
  t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
  const source = path.join(directory, 'source with spaces');
  fs.mkdirSync(source);
  return {directory, source, build: path.join(source, 'out/build')};
}
function powershell(script) {
  return cp.spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand',
    Buffer.from(`[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\n$ProgressPreference='SilentlyContinue'\n` +
      `try {$ErrorActionPreference='Stop'\n${script}\n} catch {[Console]::Error.WriteLine($_.Exception.Message);exit 1}`, 'utf16le').toString('base64')],
  {encoding: 'utf8', windowsHide: true, timeout: 30000});
}
function argumentsFor(f, options = {}) {
  const executable = options.executable || path.join(f.directory, 'version.ps1');
  if (!options.executable) write(executable, `Write-Output 'cmake version ${options.version || '4.2.3'}'\n$global:LASTEXITCODE=0\n`);
  return powershell(`. ${quote(options.helper || helper)}\n` +
    `$configure=@(Get-SouiCMakeConfigureArguments -CMake ${quote(executable)} -WorkspaceRoot ${quote(f.source)} ` +
    `-SourceDirectory ${quote(options.source || f.source)} -BuildDirectory ${quote(options.build || f.build)} ` +
    `-Architecture '${options.architecture || 'x64'}'${options.fresh ? ' -FreshConfigure' : ''})\n` +
    'ConvertTo-Json -InputObject $configure -Compress');
}
function succeeded(result) {
  assert.equal(result.status, 0, result.stderr || result.stdout);
  return JSON.parse(result.stdout.trim());
}
function cache(f, options = {}) {
  write(path.join(f.build, 'CMakeCache.txt'), [
    'CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022',
    `CMAKE_GENERATOR_PLATFORM:INTERNAL=${options.platform || 'x64,version=10.0.19041.0'}`,
    `CMAKE_HOME_DIRECTORY:INTERNAL=${f.source.replace(/\\/g, '/')}`,
    `CMAKE_CACHEFILE_DIR:INTERNAL=${f.build.replace(/\\/g, '/')}`,
    'CMAKE_SYSTEM_VERSION:UNINITIALIZED=10.0.19041.0',
    ...options.extra || [], '',
  ].join('\n'));
  write(path.join(f.build, 'CMakeFiles/4.2.3/CMakeSystem.cmake'),
    `set(CMAKE_SYSTEM_VERSION "${options.system || '10.0.19041.0'}")\n`);
}
function snapshot(directory) {
  const files = {};
  function visit(root) {
    for (const item of fs.readdirSync(root, {withFileTypes: true})) {
      const file = path.join(root, item.name);
      if (item.isDirectory()) visit(file);
      else if (!item.isSymbolicLink()) files[path.relative(directory, file)] = fs.readFileSync(file).toString('base64');
    }
  }
  visit(directory);
  return files;
}

test('all configuration entry points use the shared SDK selector', () => {
  for (const entry of ['Build', 'Verify-Release', 'Test-Acceptance']) {
    const text = fs.readFileSync(path.join(repository, `tools/${entry}.ps1`), 'utf8');
    assert.match(text, /Get-SouiCMakeConfigureArguments/, `${entry} must use the common configuration contract`);
    assert.match(text, /\[switch\]\$FreshConfigure/, `${entry} must expose explicit cache refresh`);
  }
});

test('CMake version and architecture select a compatible exact SDK request', {skip: !windows}, t => {
  const f = fixture(t);
  for (const version of ['3.24.0', '3.26.6', '3.27.0', '4.2.3']) {
    for (const architecture of ['x86', 'x64']) {
      const args = succeeded(argumentsFor(f, {version, architecture}));
      const expected = architecture === 'x86' ? 'Win32' : 'x64';
      assert.equal(args[args.indexOf('-A') + 1], Number(version.split('.')[0]) >= 4 || version.startsWith('3.27.')
        ? `${expected},version=10.0.19041.0` : expected);
      assert.ok(args.includes('-DCMAKE_SYSTEM_VERSION=10.0.19041.0'));
      assert.ok(args.some(arg => arg.startsWith('-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES:STRING=') && arg.includes('SouiWindowsSdk.cmake')));
      assert.equal(args.includes('--fresh'), false);
    }
  }
  for (const version of ['3.23.9', 'unrecognizable']) {
    const result = argumentsFor(f, {version});
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /CMake.*3\.24|CMake version/i);
    assert.equal(fs.existsSync(f.build), false);
  }
});

test('platform and persisted system conflicts are refused without changing cached bytes', {skip: !windows}, t => {
  const f = fixture(t);
  for (const options of [{platform: 'x64'}, {platform: 'Win32,version=10.0.19041.0'}, {system: '10'}]) {
    cache(f, options);
    const before = snapshot(f.build);
    const result = argumentsFor(f);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /FreshConfigure/);
    assert.deepEqual(snapshot(f.build), before);
  }
});

test('matching cache is reusable and preserves other project includes', {skip: !windows}, t => {
  const f = fixture(t);
  cache(f, {extra: ['CMAKE_PROJECT_TOP_LEVEL_INCLUDES:STRING=other-setup.cmake']});
  const before = snapshot(f.build);
  const args = succeeded(argumentsFor(f));
  assert.ok(args.some(arg => arg.includes('other-setup.cmake;') && arg.includes('SouiWindowsSdk.cmake')));
  assert.equal(args.includes('--fresh'), false);
  assert.deepEqual(snapshot(f.build), before);
});

test('release verification adapts explicit refresh for historical Build without altering it', {skip: !windows}, t => {
  const f = fixture(t);
  const buildScript = path.join(f.source, 'tools/Build.ps1');
  const executable = path.join(f.directory, 'version.ps1');
  write(executable, "Write-Output 'cmake version 4.2.3'\n$global:LASTEXITCODE=0\n");
  function capture(current, fresh) {
    write(buildScript, `param([string]$Architecture,[string]$Configuration${current ? ',[switch]$FreshConfigure' : ''})\nthrow 'must not execute fixture Build'\n`);
    const before = fs.readFileSync(buildScript);
    const result = powershell(`. ${quote(helper)}\n` +
      `$releaseTree=[Management.Automation.Language.Parser]::ParseFile(${quote(path.join(repository, 'tools/Verify-Release.ps1'))},[ref]$null,[ref]$null)\n` +
      "$releaseFunction=$releaseTree.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Build-And-Smoke'},$true)\n" +
      'Invoke-Expression $releaseFunction.Extent.Text\n' +
      `$FreshConfigure=$${fresh};$cmake=${quote(executable)};$powershell='powershell.exe';$output=${quote(path.join(f.source, 'out/release'))}\n` +
      '$script:captured=[Collections.Generic.List[object]]::new()\n' +
      'function Invoke-Checked([string]$Executable,[string[]]$Arguments,[string]$Log){\n' +
      '  $script:captured.Add([pscustomobject]@{executable=$Executable;arguments=$Arguments;log=$Log})\n' +
      "  if($Executable -eq 'powershell.exe'){throw 'build captured'}\n}\n" +
      `try {Build-And-Smoke ${quote(f.source)} 'fixture'} catch {if($_.Exception.Message -ne 'build captured'){throw}}\n` +
      'ConvertTo-Json -InputObject @($script:captured.ToArray()) -Depth 4 -Compress');
    assert.deepEqual(fs.readFileSync(buildScript), before);
    assert.equal(result.status, 0, result.stderr);
    return JSON.parse(result.stdout.slice(result.stdout.indexOf('[')).trim());
  }
  const historical = capture(false, true);
  assert.equal(historical.length, 2);
  assert.equal(historical[0].arguments[historical[0].arguments.indexOf('-A') + 1], 'x64');
  assert.ok(historical[0].arguments.includes('--fresh'));
  assert.ok(historical[0].arguments.some(arg => arg.includes('SouiWindowsSdk.cmake')));
  assert.match(historical[0].log, /legacy-configure\.log$/);
  assert.equal(historical[1].arguments.includes('-FreshConfigure'), false);
  const normal = capture(false, false);
  assert.equal(normal.length, 1);
  assert.equal(normal[0].arguments.includes('-FreshConfigure'), false);
  const current = capture(true, true);
  assert.equal(current.length, 1);
  assert.ok(current[0].arguments.includes('-FreshConfigure'));
});

test('FreshConfigure is opt-in and only schedules CMake refresh inside owned out', {skip: !windows}, t => {
  const f = fixture(t);
  cache(f, {platform: 'x64', system: '10'});
  const before = snapshot(f.build);
  const args = succeeded(argumentsFor(f, {fresh: true}));
  assert.ok(args.includes('--fresh'));
  assert.deepEqual(snapshot(f.build), before, 'constructing configure arguments must never clean anything');
  for (const build of [f.source, path.join(f.source, 'out'), path.join(f.source, 'out/../../outside'),
    path.join(f.source, 'out-sibling/build'), path.join(f.directory, 'foreign/out/build')]) {
    const result = argumentsFor(f, {fresh: true, build});
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /workspace.*out|generated.*out/i);
  }
  assert.deepEqual(snapshot(f.build), before);
});

test('FreshConfigure refuses foreign cache ownership and incomplete cache evidence', {skip: !windows}, t => {
  const f = fixture(t);
  cache(f);
  const file = path.join(f.build, 'CMakeCache.txt');
  write(file, fs.readFileSync(file, 'utf8').replace(f.source.replace(/\\/g, '/'), path.join(f.directory, 'foreign').replace(/\\/g, '/')));
  const before = snapshot(f.build);
  const foreign = argumentsFor(f, {fresh: true});
  assert.notEqual(foreign.status, 0);
  assert.match(foreign.stderr, /source|ownership/i);
  assert.deepEqual(snapshot(f.build), before);
  fs.unlinkSync(file);
  const incomplete = argumentsFor(f, {fresh: true});
  assert.notEqual(incomplete.status, 0);
  assert.match(incomplete.stderr, /ownership|CMakeCache/i);
});

test('FreshConfigure refuses directory links including links inside CMakeFiles', {skip: !windows}, t => {
  const f = fixture(t);
  const outside = path.join(f.directory, 'foreign');
  write(path.join(outside, 'keep.txt'), 'foreign bytes');
  fs.mkdirSync(path.dirname(f.build), {recursive: true});
  fs.symlinkSync(outside, f.build, 'junction');
  const linkedBuild = argumentsFor(f, {fresh: true});
  assert.notEqual(linkedBuild.status, 0);
  assert.match(linkedBuild.stderr, /link|reparse/i);
  fs.unlinkSync(f.build);
  cache(f);
  const link = path.join(f.build, 'CMakeFiles/foreign');
  fs.symlinkSync(outside, link, 'junction');
  const linkedCache = argumentsFor(f, {fresh: true});
  assert.notEqual(linkedCache.status, 0);
  assert.match(linkedCache.stderr, /link|reparse/i);
  assert.equal(fs.readFileSync(path.join(outside, 'keep.txt'), 'utf8'), 'foreign bytes');
  fs.unlinkSync(link);
  const broken = {...f, build: path.join(f.source, 'out/broken')};
  write(path.join(broken.build, 'CMakeCache.txt'), fs.readFileSync(path.join(f.build, 'CMakeCache.txt'), 'utf8')
    .replace(f.build.replace(/\\/g, '/'), broken.build.replace(/\\/g, '/')));
  const brokenLink = path.join(broken.build, 'CMakeFiles');
  fs.symlinkSync(path.join(outside, 'missing'), brokenLink, 'junction');
  const brokenCache = argumentsFor(broken, {fresh: true});
  assert.notEqual(brokenCache.status, 0);
  assert.match(brokenCache.stderr, /link|reparse/i);
  fs.unlinkSync(brokenLink);
});

test('SDK guard checks the selected SDK rather than the requested system cache', t => {
  const f = fixture(t);
  for (const selected of ['10.0.19041.0', '10.0.26100.0', '']) {
    const script = path.join(f.directory, 'check.cmake');
    write(script, `set(CMAKE_GENERATOR "Visual Studio 17 2022")\nset(CMAKE_SYSTEM_VERSION "10.0.19041.0")\n` +
      `set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "${selected}")\ninclude("${guard.replace(/\\/g, '/')}")\n`);
    const result = cp.spawnSync('cmake', ['-P', script], {encoding: 'utf8', windowsHide: true, timeout: 15000});
    if (selected === '10.0.19041.0') assert.equal(result.status, 0, result.stderr);
    else {
      assert.notEqual(result.status, 0);
      assert.match(result.stderr, /selected|19041/i);
    }
  }
});

test('packaging carries the selector and SDK guard into a relocated generated project', {skip: !windows}, t => {
  const f = fixture(t);
  for (const relative of sdk.managedRoots) write(path.join(f.source, relative, 'fixture.txt'), 'fixture\n');
  for (const relative of sdk.managedFiles) write(path.join(f.source, relative), 'fixture\n');
  write(path.join(f.source, 'tools/internal/CMake-Configure.ps1'), fs.readFileSync(helper));
  write(path.join(f.source, 'cmake/SouiWindowsSdk.cmake'), fs.readFileSync(guard));
  const pkg = path.join(f.directory, 'package');
  sdk.pack(f.source, pkg, '0.5.0-preview.1');
  const project = path.join(f.directory, 'generated project');
  sdk.install(pkg, project);
  const relocated = path.join(f.directory, 'relocated project');
  fs.renameSync(project, relocated);
  sdk.readLock(relocated);
  const args = succeeded(argumentsFor({...f, source: relocated, build: path.join(relocated, 'out/x64')},
    {helper: path.join(relocated, 'tools/internal/CMake-Configure.ps1')}));
  const includes = args.find(arg => arg.startsWith('-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES:STRING='));
  assert.ok(includes.includes(relocated.replace(/\\/g, '/')));
  assert.equal(includes.includes(repository.replace(/\\/g, '/')), false);
});

test('Visual Studio configure-only fixture actually selects 19041 and can explicitly refresh a legacy cache', {skip: !windows}, t => {
  const f = fixture(t);
  write(path.join(f.source, 'CMakeLists.txt'), 'cmake_minimum_required(VERSION 3.24)\n' +
    'project(SdkSelectionFixture LANGUAGES NONE)\nadd_custom_target(SdkSelectionFixture)\n' +
    'file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")\n');
  const executable = powershell('(Get-Command cmake.exe -ErrorAction Stop).Source').stdout.trim();
  assert.ok(executable);
  const legacy = cp.spawnSync(executable, ['-S', f.source, '-B', f.build, '-G', 'Visual Studio 17 2022', '-A', 'x64'],
    {encoding: 'utf8', windowsHide: true, timeout: 30000});
  assert.equal(legacy.status, 0, legacy.stderr || legacy.stdout);
  const before = snapshot(f.build);
  const refused = argumentsFor(f, {executable});
  assert.notEqual(refused.status, 0);
  assert.deepEqual(snapshot(f.build), before);
  const args = succeeded(argumentsFor(f, {executable, fresh: true}));
  const configured = cp.spawnSync(executable, args, {encoding: 'utf8', windowsHide: true, timeout: 30000});
  assert.equal(configured.status, 0, configured.stderr || configured.stdout);
  assert.equal(fs.readFileSync(path.join(f.build, 'selected.txt'), 'utf8'), '10.0.19041.0');
  assert.match(fs.readFileSync(path.join(f.build, 'SdkSelectionFixture.vcxproj'), 'utf8'),
    /<WindowsTargetPlatformVersion>10\.0\.19041\.0<\/WindowsTargetPlatformVersion>/);
  const again = cp.spawnSync(executable, succeeded(argumentsFor(f, {executable})),
    {encoding: 'utf8', windowsHide: true, timeout: 30000});
  assert.equal(again.status, 0, again.stderr || again.stdout);
  // Historical Build uses the plain platform even with a new CMake binary.
  // Its explicit fresh adapter must retain the result assertion and initialize
  // a cache that the original command can reuse without changing platforms.
  const restored = succeeded(argumentsFor(f, {executable, fresh: true}));
  restored[restored.indexOf('-A') + 1] = 'x64';
  const historical = cp.spawnSync(executable, restored,
    {encoding: 'utf8', windowsHide: true, timeout: 30000});
  assert.equal(historical.status, 0, historical.stderr || historical.stdout);
  assert.match(historical.stdout, /SOUI verified selected Windows SDK 10\.0\.19041\.0/);
  const original = cp.spawnSync(executable, ['-S', f.source, '-B', f.build, '-G', 'Visual Studio 17 2022',
    '-A', 'x64', '-DCMAKE_SYSTEM_VERSION=10.0.19041.0'],
  {encoding: 'utf8', windowsHide: true, timeout: 30000});
  assert.equal(original.status, 0, original.stderr || original.stdout);
  assert.equal(fs.readFileSync(path.join(f.build, 'selected.txt'), 'utf8'), '10.0.19041.0');
  const win32 = {...f, build: path.join(f.source, 'out/win32')};
  const x86 = cp.spawnSync(executable, succeeded(argumentsFor(win32, {executable, architecture: 'x86'})),
    {encoding: 'utf8', windowsHide: true, timeout: 30000});
  assert.equal(x86.status, 0, x86.stderr || x86.stdout);
  assert.equal(fs.readFileSync(path.join(win32.build, 'selected.txt'), 'utf8'), '10.0.19041.0');
  assert.match(fs.readFileSync(path.join(win32.build, 'SdkSelectionFixture.vcxproj'), 'utf8'),
    /<WindowsTargetPlatformVersion>10\.0\.19041\.0<\/WindowsTargetPlatformVersion>/);
});
