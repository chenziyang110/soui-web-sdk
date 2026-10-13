[CmdletBinding()]
param(
  [string]$ExpectedVersion='',
  [string]$PreviousPackage='',
  [string]$WorkspaceParent='',
  [ValidateSet('x64','Win32')][string]$Architecture='x64',
  [switch]$FreshConfigure,
  [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
  [string]$LlvmRoot,[string]$MsvcRoot,[string]$WindowsSdkRoot,
  [string]$NinjaExe,[string]$WindowsCmdExe,[string]$PowerShellExe,[string]$DevkitRoot
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdkArchitecture=if($Architecture-eq'Win32'){'x86'}else{'x64'}
$llvmPaths=[ordered]@{LlvmRoot=$LlvmRoot;MsvcRoot=$MsvcRoot;WindowsSdkRoot=$WindowsSdkRoot;NinjaExe=$NinjaExe;WindowsCmdExe=$WindowsCmdExe;PowerShellExe=$PowerShellExe}
if($NativeToolchain-eq'LLVM22MSVC142'){
  foreach($value in $llvmPaths.Values){if([string]::IsNullOrWhiteSpace($value)-or$value-notmatch'^[A-Za-z]:[\\/]'-or$value-match'[;"\x00-\x1f]'){throw 'LLVM release validation requires all six explicit safe tool paths'}}
  if(-not$DevkitRoot-or$DevkitRoot-notmatch'^[A-Za-z]:[\\/]'-or$DevkitRoot-match'[;"\x00-\x1f]'){throw 'LLVM release validation requires a separate explicit stock DevkitRoot'}
  $DevkitRoot=[IO.Path]::GetFullPath($DevkitRoot)
  if([string]::Equals($DevkitRoot,$root,[StringComparison]::OrdinalIgnoreCase)){throw 'LLVM stock T and historical MSVC stock R must be distinct'}
}elseif(@($llvmPaths.Values|Where-Object{$_}).Count-or$PSBoundParameters.ContainsKey('DevkitRoot')){throw 'LLVM paths and DevkitRoot require explicit LLVM22MSVC142'}
. (Join-Path $PSScriptRoot 'internal/CMake-Configure.ps1')
$version=([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
if(-not $ExpectedVersion){$ExpectedVersion=$version}
if($version -ne $ExpectedVersion){throw "Freeze VERSION at $ExpectedVersion before release verification (found $version)."}
$node=(Get-Command node.exe -ErrorAction Stop).Source
$cmake=(Get-Command cmake.exe -ErrorAction Stop).Source
$powershell=(Get-Command powershell.exe -ErrorAction Stop).Source
if($NativeToolchain-eq'LLVM22MSVC142'){$powershell=$PowerShellExe}
$helper=Join-Path $root 'tools/internal/release-verification.cjs'
$package=Join-Path $root "dist/sdk/$version"
if(-not $PreviousPackage){$PreviousPackage=Join-Path $root 'dist/sdk/0.3.0-preview.10'}
if(-not $WorkspaceParent){$WorkspaceParent=[IO.Path]::GetFullPath((Join-Path $root '..\..'))}
$runId=[Guid]::NewGuid().ToString('N')
$workspace=[IO.Path]::GetFullPath((Join-Path $WorkspaceParent "soui-release-$sdkArchitecture-$runId"))
$output=Join-Path $root "out/release-validation/$sdkArchitecture/$runId"
$runFile=Join-Path $output 'run.json'
function Invoke-Json([string[]]$Arguments){
  $text=@(& $node $helper @Arguments)
  if($LASTEXITCODE -ne 0){throw 'Release evidence validation failed.'}
  return (($text -join "`n") | ConvertFrom-Json)
}
function Invoke-Checked([string]$Executable,[string[]]$Arguments,[string]$Log){
  & $Executable @Arguments *> $Log
  if($LASTEXITCODE -ne 0){throw "Validation command failed; inspect $Log"}
}
function Build-And-Smoke([string]$Project,[string]$Phase){
  Write-Output "Building independent project: $Phase"
  $compileProject=$Project
  $llvmPhase=$NativeToolchain-eq'LLVM22MSVC142'-and$Phase-notin@('baseline','rollback')
  if($NativeToolchain-eq'LLVM22MSVC142'){$prepared=Invoke-Json @('prepare-phase',$runFile,$Phase);$compileProject=[string]$prepared.project}
  if($llvmPhase){
    $arguments=@((Join-Path $root 'tools/internal/standard-build.cjs'),'--root',$compileProject,'--devkit-root',$DevkitRoot,
      '--architecture',$sdkArchitecture,'--configuration','Release','--native-toolchain',$NativeToolchain,'--fresh-configure')
    $flags=[ordered]@{LlvmRoot='--llvm-root';MsvcRoot='--msvc-root';WindowsSdkRoot='--windows-sdk-root';NinjaExe='--ninja-exe';WindowsCmdExe='--windows-cmd-exe';PowerShellExe='--powershell-exe'}
    foreach($entry in $flags.GetEnumerator()){$arguments+=@($entry.Value,[string]$llvmPaths[$entry.Key])}
    Invoke-Checked $node $arguments (Join-Path $output "$Phase-build.log")
  }else{
  $Project=$compileProject
  $buildEntry=Join-Path $Project 'tools/Build.ps1'
  $refresh=@()
  if($FreshConfigure){
    if((Get-Command -Name $buildEntry -ErrorAction Stop).Parameters.ContainsKey('FreshConfigure')){
      $refresh=@('-FreshConfigure')
    } else {
      # Frozen historical scripts cannot accept the new switch or version=
      # platform. Validate ownership using today's helper, then initialize
      # their original platform with the same actual-SDK assertion.
      $legacyConfigure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $Project -SourceDirectory $Project `
        -BuildDirectory (Join-Path $Project ("out/"+$sdkArchitecture)) -Architecture $sdkArchitecture -FreshConfigure)
      $legacyConfigure[[Array]::IndexOf($legacyConfigure,'-A')+1]=$Architecture
      Write-Output "Explicitly refreshing historical configuration: $Phase; selected SDK is checked in the configure log"
      Invoke-Checked $cmake $legacyConfigure (Join-Path $output "$Phase-legacy-configure.log")
    }
  }
  Invoke-Checked $powershell (@('-NoProfile','-ExecutionPolicy','Bypass','-File',
    $buildEntry,'-Architecture',$sdkArchitecture,'-Configuration','Release')+$refresh) (Join-Path $output "$Phase-build.log")
  }
  # The current helper validates frozen historical builds without replacing their tools.
  # The actual PowerShell entry holds the lease; the nonce only prevents accidental use.
  $mutex=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
  $acquired=$false
  $noncePresent=Test-Path -LiteralPath 'Env:SOUI_RELEASE_DESKTOP_NONCE'
  $previousNonce=if($noncePresent){(Get-Item -LiteralPath 'Env:SOUI_RELEASE_DESKTOP_NONCE').Value}else{$null}
  try{
    try{$acquired=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{
      $acquired=$true # The exception transfers ownership; release it in finally, then fail.
      throw 'Abandoned desktop validation lease refused; retain this attempt.'
    }
    if(-not $acquired){throw 'Another desktop validation is running; retain this attempt.'}
    $env:SOUI_RELEASE_DESKTOP_NONCE=[Guid]::NewGuid().ToString('N')
    Invoke-Json @('smoke',$runFile,$Phase,$env:SOUI_RELEASE_DESKTOP_NONCE) | ConvertTo-Json -Compress
  }finally{
    try{
      if($noncePresent){Set-Item -LiteralPath 'Env:SOUI_RELEASE_DESKTOP_NONCE' -Value $previousNonce}
      else{Remove-Item -LiteralPath 'Env:SOUI_RELEASE_DESKTOP_NONCE' -ErrorAction SilentlyContinue}
      $restoredPresent=Test-Path -LiteralPath 'Env:SOUI_RELEASE_DESKTOP_NONCE'
      if($restoredPresent-ne$noncePresent-or($noncePresent-and-not[string]::Equals([string](Get-Item -LiteralPath 'Env:SOUI_RELEASE_DESKTOP_NONCE').Value,[string]$previousNonce,[StringComparison]::Ordinal))){
        throw 'Release desktop nonce environment was not exactly restored.'
      }
    }finally{
      try{if($acquired){$mutex.ReleaseMutex()}}finally{$mutex.Dispose()}
    }
  }
  Invoke-Json @('record',$runFile,$Phase) | ConvertTo-Json -Compress
}
function Install-Sdk([string]$SdkPackage,[string]$Project){
  & $node (Join-Path $root 'tools/sdk/soui-sdk.cjs') install --package $SdkPackage --project $Project
  if($LASTEXITCODE -ne 0){throw 'SDK upgrade or rollback failed; retain its recovery evidence.'}
}
$releaseEnvironment=@{}
try{
if($NativeToolchain-eq'LLVM22MSVC142'){
  $providers=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE',
    'CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','OBJC_INCLUDE_PATH',
    'LIBRARY_PATH','CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_USER_DIR','CLANG_CONFIG_FILE_SYSTEM_DIR',
    'CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM','CMAKE_GENERATOR_TOOLSET','CC','CXX','AR','RC','CFLAGS','CXXFLAGS','LDFLAGS',
    'DESTDIR','NINJA_STATUS','RCFLAGS','EXTERNAL_INCLUDE','CMAKE_BUILD_TYPE','CMAKE_TOOLCHAIN_FILE','CMAKE_PREFIX_PATH',
    'ComSpec','VcpkgEnabled','VcpkgEnableManifest','MSBUILDDISABLENODEREUSE')
  foreach($name in $providers){$item=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;$releaseEnvironment[$name]=@{present=($null-ne$item);value=if($null-ne$item){[string]$item.Value}else{$null}}}
  foreach($name in $providers){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(Test-Path -LiteralPath ('Env:'+$name)){throw ('Could not clear release provider: '+$name)}}
  $env:ComSpec=$WindowsCmdExe;$env:VcpkgEnabled='false';$env:VcpkgEnableManifest='false';$env:MSBUILDDISABLENODEREUSE='1'
  New-Item -ItemType Directory -Force -Path $output|Out-Null
  $profileFile=Join-Path $output 'requested-profile.json'
  $requested=[ordered]@{schema='soui.release-requested-profile/v1';profile=$NativeToolchain;toolsRoot=$DevkitRoot;legacyToolsRoot=$root;paths=$llvmPaths;qualificationPassed=$false;releaseReady=$false}
  [IO.File]::WriteAllText($profileFile,($requested|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
  # These two actual stock directories remain independent for the entire run.
  Invoke-Checked $powershell (@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $root 'tools/internal/Build-ValidationTools.ps1'),'-Architecture',$sdkArchitecture,'-NativeToolchain','MSVC143')+$(if($FreshConfigure){@('-FreshConfigure')}else{@()})) (Join-Path $output 'legacy-stock-build.log')
  $stock=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $DevkitRoot 'tools/internal/Build-ValidationTools.ps1'),'-Architecture',$sdkArchitecture,'-NativeToolchain',$NativeToolchain)
  foreach($entry in $llvmPaths.GetEnumerator()){$stock+=@('-'+$entry.Key,[string]$entry.Value)}
  if($FreshConfigure){$stock+='-FreshConfigure'}
  Invoke-Checked $PowerShellExe $stock (Join-Path $output 'llvm-stock-build.log')
}
$beginArgs=@('begin',$runFile,$workspace,$package,[IO.Path]::GetFullPath($PreviousPackage),$sdkArchitecture)
if($NativeToolchain-eq'LLVM22MSVC142'){$beginArgs+=$profileFile}
$begin=Invoke-Json $beginArgs
Write-Output "Release validation workspace: $($begin.workspace)"
$fresh=Join-Path $workspace 'fresh'
Invoke-Checked $powershell @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $root 'tools/New-Project.ps1'),
  '-Name','ReleaseFreshCheck','-OutputRoot',$fresh,'-SdkPackage',$package) (Join-Path $output 'fresh-create.log')
Build-And-Smoke $fresh 'fresh'
$upgrade=Join-Path $workspace 'upgrade'
Invoke-Checked $powershell @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $root 'tools/New-Project.ps1'),
  '-Name','ReleaseUpgradeCheck','-OutputRoot',$upgrade,'-SdkPackage',$PreviousPackage) (Join-Path $output 'upgrade-create.log')
# This retained business fixture uses preview.9's existing public API so that
# rollback is a meaningful supported application, independent of new template APIs.
$fixtureNative=@'
#include <windows.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include <json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <stdexcept>
namespace {
struct fixture {
  soui::sdk::soui_miniblink_host host{std::make_unique<soui::sdk::dynamic_miniblink_adapter>()};
  std::filesystem::path log;
  ULONGLONG started=GetTickCount64();
  bool closing=false;
  int exit_code=0;
  void append(const char* value){std::ofstream(log,std::ios::app)<<value<<'\n';}
};
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
  auto* state=reinterpret_cast<fixture*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE){state=static_cast<fixture*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
  if(state){
    if(message==soui::sdk::bridge_event_message){state->host.deliver_posted_event(reinterpret_cast<void*>(lparam));return 0;}
    if(message==WM_SIZE){(void)state->host.resize(std::max(1,static_cast<int>(LOWORD(lparam))),std::max(1,static_cast<int>(HIWORD(lparam))));return 0;}
    if(message==WM_CLOSE){state->closing=true;return 0;}
    if(message==WM_TIMER){
      state->host.pump();
      if(!state->closing && state->host.application_ready()){state->append("application-ready");state->closing=true;}
      if(!state->closing && GetTickCount64()-state->started>20000){state->append("application-timeout");state->exit_code=2;state->closing=true;}
      if(state->closing){
        const auto closed=state->host.destroy();
        if(closed.ok){state->append("host-close-complete");KillTimer(window,1);DestroyWindow(window);PostQuitMessage(state->exit_code);}
        else if(GetTickCount64()-state->started>25000){state->append("host-close-failed");DestroyWindow(window);PostQuitMessage(3);}
      }
      return 0;
    }
  }
  if(message==WM_NCDESTROY)SetWindowLongPtrW(window,GWLP_USERDATA,0);
  return DefWindowProcW(window,message,wparam,lparam);
}
int run(HINSTANCE instance){
  std::wstring executable(32768,L'\0');
  const DWORD length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
  if(!length || length>=executable.size())return 4;
  executable.resize(length);
  const auto root=std::filesystem::path(executable).parent_path();
  soui::sdk::json_bridge bridge("app://release-upgrade");
  // Bridge outlives Host even when startup or teardown fails.
  fixture state;state.log=root/"smoke.log";std::ofstream(state.log)<<"process-start\n";
  SetEnvironmentVariableW(L"SOUI_GPU_COMPOSITOR",nullptr);SetEnvironmentVariableW(L"SOUI_GPU_DIRECT_PRESENT",nullptr);
  if(!bridge.register_method("app.greet","app.greet",[](std::string_view input){
    const auto value=nlohmann::json::parse(input,nullptr,false);
    if(!value.is_object() || value.size()!=1 || !value.contains("name") || !value["name"].is_string() || value["name"].get_ref<const std::string&>().size()>128)
      return std::string{"{\"error\":\"E_INVALID_PARAMS\",\"message\":\"Expected a bounded name.\"}"};
    return nlohmann::json{{"message","Hello, "+value["name"].get<std::string>()+"."}}.dump();
  }))return 5;
  WNDCLASSW klass{};klass.lpfnWndProc=window_proc;klass.hInstance=instance;klass.lpszClassName=L"SouiReleaseUpgradeFixture";
  if(!RegisterClassW(&klass))return 6;
  HWND window=CreateWindowW(klass.lpszClassName,L"ReleaseUpgradeCheck",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,900,600,nullptr,nullptr,instance,&state);
  if(!window)return 7;
  soui::sdk::host_config config;
#if defined(_WIN64)
  config.architecture="x64";
#else
  config.architecture="x86";
#endif
  config.miniblink_binary=root/"node.dll";
  config.resource_root=root/"resources";config.owner_window=window;config.bridge_adapter=&bridge;
  const auto mounted=state.host.start(config);
  if(!mounted.ok){state.append(mounted.code.c_str());DestroyWindow(window);return 8;}
  state.append("host-started");ShowWindow(window,SW_HIDE);SetTimer(window,1,16,nullptr);
  MSG message{};BOOL status;
  while((status=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
  return status<0?9:static_cast<int>(message.wParam);
}
}
int APIENTRY wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
  const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(FAILED(com))return 10;
  int result=1;try{result=run(instance);}catch(const std::exception&){}
  const auto shutdown=soui::sdk::shutdown_miniblink_runtime();if(!shutdown.ok && result==0)result=11;
  CoUninitialize();return result;
}
'@
$fixtureWeb=@'
import {connectSouiNative, type SouiCoreMethodMap} from '@soui-sdk';
import './style.css';
type Methods = SouiCoreMethodMap & {'app.greet':{params:{name:string};result:{message:string}}};
const status=document.getElementById('app');
void connectSouiNative<Methods>().then(async client=>{
  const methods=await client.discover({pageSize:2});
  if(!methods.methods.some(method=>method.name==='app.greet'))throw new Error('Missing greet service');
  const result=await client.call('app.greet',{name:'release-upgrade'});
  if(result.message!=='Hello, release-upgrade.')throw new Error('Unexpected native response');
  if(status)status.textContent=result.message;
  await client.call('bridge.probe.report',{ready:true});
}).catch(error=>{if(status)status.textContent=String(error);});
'@
[IO.File]::WriteAllText((Join-Path $upgrade 'app/main.cpp'),$fixtureNative,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $upgrade 'web/src/main.tsx'),$fixtureWeb,[Text.UTF8Encoding]::new($false))
$fixtureCmake=@'
cmake_minimum_required(VERSION 3.24)
project(ReleaseUpgradeCheck VERSION 0.1.0 LANGUAGES C CXX RC)
add_subdirectory(sdk)
include(cmake/SouiApplication.cmake)
soui_add_application(ReleaseUpgradeCheck ROOT "${CMAKE_CURRENT_SOURCE_DIR}" SOURCES app/main.cpp)
'@
[IO.File]::WriteAllText((Join-Path $upgrade 'CMakeLists.txt'),$fixtureCmake,[Text.UTF8Encoding]::new($false))
# Other template source files may import this version's new public types. The
# compatibility fixture owns its entire web/src tree; keep only its main and CSS.
foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $upgrade 'web/src') -File)){
  if($file.Name -notin @('main.tsx','style.css')){Remove-Item -LiteralPath $file.FullName}
}
Invoke-Json @('business',$runFile) | ConvertTo-Json -Compress
Build-And-Smoke $upgrade 'baseline'
Install-Sdk $package $upgrade
Build-And-Smoke $upgrade 'upgrade'
Install-Sdk $PreviousPackage $upgrade
Build-And-Smoke $upgrade 'rollback'
Install-Sdk $package $upgrade
Build-And-Smoke $upgrade 'reupgrade'
$relocated=Join-Path $workspace 'relocated'
if(Test-Path -LiteralPath $relocated){throw 'Relocation destination already exists.'}
New-Item -ItemType Directory -Path $relocated | Out-Null
foreach($relative in @(Invoke-Json @('copy-files',$runFile))){
  $destination=Join-Path $relocated $relative
  New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
  Copy-Item -LiteralPath (Join-Path $fresh $relative) -Destination $destination
}
Build-And-Smoke $relocated 'relocation'
Write-Output 'Building installed SDK and external CMake consumer'
$refresh=if($FreshConfigure){@('-FreshConfigure')}else{@()}
if($NativeToolchain-eq'LLVM22MSVC142'){
  $sdkArgs=@((Join-Path $root 'tools/internal/standard-build.cjs'),'--root',$root,'--devkit-root',$DevkitRoot,'--architecture',$sdkArchitecture,'--configuration','Release','--sdk-only','--install','--native-toolchain',$NativeToolchain)
  $flags=[ordered]@{LlvmRoot='--llvm-root';MsvcRoot='--msvc-root';WindowsSdkRoot='--windows-sdk-root';NinjaExe='--ninja-exe';WindowsCmdExe='--windows-cmd-exe';PowerShellExe='--powershell-exe'}
  foreach($entry in $flags.GetEnumerator()){$sdkArgs+=@($entry.Value,[string]$llvmPaths[$entry.Key])};if($FreshConfigure){$sdkArgs+='--fresh-configure'}
  Invoke-Checked $node $sdkArgs (Join-Path $output 'installed-sdk-build.log')
}else{
Invoke-Checked $powershell (@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $root 'tools/Build.ps1'),
  '-Architecture',$sdkArchitecture,'-Configuration','Release','-SdkOnly','-Install')+$refresh) (Join-Path $output 'installed-sdk-build.log')
}
$consumer=Join-Path $output 'consumer'
if($NativeToolchain-eq'LLVM22MSVC142'){
  . (Join-Path $root 'tools/internal/Find-LlvmToolchain.ps1')
  $profile=Get-SouiLlvmToolchain -Architecture $sdkArchitecture @llvmPaths -ToolIdentityBridge (Join-Path $root 'tools/internal/Invoke-ToolFileIdentity.ps1') -ToolIdentityHelper (Join-Path $root 'tools/internal/Tool-FileIdentity.ps1')
  New-Item -ItemType Directory -Path $consumer|Out-Null
  $sdkRequestPath=Join-Path $root "out/sdk-$sdkArchitecture/llvm-profile-request-Release.json"
  if((Get-Item -LiteralPath $sdkRequestPath).Length-gt1048576){throw 'SDK request exceeds1MiB'}
  $sdkToolchain=([IO.File]::ReadAllText($sdkRequestPath)|ConvertFrom-Json).toolchain
  $toolchain=Join-Path $root 'cmake/toolchains/SouiLLVM22.cmake';$values=@{path=$toolchain;bytes=(Get-Item -LiteralPath $toolchain).Length;sha256=(Get-FileHash -LiteralPath $toolchain -Algorithm SHA256).Hash.ToLowerInvariant()}
  $names=@($sdkToolchain.PSObject.Properties.Name);if($names.Count-ne3-or@($names|Where-Object{$_-cnotin@('path','bytes','sha256')}).Count){throw 'Fixed SDK toolchain keys required'}
  $ownToolchain=[ordered]@{};foreach($name in $names){$ownToolchain[$name]=$values[$name]}
  $request=[ordered]@{schema='soui.build-toolchain-request/v1';profile=$NativeToolchain;architecture=$sdkArchitecture;discovery=$profile;toolchain=$ownToolchain}
  [IO.File]::WriteAllText((Join-Path $consumer 'llvm-profile-request-Release.json'),($request|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
  Invoke-Json @('prepare-consumer',$runFile)|ConvertTo-Json -Compress
  $configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $root -SourceDirectory (Join-Path $root 'tests/package-consumer') -BuildDirectory $consumer -Architecture $sdkArchitecture -FreshConfigure -NativeToolchain $NativeToolchain -LlvmToolchain $profile)
  $consumerInstall=Join-Path $root ("out/install/"+$sdkArchitecture)
  $configure+=@("-DSouiSdk_DIR=$consumerInstall/lib/cmake/SouiSdk")
  $command=[ordered]@{schema='soui.installed-acceptance-llvm-configure/v1';cmake=$cmake;args=$configure}
  [IO.File]::WriteAllText((Join-Path $output 'consumer-llvm-configure.json'),($command|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
  Invoke-Checked $node @((Join-Path $root 'tools/internal/llvm-installed-acceptance.cjs'),'build-release',$output) (Join-Path $output 'consumer-controller.log')
}else{
$configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $root -SourceDirectory (Join-Path $root 'tests/package-consumer') `
  -BuildDirectory $consumer -Architecture $sdkArchitecture -FreshConfigure:$FreshConfigure)
Invoke-Checked $cmake ($configure+@("-DSouiSdk_DIR=$(Join-Path $root ("out/install/"+$sdkArchitecture+"/lib/cmake/SouiSdk"))")) (Join-Path $output 'consumer-configure.log')
Invoke-Checked $cmake @('--build',$consumer,'--config','Release','--parallel','2') (Join-Path $output 'consumer-build.log')
}
Invoke-Json @('consumer',$runFile) | ConvertTo-Json -Compress
Invoke-Json @('complete',$runFile) | ConvertTo-Json -Depth 12
Write-Output "Independent release validation passed; original evidence retained in $output"
}finally{
  $errors=[Collections.Generic.List[string]]::new();foreach($name in $releaseEnvironment.Keys){try{$state=$releaseEnvironment[$name];if($state.present){Set-Item -LiteralPath ('Env:'+$name) -Value $state.value -ErrorAction Stop}else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue};$current=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(($null-ne$current)-ne$state.present-or($state.present-and[string]$current.Value-cne$state.value)){throw 'Presence/value differs'}}catch{$errors.Add($name+': '+$_.Exception.Message)}}
  if($errors.Count){throw ('Release provider environment restore failed: '+($errors-join'; '))}
}
