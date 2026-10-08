'use strict';
// Files only: no application, PowerShell, archive helper, or VM execution.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=__dirname,parent='C:/SOUI-SDK-production-validation/preview11-win7-preparation-v2-20260926';
const parentSerial='C:/SOUI-SDK-production-validation/preview11-win7-apps-attempt2/serial-export.txt';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const json=v=>JSON.stringify(v,null,2)+'\n';
const write=(n,b)=>{const p=path.join(root,n);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'})};
if(fs.readdirSync(root).some(n=>n!=='Prepare-Layout.cjs'))throw new Error('Refuse populated preparation directory');
const frozen=fs.readFileSync(path.join(parent,'freeze.json'));
if(hash(frozen)!=='20f5b68956e9483dfeff26349bd81d49bcd81e4f6aeb8bf0ee7d462588dc9d13')throw new Error('Parent freeze changed');
const manifest=JSON.parse(frozen);
const expected=new Map(manifest.files.map(f=>[f.path,f]));
function copy(from,to=from){
  const f=expected.get(from),b=fs.readFileSync(path.join(parent,from));
  if(!f||b.length!==f.bytes||hash(b)!==f.sha256)throw new Error('Parent file changed: '+from);
  write(to,b);
}
write('parent-freeze.json',frozen);
copy('input.json','input.parent.json');
copy('build-Release.json');
for(const f of manifest.files){
  if(f.path.startsWith('payloads/')||f.path.startsWith('harness/'))copy(f.path);
  else if(f.path.startsWith('transfer/'))copy(f.path,'volume/soui-input/'+f.path.slice('transfer/'.length));
}
for(const name of ['Verify-AppSerial.cjs','SelfTest-Serial.cjs','Freeze-Preparation.cjs','Verify-Frozen.cjs','SelfTest-Diagnostics.ps1'])copy(name);
const input=JSON.parse(fs.readFileSync(path.join(root,'input.parent.json')));
input.parentFreezeSha256=hash(frozen);
input.parentInputSha256=hash(fs.readFileSync(path.join(root,'input.parent.json')));
input.layoutVersion=3;
input.mountRoot=path.join(root,'volume');
input.inputSubdir='soui-input';
input.preparedAt=new Date().toISOString();
delete input.diagnosticPreparationSha256;
input.layoutPreparationSha256=hash(fs.readFileSync(__filename));
write('input.json',json(input));
const raw=fs.readFileSync(parentSerial);
if(raw.length!==5978||hash(raw)!=='3984889246a74c24d67dbd8775a7198af16852e3ba36da046dab2c9f736dfd94')throw new Error('Parent failure raw serial changed');
const {decodeSerial}=require(path.join(parent,'Verify-AppSerial.cjs'));
const decoded=decodeSerial(raw);
if(decoded.size!==8||decoded.get('run-exitcode.txt')?.toString('utf8').trim()!=='1')throw new Error('Parent failure contents differ');
write('parent-attempt2/serial-export.txt',raw);
for(const[n,b]of decoded)write('parent-attempt2/'+n,b);
write('layout-preparation.json',json({schema:'soui.win7-input-subdirectory-preparation/v1',preparedAt:input.preparedAt,
  parentRoot:parent,parentFreezeSha256:input.parentFreezeSha256,parentInputSha256:input.parentInputSha256,
  parentFailure:{serialPath:parentSerial,bytes:raw.length,sha256:hash(raw),runExitCode:1,applicationsStarted:false,
    rawFiles:[...decoded.keys()],observedExtraDirectory:'D:\\$RECYCLE.BIN',observedCount:11,expectedCount:10},
  inputSha256:hash(fs.readFileSync(path.join(root,'input.json'))),diagnosticsVersion:2,layoutVersion:3,
  mountRoot:input.mountRoot,inputSubdir:input.inputSubdir,transferFiles:10,payloadFiles:546,
  transferFilesByteIdenticalToParent:true,payloadBytesUnchanged:true,zipBytesUnchanged:true,
  guestScriptsUnchanged:true,guestExecuted:false,sourceReproductionClaim:false,releaseReady:false}));
process.stdout.write(json({status:'layout-v3-prepared-not-executed',inputSha256:hash(fs.readFileSync(path.join(root,'input.json'))),guestExecuted:false,releaseReady:false}));
