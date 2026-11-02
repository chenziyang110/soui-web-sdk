'use strict';
// Output-only exact-file copying and PE import inspection; never executes an EXE or DLL.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=f=>{const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:hash(b)};};
const write=(f,o)=>fs.writeFileSync(f,JSON.stringify(o,null,2)+'\n',{flag:'wx'});
const attempt=path.join(__dirname,'attempt-e36e27f5-e352-4f39-a955-f7df561e9f57');
const summaryFile=path.join(attempt,'summary.json'),summary=JSON.parse(fs.readFileSync(summaryFile));
assert.equal(summary.passed,true);assert.equal(summary.productExecuted,false);assert.equal(summary.executableExecuted,false);
const output=path.join(__dirname,'handoff-'+crypto.randomUUID());fs.mkdirSync(output);
const payload=path.join(output,'payload');fs.mkdirSync(payload);
function imports(filename){
  const b=fs.readFileSync(filename);assert.equal(b.toString('ascii',0,2),'MZ');
  const pe=b.readUInt32LE(0x3c);assert.equal(b.readUInt32LE(pe),0x4550);
  const optional=pe+24,magic=b.readUInt16LE(optional);assert([0x10b,0x20b].includes(magic));
  const at=optional+b.readUInt16LE(pe+20),sections=Array.from({length:b.readUInt16LE(pe+6)},(_,i)=>{
    const s=at+40*i;return {rva:b.readUInt32LE(s+12),raw:b.readUInt32LE(s+20),size:b.readUInt32LE(s+16)};
  });
  function location(rva){const s=sections.find(s=>rva>=s.rva&&rva<s.rva+s.size);assert(s,'Import RVA outside actual raw section');return s.raw+rva-s.rva;}
  function name(rva){const start=location(rva),end=b.indexOf(0,start);assert(end>start&&end-start<=260);const text=b.toString('ascii',start,end);assert(/^[A-Za-z0-9_.-]+\.dll$/i.test(text));return text.toLowerCase();}
  const directory=optional+(magic===0x20b?112:96),importRva=b.readUInt32LE(directory+8),delayRva=b.readUInt32LE(directory+13*8);
  const result=[];
  if(importRva){let off=location(importRva);for(let i=0;i<2048;i++,off+=20){assert(off+20<=b.length);if(b.subarray(off,off+20).every(x=>x===0))break;result.push({name:name(b.readUInt32LE(off+12)),kind:'import'});assert(i<2047,'Too many import descriptors');}}
  if(delayRva){let off=location(delayRva);for(let i=0;i<2048;i++,off+=32){assert(off+32<=b.length);if(b.subarray(off,off+32).every(x=>x===0))break;assert.equal(b.readUInt32LE(off)&1,1,'Delay-import RVA format required');result.push({name:name(b.readUInt32LE(off+4)),kind:'delay-import'});assert(i<2047,'Too many delay-import descriptors');}}
  return {machine:b.readUInt16LE(pe+4),imports:result};
}
const cohorts=[];
for(const arch of ['x64','x86']){
  const v=summary.verdicts.find(v=>v.architecture===arch);assert(v);assert.equal(v.wholeJobNaturallyClosed,true);
  const actual=pin(v.compiledExecutable.path);assert.equal(actual.bytes,v.compiledExecutable.bytes);assert.equal(actual.sha256,v.compiledExecutable.sha256);
  const buildFile='C:/sr06/out/'+arch+'/build-Release.json',buildPin=pin(buildFile),build=JSON.parse(fs.readFileSync(buildFile));
  assert.equal(build.architecture,arch);assert.equal(build.version,'0.6.0');
  const runtime='C:/sr06/out/'+arch+'/bin/Release/SouiStarter';
  const originalFiles=build.payloads.SouiStarter.files;
  const available=new Map(fs.readdirSync(runtime).map(n=>[n.toLowerCase(),n]));
  const destination=path.join(payload,'tray-'+arch);fs.mkdirSync(destination);
  // The API-MS-WIN-CRT stubs forward exports into ucrtbase; it is not a normal
  // import descriptor dependency, so retain its exact current app-local file.
  const ucrt=originalFiles.find(r=>r.path==='ucrtbase.dll');assert(ucrt);
  const ucrtSource=path.join(runtime,'ucrtbase.dll'),ucrtPin=pin(ucrtSource);assert.equal(ucrtPin.bytes,ucrt.bytes);assert.equal(ucrtPin.sha256,ucrt.sha256);
  const queue=[{file:v.compiledExecutable.path,name:'SouiTrayConsumer.exe',origin:'new independent installed consumer'},
    {file:ucrtSource,name:'ucrtbase.dll',origin:'exact sealed current CRT API-stub export-forward target'}],seen=new Set(),files=[],observations=[];
  while(queue.length){
    const item=queue.shift();if(seen.has(item.name.toLowerCase()))continue;seen.add(item.name.toLowerCase());
    const before=pin(item.file),target=path.join(destination,item.name);fs.copyFileSync(item.file,target,fs.constants.COPYFILE_EXCL);
    const copied=pin(target);assert.equal(copied.bytes,before.bytes);assert.equal(copied.sha256,before.sha256);assert.deepEqual(pin(item.file),before);
    const pe=imports(target);assert.equal(pe.machine,arch==='x64'?0x8664:0x14c);
    const dependencies=[];
    for(const dependency of pe.imports){
      const known=available.get(dependency.name),crt=/^(?:msvcp140[^/]*|vcruntime140[^/]*|ucrtbase|api-ms-win-crt-[^/]*)\.dll$/i.test(dependency.name);
      if(known&&crt){
        const source=path.join(runtime,known),p=pin(source),expected=originalFiles.find(r=>r.path.toLowerCase()===known.toLowerCase());assert(expected,'CRT not in sealed release payload');assert.equal(p.bytes,expected.bytes);assert.equal(p.sha256,expected.sha256);
        queue.push({file:source,name:known,origin:'same-architecture sealed current release CRT'});
        dependencies.push({...dependency,resolution:'app-local copied exact current release file'});
      }else dependencies.push({...dependency,resolution:'target Windows loader/system; not runtime-proven by this inspection'});
    }
    files.push({path:'tray-'+arch+'/'+item.name,bytes:copied.bytes,sha256:copied.sha256,source:before,origin:item.origin});
    observations.push({file:item.name,machine:pe.machine,dependencies});
  }
  assert.deepEqual(pin(buildFile),buildPin);
  cohorts.push({architecture:arch,buildOnlyVerdict:pin(path.join(attempt,arch,'case-verdict.json')),sealedCurrentBuild:buildPin,files,imports:observations,executableExecuted:false,vmValidated:false});
}
write(path.join(output,'Handoff.actual.json'),{schema:'soui.current-public-tray-vm-payload/v1',summary:pin(summaryFile),preparer:pin(__filename),payloadRoot:payload,cohorts,files:cohorts.flatMap(c=>c.files).sort((a,b)=>a.path.localeCompare(b.path)),scope:'current public installed tray/menu/hotkey independent consumer VM short preparation only',vmMounted:false,executableExecuted:false,productionQualificationComplete:false,releaseReady:false});
console.log(output);
