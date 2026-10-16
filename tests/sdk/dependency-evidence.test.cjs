'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const reader=require('../../tools/internal/dependency-evidence.cjs');
const id=require('../../tools/internal/delivery-identity.cjs');
function fixture(t){const root=fs.mkdtempSync(path.join(os.tmpdir(),'soui-dependency-reader-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));return root;}
function put(root,name,bytes){const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);return file;}
test('selection requires both actual components, matching architecture and explicit profile',()=>{
 const root=path.resolve(__dirname,'../..');
 const valid={schema:'soui.dependency-selection/v1',profile:'LLVM22MSVC142',architecture:'x64',runner:path.join(root,'out/runner.exe'),
  kernel:{report:path.join(root,'out/kernel/build-Miniblink.json'),capture:path.join(root,'out/kernel-capture/kernel')},
  soui:{report:path.join(root,'out/soui/build-SOUI.json'),capture:path.join(root,'out/soui-capture/soui')}};
 assert.deepEqual(reader.selection(valid,'x64'),valid);
 for(const change of [{profile:'MSVC143'},{architecture:'x86'},{soui:null},{kernel:{...valid.kernel,report:'relative.json'}},{extra:true}])
  assert.throws(()=>reader.selection({...valid,...change},'x64'));
 assert.throws(()=>reader.selection({...valid,soui:{...valid.soui,capture:valid.kernel.capture}},'x64'));
});
test('file pins reject changed contents',t=>{
 const root=fixture(t),file=put(root,'input.bin',Buffer.from('first'));
 const pin=reader.pin(file);assert.equal(pin.sha256,id.hash(Buffer.from('first')));reader.checkPin(pin);
 fs.writeFileSync(file,'other');assert.throws(()=>reader.checkPin(pin),/changed|differ/i);
});

test('large static libraries are hashed completely without a whole-file buffer',t=>{
 const root=fixture(t),file=path.join(root,'large.lib'),chunk=Buffer.alloc(1024*1024),hash=require('node:crypto').createHash('sha256');
 const fd=fs.openSync(file,'wx');
 try{fs.ftruncateSync(fd,512*1024*1024+1);fs.writeSync(fd,Buffer.from('A'),0,1,512*1024*1024);}finally{fs.closeSync(fd);}
 for(let i=0;i<512;++i)hash.update(chunk);hash.update('A');
 const expected={path:id.safePath(file),bytes:512*1024*1024+1,sha256:hash.digest('hex')};
 assert.deepEqual(reader.pin(file),expected);reader.checkPin(expected);
 const edit=fs.openSync(file,'r+');try{fs.writeSync(edit,Buffer.from('B'),0,1,512*1024*1024);}finally{fs.closeSync(edit);}
 assert.throws(()=>reader.checkPin(expected),/changed/i);
 assert.throws(()=>reader.pin(root),/regular|file/i);
});
test('source inventory binds directories and every physical file without normalization',t=>{
 const root=fixture(t);put(root,'a/input.cpp','one\r\n');const before=reader.snapshot(root);
 reader.verifySource(before,root);put(root,'a/input.cpp','one\n');assert.throws(()=>reader.verifySource(before,root),/source/i);
 put(root,'a/input.cpp','one\r\n');fs.mkdirSync(path.join(root,'empty'));assert.throws(()=>reader.verifySource(before,root),/source/i);
});
test('source receipt cannot substitute another root or an inconsistent file roster',t=>{
 const root=fixture(t);put(root,'a.cpp','a');const before=reader.snapshot(root);
 assert.throws(()=>reader.verifySource({...before,root:path.join(root,'elsewhere')},root));
 assert.throws(()=>reader.verifySource({...before,files:[]},root));
});
test('DLL matching requires exactly the rebuilt bytes in every actual application payload',()=>{
 const artifacts=['miniblink','soui','utilities','render-gdi','imgdecoder-gdip','soui-sys-resource'].map((kind,i)=>({kind,bytes:i+1,sha256:String(i).repeat(64)}));
 const files=artifacts.map(a=>({path:a.kind==='miniblink'?'node.dll':a.kind+'.dll',bytes:a.bytes,sha256:a.sha256}));
 const build={payloads:{SouiStarter:{files},SouiWorkbench:{files}}};reader.matchPayloads(build,artifacts);
 assert.throws(()=>reader.matchPayloads({payloads:{SouiStarter:{files:files.slice(1)}}},artifacts));
 assert.throws(()=>reader.matchPayloads({payloads:{SouiStarter:{files:[...files,{...files[0]}]}}},artifacts));
 assert.throws(()=>reader.matchPayloads({payloads:{SouiStarter:{files:files.map((f,i)=>i?f:{...f,sha256:'f'.repeat(64)})}}},artifacts));
 assert.throws(()=>reader.matchPayloads({payloads:{}},artifacts));
});
test('PE validation rejects opposite architecture, truncation and non-DLL images',()=>{
 const bytes=Buffer.alloc(512);bytes.writeUInt16LE(0x5a4d);bytes.writeUInt32LE(64,60);bytes.writeUInt32LE(0x4550,64);
 bytes.writeUInt16LE(0x8664,68);bytes.writeUInt16LE(1,70);bytes.writeUInt16LE(112,84);bytes.writeUInt16LE(0x2002,86);bytes.writeUInt16LE(0x20b,88);
 reader.pe(bytes,'x64');assert.throws(()=>reader.pe(bytes,'x86'));assert.throws(()=>reader.pe(bytes.subarray(0,64),'x64'));
 bytes.writeUInt16LE(2,86);assert.throws(()=>reader.pe(bytes,'x64'));
});

test('actual component invocation binds public entry, source owner, architecture and all selected tool roots',()=>{
 const root=path.resolve(__dirname,'../..'),vs=path.resolve(root,'out/fixture-vs'),sdk=path.resolve(root,'out/fixture-sdk');
 const llvm=path.join(vs,'VC/Tools/Llvm/x64'),msvc=path.join(vs,'VC/Tools/MSVC/14.29.30133'),report=path.join(root,'out/fixture-soui/build-SOUI.json');
 const tools={CLANG:llvm+'/bin/clang-cl.exe',LINKER:llvm+'/bin/lld-link.exe',NINJA:vs+'/ninja.exe',CMD:vs+'/cmd.exe',POWERSHELL:vs+'/pwsh.exe',RC:sdk+'/bin/10.0.19041.0/x64/rc.exe'};
 const script=path.join(root,'tools/Build-SouiLlvm.ps1'),node=vs+'/node.exe';
 const inputs={inputPins:[{path:script},{path:node},...Array.from({length:10},(_,i)=>({path:vs+'/input'+i}))],
  vsOwner:{msvcRoot:msvc,vsInstallDir:vs},discovery:{ToolPins:Object.fromEntries(Object.entries(tools).map(([key,value])=>[key,{path:value}]))}};
 const args=['-NoProfile','-NonInteractive','-File',script,'-Architecture','x64','-NativeToolchain','LLVM22MSVC142','-OutputRoot',path.dirname(report),
  '-LlvmRoot',llvm,'-MsvcRoot',msvc,'-WindowsSdkRoot',sdk,'-NinjaExe',tools.NINJA,'-WindowsCmdExe',tools.CMD,
  '-PowerShellExe',tools.POWERSHELL,'-VsInstallDir',vs,'-NodeExe',node];
 const context={cwd:root,executable:tools.POWERSHELL,args};reader.invocation(context,root,'x64','soui',report,inputs);
 for(const [flag,value] of [['-File',script+'.other'],['-Architecture','x86'],['-OutputRoot',path.join(root,'out/other')],
  ['-NativeToolchain','MSVC143'],['-MsvcRoot',msvc+'/other'],['-WindowsSdkRoot',sdk+'/other'],['-LlvmRoot',llvm+'/other'],
  ['-PowerShellExe',node],['-NodeExe',node+'.other']]){
  const changed=[...args];changed[changed.indexOf(flag)+1]=value;assert.throws(()=>reader.invocation({...context,args:changed},root,'x64','soui',report,inputs));
 }
 assert.throws(()=>reader.invocation({...context,args:[...args,'-Architecture','x64']},root,'x64','soui',report,inputs));
 assert.throws(()=>reader.invocation({...context,cwd:path.dirname(root)},root,'x64','soui',report,inputs));
});

test('v2 selection requires explicit absolute producer roots for both components',()=>{
 const root=path.resolve(__dirname,'../..'),valid={schema:'soui.dependency-selection/v2',profile:'LLVM22MSVC142',architecture:'x64',runner:path.join(root,'runner.exe'),
  kernel:{producerRoot:path.join(root,'kernel-source'),report:path.join(root,'kernel.json'),capture:path.join(root,'kernel-capture')},
  soui:{producerRoot:root,report:path.join(root,'soui.json'),capture:path.join(root,'soui-capture')}};
 assert.deepEqual(reader.selection(valid,'x64'),valid);
 for(const change of [{schema:'soui.dependency-selection/v3'},{schema:'soui.dependency-selection/v1'},
  {kernel:{...valid.kernel,producerRoot:'relative'}},{soui:{report:valid.soui.report,capture:valid.soui.capture}},
  {kernel:{...valid.kernel,extra:true}},{soui:{...valid.soui,producerRoot:'bad\nroot'}}])
   assert.throws(()=>reader.selection({...valid,...change},'x64'));
});
test('relocated component matching requires every delivered byte and directory',t=>{
 const root=fixture(t),producer=path.join(root,'producer'),delivered=path.join(root,'sdk');
 const source=path.join(producer,'third_party/sources/miniblink'),target=path.join(delivered,'third_party/sources/miniblink');
 put(source,'a.cpp','same\r\n');put(source,'sub/b.h','header');fs.mkdirSync(path.join(source,'empty'));
 fs.mkdirSync(path.dirname(target),{recursive:true});fs.cpSync(source,target,{recursive:true});
 const proof={sourceIdentity:reader.snapshot(source).identity};
 const match=reader.matchDeliveredSource(delivered,'kernel',proof);
 assert.equal(match.sourceIdentity,proof.sourceIdentity);assert.equal(match.fileCount,2);
 assert.throws(()=>reader.matchDeliveredSource(delivered,'unknown',proof));
 assert.throws(()=>reader.matchDeliveredSource(delivered,'kernel',{sourceIdentity:'bad'}));
 const changes=[()=>fs.writeFileSync(path.join(target,'a.cpp'),'same\n'),()=>fs.unlinkSync(path.join(target,'sub/b.h')),
  ()=>put(target,'extra.cpp','extra'),()=>fs.mkdirSync(path.join(target,'extra-directory'))];
 for(const change of changes){
  // Every deletion below is confined to this test's freshly created temp subtree.
  const absolute=path.resolve(target);assert(absolute.startsWith(path.resolve(root)+path.sep));
  fs.rmSync(absolute,{recursive:true,force:true});fs.cpSync(source,target,{recursive:true});change();
  assert.throws(()=>reader.matchDeliveredSource(delivered,'kernel',proof),/source/i);
 }
});
