'use strict';
// SOURCE-only tests. All SDK/Observer/measurement data below are unit fixtures,
// never actual compiler, executable, Native ownership or whole-closure evidence.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),vm=require('node:vm');
const source=fs.readFileSync(path.join(__dirname,'runner.cjs'),'utf8');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function fixture({accepted=false,changedRequest=false,changedBuild=false}={}) {
  const root=path.resolve(__dirname,'../..'),suite=path.join(root,'out','acceptance','profile-unit-only');
  const request=path.join(root,'out','sdk-x86','llvm-profile-request-Release.json');
  const original=Buffer.from('{"unit":"request-original"}\n');
  const current=changedRequest?Buffer.from('{"unit":"request-changed"}\n'):original;
  const filePin=bytes=>({path:request.replaceAll('\\','/'),bytes:bytes.length,sha256:hash(bytes)});
  const pending={architecture:'x86',configuration:'Release',buildId:'unit-build',sourceSha256:'unit-source',
    nativeToolchain:{profile:'LLVM22MSVC142',requestFile:filePin(original)}};
  const build={buildId:changedBuild?'another-unit-build':'unit-build',source:{sha256:'unit-source'},
    nativeToolchain:{profile:'LLVM22MSVC142',requestFile:filePin(current)}};
  const measurementFile=path.join(root,'out','benchmark-protocol-x86','measurement-tools.json');
  const measureBytes=Buffer.from('{"unit":"prepared-report-not-native-proof"}\n');
  const calls={writes:0,accepted:0,default:0,measurement:[]};
  const io={
    statSync(filename){assert.ok([request,measurementFile].includes(filename));return {isFile:()=>true,size:(filename===request?current:measureBytes).length};},
    readFileSync(filename){assert.ok([request,measurementFile].includes(filename));return filename===request?current:measureBytes;},
    mkdirSync(){++calls.writes;throw Error('Negative fixture reached output mutation');},
    writeFileSync(){++calls.writes;throw Error('Negative fixture reached output mutation');}
  };
  const identity={safePath:value=>path.resolve(value),hash,encoded:value=>JSON.stringify(value),
    readJson(filename){assert.equal(filename,path.join(suite,'pending.json'));return pending;}};
  const builds={
    verifyBuild(){++calls.default;throw Error('Default MSVC verifier must not authorize LLVM');},
    verifyAcceptedBuild(_root,arch,config,sdkOnly,selected){
      ++calls.accepted;assert.equal(_root,root);assert.equal(arch,'x86');assert.equal(config,'Release');assert.equal(sdkOnly,true);
      assert.equal(selected.nativeToolchain,'LLVM22MSVC142');assert.equal(selected.profileRequest,request);
      if(!accepted)throw Error('Actual standard Observer acceptance is missing');return {build};
    }
  };
  const actualRunner=path.join(root,'out','benchmark-protocol-x86','Release','OwnedProcessRunner.exe');
  const measurement={buildDirectory:arch=>{assert.equal(arch,'x86');return path.dirname(measurementFile);},
    prepared(arch,options){calls.measurement.push({arch,options});return {
      tools:{OwnedProcessRunner:actualRunner},report:{architecture:arch,source:{sha256:'unit-measurement-source'},
        nativeToolchain:{requestFile:filePin(current)},executables:[{name:'OwnedProcessRunner',bytes:1,sha256:'a'.repeat(64),coff:332}]}};}};
  const blocked=()=>{throw Error('This unit fixture cannot execute a helper or Native process');};
  const modules={
    '../../tools/internal/delivery-identity.cjs':identity,'../../tools/internal/build-evidence.cjs':builds,
    '../../tools/internal/measurement-validation.cjs':measurement,'../../tools/internal/owned-process.cjs':{launchOwned:blocked},
    './timeout-diagnostics.cjs':{captureTimeout:blocked},'./monotonic-supervisor.cjs':{createSupervisor:blocked},
    './mixed-validation.cjs':{mixedSettings:blocked,validateMixedRecords:blocked},
    './session-validation.cjs':{sessionSettings:blocked,validateSessionRecords:blocked}
  };
  const mod={exports:{}},imports=name=>name==='node:fs'?io:name.startsWith('node:')?require(name):
    Object.hasOwn(modules,name)?modules[name]:(()=>{throw Error('Unexpected dependency: '+name);})();
  vm.runInNewContext(source,{require:imports,module:mod,__dirname:path.join(root,'tests','acceptance'),Buffer,console,
    process:{env:{SOUI_OWNED_PROCESS_RUNNER:'unverified-environment.exe'}},setTimeout:blocked,clearTimeout:blocked,setInterval:blocked,clearInterval:blocked},
    {filename:'actual-runner-profile-source-unit.cjs'});
  return {runner:mod.exports,calls,root,suite,request,pending,build,actualRunner};
}
test('MSVC default refuses LLVM requests, unknown profiles and hidden selection keys',()=>{
  const f=fixture();assert.equal(f.runner.profileSelection('x86','Release').nativeToolchain,'MSVC143');
  for(const selected of [null,[],{nativeToolchain:null},{nativeToolchain:'unknown'},{profileRequest:f.request},
    {nativeToolchain:'MSVC143',profileRequest:f.request},{nativeToolchain:'LLVM22MSVC142'},{runner:'injected.exe'}])
    assert.throws(()=>f.runner.profileSelection('x86','Release',selected));
  assert.equal(f.calls.writes,0);assert.equal(f.calls.accepted,0);
});
test('LLVM selection refuses another architecture, configuration or relative request',()=>{
  const f=fixture();for(const profileRequest of ['relative.json',path.join(f.root,'out','sdk-x64','llvm-profile-request-Release.json'),
    path.join(f.root,'out','sdk-x86','llvm-profile-request-Debug.json')])
    assert.throws(()=>f.runner.profileSelection('x86','Release',{nativeToolchain:'LLVM22MSVC142',profileRequest}));
  assert.equal(f.calls.writes,0);
});
test('prepare CLI rejects repeated, missing, unknown or misplaced profile options',()=>{
  const f=fixture();for(const args of [
    ['x86','Release','--native-toolchain'],['x86','Release','--unknown','LLVM22MSVC142'],
    ['x86','Release','--native-toolchain','MSVC143','--native-toolchain','MSVC143'],
    ['x86','Release','--native-toolchain','MSVC143','unexpected-runtime'],
    ['x86','Release','--profile-request',f.request]])assert.throws(()=>f.runner.prepareArguments(args));
  const parsed=f.runner.prepareArguments(['x86','Release','fresh.dll','--native-toolchain','LLVM22MSVC142','--profile-request',f.request]);
  assert.equal(parsed.selected.profileRequest,f.request);assert.equal(f.calls.writes,0);
});
test('request-only SDK evidence cannot pass actual LLVM prepare admission',()=>{
  const f=fixture(),runtime=path.join(f.root,'new-kernel','node.dll');
  assert.throws(()=>f.runner.prepare(f.suite,'x86','Release',runtime,'ordinary',
    {nativeToolchain:'LLVM22MSVC142',profileRequest:f.request}),/Observer acceptance/);
  assert.equal(f.calls.accepted,1);assert.equal(f.calls.default,0);assert.equal(f.calls.writes,0);
});
test('LLVM missing runtime and explicit stock path are refused before SDK or writes',()=>{
  const f=fixture(),selection={nativeToolchain:'LLVM22MSVC142',profileRequest:f.request};
  for(const runtime of [undefined,'relative.dll',path.join(f.root,'third_party','prebuilt','x86','miniblink','node.dll')])
    assert.throws(()=>f.runner.prepare(f.suite,'x86','Release',runtime,'ordinary',selection),/runtime/);
  assert.equal(f.calls.accepted,0);assert.equal(f.calls.writes,0);
});
test('stage rejects original request-byte drift even when the current mock build selects LLVM',()=>{
  const f=fixture({accepted:true,changedRequest:true});assert.throws(()=>f.runner.stage(f.suite),/original request bytes changed/);
  assert.equal(f.calls.accepted,1);assert.equal(f.calls.default,0);assert.equal(f.calls.writes,0);
});
test('stage rejects another current SDK buildId before consumer deployment',()=>{
  const f=fixture({accepted:true,changedBuild:true});assert.throws(()=>f.runner.stage(f.suite),/SDK\/source changed/);
  assert.equal(f.calls.writes,0);
});
test('actual measurement selection uses the same architecture/request and ignores executable environment override',()=>{
  const f=fixture(),result=f.runner.measurementRunner({architecture:'x86',configuration:'Release',nativeToolchain:f.pending.nativeToolchain});
  assert.equal(result.runner,f.actualRunner);assert.notEqual(result.runner,'unverified-environment.exe');
  assert.equal(f.calls.measurement[0].arch,'x86');assert.equal(f.calls.measurement[0].options.toolchainRequest,f.request);
  assert.equal(result.binding.compilerQualified,false);assert.equal(result.binding.outerWholeClosure,null);assert.equal(result.binding.noninterference,null);
});
