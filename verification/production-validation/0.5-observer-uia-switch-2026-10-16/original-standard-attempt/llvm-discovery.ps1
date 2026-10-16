param([string]$Root,[string]$Architecture,[string]$LlvmRoot,[string]$MsvcRoot,[string]$WindowsSdkRoot,[string]$NinjaExe,[string]$WindowsCmdExe,[string]$PowerShellExe,[string]$Output)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
. (Join-Path $Root 'tools/internal/Find-LlvmToolchain.ps1')
$profile=Get-SouiLlvmToolchain -Architecture $Architecture -LlvmRoot $LlvmRoot -MsvcRoot $MsvcRoot -WindowsSdkRoot $WindowsSdkRoot -NinjaExe $NinjaExe -WindowsCmdExe $WindowsCmdExe -PowerShellExe $PowerShellExe -ToolIdentityBridge (Join-Path $Root 'tools/internal/Invoke-ToolFileIdentity.ps1') -ToolIdentityHelper (Join-Path $Root 'tools/internal/Tool-FileIdentity.ps1')
$tools=[ordered]@{}
foreach($entry in @(@('cmake','cmake.exe'),@('node','node.exe'),@('npm','npm.cmd'))){
 $commands=@(Get-Command $entry[1] -CommandType Application -ErrorAction Stop)
 if($commands.Count-lt1){throw 'Missing actual command resolution'}
 $tools[$entry[0]]=[IO.Path]::GetFullPath([string]$commands[0].Source)
}
$record=[ordered]@{schema='soui.standard-llvm-discovery/v1';discovery=$profile;tools=$tools;observedCompiler=$null;qualificationPassed=$false}
[IO.File]::WriteAllText($Output,($record|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
