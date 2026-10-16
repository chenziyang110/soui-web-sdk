'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',work=__dirname,e=require(root+'/tools/internal/dependency-evidence.cjs');
const files=['sdk/accessibility/uia/windows_uia_provider.cpp','sdk/include/soui/runtime/accessibility/windows_uia_provider.h',
  'tests/native/windows_uia_provider_tests.cpp','tests/native/uia/CMakeLists.txt'];
for(const relative of files)for(const area of ['before','proposal']){
  const target=path.join(work,area,relative);fs.mkdirSync(path.dirname(target),{recursive:true});
  fs.copyFileSync(path.join(root,relative),target,fs.constants.COPYFILE_EXCL);
}
const test=path.join(work,'proposal',files[2]);let body=fs.readFileSync(test,'utf8');
assert(body.includes('void com_initialization_case()'));
body=body.replace('void com_initialization_case()',fs.readFileSync(work+'/events-test.inc','utf8')+'\nvoid com_initialization_case()');
body=body.replace('{"com-initialization", com_initialization_case}', '{"property-events", property_events_case}, {"com-initialization", com_initialization_case}');
fs.writeFileSync(test,body);
for(const relative of ['cmake/toolchains/SouiLLVM22.cmake','tools/internal/Tool-FileIdentity.ps1','tools/internal/Invoke-ToolFileIdentity.ps1']){
  fs.mkdirSync(path.dirname(work+'/'+relative),{recursive:true});fs.copyFileSync(root+'/'+relative,work+'/'+relative,fs.constants.COPYFILE_EXCL);
}
fs.mkdirSync(work+'/native');
fs.writeFileSync(work+'/native/CMakeLists.txt',`cmake_minimum_required(VERSION 3.29)
project(SouiUiaEventProbe LANGUAGES CXX)
set(PROVIDER_SOURCE "" CACHE FILEPATH "Exact baseline or proposed provider")
add_executable(uia_event_probe "${work}/proposal/tests/native/windows_uia_provider_tests.cpp"
  "${root}/sdk/src/runtime/bridge/action_contract.cpp" "\${PROVIDER_SOURCE}")
target_include_directories(uia_event_probe PRIVATE "${root}/sdk/include" "${root}/third_party/include/json")
target_compile_features(uia_event_probe PRIVATE cxx_std_17)
target_compile_definitions(uia_event_probe PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_compile_options(uia_event_probe PRIVATE /U_DEBUG /utf-8 /W4)
target_link_libraries(uia_event_probe PRIVATE ole32 oleaut32 user32 gdi32)
set_property(TARGET uia_event_probe PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
`);
let build=fs.readFileSync('C:/work/sm05-legal-text-fix-1/build-native3.ps1','utf8');
build="param([ValidateSet('before','proposal')][string]$Provider='before',[ValidateSet('x64','x86')][string]$Architecture='x64',[string]$Attempt='red1')\n"+build;
build=build.replaceAll('-Architecture x64','-Architecture $Architecture').replace("'out/native-build3'","('out/'+$Architecture+'-'+$Attempt)");
build=build.replace("'-DSouiSdk_DIR=C:/work/sm05-release-050/out/install/x64/lib/cmake/SouiSdk'","('-DPROVIDER_SOURCE='+(Join-Path $PSScriptRoot ($Provider+'/sdk/accessibility/uia/windows_uia_provider.cpp')))");
fs.writeFileSync(work+'/build.ps1',build);
fs.writeFileSync(work+'/INPUTS.json',JSON.stringify({source:e.pin(root+'/source-manifest.json'),files:files.map(p=>({relative:p,pin:e.pin(root+'/'+p)})),
 actionContract:e.pin(root+'/sdk/src/runtime/bridge/action_contract.cpp'),currentVersionQualified:false},null,2),{flag:'wx'});
