$ErrorActionPreference='Stop'
$repo='C:\SOUI-SDK-development\preview9-tray'
foreach($arch in @('x86','x64')){
  $dir=Join-Path $PSScriptRoot "tray-$arch-attempt1"
  if(Test-Path -LiteralPath $dir){throw 'Refuse overwrite tray build'}
  New-Item -ItemType Directory -Path $dir | Out-Null
  Copy-Item -LiteralPath (Join-Path $repo 'tests/tray-consumer') -Destination (Join-Path $dir 'source') -Recurse
  $platform=if($arch -eq 'x86'){'Win32'}else{'x64'}
  $steps=@(
    @{name='configure';args=@('-S',(Join-Path $dir 'source'),'-B',(Join-Path $dir 'build'),'-G','Visual Studio 17 2022','-A',$platform,'-DCMAKE_SYSTEM_VERSION=10.0.19041.0',('-DSouiSdk_DIR='+(Join-Path $PSScriptRoot "$arch-attempt1/sdk-install/lib/cmake/SouiSdk")))},
    @{name='build';args=@('--build',(Join-Path $dir 'build'),'--config','Release','--parallel','2')}
  )
  foreach($step in $steps){
    $started=[DateTime]::UtcNow.ToString('o')
    $argv=$step.args
    & cmake.exe @argv *> (Join-Path $dir ($step.name+'.log'))
    $code=$LASTEXITCODE
    [ordered]@{program='cmake.exe';arguments=$argv;startedAt=$started;finishedAt=[DateTime]::UtcNow.ToString('o');exitCode=$code}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $dir ($step.name+'.result.json')) -Encoding utf8
    if($code -ne 0){throw 'Tray configure/build failed; preserve raw attempt'}
  }
  Write-Output "$arch independent tray consumer built"
}
