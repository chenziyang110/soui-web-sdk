'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const repository = path.resolve(__dirname, '../..');
test('actual PowerShell optional web blocks preserve default, SDK-only, generated, and SkipWeb branches', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-gui-hooks-'));
  t.after(() => {
    assert.ok(fs.realpathSync(root).startsWith(fs.realpathSync(os.tmpdir()) + path.sep));
    fs.rmSync(root, {recursive: true, force: true});
  });
  const driver = path.join(root, 'driver.ps1'), script = String.raw`
param([string]$BuildFile,[string]$FixtureRoot)
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($BuildFile,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Build parser failed'}
$blocks=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.IfStatementAst]},$true))
$web=@($blocks | Where-Object {$_.Extent.Text -match '^if\(-not \$SkipWeb\)'})
$verify=@($blocks | Where-Object {$_.Extent.Text -match '^if\(\$WithGuiBindings\)' -and $_.Extent.Text -match ' verify --root '})
if($web.Count -ne 1 -or $verify.Count -ne 2){throw 'Expected exact Build optional blocks'}
$root=$FixtureRoot;$output=Join-Path $root 'out/x64';New-Item -ItemType Directory -Force $output|Out-Null
$capture=Join-Path $root 'calls.jsonl'
$stub=Join-Path $root 'capture.ps1'
$stubSource='[PSCustomObject]@{argv=[string[]]$args} | ConvertTo-Json -Compress | Add-Content -LiteralPath $env:SOUI_GUI_HOOK_CAPTURE; $global:LASTEXITCODE=0'
[IO.File]::WriteAllText($stub,$stubSource)
$env:SOUI_GUI_HOOK_CAPTURE=$capture
$node=[PSCustomObject]@{Source=$stub};$npm=[PSCustomObject]@{Source=$stub}
$evidence='unused';$evidenceArgs=@('x64','Release')
$cases=@(
 @{name='default-sdk';sdk=$true;skip=$false;gui=$false;generated=$false;npm=2;build=0;verify=0},
 @{name='default-apps';sdk=$false;skip=$false;gui=$false;generated=$false;npm=8;build=0;verify=0},
 @{name='optional-sdk';sdk=$true;skip=$false;gui=$true;generated=$false;npm=0;build=1;verify=2},
 @{name='optional-apps';sdk=$false;skip=$false;gui=$true;generated=$false;npm=6;build=1;verify=2},
 @{name='optional-generated';sdk=$false;skip=$false;gui=$true;generated=$true;npm=2;build=1;verify=2},
 @{name='skip-default';sdk=$false;skip=$true;gui=$false;generated=$false;npm=0;build=0;verify=0},
 @{name='skip-optional';sdk=$true;skip=$true;gui=$true;generated=$false;npm=0;build=0;verify=2}
)
$results=@()
foreach($case in $cases){
 if(Test-Path -LiteralPath $capture){Remove-Item -LiteralPath $capture}
 $SdkOnly=$case.sdk;$SkipWeb=$case.skip;$WithGuiBindings=$case.gui;$generated=$case.generated
 & ([ScriptBlock]::Create($web[0].Extent.Text)) | Out-Null
 foreach($block in $verify){& ([ScriptBlock]::Create($block.Extent.Text)) | Out-Null}
 $calls=@();if(Test-Path -LiteralPath $capture){$calls=@(Get-Content -LiteralPath $capture | ForEach-Object {$_|ConvertFrom-Json})}
 $npmCalls=@($calls | Where-Object {$_.argv[0] -eq '--prefix'}).Count
 $buildCalls=@($calls | Where-Object {$_.argv[0] -like '*gui-bindings-delivery.cjs' -and $_.argv[1] -eq 'build'}).Count
 $verifyCalls=@($calls | Where-Object {$_.argv[0] -like '*gui-bindings-delivery.cjs' -and $_.argv[1] -eq 'verify'}).Count
 if($npmCalls -ne $case.npm -or $buildCalls -ne $case.build -or $verifyCalls -ne $case.verify){throw ('Hook branch mismatch: '+$case.name+' '+$npmCalls+'/'+$buildCalls+'/'+$verifyCalls+' '+(ConvertTo-Json -InputObject $calls -Compress -Depth 10))}
 if(-not $case.gui -and @($calls | Where-Object {($_.argv -join ' ') -match 'bindings|react|vue'}).Count){throw 'Default branch acquired framework work'}
 $results+=@{name=$case.name;calls=$calls;passed=$true}
}
$results|ConvertTo-Json -Depth 10
`;
  fs.writeFileSync(driver, script);
  const shell = process.env.SOUI_GUI_TEST_POWERSHELL || (process.platform === 'win32' ? 'powershell.exe' : 'pwsh');
  const result = cp.spawnSync(shell, ['-NoProfile', '-File', driver, '-BuildFile', path.join(repository, 'tools/Build.ps1'), '-FixtureRoot', root],
    {encoding: 'utf8', windowsHide: true, timeout: 30000});
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stderr}\n${result.stdout}`);
  const cases = JSON.parse(result.stdout);
  assert.equal(cases.length, 7);
  assert.equal(cases.every(entry => entry.passed), true);
});
test('standard tests and collection make framework installation explicitly optional', () => {
  const ps = fs.readFileSync(path.join(repository, 'tools/Test.ps1'), 'utf8');
  assert.match(ps, /if\(\$WithGuiBindings\)\{\s*& node .*gui-bindings-delivery\.cjs'\) test --root \$root/);
  assert.match(ps, /\$NativeOnly -and \$WithGuiBindings/);
  const collect = fs.readFileSync(path.join(repository, 'tools/Collect-Verification.cjs'), 'utf8');
  const optional = collect.slice(collect.indexOf('if(withGuiBindings){'), collect.indexOf("if(scope==='--foundation')"));
  assert.match(optional, /gui-bindings-delivery\.cjs','test','--root',root/);
  assert.match(collect, /if\(withGuiBindings\)requirePassed\(Boolean\(currentPackage\.optionalGuiBindings\)/);
});

// Web/native evidence boundary regressions execute the actual Build AST calls.
function webEvidenceFixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-web-evidence-'));
  t.after(() => {
    const actual = fs.realpathSync(root), parent = fs.realpathSync(os.tmpdir());
    assert.ok(actual.startsWith(parent + path.sep));
    fs.rmSync(root, {recursive: true, force: true});
  });
  return root;
}
function writeWebFixture(root, relative, text) {
  const file = path.join(root, relative);
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, text);
  return file;
}

test('actual Build evidence calls separate Web arguments from both native profiles', {timeout: 45000}, t => {
  const root = webEvidenceFixture(t);
  writeWebFixture(root, 'cmake/toolchains/SouiLLVM22.cmake', '# fixture identity only; never loaded\n');
  const driver = writeWebFixture(root, 'capture-build-evidence.ps1', String.raw`
param([string]$BuildFile,[string]$FixtureRoot)
$ErrorActionPreference='Stop'
# The extracted AST still invokes the real hash command. Establish its module
# in this driver rather than relying on autoload inside a transient stub scope.
Import-Module Microsoft.PowerShell.Utility -Global -Force -ErrorAction Stop
Get-Command Get-FileHash -ErrorAction Stop | Out-Null
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($BuildFile,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Build parser failed'}
$tries=@($ast.EndBlock.Statements | Where-Object {$_ -is [Management.Automation.Language.TryStatementAst]})
if($tries.Count -ne 1){throw 'Expected the actual Build main try block'}
$statements=@($tries[0].Body.Statements)
$setup=@($statements | Where-Object {$_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq '$evidence'})
function EvidenceCall([string]$Mode){
  $found=@($statements | Where-Object {
    $_ -is [Management.Automation.Language.PipelineAst] -and $_.PipelineElements.Count -eq 1 -and
    $_.PipelineElements[0] -is [Management.Automation.Language.CommandAst] -and
    $_.PipelineElements[0].CommandElements.Count -ge 3 -and
    $_.PipelineElements[0].CommandElements[0].Extent.Text -eq '$node.Source' -and
    $_.PipelineElements[0].CommandElements[1].Extent.Text -eq '$evidence' -and
    (($Mode -eq 'web' -and $_.PipelineElements[0].CommandElements[2].Extent.Text -like '*record-web*') -or
     $_.PipelineElements[0].CommandElements[2].Extent.Text -eq $Mode)
  })
  if($found.Count -ne 1){throw ('Expected one actual evidence call: '+$Mode)}
  return $found[0]
}
$start=EvidenceCall 'start';$web=EvidenceCall 'web';$finish=EvidenceCall 'finish'
if($setup.Count -ne 1 -or $setup[0].Extent.StartOffset -ge $start.Extent.StartOffset){throw 'Expected evidence setup before start'}
$text=[IO.File]::ReadAllText($BuildFile)
$setupText=$text.Substring($setup[0].Extent.StartOffset,$start.Extent.EndOffset-$setup[0].Extent.StartOffset)
$root=$FixtureRoot;$capture=Join-Path $root 'calls.jsonl'
$stub=Join-Path $root 'capture.ps1'
[IO.File]::WriteAllText($stub,'[PSCustomObject]@{argv=[string[]]$args} | ConvertTo-Json -Compress | Add-Content -LiteralPath $env:SOUI_WEB_EVIDENCE_CAPTURE; $global:LASTEXITCODE=0')
$env:SOUI_WEB_EVIDENCE_CAPTURE=$capture
$node=[PSCustomObject]@{Source=$stub}
$results=@()
foreach($profile in @('MSVC143','LLVM22MSVC142')){
 foreach($Architecture in @('x86','x64')){
  foreach($Configuration in @('Debug','Release')){
   foreach($SdkOnly in @($false,$true)){
    foreach($SkipWeb in @($false,$true)){
     if(Test-Path -LiteralPath $capture){Remove-Item -LiteralPath $capture}
     $NativeToolchain=$profile
     $WebProfile=if(-not $SdkOnly -and $Architecture -eq 'x64'){'source-diagnostic'}else{'distribution'}
     # This discovery is a request fixture, not observed compiler evidence.
     $llvm=if($profile -eq 'LLVM22MSVC142'){@{FixtureDiscoveryOnly=$true}}else{$null}
     $output=Join-Path $root ('out/'+$(if($SdkOnly){'sdk-'}else{''})+$Architecture)
     New-Item -ItemType Directory -Force -Path $output | Out-Null
     . ([ScriptBlock]::Create($setupText)) | Out-Null
     . ([ScriptBlock]::Create($web.Extent.Text)) | Out-Null
     . ([ScriptBlock]::Create($finish.Extent.Text)) | Out-Null
     $calls=@(Get-Content -LiteralPath $capture | ForEach-Object {$_|ConvertFrom-Json})
     if($calls.Count -ne 3){throw 'Expected start/Web/finish calls'}
     $results+=@{profile=$profile;webProfile=$WebProfile;architecture=$Architecture;configuration=$Configuration;sdkOnly=$SdkOnly;skipWeb=$SkipWeb;calls=$calls}
    }
   }
  }
 }
}
ConvertTo-Json -InputObject $results -Depth 12 -Compress
`);
  const shell = process.env.SOUI_GUI_TEST_POWERSHELL || (process.platform === 'win32' ? 'powershell.exe' : 'pwsh');
  const result = cp.spawnSync(shell, ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', driver,
    '-BuildFile', path.join(repository, 'tools/Build.ps1'), '-FixtureRoot', root],
  {encoding: 'utf8', windowsHide: true, timeout: 40000});
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stderr}\n${result.stdout}`);
  const rows = JSON.parse(result.stdout);
  assert.equal(rows.length, 32);
  for (const row of rows) {
    const base = [row.architecture, row.configuration, ...(row.sdkOnly ? ['sdk-only'] : []), '--web-profile', row.webProfile];
    const native = row.profile === 'LLVM22MSVC142' ? [...base, '--native-toolchain', row.profile, '--profile-request',
      path.join(root, 'out', `${row.sdkOnly ? 'sdk-' : ''}${row.architecture}`, `llvm-profile-request-${row.configuration}.json`)] : base;
    assert.deepEqual(row.calls[0].argv, [path.join(root, 'tools/internal/build-evidence.cjs'), 'start', ...native]);
    assert.deepEqual(row.calls[1].argv, [path.join(root, 'tools/internal/build-evidence.cjs'), row.skipWeb ? 'verify-web' : 'record-web', ...base]);
    assert.deepEqual(row.calls[2].argv, [path.join(root, 'tools/internal/build-evidence.cjs'), 'finish', ...native]);
  }
});

for (const sdkOnly of [true, false]) test(`real Web CLI ${sdkOnly ? 'SDK' : 'applications'} keeps profile rejection and source/output gates`, t => {
  const root = webEvidenceFixture(t);
  for (const name of ['build-evidence.cjs', 'delivery-identity.cjs', 'sdk-version.cjs', 'web-build-profile.cjs']) {
    writeWebFixture(root, `tools/internal/${name}`, fs.readFileSync(path.join(repository, 'tools/internal', name)));
  }
  writeWebFixture(root, 'VERSION', '0.5.0-preview.1\n');
  const source = writeWebFixture(root, 'sdk/include/soui/version.h', '#define SOUI_SDK_VERSION_STRING "0.5.0-preview.1"\n');
  writeWebFixture(root, 'sdk/web/package.json', JSON.stringify({version: '0.5.0-preview.1'}));
  writeWebFixture(root, 'sdk/web/package-lock.json', JSON.stringify({version: '0.5.0-preview.1', packages: {'': {version: '0.5.0-preview.1'}}}));
  const artifact = writeWebFixture(root, 'sdk/web/dist/client.js', 'fixture output one\n');
  if (!sdkOnly) for (const name of ['templates/desktop-app', 'templates/modern-app', 'demos/workbench']) {
    writeWebFixture(root, `${name}/web/dist/index.html`, `<p>${name}</p>\n`);
  }
  const reader = path.join(root, 'tools/internal/build-evidence.cjs');
  const args = ['x64', 'Release', ...(sdkOnly ? ['sdk-only'] : [])];
  const run = (mode, extra = []) => cp.spawnSync(process.execPath, [reader, mode, ...args, ...extra],
    {encoding: 'utf8', windowsHide: true, timeout: 10000});
  const success = mode => { const result = run(mode); assert.equal(result.status, 0, `${result.stderr}\n${result.stdout}`); return JSON.parse(result.stdout); };
  success('start');
  assert.equal(success('record-web').webVerified, true);
  assert.equal(success('verify-web').webVerified, true);
  const record = path.join(root, `out/web-${sdkOnly ? 'sdk' : 'applications'}.json`), before = fs.readFileSync(record);
  for (const mode of ['record-web', 'verify-web']) {
    const rejected = run(mode, ['--native-toolchain', 'LLVM22MSVC142', '--profile-request', path.join(root, 'synthetic-request.json')]);
    assert.notEqual(rejected.status, 0);
    assert.match(rejected.stderr, /Web evidence does not select a native profile/);
    assert.deepEqual(fs.readFileSync(record), before);
  }
  fs.writeFileSync(artifact, 'fixture output two\n');
  const staleOutput = run('verify-web');
  assert.notEqual(staleOutput.status, 0); assert.match(staleOutput.stderr, /SkipWeb requires matching Web source and output evidence/);
  assert.equal(success('record-web').webVerified, true);
  assert.equal(success('verify-web').webVerified, true);
  fs.appendFileSync(source, '// changed source\n');
  const staleSource = run('verify-web');
  assert.notEqual(staleSource.status, 0); assert.match(staleSource.stderr, /SkipWeb requires matching Web source and output evidence/);
  const changedDuringBuild = run('record-web');
  assert.notEqual(changedDuringBuild.status, 0); assert.match(changedDuringBuild.stderr, /Source changed during Web build/);
  const pending = JSON.parse(fs.readFileSync(path.join(root, `out/${sdkOnly ? 'sdk-' : ''}x64/build-Release.json`)));
  assert.equal(pending.nativeBuild, 'pending');
  assert.equal(pending.releaseReady, false);
});
