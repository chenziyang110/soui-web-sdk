param(
  [Parameter(Mandatory=$true)][ValidateSet('x86','x64')][string]$Architecture,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9-]+$')][string]$Attempt
)
$ErrorActionPreference='Stop'
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
$repo='C:\SOUI-SDK-development\work-area-placement'
$app=Join-Path $repo "out\$Architecture\bin\Release\SouiWorkbench\SouiWorkbench.exe"
$probe='C:\SOUI-SDK-production-validation\preview15-v02-win7-current-20260927\layout-interaction-v8-attempt1\volume\soui-input\V02InputProbe.exe'
$build=Join-Path $PSScriptRoot "workarea-$Architecture-$(if($Architecture -eq 'x64'){'attempt2'}else{'attempt1'})-after-official-input.json"
$output=Join-Path $PSScriptRoot "gui-runs\$Attempt"
if(Test-Path -LiteralPath $output){throw 'GUI attempt already exists'}
[IO.Directory]::CreateDirectory($output) | Out-Null
$script:target=$null
$script:failure=''
$script:identityBefore=$false
$script:result=[ordered]@{schema='soui.work-area-gui/v1';architecture=$Architecture;attempt=$Attempt;
  startedAt=[DateTime]::UtcNow.ToString('o');sourceSha256='';buildId='';appSha256Before='';
  appSha256After='';probeSha256='';pid=0;processStartUtc='';workArea=$null;
  inspect=$null;resize=$null;closePosted=$false;exitCode=$null;timedOut=$false;
  identityBefore=$null;identityAfter=$null;guiVerified=$false;win7Verified=$false;releaseReady=$false;failure=''}
$gateFile=Join-Path $output 'gates.ndjson'
$utf8=New-Object Text.UTF8Encoding($false)

function Assert-Idle([string]$stage) {
  $busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
    $_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\.exe$' -and
    ($script:target -eq $null -or $_.ProcessId -ne $script:target.Id)
  } | Select-Object ProcessId,Name,CreationDate)
  $gate=[ordered]@{at=[DateTime]::UtcNow.ToString('o');stage=$stage;targetPid=$(if($script:target){$script:target.Id}else{0});processes=$busy}
  [IO.File]::AppendAllText($gateFile,($gate|ConvertTo-Json -Depth 5 -Compress)+"`n",$utf8)
  if($busy.Count){throw "Other workload appeared at $stage"}
}

function Invoke-Probe([string]$action,[string]$argument,[string]$creation,[string]$label) {
  Assert-Idle ("before-$label")
  $file=Join-Path $output "$label.json"
  $stdout=Join-Path $output "$label.stdout.log"
  $stderr=Join-Path $output "$label.stderr.log"
  $start=New-Object Diagnostics.ProcessStartInfo
  $start.FileName=$probe
  $start.Arguments=([string]$script:target.Id)+' '+$creation+' '+$action+' '+$argument+' "'+$file+'"'
  $start.WorkingDirectory=Split-Path -Parent $probe
  $start.UseShellExecute=$false
  $start.CreateNoWindow=$true
  $start.RedirectStandardOutput=$true
  $start.RedirectStandardError=$true
  $process=New-Object Diagnostics.Process
  $process.StartInfo=$start
  $code=-1;$timedOut=$false;$outText='';$errText=''
  try {
    if(-not $process.Start()){throw "Native input probe did not start: $label"}
    $outRead=$process.StandardOutput.ReadToEndAsync()
    $errRead=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(5000)){
      $timedOut=$true
      $process.Kill()
      $process.WaitForExit()
    }
    $code=$process.ExitCode
    $outText=$outRead.GetAwaiter().GetResult()
    $errText=$errRead.GetAwaiter().GetResult()
  } finally {$process.Dispose()}
  [IO.File]::WriteAllText($stdout,$outText,$utf8)
  [IO.File]::WriteAllText($stderr,$errText,$utf8)
  [IO.File]::WriteAllText((Join-Path $output "$label-exit.json"),
    (@{action=$action;argument=$argument;exitCode=$code;timedOut=$timedOut;at=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Compress)+"`n",$utf8)
  Assert-Idle ("after-$label")
  return [pscustomobject]@{exitCode=$code;timedOut=$timedOut;file=$file}
}

function Invoke-Identity([string]$phase) {
  Assert-Idle "before-identity-$phase"
  $stdout=Join-Path $output "identity-$phase.stdout.log"
  $stderr=Join-Path $output "identity-$phase.stderr.log"
  & node "$PSScriptRoot\Verify-GuiIdentity.cjs" $Architecture $Attempt $phase 1> $stdout 2> $stderr
  $code=$LASTEXITCODE
  [IO.File]::WriteAllText((Join-Path $output "identity-$phase-exit.json"),
    (@{phase=$phase;exitCode=$code;at=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Compress)+"`n",$utf8)
  if($code -ne 0){throw "Full source and payload identity failed at $phase"}
  Assert-Idle "after-identity-$phase"
  return Get-Content -LiteralPath (Join-Path $output "identity-$phase.json") -Raw | ConvertFrom-Json
}

try {
  Assert-Idle 'before-launch'
  if(-not (Test-Path -LiteralPath $app -PathType Leaf) -or -not (Test-Path -LiteralPath $probe -PathType Leaf)){
    throw 'Application or native input probe is missing'
  }
  $script:result.identityBefore=Invoke-Identity 'before'
  $script:identityBefore=$true
  $script:result.sourceSha256=$script:result.identityBefore.sourceSha256
  $script:result.buildId=$script:result.identityBefore.buildId
  $script:result.appSha256Before=(Get-FileHash -LiteralPath $app -Algorithm SHA256).Hash.ToLowerInvariant()
  $script:result.probeSha256=(Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash.ToLowerInvariant()
  Add-Type -AssemblyName System.Windows.Forms
  $area=[System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
  $script:result.workArea=@($area.Left,$area.Top,$area.Right,$area.Bottom)

  $start=New-Object Diagnostics.ProcessStartInfo
  $start.FileName=$app
  $start.Arguments='--page=data --width=850 --height=640'
  $start.WorkingDirectory=Split-Path -Parent $app
  $start.UseShellExecute=$false
  foreach($key in @($start.EnvironmentVariables.Keys)){
    if($key -like 'SOUI_*'){$start.EnvironmentVariables.Remove($key)}
  }
  $script:target=New-Object Diagnostics.Process
  $script:target.StartInfo=$start
  if(-not $script:target.Start()){throw 'Workbench process did not start'}
  $script:result.pid=$script:target.Id
  $script:result.processStartUtc=$script:target.StartTime.ToUniversalTime().ToString('o')
  $watch=[Diagnostics.Stopwatch]::StartNew()
  $ready=$false
  $creation=''
  for($index=1;$index -le 40 -and $watch.ElapsedMilliseconds -lt 45000;$index++){
    if($script:target.HasExited){throw 'Workbench exited before a visible window was ready'}
    $inspection=Invoke-Probe 'inspect' '-' '0' ("inspect-{0:d2}" -f $index)
    if($inspection.exitCode -eq 0){
      $raw=[IO.File]::ReadAllText($inspection.file,$utf8)
      $parsed=$raw|ConvertFrom-Json
      $match=[regex]::Match($raw,'"creationFiletime":([0-9]+)')
      if(-not $match.Success){throw 'Native probe omitted process creation identity'}
      if($parsed.foregroundBefore){
        $creation=$match.Groups[1].Value
        $script:result.inspect=$parsed
        $ready=$true
        break
      }
    }
    [Threading.Thread]::Sleep(1000)
  }
  if(-not $ready){throw 'Workbench did not become naturally foreground within 45 seconds'}
  $resized=Invoke-Probe 'resize-drag' '80,60' $creation 'resize-drag'
  if($resized.exitCode -ne 0){throw 'Native resize input failed'}
  $script:result.resize=Get-Content -LiteralPath $resized.file -Raw | ConvertFrom-Json
  if($script:result.resize.hwnd -ne $script:result.inspect.hwnd){throw 'Window identity changed before resize'}
  Assert-Idle 'after-resize'
} catch {
  $script:failure=$_.ToString()
} finally {
  if($script:target -ne $null){
    try {
      if(-not $script:target.HasExited){
        $script:result.closePosted=$script:target.CloseMainWindow()
        $closeWatch=[Diagnostics.Stopwatch]::StartNew()
        while(-not $script:target.WaitForExit(500) -and $closeWatch.ElapsedMilliseconds -lt 20000){
          Assert-Idle 'closing'
        }
        if(-not $script:target.HasExited){
          $script:result.timedOut=$true
          $script:target.Kill()
          $script:target.WaitForExit()
          if(-not $script:failure){$script:failure='Workbench close exceeded 20 seconds'}
        }
      }
      $script:result.exitCode=$script:target.ExitCode
    } catch {if(-not $script:failure){$script:failure=$_.ToString()}}
    $script:target.Dispose()
  }
  try {
    $script:result.appSha256After=(Get-FileHash -LiteralPath $app -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Idle 'after-close'
    if($script:identityBefore){$script:result.identityAfter=Invoke-Identity 'after'}
  } catch {if(-not $script:failure){$script:failure=$_.ToString()}}
  if($script:result.appSha256Before -ne $script:result.appSha256After -and -not $script:failure){
    $script:failure='Workbench executable changed during GUI input'
  }
  if($script:result.exitCode -ne 0 -and -not $script:failure){$script:failure='Workbench native exit was nonzero'}
  if(-not $script:result.closePosted -and -not $script:failure){$script:failure='Workbench close message was not posted'}
  $script:result.guiVerified=($script:failure -eq '' -and $script:result.resize -ne $null)
  $script:result.failure=$script:failure
  $script:result.finishedAt=[DateTime]::UtcNow.ToString('o')
  [IO.File]::WriteAllText((Join-Path $output 'result.json'),
    ($script:result|ConvertTo-Json -Depth 8)+"`n",$utf8)
}
if($script:failure){Write-Error $script:failure;exit 1}
Write-Output ('GUI resize verified for '+$Architecture+' build '+$script:result.buildId)
