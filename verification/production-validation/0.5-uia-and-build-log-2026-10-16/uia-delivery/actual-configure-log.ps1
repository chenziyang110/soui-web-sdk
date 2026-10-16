$ErrorActionPreference='Stop'
$root='C:/work/sm05-release-050'
. (Join-Path $root 'tools/internal/Find-LlvmToolchain.ps1')
. (Join-Path $root 'tools/internal/CMake-Configure.ps1')
$paths=@{LlvmRoot='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64';MsvcRoot='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.29.30133';WindowsSdkRoot='C:/Program Files (x86)/Windows Kits/10';NinjaExe='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe';WindowsCmdExe='C:/Windows/System32/cmd.exe';PowerShellExe='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe'}
$profile=Get-SouiLlvmToolchain -Architecture x64 @paths -ToolIdentityBridge (Join-Path $PSScriptRoot 'tools/internal/Invoke-ToolFileIdentity.ps1') -ToolIdentityHelper (Join-Path $PSScriptRoot 'tools/internal/Tool-FileIdentity.ps1')
$cmake='C:/Program Files/CMake/bin/cmake.exe'
$build=Join-Path $PSScriptRoot 'out/actual-log-build'
$configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $PSScriptRoot -SourceDirectory (Join-Path $PSScriptRoot 'native-log-probe') -BuildDirectory $build -Architecture x64 -FreshConfigure -NativeToolchain LLVM22MSVC142 -LlvmToolchain $profile)
# Load only the exact existing production pin/path helper function definitions.
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'tools/internal/Observe-LlvmBuild.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Existing observer syntax failed'}
foreach($name in @('Need','Plain','ObsPin','MatchPin')){
  $matches=@($ast.FindAll({param($node) $node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq$name},$true))
  if($matches.Count-ne1){throw 'Unique actual production helper required'}
  . ([scriptblock]::Create($matches[0].Extent.Text))
}
. (Join-Path $PSScriptRoot 'proposal/tools/internal/Capture-CMakeConfigureLog.ps1')
& $cmake @configure
if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
$evidence=Join-Path $PSScriptRoot 'actual-log-capture'
New-Item -ItemType Directory -Path $evidence -ErrorAction Stop | Out-Null
$capture=Copy-SouiCMakeConfigureLog -BuildDirectory $build -EvidenceDirectory $evidence
[IO.File]::WriteAllText((Join-Path $evidence 'binding.json'),($capture|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
& $cmake @configure
if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
$latest=ObsPin $capture.live.path
Need ($latest.sha256-cne$capture.live.sha256) 'A real subsequent fresh configure must change the shared log'
[void](MatchPin $capture.snapshot)
$refused=$false
try{[void](Copy-SouiCMakeConfigureLog -BuildDirectory $build -EvidenceDirectory $evidence)}catch{$refused=$_.Exception.Message-ceq'Fresh configure log snapshot required'}
Need $refused 'A second capture must not overwrite the first original'
[IO.File]::WriteAllText((Join-Path $evidence 'after.json'),($latest|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
Write-Output 'ACTUAL_CONFIGURE_LOG_CHANGED_AND_ORIGINAL_PRESERVED'
