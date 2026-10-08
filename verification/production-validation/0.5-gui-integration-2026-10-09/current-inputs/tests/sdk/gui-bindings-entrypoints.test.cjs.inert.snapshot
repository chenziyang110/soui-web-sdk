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
