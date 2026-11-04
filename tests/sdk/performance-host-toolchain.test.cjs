'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {spawnSync}=require('node:child_process');

test('performance consumer executes the actual SDK guard for Visual Studio and explicit LLVM',t=>{
  const repository=path.resolve(__dirname,'../..');
  const source=fs.readFileSync(path.join(repository,'benchmarks/sdk-performance-host/CMakeLists.txt'),'utf8');
  const begin=source.indexOf('if(NOT WIN32 OR NOT MSVC)');
  const end=source.indexOf('set(MATCHED_SDK_ROOT ',begin);
  assert.ok(begin>=0&&end>begin,'consumer toolchain guard boundaries must exist');
  const guard=source.slice(begin,end);
  const parent=fs.realpathSync(os.tmpdir());
  const folder=fs.mkdtempSync(path.join(parent,'soui-perf-toolchain-'));
  t.after(()=>{assert.equal(path.dirname(folder),parent);fs.rmSync(folder,{recursive:true,force:true});});
  const cases=[
    {name:'visual-studio-sdk',sdk:'10.0.19041.0',pass:true},
    {name:'visual-studio-wrong-sdk',sdk:'10.0.22621.0',pass:false},
    {name:'llvm',profile:'LLVM22MSVC142',generator:'Ninja Multi-Config',system:'10.0.19041.0',verifier:'ok',pass:true,calls:1},
    {name:'llvm-missing-product-toolchain',profile:'LLVM22MSVC142',generator:'Ninja Multi-Config',system:'10.0.19041.0',pass:false},
    {name:'llvm-wrong-sdk',profile:'LLVM22MSVC142',generator:'Ninja Multi-Config',system:'10.0.22621.0',verifier:'ok',pass:false},
    {name:'llvm-wrong-generator',profile:'LLVM22MSVC142',generator:'Ninja',system:'10.0.19041.0',verifier:'ok',pass:false},
    {name:'llvm-invalid-actual-language',profile:'LLVM22MSVC142',generator:'Ninja Multi-Config',system:'10.0.19041.0',verifier:'fail',pass:false,calls:1},
    {name:'llvm-vs-output-cannot-select-sdk',profile:'LLVM22MSVC142',generator:'Ninja Multi-Config',sdk:'10.0.19041.0',system:'10.0.22621.0',verifier:'ok',pass:false},
    {name:'unselected-llvm-function',generator:'Ninja Multi-Config',system:'10.0.19041.0',verifier:'ok',pass:false}
  ];
  for(const item of cases){
    const script=path.join(folder,item.name+'.cmake');
    const witness=path.join(folder,item.name+'.txt').replaceAll('\\','/');
    const verifier=item.verifier?`function(soui_llvm22_verify_project)\nfile(APPEND "${witness}" "verified\\n")\n${item.verifier==='fail'?'message(FATAL_ERROR "actual language verification failed")':''}\nendfunction()\n`:'';
    fs.writeFileSync(script,`set(WIN32 TRUE)\nset(MSVC TRUE)\nset(SOUI_NATIVE_TOOLCHAIN "${item.profile||'MSVC143'}")\nset(CMAKE_GENERATOR "${item.generator||'Visual Studio 17 2022'}")\nset(CMAKE_SYSTEM_VERSION "${item.system||''}")\nset(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "${item.sdk||''}")\n${verifier}${guard}`);
    const result=spawnSync('cmake',['-P',script],{encoding:'utf8',windowsHide:true,timeout:15000});
    assert.ifError(result.error);
    assert.equal(result.status===0,item.pass,`${item.name}: ${result.stdout}\n${result.stderr}`);
    const calls=fs.existsSync(witness)?fs.readFileSync(witness,'utf8').trim().split('\n').length:0;
    assert.equal(calls,item.calls||0,item.name+' must execute the actual verifier only on the admitted LLVM branch');
  }
});
