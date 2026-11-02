[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('stage','run')][string]$Mode,
 [Parameter(Mandatory=$true)][string]$Manifest,[Parameter(Mandatory=$true)][string]$ManifestSha256,
 [Parameter(Mandatory=$true)][string]$ExpectedMachine,[Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [string]$RootGate,[string]$StageFile,[string]$StageSha256)
$ErrorActionPreference='Stop'
if($ManifestSha256-notmatch'^[a-f0-9]{64}$'-or(Get-FileHash -LiteralPath $Manifest -Algorithm SHA256).Hash.ToLowerInvariant()-cne$ManifestSha256){throw 'Frozen current manifest required'}
$plan=[IO.File]::ReadAllText($Manifest,[Text.Encoding]::UTF8)|ConvertFrom-Json
if($env:COMPUTERNAME-ieq$plan.preparationHost-or$env:COMPUTERNAME-ine$ExpectedMachine){throw 'Task VM identity required; host execution forbidden'}
if($EvidenceDirectory-notmatch'^C:\\issue-closeout-evidence\\mp77c-[a-z0-9-]+$'-or(Test-Path -LiteralPath $EvidenceDirectory)){throw 'Fresh current evidence directory required'}
if($Mode-eq'run'-and($RootGate-cne'ROOT_REVIEWED_CURRENT_MESSAGE_PORT_STAGE_V1'-or$StageSha256-notmatch'^[a-f0-9]{64}$')){throw 'Root stage review required'}
$saved=@{};$names=@('PATH','NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER')
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation');$acquired=$false;$exitCode=1
try{
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true;throw 'Abandoned desktop lease'}
  if(-not$acquired){throw 'Another desktop operator owns validation'}
  New-Item -ItemType Directory -Path $EvidenceDirectory|Out-Null
  $process=[Diagnostics.Process]::GetCurrentProcess()
  [IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'Lease.actual.json'),(@{name='Local\SouiDevKit.DesktopValidation';acquired=$true;ownerProcessId=$PID;ownerCreationFileTime=$process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
  foreach($name in $names){$entry=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;$saved[$name]=@{present=($null-ne$entry);value=if($null-ne$entry){[string]$entry.Value}else{$null}};if($name-ne'PATH'){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}}
  $root=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Manifest))
  if($Mode-eq'run'){$env:PATH=(Join-Path $root 'runtime-x64')+';'+$saved.PATH.value}
  [IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'Environment.before-and-active.json'),(@{pathBefore=$saved.PATH;activePath=$env:PATH;cleared=$names|Where-Object{$_-ne'PATH'};mode=$Mode;version='0.6.0'}|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
  $arguments=@((Join-Path $root 'Guest-Current.cjs'),$Mode,$Manifest,$ManifestSha256,$ExpectedMachine,$EvidenceDirectory)
  if($Mode-eq'run'){$arguments+=@($RootGate,$StageFile,$StageSha256)}
  [IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'Invocation.actual.json'),(@{executable=$plan.tools.node;args=$arguments;mode=$Mode;version='0.6.0';oldFourArms=$false}|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
  $previousPreference=$ErrorActionPreference
  $nativeExitCode=$null
  try{
    # Windows PowerShell 5 wraps native stderr. Preserve the actual native exit
    # even when the controller correctly writes a diagnostic to stderr.
    $ErrorActionPreference='Continue';$global:LASTEXITCODE=$null
    & $plan.tools.node.path @arguments 1>(Join-Path $EvidenceDirectory 'stdout.log') 2>(Join-Path $EvidenceDirectory 'stderr.log')
    $nativeExitCode=$LASTEXITCODE
  }finally{$ErrorActionPreference=$previousPreference}
  if($null-eq$nativeExitCode){throw 'No actual native controller exit captured'}
  $exitCode=$nativeExitCode
}finally{
  $errors=[Collections.Generic.List[string]]::new()
  foreach($name in $saved.Keys){try{if($saved[$name].present){Set-Item -LiteralPath ('Env:'+$name) -Value $saved[$name].value}else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue};$actual=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(($null-ne$actual)-ne$saved[$name].present-or($saved[$name].present-and[string]$actual.Value-cne$saved[$name].value)){throw 'Environment restore mismatch'}}catch{$errors.Add($name)}}
  try{if($acquired){$lease.ReleaseMutex()}}finally{$lease.Dispose()}
  if($errors.Count){$exitCode=1}
  if(Test-Path -LiteralPath $EvidenceDirectory){[IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'PowerShell.finished.json'),(@{exitCode=$exitCode;leaseReleased=$acquired;environmentRestored=($errors.Count-eq0);restoreErrors=@($errors.ToArray());pathAfter=$env:PATH;oldFourArmsPassed=$false;releaseReady=$false}|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))}
}
exit $exitCode
