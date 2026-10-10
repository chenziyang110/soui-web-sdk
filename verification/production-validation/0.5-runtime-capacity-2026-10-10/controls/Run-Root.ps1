[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][ValidateSet('ROOT_LIVE_CAPACITY_CRT_V2')][string]$RootGate,
 [ValidateSet('All','x64','Win32')][string]$Architecture='All',
 [ValidateSet('Debug','Release')][string]$Configuration='Debug'
)
$ErrorActionPreference='Stop'
$taskPacketRoot='F:/work/sm05-broker/out/live-capacity-crt-root-44dcdd8d-1743-46b5-a549-581157961991'
$taskReleaseRoot='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf'
$taskDebugRoot='F:/work/sm05-broker/out/live-capacity-debug-draft-c6ed77b2-6101-4a9f-8437-32a8737b1a8c'
$taskLease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$taskAcquired=$false
$taskEnvNames=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','VcpkgEnabled','VcpkgEnableManifest')
$taskPriorEnv=@{}
foreach($taskName in $taskEnvNames){$taskPriorEnv[$taskName]=[Environment]::GetEnvironmentVariable($taskName,'Process')}
function CheckPin($taskPin){
 $taskItem=Get-Item -LiteralPath $taskPin.path -Force
 if($taskItem.PSIsContainer-or($taskItem.Attributes-band[IO.FileAttributes]::ReparsePoint)-or
    $taskItem.Length-ne$taskPin.bytes-or
    (Get-FileHash -LiteralPath $taskPin.path -Algorithm SHA256).Hash.ToLowerInvariant()-cne$taskPin.sha256){throw "Frozen input changed: $($taskPin.path)"}
}
try{
 try{$taskAcquired=$taskLease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$taskAcquired=$true}
 if(-not$taskAcquired){throw 'Desktop validation lease busy; no execution'}
 $taskControlPins=@([pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf/README.md';bytes=2181;sha256='496b9e23aac6dd96c81a051290fb454b493842b8f1c5e86d3ef6f46680d5018c'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf/Run-Root.ps1';bytes=6046;sha256='4951c2d498b810eb393ed974e660ce4de5120fd107223de06e8795d412ea23a9'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf/fixed-compiler.cmake';bytes=1330;sha256='011e4f138d6c25c10e6b2f24df0a212b59951886f586359255cffba59b13a738'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf/inputs.json';bytes=60380;sha256='cfa3e62cc0b3f3a13ba49bac2f79813f338de92a0f2f4f4e9a0079ff175e81e2'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf/plan.Win32.json';bytes=47848;sha256='1e9014acddc3ab9beeae679427384ae567848e52ff3511592f0c3ae2bcc94b63'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf/plan.x64.json';bytes=47830;sha256='51ddf1b7fdad9f1ffc7d04b903335fb7fb47207ee8c7696f8aa4a0337ae00622'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf/run-root.cjs';bytes=4640;sha256='139b305e97e075ab45793999785f05bccc0ec6e47b66498ac86054ee0bb1c350'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf/seal.json';bytes=2766;sha256='b58195e3a1e2f6f34a6c6a770b810408653563cdea32be4ab19077305ed2f903'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-integration-root-3350b8e9-52e6-4826-96fa-db82d6d457bf/tools.json';bytes=6397;sha256='31a322f2791986931aba22fa459b11a1bb41dce69daeb66c38d13807d9584411'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-debug-draft-c6ed77b2-6101-4a9f-8437-32a8737b1a8c/DELTA.json';bytes=7430;sha256='7a73a54efac8d912f12c616b7fce277fc042930501d0f09bae1e290c6e242f97'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-debug-draft-c6ed77b2-6101-4a9f-8437-32a8737b1a8c/README.md';bytes=1865;sha256='cf896035fc91a1a3b5d61ba89e49a2e127fba57bf100200b68551b1fc98e2b87'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-debug-draft-c6ed77b2-6101-4a9f-8437-32a8737b1a8c/plan.Win32.draft.json';bytes=47842;sha256='633c2cbe121d69e36c985747f7f152aa17c2d97146ab3dda637f4198b1e65303'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-debug-draft-c6ed77b2-6101-4a9f-8437-32a8737b1a8c/plan.x64.draft.json';bytes=47824;sha256='b9312236a2824eb5c15facc3db9d28ca582d5207b499c4638bb1512375d4e7ef'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-debug-draft-c6ed77b2-6101-4a9f-8437-32a8737b1a8c/seal.json';bytes=1287;sha256='0d6f10387c1eb05c345ac74471ee222d4e4ebe8f139962789315591da0ac3c6c'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-crt-root-44dcdd8d-1743-46b5-a549-581157961991/inputs.json';bytes=60379;sha256='c22738e3e33e9bc5c431776f6d54b22d6911af1e1cf068f4633e67a7c780bd33'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-crt-root-44dcdd8d-1743-46b5-a549-581157961991/plan.x64.Debug.json';bytes=47860;sha256='01663e3044e55d505ba1ab4b1cee8e4ed91bac108c41877c12d5b8121d47ca73'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-crt-root-44dcdd8d-1743-46b5-a549-581157961991/plan.Win32.Debug.json';bytes=47878;sha256='3b74743227e372fbee5f24c1209da93e0381f6052aa691018caae5929dc3febb'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-crt-root-44dcdd8d-1743-46b5-a549-581157961991/plan.x64.Release.json';bytes=47878;sha256='3f029466af5f3e0c25c01e8c5d0599ec2d990ef83ef2f8aa48442fda181bb22e'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-crt-root-44dcdd8d-1743-46b5-a549-581157961991/plan.Win32.Release.json';bytes=47896;sha256='30ff50f6ea94095501a38f5930374e3df49f8aa52419c8cd6365c1a9f4335ef0'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-debug-root-failure-08024e0f-bcc7-4f0a-a9d6-a0e4c93491d0/review.json';bytes=6527;sha256='0b3adf94d67c69c7dafc16c14d7ef823551491bca857a79005aa4a795fa0bf8a'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-crt-root-44dcdd8d-1743-46b5-a549-581157961991/before/tests/native/runtime_metrics/CMakeLists.txt';bytes=1197;sha256='d7d8f78d5b399affa00be630c81591dddf7daad455c993402af4e8fb1324f2ce'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-crt-root-44dcdd8d-1743-46b5-a549-581157961991/before/tests/native/engine_capacity/CMakeLists.txt';bytes=3820;sha256='fb9ecb87478a73598e9d466562d1da894840b71e1dd2c3791bb6f76f051856f0'},[pscustomobject]@{path='F:/work/sm05-broker/out/live-capacity-crt-root-44dcdd8d-1743-46b5-a549-581157961991/before/tests/native/scope_capacity/CMakeLists.txt';bytes=1042;sha256='42c3e405f3a31e5a0a831b51939f5ba6a7e9da467db9b8905494475dcd61f636'})
 foreach($taskPin in $taskControlPins){CheckPin $taskPin}
 $taskActualNames=[string[]]@(Get-ChildItem -LiteralPath $taskPacketRoot -Recurse -Force -File|ForEach-Object{
  if($_.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Packet reparse member'}
  $_.FullName.Substring($taskPacketRoot.Length+1).Replace('\','/')
 })
 [Array]::Sort($taskActualNames,[StringComparer]::Ordinal)
 $taskExpectedNames=@('DELTA.json','README.md','Run-Root.ps1','before/tests/native/engine_capacity/CMakeLists.txt','before/tests/native/runtime_metrics/CMakeLists.txt','before/tests/native/scope_capacity/CMakeLists.txt','inputs.json','plan.Win32.Debug.json','plan.Win32.Release.json','plan.x64.Debug.json','plan.x64.Release.json','seal.json')
 if(($taskActualNames-join[char]0)-cne($taskExpectedNames-join[char]0)){throw 'Exact packet membership changed'}
 foreach($taskParent in @(
  [pscustomobject]@{root=$taskReleaseRoot;members=@('README.md','Run-Root.ps1','fixed-compiler.cmake','inputs.json','plan.Win32.json','plan.x64.json','run-root.cjs','seal.json','tools.json')},
  [pscustomobject]@{root=$taskDebugRoot;members=@('DELTA.json','README.md','plan.Win32.draft.json','plan.x64.draft.json','seal.json')}
 )){
  $taskParentNames=[string[]]@(Get-ChildItem -LiteralPath $taskParent.root -Recurse -Force|ForEach-Object{
   if($_.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Parent reparse member'}
   if(-not$_.PSIsContainer){$_.FullName.Substring($taskParent.root.Length+1).Replace('\','/')}
  })
  [Array]::Sort($taskParentNames,[StringComparer]::Ordinal)
  if(($taskParentNames-join[char]0)-cne($taskParent.members-join[char]0)){throw 'Exact immutable parent membership differs'}
 }
 $taskInputs=Get-Content -LiteralPath (Join-Path $taskPacketRoot 'inputs.json') -Raw|ConvertFrom-Json
 if($taskInputs.schema-cne'soui.live-capacity-integration-inputs/v1'-or$taskInputs.nodeVersion-cne'24.14.0'){throw 'Frozen inputs schema differs'}
 CheckPin $taskInputs.node
 [Environment]::SetEnvironmentVariable('NODE_OPTIONS',$null,'Process')
 [Environment]::SetEnvironmentVariable('NODE_PATH',$null,'Process')
 [Environment]::SetEnvironmentVariable('NODE_REPL_EXTERNAL_MODULE',$null,'Process')
 [Environment]::SetEnvironmentVariable('VcpkgEnabled','false','Process')
 [Environment]::SetEnvironmentVariable('VcpkgEnableManifest','false','Process')
 $taskArchs=if($Architecture-ceq'All'){@('x64','Win32')}else{@($Architecture)}
 $taskAnyFailure=$false
 foreach($taskArch in $taskArchs){
  foreach($taskPin in $taskControlPins){CheckPin $taskPin}
  $taskPlanFile=Join-Path $taskPacketRoot ("plan.$taskArch.$Configuration.json")
  $taskPlan=Get-Content -LiteralPath $taskPlanFile -Raw|ConvertFrom-Json
  foreach($taskPin in $taskPlan.pins){CheckPin $taskPin}
  if(Test-Path -LiteralPath $taskPlan.output){throw "Fresh output already exists: $($taskPlan.output)"}
  $taskPlanHash=(Get-FileHash -LiteralPath $taskPlanFile -Algorithm SHA256).Hash.ToLowerInvariant()
  & $taskInputs.node.path (Join-Path $taskReleaseRoot 'run-root.cjs') 'ROOT_FINITE_OWNED_V1' $taskPlanFile $taskPlanHash
  $taskExit=$LASTEXITCODE
  if($taskExit-ne0){$taskAnyFailure=$true;continue}
  # Natural CTest root0 is also bound to the exact 29-case JUnit set.
  $taskXmlSettings=[Xml.XmlReaderSettings]::new()
  $taskXmlSettings.DtdProcessing=[Xml.DtdProcessing]::Prohibit
  $taskXmlSettings.XmlResolver=$null
  $taskXmlReader=[Xml.XmlReader]::Create($taskPlan.junit,$taskXmlSettings)
  try{$taskDoc=[Xml.XmlDocument]::new();$taskDoc.XmlResolver=$null;$taskDoc.Load($taskXmlReader)}finally{$taskXmlReader.Dispose()}
  $taskTestNodes=@($taskDoc.SelectNodes('//testcase'))
  $taskObservedNames=[string[]]@($taskTestNodes|ForEach-Object{$_.GetAttribute('name')})
  $taskExpectedCases=[string[]]@($taskPlan.expectedCaseNames)
  [Array]::Sort($taskObservedNames,[StringComparer]::Ordinal)
  [Array]::Sort($taskExpectedCases,[StringComparer]::Ordinal)
  if($taskTestNodes.Count-ne29-or($taskObservedNames-join[char]0)-cne($taskExpectedCases-join[char]0)-or
     $taskDoc.SelectNodes('//failure|//error|//skipped').Count-ne0-or
     @($taskTestNodes|Where-Object{$_.GetAttribute('status')-cne'run'}).Count-ne0){throw 'Actual JUnit case membership/status differs'}
  foreach($taskPin in $taskPlan.pins){CheckPin $taskPin}
 }
 if($taskAnyFailure){exit 1}
}finally{
 try{foreach($taskName in $taskEnvNames){[Environment]::SetEnvironmentVariable($taskName,$taskPriorEnv[$taskName],'Process')}}
 finally{if($taskAcquired){$taskLease.ReleaseMutex()};$taskLease.Dispose()}
}
