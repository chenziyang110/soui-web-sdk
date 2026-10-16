$ErrorActionPreference='Stop'
$candidate='C:/work/sm05-release-050'
$proposed=Join-Path $PSScriptRoot 'proposed/tools/Verify-Release.ps1'
$tokens=$null;$parseErrors=$null
$null=[Management.Automation.Language.Parser]::ParseFile($proposed,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw ($parseErrors|Out-String)}
$text=[IO.File]::ReadAllText($proposed)
$split=$text.IndexOf(". (Join-Path `$PSScriptRoot 'internal/CMake-Configure.ps1')")
if($split -lt 0){throw 'Exact preflight boundary missing'}
$preflight=Join-Path $PSScriptRoot 'preflight-fragment.ps1'
$fragment=$text.Substring(0,$split)+"`n[pscustomobject]@{modern=`$DevkitRoot;legacy=`$legacyToolsRoot}"
$stream=[IO.File]::Open($preflight,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes($fragment);$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
$argsValid=@{NativeToolchain='LLVM22MSVC142';DevkitRoot=$candidate;LegacyDevkitRoot='C:/work/sm05-release-stock21-1/source';LlvmRoot='C:/tools/llvm';MsvcRoot='C:/tools/msvc';WindowsSdkRoot='C:/tools/sdk';NinjaExe='C:/tools/ninja.exe';WindowsCmdExe='C:/Windows/System32/cmd.exe';PowerShellExe='C:/tools/pwsh.exe'}
$actual=& $preflight @argsValid
if($actual.modern-ne[IO.Path]::GetFullPath($candidate)-or$actual.legacy-ne[IO.Path]::GetFullPath($argsValid.LegacyDevkitRoot)){throw 'Explicit owners changed'}
$cases=@(
 @{NativeToolchain='MSVC143';LegacyDevkitRoot=$candidate},
 @{NativeToolchain='LLVM22MSVC142';DevkitRoot=$candidate;LegacyDevkitRoot=$candidate;LlvmRoot='C:/tools/llvm';MsvcRoot='C:/tools/msvc';WindowsSdkRoot='C:/tools/sdk';NinjaExe='C:/tools/ninja.exe';WindowsCmdExe='C:/Windows/System32/cmd.exe';PowerShellExe='C:/tools/pwsh.exe'}
)
foreach($bad in @('','relative','C:/bad;path',"C:/bad`npath")){$case=$argsValid.Clone();$case.LegacyDevkitRoot=$bad;$cases+=,$case}
foreach($case in $cases){$rejected=$false;try{$null=& $preflight @case}catch{$rejected=$true};if(-not$rejected){throw 'Unsafe preflight accepted'}}
[pscustomobject]@{wholeScriptParsed=$true;actualPreflightPasses=1;actualPreflightRejections=$cases.Count;sourceCandidateChanged=$false}|ConvertTo-Json
