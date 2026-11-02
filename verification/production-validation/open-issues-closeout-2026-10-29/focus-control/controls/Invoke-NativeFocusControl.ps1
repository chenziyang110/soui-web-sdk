[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$ExpectedMachine,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$PreparationSha256,
  [Parameter(Mandatory=$true)][ValidateSet('ROOT_REVIEWED_NATIVE_FOCUS_CONTROL_V1')][string]$RootGate
)
$ErrorActionPreference='Stop'
if($env:COMPUTERNAME-ine'SOUI-7AEMMK5Q0T'-or$env:COMPUTERNAME-ine$ExpectedMachine-or-not[Environment]::Is64BitProcess){throw 'Named x64 task VM required; host execution forbidden'}
if([Threading.Thread]::CurrentThread.GetApartmentState()-ne[Threading.ApartmentState]::STA){throw 'Start Windows PowerShell with -STA'}
if($EvidenceDirectory-notmatch'^C:\\issue-closeout-evidence\\focusc-[a-z0-9-]+$'-or(Test-Path -LiteralPath $EvidenceDirectory)){throw 'Fresh task-VM evidence directory required'}
$preparationFile=Join-Path $PSScriptRoot 'Preparation.actual.json'
if((Get-FileHash -LiteralPath $preparationFile -Algorithm SHA256).Hash.ToLowerInvariant()-cne$PreparationSha256){throw 'Reviewed preparation identity differs'}
$plan=[IO.File]::ReadAllText($preparationFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
if($plan.schema-cne'soui.native-tray-focus-control-preparation/v1'-or$plan.executed-or$plan.compiled-or$plan.preparationHost-ieq$env:COMPUTERNAME-or$plan.expectedGuest-ine$ExpectedMachine){throw 'Unexecuted current preparation identity required'}
foreach($entry in $plan.files){
  if($entry.relative-notmatch'^[A-Za-z0-9][A-Za-z0-9_.-]+$'){throw 'Flat reviewed input required'}
  $file=Join-Path $PSScriptRoot $entry.relative
  $item=Get-Item -LiteralPath $file
  if($item.PSIsContainer-or($item.Attributes-band[IO.FileAttributes]::ReparsePoint)-or$item.Length-ne$entry.bytes-or(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()-cne$entry.sha256){throw ('Reviewed input differs: '+$entry.relative)}
}
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false;$exitCode=2;$probeExitCode=$null;$saved=@{}
$names=@('PATH','NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER')
try{
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true;throw 'Abandoned desktop validation lease'}
  if(-not$acquired){throw 'Another task owns desktop validation'}
  New-Item -ItemType Directory -Path $EvidenceDirectory|Out-Null
  foreach($name in $names){$entry=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;$saved[$name]=@{present=($null-ne$entry);value=if($null-ne$entry){[string]$entry.Value}else{$null}};if($name-ne'PATH'){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}}
  $process=[Diagnostics.Process]::GetCurrentProcess()
  [IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'Invocation.actual.json'),(@{schema='soui.native-tray-focus-control-invocation/v1';machine=$env:COMPUTERNAME;pid=$PID;creationFileTime=$process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString();powershell=$process.MainModule.FileName;preparationSha256=$PreparationSha256;rootGate=$RootGate;deadlineMs=60000;foregroundAttemptsMaximum=1;focusCallsMaximum=1;inputInjection=$false;externalGestureEvidenceRequired=$true;oldTrayConsumerRerun=$false;releaseReady=$false}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
  $compileStarted=[DateTime]::UtcNow.ToString('o')
  Add-Type -Path (Join-Path $PSScriptRoot 'NativeFocusControl.cs') -ReferencedAssemblies 'System.dll','System.Core.dll','System.Web.Extensions.dll' -ErrorAction Stop
  [IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'Compilation.actual.json'),(@{startedUtc=$compileStarted;completedUtc=[DateTime]::UtcNow.ToString('o');mechanism='Windows PowerShell Add-Type CSharp in task VM';hostCompilerExecuted=$false;sourceSha256=($plan.files|Where-Object{$_.relative-eq'NativeFocusControl.cs'}).sha256;assemblyFullName=[SouiIssueFocusControl.Probe].Assembly.FullName;assemblyLocation=[SouiIssueFocusControl.Probe].Assembly.Location;scope='Direct native diagnostic only; no SDK/product compiled or executed';releaseReady=$false}|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
  $probeExitCode=[SouiIssueFocusControl.Probe]::Run($EvidenceDirectory,$ExpectedMachine,$RootGate)
  $exitCode=$probeExitCode
}catch{
  if(Test-Path -LiteralPath $EvidenceDirectory){[IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'PowerShell.error.json'),(@{type=$_.Exception.GetType().FullName;message=$_.Exception.Message;probeExitCode=$probeExitCode;releaseReady=$false}|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))}
  $exitCode=2
}finally{
  $errors=[Collections.Generic.List[string]]::new()
  foreach($name in $saved.Keys){try{if($saved[$name].present){Set-Item -LiteralPath ('Env:'+$name) -Value $saved[$name].value}else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue};$actual=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(($null-ne$actual)-ne$saved[$name].present-or($saved[$name].present-and[string]$actual.Value-cne$saved[$name].value)){throw 'Restore mismatch'}}catch{$errors.Add($name)}}
  try{if($acquired){$lease.ReleaseMutex()}}finally{$lease.Dispose()}
  if($errors.Count){$exitCode=2}
  if(Test-Path -LiteralPath $EvidenceDirectory){[IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'PowerShell.finished.json'),(@{schema='soui.native-tray-focus-control-launcher-result/v1';exitCode=$exitCode;probeExitCode=$probeExitCode;environmentRestored=($errors.Count-eq0);restoreErrors=@($errors.ToArray());leaseReleased=$acquired;inputInjection=$false;externalGestureEvidenceRequired=$true;sdkFixed=$false;productionQualificationComplete=$false;releaseReady=$false}|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))}
}
exit $exitCode
