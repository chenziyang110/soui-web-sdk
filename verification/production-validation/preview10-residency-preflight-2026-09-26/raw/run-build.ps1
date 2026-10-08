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
  Write-Output "$Architecture $Name exit=$rawCode"
  if($rawCode -ne 0){throw "$Name failed; preserve this attempt"}
}
Invoke-Recorded 'full-build' 'powershell.exe' @('-NoProfile','-File','tools/Build.ps1','-Architecture',$Architecture,'-Configuration','Release')
Copy-Item -LiteralPath "out/$Architecture/build-Release.json" -Destination (Join-Path $evidenceRoot 'full-build.json')
Copy-Item -LiteralPath "out/$Architecture/native-Release.log" -Destination (Join-Path $evidenceRoot 'full-native.log')
Invoke-Recorded 'sdk-build' 'powershell.exe' @('-NoProfile','-File','tools/Build.ps1','-SdkOnly','-Architecture',$Architecture,'-Configuration','Release')
Copy-Item -LiteralPath "out/sdk-$Architecture/build-Release.json" -Destination (Join-Path $evidenceRoot 'sdk-build.json')
Copy-Item -LiteralPath "out/sdk-$Architecture/native-Release.log" -Destination (Join-Path $evidenceRoot 'sdk-native.log')
$testPattern='^(sdk.native.(contract|task-service|bridge-budget|task-store-capacity|tray-service|menu_service|global_shortcut)|demo.native.public-task)$'
Invoke-Recorded 'no-desktop-tests' 'ctest.exe' @('--test-dir',"out/$Architecture",'-C','Release','--output-on-failure','-V','-R',$testPattern,'--output-junit',(Join-Path $evidenceRoot 'tests.xml'))
Copy-Item -LiteralPath "out/$Architecture/Testing/Temporary/LastTest.log" -Destination (Join-Path $evidenceRoot 'LastTest.log')
$installedRoot=Join-Path $evidenceRoot 'sdk-install'
Invoke-Recorded 'install' 'cmake.exe' @('--install',"out/sdk-$Architecture",'--config','Release','--prefix',$installedRoot)
$consumerSource=Join-Path $evidenceRoot 'consumer-source'
Copy-Item -LiteralPath 'tests/tray-consumer' -Destination $consumerSource -Recurse
$consumerBuild=Join-Path $evidenceRoot 'consumer-build'
$platform=if($Architecture -eq 'x86'){'Win32'}else{'x64'}
Invoke-Recorded 'consumer-configure' 'cmake.exe' @('-S',$consumerSource,'-B',$consumerBuild,'-G','Visual Studio 17 2022','-A',$platform,'-DCMAKE_SYSTEM_VERSION=10.0.19041.0',"-DSouiSdk_DIR=$installedRoot/lib/cmake/SouiSdk")
Invoke-Recorded 'consumer-build' 'cmake.exe' @('--build',$consumerBuild,'--config','Release','--parallel','2')
Write-Output "Completed without desktop tests: $evidenceRoot"
