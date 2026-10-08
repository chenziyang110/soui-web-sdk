[CmdletBinding()]
param([switch]$FreshConfigure)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'CMake-Configure.ps1')
$cmake=(Get-Command cmake.exe -ErrorAction Stop).Source
$validationSource=Join-Path $workspace 'benchmarks/protocol'
$validationBuild=Join-Path $workspace 'out/benchmark-protocol'
$arguments=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $workspace `
  -SourceDirectory $validationSource -BuildDirectory $validationBuild -Architecture x64 -FreshConfigure:$FreshConfigure)
New-Item -ItemType Directory -Force -Path $validationBuild | Out-Null
& $cmake @arguments *> (Join-Path $validationBuild 'configure.log')
if($LASTEXITCODE -ne 0){throw "Validation tools configure failed: $validationBuild/configure.log"}
& $cmake --build $validationBuild --config Release --target OwnedProcessRunner OwnedProcessAssignmentFixture --parallel 2 *> (Join-Path $validationBuild 'compile.log')
if($LASTEXITCODE -ne 0){throw "Validation tools build failed: $validationBuild/compile.log"}
Write-Output (Join-Path $validationBuild 'Release/OwnedProcessRunner.exe')
