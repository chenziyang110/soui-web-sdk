'use strict';
// Executes the actual PowerShell lifecycle with inert Process/file-pin doubles.
// No compiler, VCTIP, native supervisor, product, or process termination runs.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {spawnSync}=require('node:child_process');

test('MSVC lifecycle preserves native errors, rejects foreign services, and requires bounded natural retirement',()=>{
  const repository=path.resolve(__dirname,'../..');
  const folder=fs.mkdtempSync(path.join(os.tmpdir(),'soui-msvc-lifecycle-'));
  const driver=path.join(folder,'lifecycle.ps1');
  fs.writeFileSync(driver,String.raw`
param([string]$Repository,[string]$Evidence)
$ErrorActionPreference='Stop'
. (Join-Path $Repository 'tools/internal/CMake-Configure.ps1')
. (Join-Path $Repository 'tools/internal/Msvc-CompilerLifecycle.ps1')
$script:launches=0;$script:busy=$false;$script:mode='normal';$script:waited=@();$script:streams=@()
function Get-Process {param($Name,$ErrorAction);if($script:busy-or$script:mode-eq'replaced'){[pscustomobject]@{Id=999;ProcessName='vctip'}}}
function Need([bool]$Value,[string]$Message){if(-not$Value){throw $Message}}
function Must-Fail([scriptblock]$Action,[string]$Pattern){
  $caught=$null;try{& $Action}catch{$caught=$_}
  Need ($null-ne$caught-and$caught.Exception.Message-match$Pattern) ('Expected failure: '+$Pattern)
  return $caught
}
# Exercise the actual global-service/event guard, without modifying any
# existing resource. An event created here has a fresh fixture-only name.
$readyName='Local\SouiFixture.'+[Guid]::NewGuid().ToString()
$script:busy=$true
$null=Must-Fail {Assert-SouiCompilerPhaseIdle $readyName} 'preexisting compiler'
$script:busy=$false
$event=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$readyName)
try{$null=Must-Fail {Assert-SouiCompilerPhaseIdle $readyName} 'preexisting VCTIP ready event'}finally{$event.Dispose()}
# Wrong bytes must fail before launch, even at an otherwise valid tool path.
$pinFile=Join-Path $Evidence 'inert.bin';[IO.File]::WriteAllText($pinFile,'inert fixture')
$null=Must-Fail {Open-SouiCompilerPhaseFile $pinFile ('0'*64)} 'reviewed tool'
Need ($script:launches-eq0) 'A rejected input launched a process'

function Open-SouiCompilerPhaseFile([string]$Path,[string]$ExpectedSha256){
  $stream=[IO.MemoryStream]::new();$script:streams+=,$stream
  [pscustomobject]@{stream=$stream;path=$Path;bytes=1;sha256=$(if($ExpectedSha256){$ExpectedSha256}else{'a'*64})}
}
function Assert-SouiCompilerPhaseIdle([string]$ReadyName){if($script:mode-eq'busy'){throw 'preexisting compiler fixture'}}
function Wait-SouiCompilerPhaseReady($Process,[string]$ReadyName){if($script:mode-eq'readiness'){throw 'fixture readiness failed'}}
function Start-Process {
  param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle,[switch]$PassThru,$RedirectStandardOutput,$RedirectStandardError)
  Need (($ArgumentList-join' ')-ceq'-timeout:20') 'Wrong server CLI'
  Need ($WindowStyle-ceq'Hidden'-and$PassThru) 'Wrong server launch mode'
  $script:launches++
  $process=[pscustomobject]@{Id=(100+$script:launches);Handle=[IntPtr]::Zero;StartTime=[DateTime]::UtcNow;
    HasExited=$false;ExitCode=$(if($script:mode-eq'nonzero'){7}else{0});disposed=$false}
  $process|Add-Member ScriptMethod WaitForExit {param([int]$Milliseconds)
    $script:waited+=$Milliseconds
    if($script:mode-eq'timeout'){return $false};$this.HasExited=$true;return $true
  }
  $process|Add-Member ScriptMethod Dispose {$this.disposed=$true}
  return $process
}
$toolchain=[pscustomobject]@{MsvcRoot=(Join-Path $Evidence 'VS/VC/Tools/MSVC/14.44.35207');
  MsvcVersion='14.44.35207';HostArchitecture='x64';Toolset='v143'}
$results=@()
foreach($arch in @('x86','x64')){
  $script:mode='normal'
  $state=Start-SouiMsvcCompilerPhase -Toolchain $toolchain -Architecture $arch -EvidenceRoot $Evidence -Phase configure
  Complete-SouiMsvcCompilerPhase $state 0 'native fixture failed'
  Need ($state.closed-and$state.process.disposed) 'Owned handles were not disposed'
  $record=Get-Content -Raw -LiteralPath (Join-Path $state.directory 'returned.json')|ConvertFrom-Json
  Need ($record.serverNaturallyExited-and$record.nativeExitCode-eq0) 'Natural return metadata differs'
  Need ($record.originalOwnedJobClosureRequired-and-not$record.readinessIsOwnershipProof-and-not$record.compilerHelperSuppressionConfigured) 'Lifecycle metadata granted false proof'
  Need ($record.compiler.path-match('Hostx64[\\/]'+$arch+'[\\/]cl.exe$')) 'Compiler target path differs'
  Need ($record.image.path-match('Hostx64[\\/]'+$arch+'[\\/]vctip.exe$')) 'Helper target path differs'
  Close-SouiMsvcCompilerPhase $state # idempotent; no second wait/return file
  $results+=@{case=('natural-'+$arch);passed=$true}
}
foreach($scenario in @('busy','readiness','nonzero','timeout','replaced','native-error','native-and-timeout')){
  $script:mode=if($scenario-eq'native-and-timeout'){'timeout'}elseif($scenario-eq'native-error'){'normal'}else{$scenario}
  $state=$null;$before=$script:launches
  $caught=$null
  try{
    $state=Start-SouiMsvcCompilerPhase -Toolchain $toolchain -Architecture x64 -EvidenceRoot $Evidence -Phase build
    $nativeExit=if($scenario-like'native-*'){23}else{0}
    Complete-SouiMsvcCompilerPhase $state $nativeExit 'original compiler failure'
  }catch{$caught=$_}
  Need ($null-ne$caught) ('Failure was accepted: '+$scenario)
  if($scenario-eq'busy'){Need ($script:launches-eq$before) 'Foreign-service guard launched a helper'}
  if($scenario-like'native-*'){
    Need ($caught.Exception.Message-ceq'original compiler failure'-and$caught.Exception.Data['nativeExitCode']-eq23) 'Original compiler error was replaced'
    if($scenario-eq'native-and-timeout'){Need ($caught.Exception.Data['compilerLifecycleFailure']-match'did not retire') 'Secondary failure is missing'}
  }
  if($null-ne$state){
    Need ($state.closed-and$state.process.disposed) 'Failure leaked held handles'
    $record=Get-Content -Raw -LiteralPath (Join-Path $state.directory 'returned.json')|ConvertFrom-Json
    if($scenario-in@('timeout','nonzero','replaced','native-and-timeout')){Need (-not$record.serverNaturallyExited) 'Failed retirement reported as natural'}
  }
  $results+=@{case=$scenario;passed=$true}
}
Need (@($script:waited|Where-Object{$_-ne30000}).Count-eq0) 'Natural wait was not fixed and bounded'
Need (@($script:streams|Where-Object{$_.CanRead}).Count-eq0) 'File pin was not disposed'
$results|ConvertTo-Json -Depth 5 -Compress
`);
  const env={...process.env};for(const key of Object.keys(env))if(key.toLowerCase()==='psmodulepath')delete env[key];
  const shell=process.env.SOUI_GUI_TEST_POWERSHELL||(process.platform==='win32'?'powershell.exe':'pwsh');
  const actual=spawnSync(shell,['-NoProfile','-NonInteractive','-File',driver,'-Repository',repository,'-Evidence',folder],
    {encoding:'utf8',windowsHide:true,timeout:30000,env});
  assert.equal(actual.status,0,`${actual.error||''}\n${actual.stderr}\n${actual.stdout}`);
  // PowerShell warning records are intentionally separate from the JSON payload.
  const line=actual.stdout.trim().split(/\r?\n/).findLast(row=>row.startsWith('[{'));
  const cases=JSON.parse(line);assert.equal(cases.length,9);assert.equal(cases.every(row=>row.passed),true);
});
