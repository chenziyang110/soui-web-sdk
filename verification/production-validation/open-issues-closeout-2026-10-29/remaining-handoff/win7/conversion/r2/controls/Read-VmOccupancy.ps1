[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$PlanFile,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$PlanSha256
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Assert-Pin($Row){$item=Get-Item -LiteralPath $Row.path -Force;if($item.Attributes-band[IO.FileAttributes]::ReparsePoint-or$item.Length-ne$Row.bytes-or(Get-FileHash -LiteralPath $Row.path -Algorithm SHA256).Hash.ToLowerInvariant()-cne$Row.sha256){throw 'Pinned occupancy dependency changed'}}
function Native-Names {
  $all=@(Get-Process -ErrorAction Stop);$selected=[Collections.Generic.List[object]]::new()
  if($all.Count-eq0-or@($all|Where-Object{$_.Id-eq$PID-and$_.ProcessName-ieq'powershell'}).Count-ne1){throw 'Native process snapshot omits the known current PowerShell identity'}
  $names=@('vmware-vmx','vmware','qemu-system-x86_64','qemu-system-i386','VirtualBoxVM','VBoxHeadless')
  foreach($item in $all){
    $name=[string]$item.ProcessName
    if(-not$name-or$name.Length-gt256-or$name-match'[\r\n]'){throw 'Native process name snapshot incomplete'}
    if($name-in$names){$selected.Add([pscustomobject]@{pid=$item.Id;name=($name+'.exe');creationFileTime=$item.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()})}
  }
  return [pscustomobject]@{nativeProcessCount=$all.Count;allNamesReadable=$true;selected=@($selected.ToArray()|Sort-Object pid)}
}
if((Get-FileHash -LiteralPath $PlanFile -Algorithm SHA256).Hash.ToLowerInvariant()-cne$PlanSha256){throw 'Occupancy plan changed'}
$plan=[IO.File]::ReadAllText($PlanFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
if($plan.schema-cne'soui.one-win7-immutable-base-conversion-plan/v2'){throw 'Exact r2 occupancy plan required'}
foreach($row in @($plan.tools.python,$plan.tools.occupancyPython,$plan.tools.occupancyPs)+@($plan.occupancyDependencies)){Assert-Pin $row}
$before=Native-Names
$request=@{schema='soui.native-vm-pid-selection/v1';selected=@($before.selected)}|ConvertTo-Json -Depth 5 -Compress
$nativeExit=$null;$nativePreference=$ErrorActionPreference
try{
  $ErrorActionPreference='Continue';$global:LASTEXITCODE=$null
  $response=@($request|& $plan.tools.python.path '-I' '-S' '-B' $plan.tools.occupancyPython.path $PlanFile $PlanSha256 2>&1)
  $nativeExit=$LASTEXITCODE
}finally{$ErrorActionPreference=$nativePreference}
if($null-eq$nativeExit-or$nativeExit-ne0){throw 'Targeted Python occupancy query failed; source occupancy is unknown'}
$observed=($response-join"`n")|ConvertFrom-Json
if($observed.schema-cne'soui.targeted-vm-commandline-observation/v1'-or-not$observed.rawCommandLinesOmitted-or$observed.allNameEnumerationByPython){throw 'Unexpected targeted occupancy response'}
$after=Native-Names
if(($before.selected|ConvertTo-Json -Depth 5 -Compress)-cne($after.selected|ConvertTo-Json -Depth 5 -Compress)){throw 'Native VM PID/name/birth snapshot changed during targeted query'}
if($observed.rows.Count-ne$before.selected.Count){throw 'Incomplete selected VM command-line observations'}
for($i=0;$i-lt$before.selected.Count;$i++){
  $a=$before.selected[$i];$b=$observed.rows[$i]
  if($a.pid-ne$b.pid-or$a.name-ine$b.name-or$a.creationFileTime-cne$b.creationFileTime-or-not$b.commandLineAvailable-or$b.referencesExactFvmRoot){throw 'Selected VM identity inaccessible/changed or selected source is occupied'}
}
$locks=@(Get-ChildItem -LiteralPath 'F:\vm' -Force|Where-Object{$_.Name-match'(?i)\.(lck|lock)$'}|Select-Object -ExpandProperty Name)
if($locks.Count){throw 'Source VM lock metadata present'}
foreach($row in @($plan.tools.python,$plan.tools.occupancyPython,$plan.tools.occupancyPs)+@($plan.occupancyDependencies)){Assert-Pin $row}
[ordered]@{schema='soui.native-ps-targeted-psutil-vm-occupancy/v1';observedUtc=[DateTime]::UtcNow.ToString('o');nativeBefore=$before;nativeAfter=$after;vmProcesses=@($observed.rows);sourceLocks=$locks;rawCommandLinesOmitted=$true;WmiOrCimUsed=$false;unknownNamesAdmitted=$false}|ConvertTo-Json -Depth 8 -Compress
