'use strict';
// File-only preparation. Does not execute PowerShell, application, archive, or VM code.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=__dirname;
const parent='C:/SOUI-SDK-production-validation/preview11-win7-preparation-20260926';
const serial='C:/SOUI-SDK-production-validation/preview11-win7-apps-attempt1/serial-export.txt';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const json=v=>JSON.stringify(v,null,2)+'\n';
const put=(name,b)=>fs.writeFileSync(path.join(root,name),b,{flag:'wx'});
if(fs.existsSync(path.join(root,'freeze.json')) || fs.existsSync(path.join(root,'diagnostic-preparation.json'))) throw new Error('Refuse to overwrite prepared/frozen diagnostics');
const parentFreezeBytes=fs.readFileSync(path.join(root,'parent-freeze.json'));
const parentFreeze=JSON.parse(parentFreezeBytes);
if(hash(parentFreezeBytes)!=='96d05251d6dd78848306fa3868c671f511c2b9ebc658951bf2f29d8e6a81b5ba') throw new Error('Parent freeze differs');
const inputBytes=fs.readFileSync(path.join(root,'input.parent.json'));
const input=JSON.parse(inputBytes);
if(hash(inputBytes)!=='84f275ea91aa5af51b8844724ec1dc4d70d0a2116268a4d1070f9539266863b0' || JSON.stringify(input)!==JSON.stringify(JSON.parse(fs.readFileSync(path.join(root,'input.json'))))) throw new Error('Parent input differs');
for(const f of parentFreeze.files) {
  if(!f.path.startsWith('payloads/') && !['transfer/SouiStarter.zip','transfer/SouiWorkbench.zip','transfer/SouiStarter-payload.tsv','transfer/SouiWorkbench-payload.tsv','build-Release.json'].includes(f.path)) continue;
  const copied=fs.readFileSync(path.join(root,f.path)),original=fs.readFileSync(path.join(parent,f.path));
  if(copied.length!==f.bytes || hash(copied)!==f.sha256 || !original.equals(copied)) throw new Error('Original payload byte identity differs: '+f.path);
}
const guard="if(@($files | Where-Object {$_.PSIsContainer}).Count -ne 0 -or $files.Count -ne $transferLines.Count+1){throw 'Transfer file set differs'}";
for(const file of [path.join(parent,'harness/run-apps.ps1'),path.join(root,'harness/run-apps.ps1')]) {
  if(!fs.readFileSync(file,'utf8').includes(guard)) throw new Error('Transfer set guard changed');
}
fs.copyFileSync(path.join(root,'harness/run-apps.ps1'),path.join(root,'transfer/run-apps.ps1'));
const transfer=path.join(root,'transfer');
const record=name=>{const b=fs.readFileSync(path.join(transfer,name));return {path:name,bytes:b.length,sha256:hash(b)}};
const records=fs.readdirSync(transfer).filter(n=>n!=='transfer-files.tsv').sort().map(record);
if(records.length!==9) throw new Error('Unexpected transfer file count');
fs.writeFileSync(path.join(transfer,'transfer-files.tsv'),'\uFEFF'+records.map(f=>`${f.sha256}\t${f.bytes}\t${f.path}`).join('\r\n')+'\r\n');
input.transferFiles=fs.readdirSync(transfer).sort().map(record);
input.diagnosticsVersion=2;
input.parentFreezeSha256=hash(parentFreezeBytes);
input.parentInputSha256=hash(inputBytes);
input.preparedAt=new Date().toISOString();
input.diagnosticPreparationSha256=hash(fs.readFileSync(__filename));
fs.writeFileSync(path.join(root,'input.json'),json(input));
const raw=fs.readFileSync(serial);
const {decodeSerial}=require(path.join(parent,'Verify-AppSerial.cjs'));
const decoded=decodeSerial(raw);
if(raw.length!==677 || decoded.size!==4 || decoded.get('run-exitcode.txt')?.toString('utf8').trim()!=='1' || decoded.get('harness-exception.txt')?.toString('utf8').trim()!=='Transfer file set differs') throw new Error('Parent failure identity differs');
fs.mkdirSync(path.join(root,'parent-attempt1'));
put('parent-attempt1/serial-export.txt',raw);
for(const[n,b]of decoded) put('parent-attempt1/'+n,b);
put('diagnostic-preparation.json',json({schema:'soui.win7-transfer-diagnostic-preparation/v1',preparedAt:input.preparedAt,
  parentRoot:parent,parentFreezeSha256:hash(parentFreezeBytes),parentInputSha256:hash(inputBytes),
  parentFailure:{serialPath:serial,bytes:raw.length,sha256:hash(raw),runExitCode:1,applicationsStarted:false,rawFiles:[...decoded.keys()]},
  inputSha256:hash(fs.readFileSync(path.join(root,'input.json'))),diagnosticsVersion:2,transferFiles:input.transferFiles.length,
  transferManifestRows:records.length,payloadFiles:546,payloadBytesUnchanged:true,zipBytesUnchanged:true,
  transferGuardUnchanged:true,guestExecuted:false,sourceReproductionClaim:false,releaseReady:false}));
process.stdout.write(json({status:'diagnostic-preparation-written-not-executed',inputSha256:hash(fs.readFileSync(path.join(root,'input.json'))),guestExecuted:false,releaseReady:false}));
