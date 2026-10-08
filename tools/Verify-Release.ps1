[CmdletBinding()]
param(
  [string]$ExpectedVersion='0.3.0-preview.10',
  [string]$PreviousPackage='',
  [string]$WorkspaceParent=''
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version=([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
if($version -ne $ExpectedVersion){throw "Freeze VERSION at $ExpectedVersion before release verification (found $version)."}
$node=(Get-Command node.exe -ErrorAction Stop).Source
$cmake=(Get-Command cmake.exe -ErrorAction Stop).Source
$powershell=(Get-Command powershell.exe -ErrorAction Stop).Source
$helper=Join-Path $root 'tools/internal/release-verification.cjs'
$package=Join-Path $root "dist/sdk/$version"
if(-not $PreviousPackage){$PreviousPackage=Join-Path $root 'dist/sdk/0.3.0-preview.9'}
if(-not $WorkspaceParent){$WorkspaceParent=[IO.Path]::GetFullPath((Join-Path $root '..\..'))}
$runId=[Guid]::NewGuid().ToString('N')
$workspace=[IO.Path]::GetFullPath((Join-Path $WorkspaceParent "soui-release-$runId"))
$output=Join-Path $root "out/release-validation/$runId"
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
  Invoke-Checked $powershell @('-NoProfile','-ExecutionPolicy','Bypass','-File',
    (Join-Path $Project 'tools/Build.ps1'),'-Architecture','x64','-Configuration','Release') (Join-Path $output "$Phase-build.log")
  $evidence=Join-Path $Project 'tools/internal/build-evidence.cjs'
  $metadataText=@(& $node $evidence begin-smoke x64 Release)
  if($LASTEXITCODE -ne 0){throw 'Generated project build identity failed.'}
  $metadata=($metadataText -join "`n") | ConvertFrom-Json
  $target=[string]$metadata.scenarios[0].target
  $bin=Join-Path $Project "out/x64/bin/Release/$target"
  $log=Join-Path $bin 'smoke.log'
  if(Test-Path -LiteralPath $log){Remove-Item -LiteralPath $log}
  $mutex=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
  $acquired=$false
  $process=$null
  try{
    try{$acquired=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true}
    if(-not $acquired){throw 'Another desktop validation is running; retain this attempt and retry after it finishes.'}
    $process=Start-Process -FilePath (Join-Path $bin "$target.exe") -WorkingDirectory $bin -WindowStyle Hidden -ArgumentList '--smoke-test' -PassThru
    $finished=$process.WaitForExit(45000)
    if(-not $finished){$process.Kill();$process.WaitForExit()}
    $process.Refresh()
    $text=if(Test-Path -LiteralPath $log){[IO.File]::ReadAllText($log)}else{''}
    $passed=$finished -and $process.ExitCode -eq 0 -and $text.Contains('application-ready') -and $text.Contains('host-close-complete')
    $run=[ordered]@{target=$target;scenario='default';passed=$passed;exitCode=$process.ExitCode;timedOut=(-not $finished);log=$text}
    $report=[ordered]@{schema=$metadata.schema;buildId=$metadata.buildId;sourceSha256=$metadata.sourceSha256;
      version=$metadata.version;startedAt=$metadata.startedAt;architecture='x64';configuration='Release';
      releaseReady=$false;runs=@($run);passed=$false}
    [IO.File]::WriteAllText((Join-Path $Project 'out/x64/smoke-Release.json'),($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
  }finally{
    if($process){$process.Dispose()}
    if($acquired){$mutex.ReleaseMutex()}
    $mutex.Dispose()
  }
  & $node $evidence finish-smoke x64 Release
  if($LASTEXITCODE -ne 0){throw "Independent project startup failed: $Phase"}
  Invoke-Json @('record',$runFile,$Phase) | ConvertTo-Json -Compress
}
function Install-Sdk([string]$SdkPackage,[string]$Project){
  & $node (Join-Path $root 'tools/sdk/soui-sdk.cjs') install --package $SdkPackage --project $Project
  if($LASTEXITCODE -ne 0){throw 'SDK upgrade or rollback failed; retain its recovery evidence.'}
}
$begin=Invoke-Json @('begin',$runFile,$workspace,$package,[IO.Path]::GetFullPath($PreviousPackage))
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
  soui::sdk::host_config config;config.architecture="x64";config.miniblink_binary=root/"node.dll";
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
Invoke-Checked $powershell @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $root 'tools/Build.ps1'),
  '-Architecture','x64','-Configuration','Release','-SdkOnly','-Install') (Join-Path $output 'installed-sdk-build.log')
$consumer=Join-Path $output 'consumer'
Invoke-Checked $cmake @('-S',(Join-Path $root 'tests/package-consumer'),'-B',$consumer,'-G','Visual Studio 17 2022','-A','x64',
  '-DCMAKE_SYSTEM_VERSION=10.0.19041.0',"-DSouiSdk_DIR=$(Join-Path $root 'out/install/x64/lib/cmake/SouiSdk')") (Join-Path $output 'consumer-configure.log')
Invoke-Checked $cmake @('--build',$consumer,'--config','Release','--parallel','2') (Join-Path $output 'consumer-build.log')
Invoke-Json @('consumer',$runFile) | ConvertTo-Json -Compress
Invoke-Json @('complete',$runFile) | ConvertTo-Json -Depth 12
Write-Output "Independent release validation passed; original evidence retained in $output"
