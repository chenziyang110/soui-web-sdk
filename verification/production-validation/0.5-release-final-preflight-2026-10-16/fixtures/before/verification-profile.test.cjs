'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs'),vm=require('node:vm');
const profile=require('../../tools/internal/verification-profile.cjs');
const root=path.resolve(__dirname,'../..');
test('collection defaults retain the MSVC full scope and optional flags',()=>{
  assert.deepEqual(profile.parse([]),{scope:'full',withGuiBindings:false,withDisplayConfiguration:false,nativeToolchain:'MSVC143'});
  assert.deepEqual(profile.parse(['--foundation','--with-gui-bindings','--with-display-configuration','--native-toolchain','LLVM22MSVC142']),
    {scope:'--foundation',withGuiBindings:true,withDisplayConfiguration:true,nativeToolchain:'LLVM22MSVC142'});
});
test('ambiguous, missing or unsupported collection options are rejected',()=>{
  for(const args of [['--native-toolchain'],['--native-toolchain',''],['--native-toolchain','unknown'],
    ['--native-toolchain','MSVC143','--native-toolchain','MSVC143'],['full','--foundation'],
    ['--with-gui-bindings','--with-gui-bindings'],['--toolchain-request','elsewhere.json'],['unknown']])
    assert.throws(()=>profile.parse(args),/collection|toolchain|Usage/i);
});
for(const arch of ['x64','x86'])test('measurement request belongs to the selected '+arch+' preparation',()=>{
  const args=profile.measurementArgs(root,'native',arch,'LLVM22MSVC142');
  assert.deepEqual(args,['tools/internal/measurement-validation.cjs','native',arch,'--toolchain-request',
    path.join(root,'out',arch==='x64'?'benchmark-protocol':'benchmark-protocol-x86','llvm-profile-request-Release.json')]);
  assert.deepEqual(profile.measurementArgs(root,'native',arch,'MSVC143'),['tools/internal/measurement-validation.cjs','native',arch]);
});
test('measurement routing rejects an unknown command, architecture or profile',()=>{
  for(const [command,arch,selected] of [['source','x64','MSVC143'],['native','arm64','LLVM22MSVC142'],['native','x64','other']])
    assert.throws(()=>profile.measurementArgs(root,command,arch,selected));
});

test('build requests retain architecture, configuration, SDK-only ownership and explicit profile',()=>{
  for(const arch of ['x64','x86'])for(const configuration of ['Debug','Release'])for(const sdkOnly of [false,true]){
    assert.deepEqual(profile.buildRequest(root,arch,configuration,sdkOnly,'LLVM22MSVC142'),{
      nativeToolchain:'LLVM22MSVC142',profileRequest:path.join(root,'out',(sdkOnly?'sdk-':'')+arch,'llvm-profile-request-'+configuration+'.json')});
    assert.deepEqual(profile.buildRequest(root,arch,configuration,sdkOnly,'MSVC143'),{nativeToolchain:'MSVC143'});
  }
  for(const args of [['arm64','Release',false,'MSVC143'],['x64','Fast',false,'MSVC143'],['x64','Release','yes','MSVC143'],['x64','Release',false,'unknown']])
    assert.throws(()=>profile.buildRequest(root,...args));
});

test('the actual collector cannot infer an opposite build profile from existing receipts',()=>{
  for(const selected of ['MSVC143','LLVM22MSVC142']){
    const stop=new Error('stop before any actual native build acceptance');let observed;
    const sandbox={__dirname:path.join(root,'tools'),console,Buffer,
      process:{argv:['node','collector','--native-toolchain',selected],execPath:process.execPath},
      require:name=>{
        if(name==='node:fs')return {mkdirSync(){},readFileSync(){return '0.5.0-preview.1';},writeFileSync(){}};
        if(name==='node:path')return path;
        if(name==='node:crypto')return require(name);
        if(name==='node:child_process')return {spawnSync(){return {status:0,stdout:'',stderr:''};}};
        if(name==='./internal/verification-profile.cjs')return profile;
        if(name==='./internal/build-evidence.cjs')return {verifySmoke(...args){observed=args;throw stop;}};
        return {};
      }};
    assert.throws(()=>vm.runInNewContext(fs.readFileSync(path.join(root,'tools/Collect-Verification.cjs'),'utf8'),sandbox),error=>error===stop);
    assert.deepEqual(observed,[root,'x86','Debug',profile.buildRequest(root,'x86','Debug',false,selected)]);
  }
});
test('the actual collector forwards LLVM requests to every measurement group before later work',()=>{
  const calls=[],stop=new Error('fixture stops before later collection; no native execution');
  const text=fs.readFileSync(path.join(root,'tools/Collect-Verification.cjs'),'utf8');
  const sandbox={__dirname:path.join(root,'tools'),console,Buffer,
    process:{argv:['node','collector','--native-toolchain','LLVM22MSVC142'],execPath:process.execPath},
    require:name=>{
      if(name==='node:fs')return {mkdirSync(){},readFileSync(){return '0.5.0-preview.1';},writeFileSync(){}};
      if(name==='node:path')return path;
      if(name==='node:crypto')return require(name);
      if(name==='node:child_process')return {spawnSync:(executable,args)=>{
        if(calls.length===8)throw stop;calls.push({executable,args});return {status:0,stdout:'',stderr:''};}};
      if(name==='./internal/verification-profile.cjs')return profile;
      return {};
    }};
  assert.throws(()=>vm.runInNewContext(text,sandbox),error=>error===stop);
  const actual=calls.filter(row=>row.args[0]==='tools/internal/measurement-validation.cjs');
  const expected=[['protocol','x64'],['native','x64'],['native','x86'],['tail-protocol','x64'],['tail-native','x64'],['tail-native','x86']];
  assert.equal(actual.length,expected.length);
  expected.forEach(([command,arch],index)=>assert.deepEqual(Array.from(actual[index].args),profile.measurementArgs(root,command,arch,'LLVM22MSVC142')));
});

test('LLVM collection selects both genuine component readers without requesting a synthetic All report',()=>{
  const calls=[],stop=new Error('stop at actual second dependency verification; no native acceptance');
  const sandbox={__dirname:path.join(root,'tools'),console,Buffer,
    process:{argv:['node','collector','--native-toolchain','LLVM22MSVC142'],execPath:process.execPath},
    require:name=>{
      if(name==='node:fs')return {mkdirSync(){},writeFileSync(){},readFileSync(file){assert(!String(file).endsWith('build-All.json'));return '0.5.0-preview.1';}};
      if(name==='node:path')return path;
      if(name==='node:crypto')return require(name);
      if(name==='node:child_process')return {spawnSync(){return {status:0,stdout:'',stderr:''};}};
      if(name==='./internal/verification-profile.cjs')return profile;
      if(name==='./internal/build-evidence.cjs')return {verifySmoke(){return {build:{nativeBuild:'passed'},smoke:{passed:true}};},verifyModernSmoke(){return {smoke:{}};}};
      if(name==='./internal/dependency-evidence.cjs')return {verifyRecorded(...args){calls.push(args);if(calls.length===2)throw stop;return {};}};
      return {};
    }};
  assert.throws(()=>vm.runInNewContext(fs.readFileSync(path.join(root,'tools/Collect-Verification.cjs'),'utf8'),sandbox),error=>error===stop);
  assert.deepEqual(calls,['x86','x64'].map(arch=>[root,arch,profile.buildRequest(root,arch,'Release',false,'LLVM22MSVC142')]));
});
