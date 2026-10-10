[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('ROOT_THREAD_PERSISTENT_DEFAULT_V1')][string]$RootGate)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot.Replace('\','/')
function Check($taskPin){$taskItem=Get-Item -LiteralPath $taskPin.path -Force;if($taskItem.PSIsContainer-or($taskItem.Attributes-band[IO.FileAttributes]::ReparsePoint)-or$taskItem.Length-ne$taskPin.bytes-or(Get-FileHash -LiteralPath $taskPin.path -Algorithm SHA256).Hash.ToLowerInvariant()-cne$taskPin.sha256){throw "Pin drift $($taskPin.path)"}}
function Members([string]$taskDir){$taskNames=[string[]]@(Get-ChildItem -LiteralPath $taskDir -Recurse -File -Force|ForEach-Object{if($_.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Reparse member'};$_.FullName.Substring($taskDir.Length+1).Replace('\','/')});[Array]::Sort($taskNames,[StringComparer]::Ordinal);return ,$taskNames}
function Packet {
 $taskSeal=Get-Content -LiteralPath (Join-Path $taskRoot 'seal.json') -Raw|ConvertFrom-Json -DateKind String
 $taskNames=Members $taskRoot
 if(($taskNames-join[char]0)-cne($taskSeal.members-join[char]0)-or$taskSeal.schema-cne'soui.thread-persistent-default-root-seal/v1'){throw 'Packet membership/schema'}
 foreach($taskPin in $taskSeal.core){Check $taskPin}
}
$taskLease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation');$taskAcquired=$false
$taskNames=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','CL','_CL_','LINK','_LINK_','VcpkgEnabled','VcpkgEnableManifest','MSBUILDDISABLENODEREUSE');$taskPrior=@{}
foreach($taskName in $taskNames){$taskPrior[$taskName]=[Environment]::GetEnvironmentVariable($taskName,'Process')}
try{
 try{$taskAcquired=$taskLease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$taskAcquired=$true};if(-not$taskAcquired){throw 'Desktop validation lease busy'}
 Packet;$taskInputs=Get-Content -LiteralPath (Join-Path $taskRoot 'inputs.json') -Raw|ConvertFrom-Json -DateKind String
 if($taskInputs.schema-cne'soui.thread-persistent-default-root-inputs/v1'-or$taskInputs.records.Count-ne2-or($taskInputs.records.architecture-join',')-cne'x64,Win32'){throw 'Inputs scope differs'}
 Check $taskInputs.node;Check $taskInputs.driver
 if(((Members $taskInputs.sourceRoot)-join[char]0)-cne($taskInputs.sourceMembers-join[char]0)){throw 'Source membership differs'}
 foreach($taskRecord in $taskInputs.records){Check $taskRecord.plan;if(Test-Path -LiteralPath $taskRecord.base){throw 'Fresh output required'}}
 foreach($taskName in @('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','CL','_CL_','LINK','_LINK_')){[Environment]::SetEnvironmentVariable($taskName,$null,'Process')}
 foreach($taskName in @('VcpkgEnabled','VcpkgEnableManifest')){[Environment]::SetEnvironmentVariable($taskName,'false','Process')};[Environment]::SetEnvironmentVariable('MSBUILDDISABLENODEREUSE','1','Process')
 $taskFailed=$false
 foreach($taskRecord in $taskInputs.records){
  try{
   Packet;Check $taskRecord.plan;$taskPlan=Get-Content -LiteralPath $taskRecord.plan.path -Raw|ConvertFrom-Json -DateKind String
   foreach($taskPin in $taskPlan.pins){Check $taskPin}
   [IO.Directory]::CreateDirectory((Join-Path $taskRecord.base 'empty-user-props'))|Out-Null
   & $taskInputs.node.path $taskInputs.driver.path ROOT_FINITE_OWNED_V1 $taskRecord.plan.path $taskRecord.plan.sha256
   if($LASTEXITCODE-ne0){throw 'Strict configure/build/CTest failed'}
   $taskSummary=Get-Content -LiteralPath (Join-Path $taskPlan.output 'summary.json') -Raw|ConvertFrom-Json -DateKind String
   if(-not$taskSummary.completed-or$taskSummary.failure-or$taskSummary.stages.Count-ne3-or@($taskSummary.stages|Where-Object{-not$_.accepted-or-not$_.result.passed-or$_.result.exitCode-ne0-or$_.result.supervisorExitCode-ne0-or$_.result.terminal.terminatedOwnedJob-or$_.result.terminal.activeProcesses-ne0}).Count){throw 'Natural whole-Job stages differ'}
   [xml]$taskJUnit=Get-Content -LiteralPath (Join-Path $taskRecord.base 'junit.xml') -Raw
   $taskCases=@($taskJUnit.testsuite.testcase);$taskExpected=[string[]]@($taskRecord.expectedTestCases|ForEach-Object{'acceptance.thread-sampler.'+$_});$taskActual=[string[]]@($taskCases|ForEach-Object{$_.name});[Array]::Sort($taskExpected,[StringComparer]::Ordinal);[Array]::Sort($taskActual,[StringComparer]::Ordinal)
   if($taskCases.Count-ne15-or($taskActual-join[char]0)-cne($taskExpected-join[char]0)-or@($taskCases|Where-Object{$_.status-cne'run'-or$_.failure-or$_.skipped}).Count){throw '15 exact passing test cases required'}
   foreach($taskPin in $taskPlan.pins){Check $taskPin};Write-Output ('15 passing thread sampler cases: '+$taskRecord.architecture)
  }catch{$taskFailed=$true;Write-Output ('Thread sampler failed: '+$taskRecord.architecture+'; '+$_.Exception.Message)}
 }
 if($taskFailed){exit 1}
}finally{try{foreach($taskName in $taskNames){[Environment]::SetEnvironmentVariable($taskName,$taskPrior[$taskName],'Process')}}finally{if($taskAcquired){$taskLease.ReleaseMutex()};$taskLease.Dispose()}}
