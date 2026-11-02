[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('preflight','run')][string]$Mode,
  [Parameter(Mandatory=$true)][string]$Manifest,
  [Parameter(Mandatory=$true)][string]$ManifestSha256,
  [Parameter(Mandatory=$true)][string]$ExpectedMachine,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
  [string]$PreflightFile,
  [string]$PreflightSha256,
  [string]$RootGate
)
$ErrorActionPreference='Stop'
if($ManifestSha256-notmatch'^[a-f0-9]{64}$'){throw 'Exact frozen manifest digest required'}
if((Get-FileHash -LiteralPath $Manifest -Algorithm SHA256).Hash.ToLowerInvariant()-cne$ManifestSha256){throw 'Manifest digest differs'}
$document=[IO.File]::ReadAllText($Manifest,[Text.Encoding]::UTF8)|ConvertFrom-Json
if($env:COMPUTERNAME-ieq$document.preparationHost-or$env:COMPUTERNAME-ine$ExpectedMachine){throw 'Task VM identity required; host execution is forbidden'}
$folder=[IO.Path]::GetFullPath($EvidenceDirectory)
if($folder-notmatch'^C:\\issue-closeout-evidence\\mp77-[a-z0-9-]+$'-or(Test-Path -LiteralPath $folder)){throw 'Fresh task evidence directory required'}
$cfg=[IO.File]::ReadAllText($document.executionInput.path,[Text.Encoding]::UTF8)|ConvertFrom-Json
$node=[string]$cfg.requiredExecutionNode.path
if((Get-FileHash -LiteralPath $node -Algorithm SHA256).Hash.ToLowerInvariant()-cne$cfg.requiredExecutionNode.sha256){throw 'Actual pinned Node differs'}
if($Mode-eq'run'-and($RootGate-cne'ROOT_REVIEWED_GUEST_PREFLIGHT_V1'-or$PreflightSha256-notmatch'^[a-f0-9]{64}$')){throw 'Independent Root preflight review is required'}
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false;$exitCode=1;$saved=@{}
$names=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER')
try{
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true;throw 'Abandoned desktop lease'}
  if(-not$acquired){throw 'Another task owns the desktop validation lease'}
  New-Item -ItemType Directory -Path $folder|Out-Null
  $process=[Diagnostics.Process]::GetCurrentProcess()
  $leaseReceipt=[ordered]@{name='Local\SouiDevKit.DesktopValidation';acquired=$true;ownerProcessId=$PID;ownerCreationFileTime=$process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString();mode=$Mode}
  [IO.File]::WriteAllText((Join-Path $folder 'Lease.actual.json'),($leaseReceipt|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
  foreach($name in $names){$entry=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;$saved[$name]=@{present=($null-ne$entry);value=if($null-ne$entry){[string]$entry.Value}else{$null}};Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}
  $env:SOUI_OWNED_PROCESS_RUNNER=[string]$cfg.subjectRunner.path
  $arguments=@((Join-Path $PSScriptRoot 'Guest-MessagePort.cjs'),$Mode,$Manifest,$ManifestSha256,$ExpectedMachine,$folder)
  if($Mode-eq'run'){$arguments+=@($RootGate,$PreflightFile,$PreflightSha256)}
  [IO.File]::WriteAllText((Join-Path $folder 'Invocation.actual.json'),([ordered]@{node=$node;args=$arguments;clearedEnvironment=$names;machine=$env:COMPUTERNAME;mode=$Mode;productHostExecutionAuthorized=$false}|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
  & $node @arguments 1>(Join-Path $folder 'controller.stdout.log') 2>(Join-Path $folder 'controller.stderr.log')
  $exitCode=$LASTEXITCODE
}finally{
  $restoreErrors=[Collections.Generic.List[string]]::new()
  foreach($name in $saved.Keys){try{if($saved[$name].present){Set-Item -LiteralPath ('Env:'+$name) -Value $saved[$name].value}else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue};$now=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(($null-ne$now)-ne$saved[$name].present-or($saved[$name].present-and[string]$now.Value-cne$saved[$name].value)){throw 'Restore mismatch'}}catch{$restoreErrors.Add($name)}}
  try{if($acquired){$lease.ReleaseMutex()}}finally{$lease.Dispose()}
  if($restoreErrors.Count){$exitCode=1}
  if(Test-Path -LiteralPath $folder){[IO.File]::WriteAllText((Join-Path $folder 'PowerShell.finished.json'),([ordered]@{exitCode=$exitCode;leaseReleased=$acquired;environmentRestored=($restoreErrors.Count-eq0);restoreErrors=$restoreErrors;sourceVersion='0.5.1-preview.1';currentSdk060Qualified=$false;releaseReady=$false}|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))}
}
exit $exitCode
