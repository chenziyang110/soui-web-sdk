[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture,
  [string]$InstallPrefix,
  [string]$EvidenceDirectory,
  [string]$CMakeExe
)
$ErrorActionPreference='Stop'
$nativeSource=$PSScriptRoot
$nativeBuild=Join-Path $nativeSource ('build-'+$Architecture)
$originalNodeReuse=Get-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -ErrorAction SilentlyContinue
$nodeReuseState=@{present=($null-ne$originalNodeReuse);value=if($null-ne$originalNodeReuse){[string]$originalNodeReuse.Value}else{$null}}
$platform=if($Architecture-eq'x86'){'Win32,version=10.0.19041.0'}else{'x64,version=10.0.19041.0'}
$configureArgs=@('-S',$nativeSource,'-B',$nativeBuild,'-G','Visual Studio 17 2022','-A',$platform,
  '-T','v143,host=x64,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.19041.0',
  ('-DCMAKE_PREFIX_PATH='+$InstallPrefix),'-DCMAKE_CONFIGURATION_TYPES=Release')
$buildArgs=@('--build',$nativeBuild,'--config','Release','--target','soui_devtools_host_tests','--parallel','2','--','/nr:false')
$observation=[ordered]@{schema='soui.native-closeout-compile/v1';architecture=$Architecture;configuration='Release';
  source=$nativeSource;build=$nativeBuild;installPrefix=$InstallPrefix;executable=$CMakeExe;
  configureArguments=$configureArgs;buildArguments=$buildArgs;configureExitCode=$null;buildExitCode=$null;
  environment=[ordered]@{MSBUILDDISABLENODEREUSE='1'};callerEnvironment=$nodeReuseState;
  callerEnvironmentRestored=$false;compilerHelperSuppressionConfigured=$false}
try {
  $env:MSBUILDDISABLENODEREUSE='1'
  & $CMakeExe @configureArgs *> (Join-Path $EvidenceDirectory 'configure.log')
  $observation.configureExitCode=$LASTEXITCODE
  if($LASTEXITCODE-ne0){throw 'External narrow CMake configuration failed.'}
  & $CMakeExe @buildArgs *> (Join-Path $EvidenceDirectory 'build.log')
  $observation.buildExitCode=$LASTEXITCODE
  if($LASTEXITCODE-ne0){throw 'External narrow native contract compilation failed.'}
} finally {
  if($nodeReuseState.present){Set-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -Value $nodeReuseState.value -ErrorAction Stop}
  else {Remove-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -ErrorAction SilentlyContinue}
  $restored=Get-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -ErrorAction SilentlyContinue
  $observation.callerEnvironmentRestored=(($null-ne$restored)-eq$nodeReuseState.present)-and
    (-not$nodeReuseState.present-or([string]$restored.Value-ceq$nodeReuseState.value))
  [IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'compile-invocation.json'),
    ($observation|ConvertTo-Json -Depth 8)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
  if(-not$observation.callerEnvironmentRestored){throw 'External native contract build environment restoration failed.'}
}
