$ErrorActionPreference='Stop'
$repo='C:\SOUI-SDK-development\preview9-tray'
$project=Join-Path $PSScriptRoot 'generated-project-attempt1'
function Run-Step([string]$name,[string]$program,[string[]]$arguments){
  $log=Join-Path $PSScriptRoot ($name+'.log')
  if(Test-Path -LiteralPath $log){throw "Refuse overwrite $name"}
  $start=[DateTime]::UtcNow.ToString('o')
  & $program @arguments *> $log
  $code=$LASTEXITCODE
  [ordered]@{name=$name;program=$program;arguments=$arguments;startedAt=$start;finishedAt=[DateTime]::UtcNow.ToString('o');exitCode=$code}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $PSScriptRoot ($name+'.result.json')) -Encoding utf8
  Write-Host "$name exit=$code"
  if($code -ne 0){throw "$name failed; raw sample preserved"}
}
Run-Step 'generated-create' 'powershell.exe' @('-NoProfile','-File',(Join-Path $repo 'tools/New-Project.ps1'),'-Name','RenewalStarter','-OutputRoot',$project,'-SdkPackage',(Join-Path $PSScriptRoot 'sdk-package-attempt1'))
foreach($arch in @('x86','x64')){
  Run-Step "generated-build-$arch" 'powershell.exe' @('-NoProfile','-File',(Join-Path $project 'tools/Build.ps1'),'-Architecture',$arch,'-Configuration','Release')
  Copy-Item -LiteralPath (Join-Path $project "out/$arch/build-Release.json") -Destination (Join-Path $PSScriptRoot "generated-$arch-build-Release.json")
  Copy-Item -LiteralPath (Join-Path $project "out/$arch/native-Release.log") -Destination (Join-Path $PSScriptRoot "generated-$arch-native-Release.log")
}
Run-Step 'generated-verify-project' 'node.exe' @((Join-Path $repo 'tools/sdk/soui-sdk.cjs'),'verify-project','--project',$project)
