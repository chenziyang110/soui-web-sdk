[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$PayloadDirectory,
  [Parameter(Mandatory=$true)][ValidatePattern('^(tray|resident)-(x64|x86)(,(tray|resident)-(x64|x86))*$')][string]$Cases,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$ManifestSha256,
  [Parameter(Mandatory=$true)][string]$ExpectedMachine
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$utf8=[Text.UTF8Encoding]::new($false)
function Write-NewJson([string]$Path,$Value){
  $raw=$utf8.GetBytes(($Value|ConvertTo-Json -Depth 16)+[Environment]::NewLine)
  $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($raw,0,$raw.Length)}finally{$stream.Dispose()}
}
function Pin([string]$Path){
  $entry=Get-Item -LiteralPath $Path
  if($entry.PSIsContainer-or($entry.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain file required'}
  [ordered]@{path=$entry.FullName;bytes=$entry.Length;sha256=(Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function EnvState([string]$Name){
  $value=[Environment]::GetEnvironmentVariable($Name,'Process')
  $present=$null-ne$value
  $sha=[Security.Cryptography.SHA256]::Create()
  try{[ordered]@{name=$Name;present=$present;utf8Bytes=if($present){$utf8.GetByteCount($value)}else{0};sha256=if($present){([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($value)))).Replace('-','').ToLowerInvariant()}else{$null}}}finally{$sha.Dispose()}
}
$payload=[IO.Path]::GetFullPath($PayloadDirectory).TrimEnd('\','/')
$folder=[IO.Path]::GetFullPath($EvidenceDirectory).TrimEnd('\','/')
$launcherFolder=$folder+'-launcher'
if([IntPtr]::Size-ne8-or$env:OS-ne'Windows_NT'){throw 'Use a 64-bit Windows PowerShell process in the task VM'}
if($payload-notmatch'^C:\\issue[a-z0-9-]{1,40}$'-or$folder-notmatch'^C:\\issue-closeout-evidence\\[a-z0-9-]{1,80}$'){throw 'Task-local short payload and fresh evidence roots required'}
if((Test-Path -LiteralPath $folder)-or(Test-Path -LiteralPath $launcherFolder)){throw 'Refuse reused evidence or launcher output'}
$manifestFile=Join-Path $payload 'Payload.manifest.json'
$manifestPin=Pin $manifestFile
if($manifestPin.sha256-cne$ManifestSha256){throw 'Exact manifest digest differs'}
$manifest=[IO.File]::ReadAllText($manifestFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
if($manifest.schema-cne'soui.current-public-tray-vm-payload-manifest/v1'-or$manifest.revision-ne2){throw 'Reviewed r2 manifest required'}
if($env:COMPUTERNAME-ieq$manifest.preparationHost-or$env:COMPUTERNAME-ine$ExpectedMachine){throw 'Task VM identity required; host product execution forbidden'}
$policy=$manifest.launcher
if($policy.desktopMutex-cne'Local\SouiDevKit.DesktopValidation'-or$policy.nodeRelative-cne'tools/node.exe'-or$policy.crtPrefixRelative-cne'tray-x64'){throw 'Launcher policy changed'}
$selfPin=Pin $PSCommandPath
if($selfPin.bytes-ne$policy.script.bytes-or$selfPin.sha256-cne$policy.script.sha256){throw 'Launcher script differs from reviewed manifest'}
$selected=@($Cases.Split(','))
if(@($selected|Select-Object -Unique).Count-ne$selected.Count){throw 'Duplicate cases'}
foreach($caseName in $selected){if(@($manifest.cases|Where-Object {$_.name-ceq$caseName}).Count-ne1){throw 'Case absent from reviewed manifest'}}
$node=Join-Path $payload 'tools/node.exe'
$controller=Join-Path $payload 'Guest-Shorts.cjs'
foreach($relative in @('tools/node.exe','Guest-Shorts.cjs')){
  $expected=@($manifest.files|Where-Object {$_.path-ceq$relative})
  if($expected.Count-ne1){throw 'Missing exact launch file pin'}
  $actual=Pin (Join-Path $payload $relative)
  if($actual.bytes-ne$expected[0].bytes-or$actual.sha256-cne$expected[0].sha256){throw 'Actual launch file differs'}
}
$crtDirectory=Join-Path $payload 'tray-x64'
$crtRows=@($manifest.files|Where-Object {$_.path.StartsWith('tray-x64/',[StringComparison]::Ordinal)})
$crtNames=@($crtRows|ForEach-Object {$_.path.Substring(9)}|Sort-Object)
$actualCrtNames=@(Get-ChildItem -LiteralPath $crtDirectory|ForEach-Object {if($_.PSIsContainer-or($_.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Linked or nested CRT prefix rejected'};$_.Name}|Sort-Object)
if(($crtNames-join"`n")-cne($actualCrtNames-join"`n")){throw 'CRT prefix closed file set differs'}
$crtPins=@(foreach($row in $crtRows){$actual=Pin (Join-Path $payload $row.path);if($actual.bytes-ne$row.bytes-or$actual.sha256-cne$row.sha256){throw 'CRT prefix pin differs'};$actual})
$names=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER')
if(($names-join"`n")-cne(@($policy.clearedEnvironment)-join"`n")){throw 'Cleared environment policy changed'}
$saved=@{};$before=@();$during=@();$after=@();$restoreErrors=[Collections.Generic.List[string]]::new()
$acquired=$false;$released=$false;$exitCode=1;$nativeExitCode=$null;$failure=$null
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
try{
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true;throw 'Abandoned desktop lease; refuse product execution'}
  if(-not$acquired){throw 'Desktop validation lease already held'}
  New-Item -ItemType Directory -Path $launcherFolder|Out-Null
  $owner=[Diagnostics.Process]::GetCurrentProcess()
  Write-NewJson (Join-Path $launcherFolder 'Lease.actual.json') ([ordered]@{name='Local\SouiDevKit.DesktopValidation';acquired=$true;ownerProcessId=$PID;ownerCreationFileTime=$owner.StartTime.ToUniversalTime().ToFileTimeUtc().ToString();abandoned=$false})
  foreach($name in @($names)+@('PATH')){$value=[Environment]::GetEnvironmentVariable($name,'Process');$saved[$name]=@{present=($null-ne$value);value=$value};$before+=@(EnvState $name)}
  foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
  $oldPath=$saved['PATH'].value
  [Environment]::SetEnvironmentVariable('PATH',($crtDirectory+$(if([string]::IsNullOrEmpty($oldPath)){''}else{';'+$oldPath})),'Process')
  foreach($name in @($names)+@('PATH')){$during+=@(EnvState $name)}
  $arguments=@($controller,'--cases',$Cases,'--output',$folder,'--manifest-sha256',$ManifestSha256)
  Write-NewJson (Join-Path $launcherFolder 'Invocation.actual.json') ([ordered]@{node=(Pin $node);args=$arguments;cwd=$payload;manifest=$manifestPin;launcher=$selfPin;selected=$selected;machine=$env:COMPUTERNAME;crtPrefix=$crtDirectory;crtPins=$crtPins;environmentBefore=$before;environmentDuring=$during;environmentValuesPersisted=$false;nativeOutputCapture='PowerShell textual redirection; actual child Owned/stdout.bin/stderr.bin remain controller-owned';productHostExecutionAuthorized=$false})
  $oldLocation=Get-Location
  $oldPreference=$ErrorActionPreference
  try{
    Set-Location -LiteralPath $payload
    # PS5 wraps native stderr as NativeCommandError. Do not convert a stderr
    # message into a fake exit before capturing the actual native exit code.
    $ErrorActionPreference='Continue'
    $global:LASTEXITCODE=$null
    & $node @arguments 1>(Join-Path $launcherFolder 'controller.stdout.log') 2>(Join-Path $launcherFolder 'controller.stderr.log')
    $nativeExitCode=$LASTEXITCODE
  }finally{$ErrorActionPreference=$oldPreference;Set-Location -LiteralPath $oldLocation.Path}
  if($null-eq$nativeExitCode){throw 'No actual native exit code captured'}
  $exitCode=[int]$nativeExitCode
}catch{$failure=[ordered]@{message=$_.Exception.Message};$exitCode=1}
finally{
  foreach($name in $saved.Keys){try{[Environment]::SetEnvironmentVariable($name,$saved[$name].value,'Process');$now=[Environment]::GetEnvironmentVariable($name,'Process');if(($null-ne$now)-ne$saved[$name].present-or($saved[$name].present-and$now-cne$saved[$name].value)){throw 'Restore mismatch'}}catch{$restoreErrors.Add($name)}}
  foreach($name in @($names)+@('PATH')){$after+=@(EnvState $name)}
  try{if($acquired){$lease.ReleaseMutex();$released=$true}}catch{$restoreErrors.Add('desktop-mutex')}finally{$lease.Dispose()}
  if($restoreErrors.Count){$exitCode=1}
  if(Test-Path -LiteralPath $launcherFolder){
    Write-NewJson (Join-Path $launcherFolder 'PowerShell.finished.json') ([ordered]@{exitCode=$exitCode;nativeExitCode=$nativeExitCode;failure=$failure;leaseReleased=$released;environmentRestored=($restoreErrors.Count-eq0);restoreErrors=@($restoreErrors.ToArray());environmentAfter=$after;releaseReady=$false;productionQualificationComplete=$false;fullDevtoolsGoalComplete=$false})
    $files=@(Get-ChildItem -LiteralPath $launcherFolder -File|Sort-Object Name|ForEach-Object {Pin $_.FullName})
    [long]$total=0;foreach($file in $files){$total+=$file.bytes}
    Write-NewJson (Join-Path $launcherFolder 'ExportFiles.actual.json') ([ordered]@{schema='soui.issue-short-export-files/v1';files=$files;totalBytes=$total;scope='launcher actual environment/lease/native exit only; separate from controller/product evidence'})
  }
}
Write-Output ('launcherEvidence='+$launcherFolder+' controllerEvidence='+$folder+' actualNativeExit='+$nativeExitCode+' launcherExit='+$exitCode)
exit $exitCode
