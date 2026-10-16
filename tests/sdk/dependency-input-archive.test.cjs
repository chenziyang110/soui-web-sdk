'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const reader=require('../../tools/internal/dependency-evidence.cjs');
function fixture(t){
 const base=fs.mkdtempSync(path.join(os.tmpdir(),'soui-input-archive-'));
 t.after(()=>{const absolute=path.resolve(base);assert(absolute.startsWith(path.resolve(os.tmpdir())+path.sep));fs.rmSync(absolute,{recursive:true,force:true});});
 const producer=path.join(base,'producer'),archive=path.join(base,'archive');fs.mkdirSync(path.join(producer,'tools/internal'),{recursive:true});fs.mkdirSync(archive);
 const script=path.join(producer,'tools/internal/script.ps1'),tool=path.join(base,'compiler.exe');
 fs.writeFileSync(script,'original\r\n');fs.writeFileSync(tool,'actual compiler');
 const original=reader.pin(script),binary=reader.pin(tool),inputs={inputPins:[original,binary],discovery:{ToolPins:{CLANG:binary},RequiredInputPins:[]}};
 fs.copyFileSync(script,path.join(archive,'original.ps1'));fs.writeFileSync(script,'original\n');
 const manifest={schema:'soui.dependency-input-archive/v1',producerRoot:producer,files:[{original,file:'original.ps1'}]};
 const archiveFile=path.join(archive,'archive.json');const save=()=>fs.writeFileSync(archiveFile,JSON.stringify(manifest));save();
 return {base,producer,archive,script,tool,original,binary,inputs,manifest,archiveFile,save};
}
test('an exact historical script archive preserves the original input pin without normalizing bytes',t=>{
 const f=fixture(t);assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,null),/changed/);
 const proof=reader.verifyInputPins(f.producer,f.inputs,f.archiveFile);
 assert.deepEqual(proof.files[0].original,f.original);assert.equal(proof.files[0].archived.sha256,f.original.sha256);
 assert.equal(proof.manifest.sha256,reader.pin(f.archiveFile).sha256);
 assert.equal(fs.readFileSync(f.script,'utf8'),'original\n');
 fs.writeFileSync(path.join(f.archive,'original.ps1'),'original\n');assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,f.archiveFile),/changed|differ/i);
});
test('archive cannot replace compiler/toolchain pins or bypass another changed input',t=>{
 const f=fixture(t);fs.writeFileSync(f.tool,'changed compiler');assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,f.archiveFile),/changed/i);
 fs.writeFileSync(f.tool,'actual compiler');f.manifest.files=[{original:f.binary,file:'original.ps1'}];f.save();
 assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,f.archiveFile),/script|tool|producer/i);
 f.manifest.files=[{original:f.original,file:'original.ps1'}];f.inputs.discovery.ToolPins.TEST=f.original;f.save();
 assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,f.archiveFile),/tool/i);
});
test('archive rejects unknown, duplicate, inconsistent and non-script source records',t=>{
 const f=fixture(t),row=f.manifest.files[0];
 for(const rows of [[row,row],[{...row,original:{...f.original,bytes:0}}],[{...row,original:{...f.original,path:f.script+'.missing'}}],
  [{...row,original:{...f.original,path:path.join(f.producer,'tools/compiler.exe')}}],[{...row,extra:true}],[]]){
  f.manifest.files=rows;f.save();assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,f.archiveFile));
 }
});
test('archive rejects path escape, wrong owner, malformed fields and missing archive',t=>{
 const f=fixture(t),row=f.manifest.files[0];
 for(const name of ['../original.ps1',path.join(f.archive,'original.ps1'),'nested\\original.ps1','']){
  f.manifest.files=[{...row,file:name}];f.save();assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,f.archiveFile));
 }
 f.manifest.files=[row];f.manifest.producerRoot=f.base;f.save();assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,f.archiveFile));
 f.manifest.producerRoot=f.producer;f.manifest.extra=true;f.save();assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,f.archiveFile));
 assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,f.archiveFile+'.missing'));
});
test('without an archive every original physical input still has to match exactly',t=>{
 const f=fixture(t);fs.writeFileSync(f.script,'original\r\n');assert.equal(reader.verifyInputPins(f.producer,f.inputs,null),null);
 fs.writeFileSync(f.tool,'other');assert.throws(()=>reader.verifyInputPins(f.producer,f.inputs,null),/changed/i);
});
test('v3 requires explicit archive selection and preserves v1/v2 validation',()=>{
 const base=path.resolve('C:/fixture');
 const value={schema:'soui.dependency-selection/v3',architecture:'x64',profile:'LLVM22MSVC142',runner:path.join(base,'runner.exe'),
  kernel:{producerRoot:base,report:path.join(base,'kernel.json'),capture:path.join(base,'kernel'),inputArchive:null},
  soui:{producerRoot:base,report:path.join(base,'soui.json'),capture:path.join(base,'soui'),inputArchive:path.join(base,'archive.json')}};
 assert.deepEqual(reader.selection(value,'x64'),value);
 for(const changed of [{...value.soui,inputArchive:'relative.json'},{...value.soui,inputArchive:3},{...value.soui,inputArchive:undefined}])
  assert.throws(()=>reader.selection({...value,soui:changed},'x64'));
 assert.throws(()=>reader.selection({...value,schema:'soui.dependency-selection/v2'},'x64'));
});
