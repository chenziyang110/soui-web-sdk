param([ValidateSet('x86','x64')][string]$Architecture,[string]$Attempt='attempt1')
$ErrorActionPreference='Stop'
$developmentRoot='C:\SOUI-SDK-development\preview9-tray'
$evidenceRoot=Join-Path $PSScriptRoot "$Architecture-$Attempt"
if(Test-Path -LiteralPath $evidenceRoot){throw 'Choose a new attempt; previous evidence is immutable'}
New-Item -ItemType Directory -Path $evidenceRoot | Out-Null
Set-Location -LiteralPath $developmentRoot
function Invoke-Recorded([string]$Name,[string]$Program,[string[]]$Arguments) {
  $started=[DateTime]::UtcNow.ToString('o')
  $ErrorActionPreference='Continue'
  & $Program @Arguments *> (Join-Path $evidenceRoot "$Name.log")
  $rawCode=$LASTEXITCODE
  $ErrorActionPreference='Stop'
  $record=[ordered]@{program=$Program;arguments=$Arguments;startedAt=$started;finishedAt=[DateTime]::UtcNow.ToString('o');exitCode=$rawCode}
  [IO.File]::WriteAllText((Join-Path $evidenceRoot "$Name.result.json"),($record | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
  if($Name -eq 'full-build' -or $Name -eq 'sdk-build'){
    $buildDir=if($Name -eq 'sdk-build'){"out/sdk-$Architecture"}else{"out/$Architecture"}
    foreach($file in @('build-Release.json','native-Release.log')){
      $source=Join-Path $buildDir $file
      if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $evidenceRoot "$Name-$file")}
    }
  }
  Write-Output "$Architecture $Name exit=$rawCode"
  if($rawCode -ne 0){throw "$Name failed; preserve this attempt"}
}
Invoke-Recorded 'full-build' 'powershell.exe' @('-NoProfile','-File','tools/Build.ps1','-Architecture',$Architecture,'-Configuration','Release')
Invoke-Recorded 'sdk-build' 'powershell.exe' @('-NoProfile','-File','tools/Build.ps1','-SdkOnly','-Architecture',$Architecture,'-Configuration','Release')
$testPattern='^(sdk.native.(contract|session-renewal|task-service|bridge-budget|task-store-capacity|tray-service|menu_service|global_shortcut)|demo.native.(public-task|session-renewal))$'
Invoke-Recorded 'no-desktop-tests' 'ctest.exe' @('--test-dir',"out/$Architecture",'-C','Release','--output-on-failure','-V','--timeout','35','-R',$testPattern,'--output-junit',(Join-Path $evidenceRoot 'tests.xml'))
Copy-Item -LiteralPath "out/$Architecture/Testing/Temporary/LastTest.log" -Destination (Join-Path $evidenceRoot 'LastTest.log')
$installedRoot=Join-Path $evidenceRoot 'sdk-install'
Invoke-Recorded 'install' 'cmake.exe' @('--install',"out/sdk-$Architecture",'--config','Release','--prefix',$installedRoot)
$consumerSource=Join-Path $evidenceRoot 'consumer-source'
Copy-Item -LiteralPath 'tests/session-consumer' -Destination $consumerSource -Recurse
$consumerBuild=Join-Path $evidenceRoot 'consumer-build'
$platform=if($Architecture -eq 'x86'){'Win32'}else{'x64'}
Invoke-Recorded 'consumer-configure' 'cmake.exe' @('-S',$consumerSource,'-B',$consumerBuild,'-G','Visual Studio 17 2022','-A',$platform,'-DCMAKE_SYSTEM_VERSION=10.0.19041.0',"-DSouiSdk_DIR=$installedRoot/lib/cmake/SouiSdk")
Invoke-Recorded 'consumer-build' 'cmake.exe' @('--build',$consumerBuild,'--config','Release','--parallel','2')
Invoke-Recorded 'consumer-test' (Join-Path $consumerBuild 'Release/soui_session_consumer.exe') @()
Write-Output "Completed without desktop interaction: $evidenceRoot"
