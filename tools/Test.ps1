[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64',
      [ValidateSet('Debug','Release')][string]$Configuration='Release',
      [switch]$NativeOnly,
      [switch]$RequireVendorTests,
      [switch]$UnitOnly,
      [switch]$WithGuiBindings,
      [switch]$WithDisplayConfiguration,
      [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
      [string]$LlvmRoot,[string]$MsvcRoot,[string]$WindowsSdkRoot,
      [string]$NinjaExe,[string]$WindowsCmdExe,[string]$PowerShellExe)
$ErrorActionPreference='Stop'
if($NativeOnly -and $UnitOnly){throw 'NativeOnly and UnitOnly cannot be used together.'}
if($NativeOnly -and $RequireVendorTests){throw 'RequireVendorTests cannot be used with NativeOnly; native regressions require unit and CTest execution.'}
if($NativeOnly -and $WithGuiBindings){throw 'WithGuiBindings requires unit tests; omit NativeOnly.'}
if($NativeOnly -and $WithDisplayConfiguration){throw 'WithDisplayConfiguration requires prepared unit tools; omit NativeOnly.'}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=Join-Path $root "out/$Architecture"
$evidence=Join-Path $root 'tools/internal/build-evidence.cjs'
$profileArgs=@();$validationProfile=@{};$llvmSaved=@{};$llvmPrivateInputs=@();$llvmRequest=$null
$llvmRemove=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE',
  'CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','OBJC_INCLUDE_PATH',
  'LIBRARY_PATH','CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_USER_DIR','CLANG_CONFIG_FILE_SYSTEM_DIR',
  'CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM','CMAKE_GENERATOR_TOOLSET','CC','CXX','AR','RC','CFLAGS','CXXFLAGS','LDFLAGS',
  'DESTDIR','NINJA_STATUS','RCFLAGS','EXTERNAL_INCLUDE','CMAKE_BUILD_TYPE','CMAKE_TOOLCHAIN_FILE','CMAKE_PREFIX_PATH')
$externalInputs=@($LlvmRoot,$MsvcRoot,$WindowsSdkRoot,$NinjaExe,$WindowsCmdExe,$PowerShellExe)
if($NativeToolchain-eq'MSVC143'){
  if(@($externalInputs|Where-Object{$_}).Count){throw 'LLVM paths require explicit NativeToolchain LLVM22MSVC142.'}
}else{
  if(@($externalInputs|Where-Object{[string]::IsNullOrWhiteSpace($_)}).Count){throw 'LLVM Test requires all six explicit external inputs.'}
  $llvmRequest=Join-Path $output "llvm-profile-request-$Configuration.json"
  foreach($name in @($llvmRemove)+@('ComSpec','VcpkgEnabled','VcpkgEnableManifest','MSBUILDDISABLENODEREUSE',
      'SOUI_OWNED_PROCESS_RUNNER','BENCHMARK_OWNED_RUNNER_EXE','SOUI_TEST_DESKTOP_NONCE')){
    $item=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    $llvmSaved[$name]=@{present=($null-ne$item);value=if($null-ne$item){[string]$item.Value}else{$null}}
  }
}
try{
if($NativeToolchain-eq'LLVM22MSVC142'){
  foreach($name in $llvmRemove){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(Test-Path -LiteralPath ('Env:'+$name)){throw "Cannot clear LLVM Test environment: $name"}}
  $env:ComSpec=[IO.Path]::GetFullPath($WindowsCmdExe)
  $env:VcpkgEnabled='false';$env:VcpkgEnableManifest='false';$env:MSBUILDDISABLENODEREUSE='1'
  $profileHelper=Join-Path $root 'tools/internal/llvm-profile-inputs.cjs'
  & node $profileHelper request $llvmRequest $Architecture $root | Out-Null
  if($LASTEXITCODE-ne0){throw 'The existing normal Build LLVM request is missing or stale.'}
  $request=Get-Content -LiteralPath $llvmRequest -Raw|ConvertFrom-Json
  $definitionInputs=@{SOUI_LLVM_ROOT=$LlvmRoot;SOUI_MSVC142_ROOT=$MsvcRoot;SOUI_WINDOWS_SDK_ROOT=$WindowsSdkRoot;
    SOUI_NINJA_EXECUTABLE=$NinjaExe;SOUI_WINDOWS_CMD_EXECUTABLE=$WindowsCmdExe;SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE=$PowerShellExe}
  foreach($entry in $definitionInputs.GetEnumerator()){
    $actual=[IO.Path]::GetFullPath($entry.Value).Replace([char]92,[char]47).TrimEnd('/')
    if($request.discovery.CMakeDefinitions.($entry.Key)-cne$actual){throw "Test requested LLVM input differs from normal Build: $($entry.Key)"}
  }
  $profileArgs=@('--toolchain-request',$llvmRequest)
  $validationProfile=@{NativeToolchain=$NativeToolchain;LlvmRoot=$LlvmRoot;MsvcRoot=$MsvcRoot;WindowsSdkRoot=$WindowsSdkRoot;
    NinjaExe=$NinjaExe;WindowsCmdExe=$WindowsCmdExe;PowerShellExe=$PowerShellExe;ToolchainRequest=$llvmRequest}
  & node $evidence verify-build $Architecture $Configuration --native-toolchain LLVM22MSVC142 --profile-request $llvmRequest | Out-Null
  if($LASTEXITCODE-ne0){throw 'LLVM tests require the current normal Build payload and profile evidence.'}
  & node (Join-Path $root 'tools/internal/llvm-build-observer.cjs') verify $root $Architecture $Configuration applications $llvmRequest | Out-Null
  if($LASTEXITCODE-ne0){throw 'LLVM tests require a naturally closed, accepted standard Build observation.'}
}
if(-not $NativeOnly){
  $ctestApplications=@(Get-Command ctest.exe -CommandType Application -ErrorAction Stop)
  if($ctestApplications.Count -lt 1){throw 'CTest application is missing.'}
  $ctestPath=$ctestApplications[0].Source
  $vendorHelper=Join-Path $root 'tools/internal/vendor-tests.cjs'
  $vendorJson=& node $vendorHelper inspect $root $Architecture $Configuration $ctestPath $RequireVendorTests.IsPresent.ToString().ToLowerInvariant() @profileArgs
  if($LASTEXITCODE -ne 0){throw 'Configured vendor/native test registry verification failed.'}
  $vendorState=$vendorJson | ConvertFrom-Json
  if($vendorState.mode -eq 'OFF'){
    Write-Output 'Private vendor regressions and private ABI source comparison were not evaluated (SOUI_BUILD_VENDOR_TESTS=OFF). Public Host and SDK tests remain enabled.'
  }else{
    Write-Output 'All 54 vendor tests are registered; private ABI comparison and actual CTest execution are still required.'
    if($NativeToolchain-eq'LLVM22MSVC142'){
      $llvmPrivateText=& node $vendorHelper required-source $root
      if($LASTEXITCODE-ne0){throw 'The private vendor source contract is incomplete.'}
      $llvmPrivateInputs=@($llvmPrivateText|ConvertFrom-Json)
      if($llvmPrivateInputs.Count-ne27){throw 'Vendor ON requires all 27 actual private source inputs.'}
    }
  }
  & node --test (Join-Path $root 'tests/sdk/gui-bindings-delivery.test.cjs') (Join-Path $root 'tests/sdk/gui-bindings-entrypoints.test.cjs') (Join-Path $root 'tests/sdk/vendor-tests.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Optional GUI delivery boundary tests failed'}
  if($WithGuiBindings){
    & node (Join-Path $root 'tools/sdk/gui-bindings-delivery.cjs') test --root $root
    if($LASTEXITCODE -ne 0){throw 'Optional GUI bindings tests failed'}
  }
  if($NativeToolchain-eq'LLVM22MSVC142'){
    $referenceTestPrevious=Get-Item -LiteralPath 'Env:SOUI_REFERENCE_TEST_PROFILE_REQUEST' -ErrorAction SilentlyContinue
    $referenceTestPresent=($null-ne$referenceTestPrevious)
    $referenceTestValue=if($referenceTestPresent){[string]$referenceTestPrevious.Value}else{$null}
    try{
      $env:SOUI_REFERENCE_TEST_PROFILE_REQUEST=$llvmRequest
      & node --test (Join-Path $root 'tests/sdk/llvm-profile-inputs.test.cjs') (Join-Path $root 'tests/benchmark/llvm-measurement-observation.test.cjs') (Join-Path $root 'tests/sdk/llvm-measurement-observation.test.cjs') (Join-Path $root 'tests/sdk/llvm-reference-sdk-configuration.test.cjs')
      if($LASTEXITCODE-ne0){throw 'LLVM requested-input, installed-header, Ninja reader or real consumer configuration regressions failed'}
    }finally{
      if($referenceTestPresent){$env:SOUI_REFERENCE_TEST_PROFILE_REQUEST=$referenceTestValue}
      else{Remove-Item -LiteralPath 'Env:SOUI_REFERENCE_TEST_PROFILE_REQUEST' -ErrorAction SilentlyContinue}
    }
    # Frozen before source is a regression comparison, not current compiler evidence.
    $crtResultDirectory=Join-Path $output ('llvm-crt-regression-'+[Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($crtResultDirectory) | Out-Null
    & $PowerShellExe -NoLogo -NoProfile -File (Join-Path $root 'tests/sdk/llvm-crt-options.Tests.ps1') -BeforeObserverPath (Join-Path $root 'tests/fixtures/llvm-crt/before-Observe-LlvmBuild.ps1') -OutputPath (Join-Path $crtResultDirectory 'result.json')
    if($LASTEXITCODE-ne0){throw 'LLVM CRT option regressions failed'}
    $webviewResultDirectory=Join-Path $output ('llvm-webview2-dependencies-'+[Guid]::NewGuid().ToString('N'))
    & $PowerShellExe -NoLogo -NoProfile -File (Join-Path $root 'tests/sdk/llvm-webview2-dependencies.Tests.ps1') -OutputRoot $webviewResultDirectory
    if($LASTEXITCODE-ne0){throw 'LLVM authenticated WebView2 dependency regressions failed'}
    & $PowerShellExe -NoLogo -NoProfile -File (Join-Path $root 'tests/sdk/llvm-binary-command-index.Tests.ps1')
    if($LASTEXITCODE-ne0){throw 'LLVM binary command index regressions failed'}
    & $PowerShellExe -NoLogo -NoProfile -File (Join-Path $root 'tests/sdk/soui-llvm-entry.Tests.ps1')
    if($LASTEXITCODE-ne0){throw 'SOUI LLVM public entry boundary regressions failed'}
  }
  $validationRunner=& (Join-Path $PSScriptRoot 'internal/Build-ValidationTools.ps1') -Architecture $Architecture @validationProfile
  if($LASTEXITCODE -ne 0){throw 'Owned process supervision tools build failed'}
  $previousOwnedRunner=$env:SOUI_OWNED_PROCESS_RUNNER
  $previousBenchmarkRunner=$env:BENCHMARK_OWNED_RUNNER_EXE
  try{
    $env:SOUI_OWNED_PROCESS_RUNNER=$validationRunner
    $env:BENCHMARK_OWNED_RUNNER_EXE=$validationRunner
    & node --test (Join-Path $root 'tests/benchmark/owned-process-runner.test.cjs') (Join-Path $root 'tests/benchmark/owned-process-client.test.cjs') (Join-Path $root 'tests/benchmark/owned-process-client-boundaries.test.cjs') (Join-Path $root 'tests/acceptance/owned-supervision-boundaries.test.cjs') (Join-Path $root 'tests/benchmark/run-one-cleanup.test.cjs') (Join-Path $root 'tests/sdk/cmake-sdk-selection.test.cjs')
    if($LASTEXITCODE -ne 0){throw 'Owned supervision or SDK selection tests failed'}
  }finally{
    $env:SOUI_OWNED_PROCESS_RUNNER=$previousOwnedRunner
    $env:BENCHMARK_OWNED_RUNNER_EXE=$previousBenchmarkRunner
  }
  & node --test (Join-Path $root 'tests/sdk/actiongen.test.mjs') (Join-Path $root 'tests/web/generated-actions.test.mjs')
  if($LASTEXITCODE -ne 0){throw 'Single-source action generation or generated Web contract tests failed'}
  & node --test (Join-Path $root 'tests/web/task-query.test.mjs')
  if($LASTEXITCODE -ne 0){throw 'Read-only task query contract tests failed'}
  & node (Join-Path $root 'sdk/web/node_modules/typescript/bin/tsc') -p (Join-Path $root 'tests/web/task-query-types.tsconfig.json')
  if($LASTEXITCODE -ne 0){throw 'Task query mutually exclusive parameter types failed'}
  & node (Join-Path $root 'tests/sdk/sdk-package-smoke.cjs')
  if($LASTEXITCODE -ne 0){throw 'SDK package tests failed'}
  & node --test (Join-Path $root 'tests/sdk/sdk-native-payload.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'SDK native payload tests failed'}
  & node --test (Join-Path $root 'tests/sdk/installation-interruption.test.cjs') (Join-Path $root 'tests/sdk/profile-installation-integration.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'SDK installation interruption tests failed'}
  & node --test (Join-Path $root 'tests/sdk/delivery-identity.test.cjs') (Join-Path $root 'tests/sdk/legal-notice-materials.test.cjs') (Join-Path $root 'tests/sdk/release-verification.test.cjs') (Join-Path $root 'tests/sdk/release-stock-routing.test.cjs') (Join-Path $root 'tests/sdk/sdk-build-inputs.test.cjs') (Join-Path $root 'tests/sdk/verification-profile.test.cjs') (Join-Path $root 'tests/sdk/build-source-manifest.test.cjs') (Join-Path $root 'tests/sdk/stage-soui-source.test.cjs') (Join-Path $root 'tests/native/miniblink_public_host.test.cjs') (Join-Path $root 'tests/sdk/llvm-evidence-readers.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Delivery identity or public Host tests failed'}
  & node --test (Join-Path $root 'tests/sdk/dependency-evidence.test.cjs') (Join-Path $root 'tests/sdk/dependency-input-archive.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Dependency source/runtime evidence regressions failed'}
  & node --test (Join-Path $root 'tests/calibration/reader.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Native TaskStore calibration reader regressions failed'}
  if($vendorState.mode -eq 'ON'){
    & node --test (Join-Path $root 'tests/native/miniblink_header_abi.test.cjs')
    if($LASTEXITCODE -ne 0){throw 'Private Miniblink source ABI comparison failed'}
  }
  & node --test (Join-Path $root 'tests/sdk/modern-evidence.test.cjs') (Join-Path $root 'tests/sdk/application-package.test.cjs') (Join-Path $root 'tests/sdk/console-json.test.cjs') (Join-Path $root 'tests/sdk/auto-smoke.test.cjs') (Join-Path $root 'tests/sdk/auto-packaging.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Modern delivery evidence tests failed'}
  & node --test (Join-Path $root 'tests/sdk/web-targets.test.cjs') (Join-Path $root 'tests/sdk/web-targets-integration.test.cjs') (Join-Path $root 'tests/sdk/size-report.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Web target isolation, resource manifests or size reporting tests failed'}
  & node --test (Join-Path $root 'tests/sdk/runtime-deployment.test.cjs') (Join-Path $root 'tests/sdk/runtime-bundle.test.cjs') (Join-Path $root 'tests/sdk/runtime-packaging-integration.test.cjs') (Join-Path $root 'tests/sdk/runtime-build-evidence.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Runtime dependency manifests, bundle restoration or packaging integration failed'}
  & node --test (Join-Path $root 'tests/sdk/compression-profiles.test.cjs') (Join-Path $root 'tests/sdk/compression-command.test.cjs') (Join-Path $root 'tests/sdk/compression-measurement.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Compression profiles, compatible package commands or measurement contracts failed'}
  $acceptanceInstall=Join-Path $output ('acceptance-toolchain-'+[Guid]::NewGuid().ToString('N'))
  [IO.Directory]::CreateDirectory($acceptanceInstall) | Out-Null
  & npm.cmd --prefix (Join-Path $root 'tests/acceptance') ci --no-audit --no-fund *> (Join-Path $acceptanceInstall 'install.log')
  if($LASTEXITCODE -ne 0){throw "Acceptance reader toolchain install failed; inspect $acceptanceInstall"}
  $newReaderTests=@('tests/acceptance/process-address-census.test.cjs','tests/acceptance/unicode-copy.test.cjs','tests/acceptance/resource-validation.test.cjs','tests/acceptance/session-validation.test.cjs',
    'tests/acceptance/installed-host-runner.test.cjs','tests/acceptance/resource-input-reuse.test.cjs','tests/acceptance/llvm-resource-inputs.test.cjs',
    'tests/tooling/devkit-command.test.cjs','tests/tooling/devkit-delivery.test.cjs','tests/tooling/desktop-smoke.test.cjs') |
    ForEach-Object { Join-Path $root $_ }
  foreach($testFile in $newReaderTests){if(-not [IO.File]::Exists($testFile)){throw "Required adopted test is missing: $testFile"}}
  & node --test @newReaderTests
  if($LASTEXITCODE -ne 0){throw 'Resource, session, unified-entry or desktop Owned reader tests failed'}
  & node --test (Join-Path $root 'tests/web/sdk-client.test.mjs') (Join-Path $root 'tests/web/capabilities.test.mjs') (Join-Path $root 'tests/web/action-contracts.test.mjs') (Join-Path $root 'tests/web/gui.test.mjs') (Join-Path $root 'tests/web/native-resolvers.test.mjs') (Join-Path $root 'tests/acceptance/runner.test.cjs') (Join-Path $root 'tests/acceptance/monotonic-supervisor.test.cjs') (Join-Path $root 'tests/acceptance/mixed-validation.test.cjs') (Join-Path $root 'tests/acceptance/mixed-analysis.test.cjs') (Join-Path $root 'tests/acceptance/timeout-diagnostics.test.cjs') (Join-Path $root 'tests/benchmark/page-pair.test.cjs') (Join-Path $root 'tests/benchmark/run-traces.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Web SDK, acceptance, or benchmark protocol tests failed'}
  & node (Join-Path $root 'tools/internal/measurement-validation.cjs') protocol $Architecture @profileArgs
  if($LASTEXITCODE -ne 0){throw 'Measurement protocol reader regressions failed'}
  & node (Join-Path $root 'tools/internal/measurement-validation.cjs') native $Architecture @profileArgs
  if($LASTEXITCODE -ne 0){throw 'Native measurement identity or owned lifecycle regressions failed'}
  & node (Join-Path $root 'tools/internal/measurement-validation.cjs') tail-protocol $Architecture @profileArgs
  if($LASTEXITCODE -ne 0){throw 'Compiler-tail reader, client, policy, or standard caller regressions failed'}
  & node (Join-Path $root 'tools/internal/measurement-validation.cjs') tail-native $Architecture @profileArgs
  if($LASTEXITCODE -ne 0){throw 'Native compiler-tail evidence linkage regressions failed'}
  if($WithDisplayConfiguration){
    & node (Join-Path $root 'tools/internal/measurement-validation.cjs') display $Architecture @profileArgs
    if($LASTEXITCODE -ne 0){throw 'Current display configuration is unavailable or failed; this is not a presentation/performance result'}
  }
  & node --test (Join-Path $root 'tests/web/web-features.test.mjs')
  if($LASTEXITCODE -ne 0){throw 'Web runtime feature tests failed'}
  & node --test (Join-Path $root 'tests/web/session-renewal.test.mjs') (Join-Path $root 'tests/web/agent-session-renewal.test.mjs') (Join-Path $root 'tests/web/webview2-preload.test.mjs')
  if($LASTEXITCODE -ne 0){throw 'Web session renewal tests failed'}
  & node --test (Join-Path $root 'tests/acceptance/software-cache-budget.test.cjs') (Join-Path $root 'tests/acceptance/long-run-task-quota.test.cjs') (Join-Path $root 'tests/sdk/long-run-policy.test.cjs') (Join-Path $root 'tests/sdk/long-run-cache-observation.test.cjs') (Join-Path $root 'tests/acceptance/resource-validation.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Long-run quota, owner, cache or resource reader regressions failed'}
  if((Get-FileHash -LiteralPath $vendorState.cache -Algorithm SHA256).Hash.ToLowerInvariant() -cne $vendorState.cacheSha256){
    throw 'Native test configuration changed during unit tests; rerun against a stable build.'
  }
  if($llvmPrivateInputs.Count){
    $currentPrivateText=& node $vendorHelper required-source $root
    if($LASTEXITCODE-ne0){throw 'Private vendor source became unavailable during unit tests.'}
    $currentPrivate=@($currentPrivateText|ConvertFrom-Json)
    if($currentPrivate.Count-ne$llvmPrivateInputs.Count){throw 'Private vendor source membership changed during unit tests.'}
    for($i=0;$i-lt$llvmPrivateInputs.Count;$i++){
      if($currentPrivate[$i].path-cne$llvmPrivateInputs[$i].path-or$currentPrivate[$i].bytes-ne$llvmPrivateInputs[$i].bytes-or
        $currentPrivate[$i].sha256-cne$llvmPrivateInputs[$i].sha256){throw 'Private vendor source changed during unit tests.'}
    }
  }
  & $ctestPath --test-dir $output -C $Configuration --output-on-failure
  if($LASTEXITCODE -ne 0){throw 'Native SDK contract tests failed'}
  if($NativeToolchain-eq'LLVM22MSVC142'){
    & node $vendorHelper inspect $root $Architecture $Configuration $ctestPath $RequireVendorTests.IsPresent.ToString().ToLowerInvariant() @profileArgs | Out-Null
    if($LASTEXITCODE-ne0){throw 'LLVM native configuration or registry changed during CTest.'}
    if($llvmPrivateInputs.Count){
      $afterPrivateText=& node $vendorHelper required-source $root
      if($LASTEXITCODE-ne0){throw 'Private vendor source became unavailable during CTest.'}
      $afterPrivate=@($afterPrivateText|ConvertFrom-Json)
      if($afterPrivate.Count-ne$llvmPrivateInputs.Count){throw 'Private vendor source membership changed during CTest.'}
      for($i=0;$i-lt$llvmPrivateInputs.Count;$i++){
        if($afterPrivate[$i].path-cne$llvmPrivateInputs[$i].path-or$afterPrivate[$i].bytes-ne$llvmPrivateInputs[$i].bytes-or
          $afterPrivate[$i].sha256-cne$llvmPrivateInputs[$i].sha256){throw 'Private vendor source changed during CTest.'}
      }
    }
    & node (Join-Path $root 'tools/internal/llvm-build-observer.cjs') verify $root $Architecture $Configuration applications $llvmRequest | Out-Null
    if($LASTEXITCODE-ne0){throw 'LLVM standard Build observation changed during tests.'}
  }
}
if($UnitOnly){Write-Output 'Unit and contract tests passed; desktop smoke was intentionally skipped.';exit 0}
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
$previousDesktopNonce=$env:SOUI_TEST_DESKTOP_NONCE
try{
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true;throw 'Abandoned SOUI desktop validation lease'}
  if(-not $acquired){throw 'Another SOUI desktop validation is running.'}
  # This standalone entry owns one lease. Soui uses the same helper directly
  # under its existing outer lease instead of invoking this mutex-taking block.
  $env:SOUI_TEST_DESKTOP_NONCE=[Guid]::NewGuid().ToString('N')
  & node (Join-Path $root 'tools/internal/desktop-smoke.cjs') run $Architecture $Configuration $root @profileArgs
  if($LASTEXITCODE -ne 0){throw 'Native startup/bridge/close or natural Owned application cleanup failed; inspect retained smoke-owned attempt.'}
}finally{
  $env:SOUI_TEST_DESKTOP_NONCE=$previousDesktopNonce
  if($acquired){$lease.ReleaseMutex()}
  $lease.Dispose()
}
}finally{
  $restoreErrors=[Collections.Generic.List[string]]::new()
  foreach($name in $llvmSaved.Keys){try{
    $state=$llvmSaved[$name]
    if($state.present){Set-Item -LiteralPath ('Env:'+$name) -Value $state.value -ErrorAction Stop}else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}
    $actual=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    if(($null-ne$actual)-ne$state.present-or($state.present-and[string]$actual.Value-cne$state.value)){throw 'Presence/value differs'}
  }catch{$restoreErrors.Add($name+': '+$_.Exception.Message)}}
  if($restoreErrors.Count){throw ('LLVM Test environment restore failed: '+($restoreErrors-join'; '))}
}
