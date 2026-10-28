[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [switch]$SdkOnly,
  [switch]$WithVendorTests,
  [switch]$SkipWeb,
  [ValidateSet('distribution','source-diagnostic')][string]$WebProfile='distribution',
  [switch]$WithWebView2,
  [switch]$WithAutoHost,
  [switch]$WithUia,
  [switch]$WithGuiBindings,
  [switch]$Install,
  [switch]$FreshConfigure,
  [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
  [string]$LlvmRoot,
  [string]$MsvcRoot,
  [string]$WindowsSdkRoot,
  [string]$NinjaExe,
  [string]$WindowsCmdExe,
  [string]$PowerShellExe
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if($SdkOnly -and $WebProfile -ne 'distribution'){throw 'Source diagnostics require an application Web build; omit SdkOnly.'}
if($WithVendorTests -and ($SdkOnly -or (Test-Path -LiteralPath (Join-Path $root 'project.json')))){
  throw 'WithVendorTests requires the source devkit root; omit SdkOnly and do not use a generated application.'
}
. (Join-Path $PSScriptRoot 'internal/Find-Toolchain.ps1')
. (Join-Path $PSScriptRoot 'internal/CMake-Configure.ps1')
$llvm=$null
$externalPaths=@($LlvmRoot,$MsvcRoot,$WindowsSdkRoot,$NinjaExe,$WindowsCmdExe,$PowerShellExe)
if($NativeToolchain -eq 'MSVC143'){
  if(@($externalPaths|Where-Object{$_}).Count){throw 'LLVM paths require explicit NativeToolchain LLVM22MSVC142.'}
  $toolchain=Get-SouiToolchain
}else{
  if(@($externalPaths|Where-Object{[string]::IsNullOrWhiteSpace($_)}).Count){throw 'LLVM22MSVC142 requires all six explicit tool paths.'}
  . (Join-Path $PSScriptRoot 'internal/Find-LlvmToolchain.ps1')
  $llvm=Get-SouiLlvmToolchain -Architecture $Architecture -LlvmRoot $LlvmRoot -MsvcRoot $MsvcRoot `
    -WindowsSdkRoot $WindowsSdkRoot -NinjaExe $NinjaExe -WindowsCmdExe $WindowsCmdExe -PowerShellExe $PowerShellExe `
    -ToolIdentityBridge (Join-Path $PSScriptRoot 'internal/Invoke-ToolFileIdentity.ps1') `
    -ToolIdentityHelper (Join-Path $PSScriptRoot 'internal/Tool-FileIdentity.ps1')
}
# The LLVM branch owns process-local provider cleanup; it never changes host registry or global environment.
$llvmEnvironment=@{}
$webProfilePrevious=Get-Item -LiteralPath 'Env:SOUI_WEB_BUILD_PROFILE' -ErrorAction SilentlyContinue
$webProfileEnvironment=@{present=($null-ne$webProfilePrevious);value=if($null-ne$webProfilePrevious){[string]$webProfilePrevious.Value}else{$null}}
$llvmRemove=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE',
  'CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','OBJC_INCLUDE_PATH',
  'LIBRARY_PATH','CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_USER_DIR','CLANG_CONFIG_FILE_SYSTEM_DIR',
  'CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM','CMAKE_GENERATOR_TOOLSET','CC','CXX','AR','RC','CFLAGS','CXXFLAGS','LDFLAGS',
  'DESTDIR','NINJA_STATUS','RCFLAGS','EXTERNAL_INCLUDE','CMAKE_BUILD_TYPE','CMAKE_TOOLCHAIN_FILE','CMAKE_PREFIX_PATH')
if($llvm){
  foreach($name in @($llvmRemove)+@('ComSpec','VcpkgEnabled','VcpkgEnableManifest','MSBUILDDISABLENODEREUSE')){
    $item=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    $llvmEnvironment[$name]=@{present=($null-ne$item);value=if($null-ne$item){[string]$item.Value}else{$null}}
  }
}
try {
$env:SOUI_WEB_BUILD_PROFILE=$WebProfile
if($llvm){
  foreach($name in $llvmRemove){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(Test-Path -LiteralPath ('Env:'+$name)){throw "Could not clear LLVM provider: $name"}}
  $env:ComSpec=$llvm.NativeProcessPaths.CMD
  $env:VcpkgEnabled='false';$env:VcpkgEnableManifest='false';$env:MSBUILDDISABLENODEREUSE='1'
}
if($llvm){
  $cmake=@(Get-Command cmake.exe -CommandType Application -ErrorAction Stop)[0]
  $node=@(Get-Command node.exe -CommandType Application -ErrorAction Stop)[0]
  $npm=@(Get-Command npm.cmd -CommandType Application -ErrorAction Stop)[0]
}else{
  $cmake=Get-Command cmake.exe -ErrorAction Stop
  $node=Get-Command node.exe -ErrorAction Stop
  $npm=Get-Command npm.cmd -ErrorAction Stop
}
$generated=Test-Path -LiteralPath (Join-Path $root 'project.json')
if($generated){
  $projectMetadata=Get-Content -Raw -LiteralPath (Join-Path $root 'project.json') | ConvertFrom-Json
  if($projectMetadata.runtimeProfile -eq 'modern'){$WithWebView2=$true}
  if($projectMetadata.runtimeProfile -eq 'auto'){$WithAutoHost=$true}
}
if($WithAutoHost){
  $WithWebView2=$true
}
if(Test-Path -LiteralPath (Join-Path $root 'sdk-lock.json')) {
  & $node.Source (Join-Path $root 'tools/sdk/soui-sdk.cjs') verify-project --project $root
  if($LASTEXITCODE -ne 0){throw 'Pinned SDK was modified; install a reviewed SDK version or restore its files.'}
}
if(-not $SdkOnly){
  $actionRoots=if($generated){@($root)}else{@((Join-Path $root 'templates/desktop-app'),(Join-Path $root 'templates/modern-app'))}
  if($WithAutoHost -and -not $generated){$actionRoots+=Join-Path $root 'templates/auto-app'}
  $actionManagedPaths=@('contracts/generated/actiongen-manifest.json','app/generated/actions.hpp',
    'web/src/generated/actions.ts','contracts/generated/tools.json','docs/generated/actions.md')
  foreach($actionRoot in $actionRoots){
    $actionSource=Join-Path $actionRoot 'contracts/actions.json'
    if(-not(Test-Path -LiteralPath $actionSource)){
      $actionManaged=$false
      foreach($actionPath in $actionManagedPaths){
        if(Test-Path -LiteralPath (Join-Path $actionRoot $actionPath)){$actionManaged=$true;break}
      }
      if(-not $generated -or $actionManaged){throw "Action source is missing: $actionSource"}
      continue
    }
    & $node.Source (Join-Path $root 'tools/sdk/actiongen.cjs') --source $actionSource --root $actionRoot --check
    if($LASTEXITCODE -ne 0){throw 'Generated actions are missing or stale; regenerate the application actions before building.'}
  }
}
$output=Join-Path $root "out/$(if($SdkOnly){'sdk-'}else{''})$Architecture"
$source=if($SdkOnly){Join-Path $root 'sdk'}else{$root}
$configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake.Source -WorkspaceRoot $root -SourceDirectory $source `
  -BuildDirectory $output -Architecture $Architecture -FreshConfigure:$FreshConfigure `
  -NativeToolchain $NativeToolchain -LlvmToolchain $llvm)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$evidence=Join-Path $root 'tools/internal/build-evidence.cjs'
$evidenceArgs=@($Architecture,$Configuration)
if($SdkOnly){$evidenceArgs+='sdk-only'}
$evidenceArgs+=@('--web-profile',$WebProfile)
if($WithAutoHost){$evidenceArgs+=@('--auto-host','enabled')}
$webEvidenceArgs=@($evidenceArgs)
if($llvm){
  $requestPath=Join-Path $output "llvm-profile-request-$Configuration.json"
  $toolchainPath=Join-Path $root 'cmake/toolchains/SouiLLVM22.cmake'
  $request=[ordered]@{schema='soui.build-toolchain-request/v1';profile=$NativeToolchain;architecture=$Architecture;
    discovery=$llvm;toolchain=@{path=$toolchainPath;bytes=(Get-Item -LiteralPath $toolchainPath).Length;
      sha256=(Get-FileHash -LiteralPath $toolchainPath -Algorithm SHA256).Hash.ToLowerInvariant()}}
  [IO.File]::WriteAllText($requestPath,($request|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
  $evidenceArgs+=@('--native-toolchain',$NativeToolchain,'--profile-request',$requestPath)
}
& $node.Source $evidence start @evidenceArgs
if($LASTEXITCODE -ne 0){throw 'Build identity initialization failed'}
if(-not $SkipWeb) {
  $webRoots=@('sdk/web')
  if($WithGuiBindings){
    & $node.Source (Join-Path $root 'tools/sdk/gui-bindings-delivery.cjs') build --root $root
    if($LASTEXITCODE -ne 0){throw 'Optional GUI bindings build failed'}
    $webRoots=@()
  }
  if(-not $SdkOnly){$webRoots+=if($generated){@('web')}else{@('templates/desktop-app/web','templates/modern-app/web','demos/workbench/web')}}
  if($WithAutoHost -and -not $SdkOnly -and -not $generated){$webRoots+='templates/auto-app/web'}
  foreach($relative in $webRoots){
    $web=Join-Path $root $relative
    Write-Output "Building Web package: $relative"
    $ErrorActionPreference='Continue'
    & $npm.Source --prefix $web ci --no-audit --no-fund *> (Join-Path $output ($relative.Replace('/','-')+'-install.log'))
    $ErrorActionPreference='Stop'
    if($LASTEXITCODE -ne 0){throw "npm ci failed for $relative; inspect $output"}
    $ErrorActionPreference='Continue'
    if(($generated -and $relative -eq 'web') -or $relative -eq 'templates/auto-app/web'){
      $webProject=if($generated){$root}else{Join-Path $root 'templates/auto-app'}
      & $node.Source (Join-Path $root 'tools/internal/web-targets.cjs') build $webProject *> (Join-Path $output ($relative.Replace('/','-')+'-build.log'))
    }else{
      & $npm.Source --prefix $web run build *> (Join-Path $output ($relative.Replace('/','-')+'-build.log'))
    }
    $ErrorActionPreference='Stop'
    if($LASTEXITCODE -ne 0){throw "Web build failed for $relative; inspect $output"}
  }
}
& $node.Source $evidence $(if($SkipWeb){'verify-web'}else{'record-web'}) @webEvidenceArgs
if($LASTEXITCODE -ne 0){throw 'Web source/output identity verification failed'}
if($WithGuiBindings){
  & $node.Source (Join-Path $root 'tools/sdk/gui-bindings-delivery.cjs') verify --root $root
  if($LASTEXITCODE -ne 0){throw 'Optional GUI bindings identity verification failed'}
}
$optional=@('-DSOUI_BUILD_WEBVIEW2=OFF')
if($WithWebView2){
  $webview2Package=& (Join-Path $PSScriptRoot 'internal/Restore-WebView2.ps1')
  $optional=@('-DSOUI_BUILD_WEBVIEW2=ON',"-DSOUI_WEBVIEW2_SDK_ROOT=$webview2Package")
}
$optional+=('-DSOUI_BUILD_AUTO_HOST='+$(if($WithAutoHost){'ON'}else{'OFF'}))
$optional+=('-DSOUI_BUILD_UIA='+$(if($WithUia){'ON'}else{'OFF'}))
if($WithVendorTests){$optional+=@('-DSOUI_BUILD_TESTS=ON','-DSOUI_BUILD_VENDOR_TESTS=ON')}
& $cmake.Source @configure @optional
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
$buildArguments=@('--build',$output,'--config',$Configuration,'--parallel','2')
if($llvm){$buildArguments+='--verbose'}
& $cmake.Source @buildArguments *> (Join-Path $output "native-$Configuration.log")
if($LASTEXITCODE -ne 0){throw "Native build failed; inspect $output/native-$Configuration.log"}
if($WithWebView2){& (Join-Path $PSScriptRoot 'internal/Restore-WebView2.ps1') | Out-Null}
if($Install){
  & $cmake.Source --install $output --config $Configuration --prefix (Join-Path $root "out/install/$Architecture")
  if($LASTEXITCODE -ne 0){throw 'SDK install failed'}
}
if($WithGuiBindings){
  & $node.Source (Join-Path $root 'tools/sdk/gui-bindings-delivery.cjs') verify --root $root
  if($LASTEXITCODE -ne 0){throw 'Optional GUI bindings changed during native build'}
}
if(-not $SdkOnly){
  # POST_BUILD does not run when native objects are unchanged. A Web profile
  # switch must still replace the resources before recording the payload.
  & $node.Source $evidence deploy-web @webEvidenceArgs
  if($LASTEXITCODE -ne 0){throw 'Web resource profile deployment failed'}
}
& $node.Source $evidence finish @evidenceArgs
if($LASTEXITCODE -ne 0){throw 'Build payload recording failed'}
Write-Output "Build passed: $output / $Configuration"
} finally {
  $restoreErrors=[Collections.Generic.List[string]]::new()
  try {
    if($webProfileEnvironment.present){Set-Item -LiteralPath 'Env:SOUI_WEB_BUILD_PROFILE' -Value $webProfileEnvironment.value -ErrorAction Stop}
    else {Remove-Item -LiteralPath 'Env:SOUI_WEB_BUILD_PROFILE' -ErrorAction SilentlyContinue}
    $actualWebProfile=Get-Item -LiteralPath 'Env:SOUI_WEB_BUILD_PROFILE' -ErrorAction SilentlyContinue
    if(($null-ne$actualWebProfile)-ne$webProfileEnvironment.present-or
       ($webProfileEnvironment.present-and([string]$actualWebProfile.Value-cne$webProfileEnvironment.value))){throw 'Presence/value differs'}
  }catch{$restoreErrors.Add('SOUI_WEB_BUILD_PROFILE: '+$_.Exception.Message)}
  foreach($name in $llvmEnvironment.Keys){
    try {
      $state=$llvmEnvironment[$name]
      if($state.present){Set-Item -LiteralPath ('Env:'+$name) -Value $state.value -ErrorAction Stop}else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}
      $actual=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
      if(($null-ne$actual)-ne$state.present-or($state.present-and([string]$actual.Value-cne$state.value))){throw 'Presence/value differs'}
    }catch{$restoreErrors.Add($name+': '+$_.Exception.Message)}
  }
  if($restoreErrors.Count){throw ('LLVM build environment restore failed: '+($restoreErrors-join'; '))}
}
