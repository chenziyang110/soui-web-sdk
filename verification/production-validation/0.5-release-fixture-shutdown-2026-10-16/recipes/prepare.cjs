'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root='C:/work/sm05-release-050',out=__dirname;
const original=fs.readFileSync(root+'/tools/Verify-Release.ps1','utf8');
const fixture=original.match(/\$fixtureNative=@'\r?\n([\s\S]*?)\r?\n'@/)[1];
const line='  const auto shutdown=soui::sdk::shutdown_miniblink_runtime();if(!shutdown.ok && result==0)result=11;';
assert.equal(fixture.split(line).length,2);
const replacement=`#if SOUI_SDK_VERSION_MAJOR > 0 || SOUI_SDK_VERSION_MINOR >= 5
  const auto shutdown=soui::sdk::wait_for_miniblink_runtime_shutdown(std::chrono::seconds(20));
#else
  const auto shutdown=soui::sdk::shutdown_miniblink_runtime();
#endif
  if(!shutdown.ok && result==0)result=11;`;
const fixed=fixture.replace('#include <soui/json_bridge.h>','#include <soui/json_bridge.h>\n#include <soui/version.h>\n#include <chrono>').replace(line,replacement);
const html=`<!doctype html><html><head><meta charset="utf-8"></head><body>Release fixture lifecycle regression<script>
(function(){
 var deadline=Date.now()+10000;
 function start(port){
  var first=JSON.parse(port(JSON.stringify({requestId:'release-bootstrap',sessionId:'',origin:'',token:'',method:'bridge.bootstrap',params:{protocol:'soui.bridge/1'}})));
  if(!first||!first.ok||!first.result)throw Error('bootstrap failed');
  var g=first.result;
  function call(id,method,params){return port(JSON.stringify({requestId:id,sessionId:g.sessionId,origin:g.origin,token:g.token,method:method,params:params,timeoutMs:5000}));}
  call('release-greet','app.greet',{name:'release-upgrade'}).then(function(raw){
   var value=JSON.parse(raw);if(!value.ok||value.result.message!=='Hello, release-upgrade.')throw Error('greet failed');
   return call('release-ready','bridge.probe.report',{ready:true});
  });
 }
 function wait(){if(window.SouiNative&&typeof window.SouiNative.postMessage==='function')return start(window.SouiNative.postMessage);if(Date.now()<deadline)setTimeout(wait,10);}
 wait();
})();</script></body></html>`;
for(const [variant,source]of [['red',fixture],['green',fixed]]){
 const directory=out+'/'+variant;fs.mkdirSync(directory,{recursive:true});fs.writeFileSync(directory+'/main.cpp',source,{flag:'wx'});fs.mkdirSync(directory+'/resources');fs.writeFileSync(directory+'/resources/index.html',html,{flag:'wx'});
 fs.writeFileSync(directory+'/CMakeLists.txt',`cmake_minimum_required(VERSION 3.24)
project(ReleaseFixtureRegression LANGUAGES CXX)
find_package(SouiSdk 0.5.0 EXACT CONFIG REQUIRED)
add_executable(ReleaseFixtureRegression WIN32 main.cpp)
target_link_libraries(ReleaseFixtureRegression PRIVATE Soui::Sdk "\${SOUI_PROBE_ZLIB}")
target_compile_features(ReleaseFixtureRegression PRIVATE cxx_std_17)
target_compile_options(ReleaseFixtureRegression PRIVATE /EHsc /W4 /utf-8 /U_DEBUG)
target_compile_definitions(ReleaseFixtureRegression PRIVATE NDEBUG _CRT_SECURE_NO_WARNINGS)
set_property(TARGET ReleaseFixtureRegression PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
`,{flag:'wx'});
}
fs.writeFileSync(out+'/Verify-Release.ps1',original.replace(fixture,fixed),{flag:'wx'});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(out+'/DECISION.json',JSON.stringify({originalFile:root+'/tools/Verify-Release.ps1',originalSha256:sha(Buffer.from(original)),oldFixtureSha256:sha(Buffer.from(fixture)),newFixtureSha256:sha(Buffer.from(fixed)),expectedRedExit:11,expectedGreenExit:0,scope:'Exact embedded C++ release fixture against preserved Source19 SDK and freshly rebuilt current kernel; isolated raw bridge page verifies business call and application-ready. Full actual package upgrade/rollback remains required.',candidateChanged:false},null,2),{flag:'wx'});
console.log('Prepared exact original/fixed fixture and proposed script without modifying candidate.');
