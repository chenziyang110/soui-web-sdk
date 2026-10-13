'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const vm=require('node:vm'),crypto=require('node:crypto');
const identity=require('../../tools/internal/delivery-identity.cjs');
const runnerSource=fs.readFileSync(path.join(__dirname,'runner.cjs'),'utf8');
const output=fs.mkdtempSync(path.join(os.tmpdir(),'SOUI acceptance Unicode 验收 '));
// Real filesystem unit tests. Build/Web records below are fixtures, never native producer evidence.
// Keep each fresh tree for Root inspection; this suite performs no recursive deletion.
function write(file,data){fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,data,{flag:'wx'});}
function json(file,value){write(file,identity.encoded(value));}
function snap(dir){return identity.snapshot(dir,identity.walk(dir));}
function fixture(withMessagePort=false){
 const root=path.join(output,crypto.randomUUID(),'source 源码 空格');
 const suite=path.join(root,'out','acceptance','Windows 验收 suite');
 fs.mkdirSync(suite,{recursive:true});
 const original=path.join(root,'tests','acceptance-app');
 write(path.join(original,'main.cpp'),'fixture main\r\n');
 write(path.join(original,'web','目录 验收','页面 空格.ts'),'fixture web\n');
 const messagePort=path.join(root,'tests','message-port-app');
 if(withMessagePort){
  write(path.join(messagePort,'main.cpp'),'distinct message-port fixture main\r\n');
  write(path.join(messagePort,'web','main.ts'),'distinct message-port fixture Web\n');
  write(path.join(messagePort,'web','public','message-worker.js'),'distinct message-port fixture Worker\n');
 }
 write(path.join(root,'sdk','web','dist','入口 中文 空格.js'),'fixture sdk\n');
 write(path.join(root,'sdk','web','dist','类型 目录','声明.d.ts'),'fixture types\n');
 json(path.join(root,'sdk','web','package.json'),{name:'fixture-public-web'});
 write(path.join(root,'sdk','include','soui','fixture.h'),'fixture header\n');
 json(path.join(root,'tests','acceptance','package.json'),{devDependencies:{typescript:'fixture-version'}});
 const kernel=path.join(root,'kernel 源','node.dll');write(kernel,Buffer.from([0,1,2,255]));
 const library=Buffer.from('fixture static library');
 const build={buildId:'fixture-build',version:'fixture-version',source:{sha256:'fixture-source'},
   library:{files:[{sha256:identity.hash(library)}]}};
 json(path.join(root,'out','web-sdk.json'),{schema:'soui.web-build/v1',sourceSha256:build.source.sha256,
   artifacts:identity.snapshot(root,identity.walk(root,'sdk/web/dist'))});
 return {root,suite,original,messagePort,kernel,library,build};
}
function load(f,filesystem=fs){
 const mod={exports:{}};
 const blocked=()=>{throw Error('This filesystem fixture cannot launch a process or run a native consumer');};
 const modules={
  '../../tools/internal/owned-process.cjs':{launchOwned:blocked},
  '../../tools/internal/delivery-identity.cjs':identity,
  '../../tools/internal/build-evidence.cjs':{verifyBuild:(root,arch,config,requireComplete)=>{
   assert.equal(root,f.root);assert.equal(arch,'x64');assert.equal(config,'Release');assert.equal(requireComplete,true);return f.build;}},
  './timeout-diagnostics.cjs':{captureTimeout:blocked},'./monotonic-supervisor.cjs':{createSupervisor:blocked},
  './mixed-validation.cjs':{mixedSettings:blocked,validateMixedRecords:blocked},
  './session-validation.cjs':{sessionSettings:blocked,validateSessionRecords:blocked}
 };
 const onlyRequire=name=>name==='node:fs'?filesystem:name.startsWith('node:')?require(name):
   Object.hasOwn(modules,name)?modules[name]:(()=>{throw Error('Unexpected dependency: '+name);})();
 vm.runInNewContext(runnerSource,{require:onlyRequire,module:mod,__dirname:path.join(f.root,'tests','acceptance'),
   process,Buffer,console,setTimeout,clearTimeout,setInterval,clearInterval},{filename:'actual-acceptance-runner.cjs'});
 return mod.exports;
}
function prepareStage(f,r){
 r.prepare(f.suite,'x64','Release',f.kernel);
 const pending=identity.readJson(path.join(f.suite,'pending.json'));
 const webSdk=path.join(f.suite,'web-sdk'),webOutput=path.join(f.suite,'web-output');
 write(path.join(webOutput,'页面 目录','index 中文.html'),'fixture resource\n');
 const web=snap(webOutput);
 const typeConfig=path.join(f.suite,'web-typecheck.tsconfig.json'),typeLog=path.join(f.suite,'web-typecheck.log');
 json(typeConfig,{fixture:true});write(typeLog,'fixture typecheck\n');
 json(path.join(f.suite,'web-build.json'),{schema:'soui.acceptance-web/v1',consumerSourceSha256:pending.consumerSource.sha256,
  webSdkSha256:pending.webSdk.sha256,typecheck:{passed:true,typescript:'fixture-version',configSha256:identity.hash(fs.readFileSync(typeConfig)),
   logSha256:identity.hash(fs.readFileSync(typeLog))},output:web});
 write(path.join(f.suite,'install','lib','Release','soui_sdk.lib'),f.library);
 write(path.join(f.suite,'install','include','soui','fixture.h'),fs.readFileSync(path.join(f.root,'sdk','include','soui','fixture.h')));
 write(path.join(f.suite,'consumer','bin','Release','SouiAcceptance.exe'),Buffer.from('fixture exe'));
 for(let slot=1;slot<=3;slot++){
  const destination=path.join(f.suite,'consumer','resources','window-'+slot);
  for(const file of web.files)write(path.join(destination,file.path),fs.readFileSync(path.join(webOutput,file.path)));
 }
 fs.mkdirSync(path.join(f.root,'third_party','prebuilt','x64','runtime'),{recursive:true});
 fs.mkdirSync(path.join(f.root,'third_party','prebuilt','x64','soui','bin'),{recursive:true});
 return {pending,web,webSdk};
}
test('actual prepare copies Unicode source, destination, nested names and spaces without a garbled sibling',t=>{
 const f=fixture(),r=load(f);t.diagnostic('Preserved filesystem fixture: '+f.root);
 const expected=snap(f.original);r.prepare(f.suite,'x64','Release',f.kernel);
 assert.equal(snap(path.join(f.suite,'consumer-source')).sha256,expected.sha256);
 assert.deepEqual(fs.readdirSync(path.dirname(f.suite)),['Windows 验收 suite']);
 assert.equal(identity.hash(fs.readFileSync(path.join(f.suite,'kernel-input','node.dll'))),identity.hash(fs.readFileSync(f.kernel)));
});
test('actual prepare copies the Unicode public Web SDK directory and retains every input byte',()=>{
 const f=fixture();load(f).prepare(f.suite,'x64','Release',f.kernel);
 const source=path.join(f.root,'sdk','web','dist'),copy=path.join(f.suite,'web-sdk','dist');
 assert.deepEqual(snap(copy),snap(source));
 assert.equal(fs.readFileSync(path.join(f.suite,'web-sdk','package.json'),'utf8'),fs.readFileSync(path.join(f.root,'sdk','web','package.json'),'utf8'));
});
test('actual stage copies all three Unicode resource trees and retains the independent Web snapshot',()=>{
 const f=fixture(),r=load(f),p=prepareStage(f,r);r.stage(f.suite);
 const build=identity.readJson(path.join(f.suite,'build.json'));
 for(let slot=1;slot<=3;slot++)assert.equal(snap(path.join(f.suite,'runtime','resources','window-'+slot)).sha256,p.web.sha256);
 assert.equal(build.web.output.sha256,p.web.sha256);assert.equal(build.releaseReady,false);
});
test('all three actual copy statements reject an existing file without overwriting its bytes',()=>{
 const calls=runnerSource.match(/^  fs\.cpSync\([^\r\n]+\);$/gm);
 assert.equal(calls?.length,3);
 for(let which=0;which<3;which++){
  const f=fixture(),bin=path.join(f.suite,'runtime'),webSdkRoot=path.join(f.suite,'web-sdk'),copy=path.join(f.suite,'consumer-source');
  const src=which===0?f.original:which===1?path.join(f.root,'sdk','web','dist'):path.join(f.suite,'consumer','resources');
  const dst=which===0?copy:which===1?path.join(webSdkRoot,'dist'):path.join(bin,'resources');
  fs.mkdirSync(src,{recursive:true});const leaf='collision 中文 空格.txt';write(path.join(src,leaf),'new source');write(path.join(dst,leaf),'retained destination');
  const invoke=new Function('fs','path','original','copy','root','webSdkRoot','suite','bin',calls[which]);
  assert.throws(()=>invoke(fs,path,f.original,copy,f.root,webSdkRoot,f.suite,bin),error=>error.code==='ERR_FS_CP_EEXIST');
  assert.equal(fs.readFileSync(path.join(dst,leaf),'utf8'),'retained destination');
 }
});
test('actual prepare keeps the existing source-link rejection before any consumer copy',()=>{
 const f=fixture(),outside=path.join(f.root,'linked target');fs.mkdirSync(outside);write(path.join(outside,'secret.txt'),'not a consumer input');
 fs.symlinkSync(outside,path.join(f.original,'目录 junction'),process.platform==='win32'?'junction':'dir');
 assert.throws(()=>load(f).prepare(f.suite,'x64','Release',f.kernel),/Linked path/);
 assert.equal(fs.existsSync(path.join(f.suite,'consumer-source')),false);
});
test('actual prepare rejects a linked suite before creating kernel or consumer outputs',()=>{
 const f=fixture(),alias=path.join(f.root,'out','acceptance','alias 验收');
 fs.symlinkSync(f.suite,alias,process.platform==='win32'?'junction':'dir');
 assert.throws(()=>load(f).prepare(alias,'x64','Release',f.kernel),/Linked path/);
 assert.deepEqual(fs.readdirSync(f.suite),[]);
});
test('actual prepare still rejects a copy whose bytes change before the independent snapshot',()=>{
 const f=fixture();const filesystem=Object.create(fs);
 filesystem.cpSync=(src,dst,opts)=>{fs.cpSync(src,dst,opts);if(src===f.original)fs.appendFileSync(path.join(dst,'main.cpp'),'changed');};
 assert.throws(()=>load(f,filesystem).prepare(f.suite,'x64','Release',f.kernel),/Consumer source changed during snapshot/);
 assert.equal(fs.existsSync(path.join(f.suite,'pending.json')),false);
});
// These exercise the actual runner prepare implementation against distinct real
// source trees. The build/Web records remain explicitly non-native unit fixtures.
test('default prepare remains the ordinary consumer when MessagePort source is present',()=>{
 const f=fixture(true),expected=snap(f.original),other=snap(f.messagePort);
 assert.notEqual(expected.sha256,other.sha256);
 load(f).prepare(f.suite,'x64','Release',f.kernel);
 const pending=identity.readJson(path.join(f.suite,'pending.json'));
 assert.deepEqual(pending.consumerSource,expected);
 assert.deepEqual(snap(path.join(f.suite,'consumer-source')),expected);
 assert.equal(fs.existsSync(path.join(f.suite,'consumer-source','web','public','message-worker.js')),false);
 assert.deepEqual(snap(f.messagePort),other);
});
test('explicit ordinary selection preserves the same ordinary source and public Web inputs',()=>{
 const f=fixture(true),expected=snap(f.original),sdk=snap(path.join(f.root,'sdk','web','dist'));
 load(f).prepare(f.suite,'x64','Release',f.kernel,'ordinary');
 assert.deepEqual(identity.readJson(path.join(f.suite,'pending.json')).consumerSource,expected);
 assert.deepEqual(snap(path.join(f.suite,'consumer-source')),expected);
 assert.deepEqual(snap(path.join(f.suite,'web-sdk','dist')),sdk);
});
test('explicit MessagePort selection copies its full Worker tree without changing ordinary source',()=>{
 const f=fixture(true),ordinary=snap(f.original),selected=snap(f.messagePort);
 load(f).prepare(f.suite,'x64','Release',f.kernel,'message-port');
 const pending=identity.readJson(path.join(f.suite,'pending.json'));
 assert.deepEqual(pending.consumerSource,selected);
 assert.deepEqual(snap(path.join(f.suite,'consumer-source')),selected);
 assert.equal(fs.readFileSync(path.join(f.suite,'consumer-source','web','public','message-worker.js'),'utf8'),
   fs.readFileSync(path.join(f.messagePort,'web','public','message-worker.js'),'utf8'));
 assert.deepEqual(snap(f.original),ordinary);
 assert.deepEqual(snap(path.join(f.suite,'web-sdk','dist')),snap(path.join(f.root,'sdk','web','dist')));
 assert.equal(identity.hash(fs.readFileSync(path.join(f.suite,'kernel-input','node.dll'))),identity.hash(fs.readFileSync(f.kernel)));
});
test('unknown fixture selection is refused before suite validation or filesystem side effects',()=>{
 const f=fixture(true);let accesses=0;
 const noFilesystem=new Proxy(fs,{get(){++accesses;throw Error('Unknown selector reached filesystem');}});
 const runner=load(f,noFilesystem),notASuite=path.join(f.root,'not-an-acceptance-suite');
 for(const selection of ['',null,{},[],1,'../acceptance-app','message-port-app','unknown'])
  assert.throws(()=>runner.prepare(notASuite,'x64','Release',f.kernel,selection),/Unknown trusted acceptance fixture/);
 assert.equal(accesses,0);
 assert.equal(fs.existsSync(notASuite),false);
 assert.deepEqual(fs.readdirSync(f.suite),[]);
});
test('a missing selected MessagePort source never falls back to the ordinary fixture',()=>{
 const f=fixture(true),ordinary=snap(f.original);
 fs.renameSync(f.messagePort,path.join(f.root,'retained-message-port-source'));
 assert.throws(()=>load(f).prepare(f.suite,'x64','Release',f.kernel,'message-port'),error=>error.code==='ENOENT');
 assert.equal(fs.existsSync(path.join(f.suite,'consumer-source')),false);
 assert.equal(fs.existsSync(path.join(f.suite,'pending.json')),false);
 assert.deepEqual(snap(f.original),ordinary);
});
test('the selected MessagePort tree keeps the original linked-source refusal',()=>{
 const f=fixture(true),outside=path.join(f.root,'unselected-linked-input');
 fs.mkdirSync(outside);write(path.join(outside,'private.txt'),'not a selected input');
 fs.symlinkSync(outside,path.join(f.messagePort,'linked-worker'),process.platform==='win32'?'junction':'dir');
 assert.throws(()=>load(f).prepare(f.suite,'x64','Release',f.kernel,'message-port'),/Linked path/);
 assert.equal(fs.existsSync(path.join(f.suite,'consumer-source')),false);
 assert.equal(fs.existsSync(path.join(f.suite,'pending.json')),false);
});
