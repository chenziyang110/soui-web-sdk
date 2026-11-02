[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][ValidateSet('preflight','run')][string]$Mode,
 [Parameter(Mandatory=$true)][string]$Manifest,
 [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$ManifestSha256,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [string]$RootGate='', [string]$PreflightFile='', [string]$PreflightSha256=''
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$utf8=[Text.UTF8Encoding]::new($false)
function Write-NewJson([string]$File,$Value){$bytes=$utf8.GetBytes(($Value|ConvertTo-Json -Depth 16)+[char]10);$s=[IO.File]::Open($File,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$s.Write($bytes,0,$bytes.Length)}finally{$s.Dispose()}}
function Pin([string]$File){$i=Get-Item -LiteralPath $File -Force;if($i.PSIsContainer-or($i.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain file required'};[ordered]@{path=$i.FullName;bytes=$i.Length;sha256=(Get-FileHash -LiteralPath $i.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}}
function Match-Pin($Actual,$Expected){if($Actual.bytes-ne$Expected.bytes-or$Actual.sha256-cne$Expected.sha256){throw 'Frozen image or source pin differs'}}
if([IntPtr]::Size-ne8-or$env:COMPUTERNAME-ine'SOUI-7AEMMK5Q0T'){throw 'Task VM x64 only'}
$folder=[IO.Path]::GetFullPath($EvidenceDirectory).TrimEnd('\','/')
if($folder-notmatch'^C:\\issue-closeout-evidence\\mp77p-(preflight|run)-[a-z0-9-]{1,40}$'-or(Test-Path -LiteralPath $folder)){throw 'Fresh bounded task evidence root required'}
if(($Mode-eq'preflight')-ne($folder-match'\\mp77p-preflight-')){throw 'Mode and evidence namespace differ'}
$manifestPin=Pin $Manifest;if($manifestPin.sha256-cne$ManifestSha256){throw 'Reviewed browser manifest differs'}
$m=[IO.File]::ReadAllText($Manifest,[Text.Encoding]::UTF8)|ConvertFrom-Json
if($m.schema-cne'soui.message-port-vm-carrier/v1'-or$m.producerProfile-cne'NEW fresh-paired execution metadata; unchanged original guest and AB controllers'-or$env:COMPUTERNAME-ieq$m.preparationHost){throw 'Reviewed new producer and VM required'}
$cfgFile=[string]$m.executionInput.path;Match-Pin (Pin $cfgFile) $m.executionInput
$cfg=[IO.File]::ReadAllText($cfgFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
if(-not$cfg.executionEnabled-or$cfg.rootIndependentReviewPending-or$cfg.sourceVersion-cne'0.5.1-preview.1'-or$cfg.originalProcessTimeoutMs-ne214500-or$cfg.originalOwnedTimeoutMs-ne394500-or$cfg.originalDrainMs-ne2000-or$cfg.outerInitialTimeoutMs-ne420000-or$cfg.outerDrainMs-ne2000){throw 'Frozen paired profile differs'}
$node=[string]$cfg.requiredExecutionNode.path;$controller=[string]$m.originalGuestController.path
Match-Pin (Pin $node) $cfg.requiredExecutionNode;Match-Pin (Pin $cfg.subjectRunner.path) $cfg.subjectRunner;Match-Pin (Pin $controller) $m.originalGuestController
$launcherExpected=@($m.files|Where-Object {[IO.Path]::GetFullPath([string]$_.guestPath)-ieq[IO.Path]::GetFullPath($PSCommandPath)});if($launcherExpected.Count-ne1){throw 'This exact launcher must be in the manifest'};Match-Pin (Pin $PSCommandPath) $launcherExpected[0]
$crt='C:\issues8\tray-x64';$crtPolicy=$m.supervisorCrtPolicy
if($crtPolicy.prefix-cne'C:/issues8/tray-x64'){throw 'Original process-local CRT prefix differs'}
Match-Pin (Pin $crtPolicy.payloadManifest.path) $crtPolicy.payloadManifest
$crtManifest=[IO.File]::ReadAllText($crtPolicy.payloadManifest.path,[Text.Encoding]::UTF8)|ConvertFrom-Json
if($crtManifest.revision-ne2-or$crtManifest.launcher.crtPrefixRelative-cne'tray-x64'){throw 'Exact prior r2 CRT source required'}
$crtRows=@($crtManifest.files|Where-Object {$_.path.StartsWith('tray-x64/',[StringComparison]::Ordinal)})
$actualNames=@(Get-ChildItem -LiteralPath $crt -Force|ForEach-Object{if($_.PSIsContainer-or($_.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Flat plain CRT prefix required'};$_.Name}|Sort-Object)
$expectedNames=@($crtRows|ForEach-Object{$_.path.Substring(9)}|Sort-Object)
if(($actualNames-join"`n")-cne($expectedNames-join"`n")){throw 'CRT prefix closed set differs'}
$crtPins=@(foreach($r in $crtRows){$p=Pin (Join-Path 'C:\issues8' $r.path);Match-Pin $p $r;$p})
if($Mode-eq'run'-and($RootGate-cne'ROOT_REVIEWED_GUEST_PREFLIGHT_V1'-or$PreflightSha256-notmatch'^[a-f0-9]{64}$')){throw 'Root actual-preflight review required'}
$names=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER')
$saved=@{};$restoreErrors=[Collections.Generic.List[string]]::new();$held=$false;$released=$false;$nativeExit=$null;$exitCode=1;$failure=$null
$mutex=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
try{
 try{$held=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$held=$true;throw 'Abandoned desktop lease'}
 if(-not$held){throw 'Desktop validation mutex occupied'}
 New-Item -ItemType Directory -Path $folder|Out-Null
 $owner=[Diagnostics.Process]::GetCurrentProcess()
 Write-NewJson (Join-Path $folder 'Lease.actual.json') @{name='Local\SouiDevKit.DesktopValidation';acquired=$true;mode=$Mode;ownerProcessId=$PID;ownerCreationFileTime=$owner.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}
 foreach($name in @($names)+@('PATH')){$value=[Environment]::GetEnvironmentVariable($name,'Process');$saved[$name]=@{present=($null-ne$value);value=$value}}
 foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
 [Environment]::SetEnvironmentVariable('SOUI_OWNED_PROCESS_RUNNER',[string]$cfg.subjectRunner.path,'Process')
 $oldPath=$saved['PATH'].value;[Environment]::SetEnvironmentVariable('PATH',($crt+$(if([string]::IsNullOrEmpty($oldPath)){''}else{';'+$oldPath})),'Process')
 $arguments=@($controller,$Mode,$Manifest,$ManifestSha256,'SOUI-7AEMMK5Q0T',$folder)
 if($Mode-eq'run'){$arguments+=@($RootGate,$PreflightFile,$PreflightSha256)}
 Write-NewJson (Join-Path $folder 'Invocation.actual.json') @{node=(Pin $node);args=$arguments;actualController=(Pin $controller);manifest=$manifestPin;mode=$Mode;machine=$env:COMPUTERNAME;wrapperPid=$PID;wrapperCreationFileTime=$owner.StartTime.ToUniversalTime().ToFileTimeUtc().ToString();crtPrefix=$crt;crtPins=$crtPins;clearedEnvironment=$names;originalValuesPersisted=$false;originalNativeProcessTimeoutMs=214500;originalNativeOwnedTimeoutMs=394500;originalOuterTimeoutMs=420000;originalDrainMs=2000;productHostExecutionAuthorized=$false;originalControllersModified=$false}
 $oldPreference=$ErrorActionPreference
 try{$ErrorActionPreference='Continue';$global:LASTEXITCODE=$null;& $node @arguments 1>(Join-Path $folder 'controller.stdout.log') 2>(Join-Path $folder 'controller.stderr.log');$nativeExit=$LASTEXITCODE}finally{$ErrorActionPreference=$oldPreference}
 if($null-eq$nativeExit){throw 'Actual native exit code missing'};$exitCode=[int]$nativeExit
}catch{$failure=$_.Exception.Message;[Console]::Error.WriteLine($failure);$exitCode=1}
finally{
 foreach($name in $saved.Keys){try{[Environment]::SetEnvironmentVariable($name,$saved[$name].value,'Process');$now=[Environment]::GetEnvironmentVariable($name,'Process');if(($null-ne$now)-ne$saved[$name].present-or($saved[$name].present-and$now-cne$saved[$name].value)){throw 'Environment restore mismatch'}}catch{$restoreErrors.Add($name)}}
 if($held){try{$mutex.ReleaseMutex();$released=$true}catch{$restoreErrors.Add('mutex')}};$mutex.Dispose()
 if(Test-Path -LiteralPath $folder){Write-NewJson (Join-Path $folder 'PowerShell.finished.json') @{actualNativeExitCode=$nativeExit;exitCode=$exitCode;failure=$failure;restoreErrors=@($restoreErrors.ToArray());environmentRestored=($restoreErrors.Count-eq0);pathRestored=($restoreErrors-notcontains'PATH');leaseReleased=$released;originalControllersModified=$false;originalTimelimitsChanged=$false;currentSdk060Qualified=$false;releaseReady=$false}}
}
if($restoreErrors.Count){exit 1};exit $exitCode
