$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$entry=Join-Path $root 'tools/Build-SouiLlvm.ps1'
$passed=0
function Reject([string]$Name,[hashtable]$Options,[string]$Pattern){
  $failure=$null
  try{& $entry @Options | Out-Null}catch{$failure=$_.Exception.Message}
  if(-not $failure -or $failure -notmatch $Pattern){throw "$Name did not reject at its boundary: $failure"}
  $script:passed++
}
$base=@{Architecture='x64';NativeToolchain='LLVM22MSVC142';OutputRoot=(Join-Path $root ('out/soui-entry-test-'+[Guid]::NewGuid().ToString('N')));
  LlvmRoot='C:/absent/llvm';MsvcRoot='C:/absent/msvc';WindowsSdkRoot='C:/absent/sdk';NinjaExe='C:/absent/ninja.exe';
  WindowsCmdExe='C:/absent/cmd.exe';PowerShellExe='C:/absent/pwsh.exe';VsInstallDir='C:/absent/vs';NodeExe='C:/absent/node.exe'}
Reject 'missing explicit inputs' @{} 'SOUI LLVM requires explicit'
foreach($name in @('LlvmRoot','MsvcRoot','WindowsSdkRoot','NinjaExe','WindowsCmdExe','PowerShellExe','VsInstallDir','NodeExe')){
  $options=$base.Clone();$options[$name]='C:/bad&path';Reject ('metacharacter '+$name) $options 'without cmd or MSBuild metacharacters'
}
$options=$base.Clone();$options.OutputRoot='C:/outside-sdk-output';Reject 'outside SDK out' $options 'must be a new child of this SDK out'
$options=$base.Clone();$options.OutputRoot=Join-Path $root 'out';Reject 'existing output' $options 'must be a new child of this SDK out'
$options=$base.Clone();$options.NativeToolchain='MSVC143';Reject 'no implicit profile replacement' $options 'SOUI LLVM requires explicit'
if(Test-Path -LiteralPath $base.OutputRoot){throw 'Boundary refusals wrote an output directory'}
Write-Output "SOUI LLVM entry boundaries: $passed passed"
