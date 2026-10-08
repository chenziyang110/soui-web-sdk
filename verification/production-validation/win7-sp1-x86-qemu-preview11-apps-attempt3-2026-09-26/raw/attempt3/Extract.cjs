'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const prep='C:/SOUI-SDK-production-validation/preview11-win7-preparation-v3-20260926';
const {decodeSerial}=require(path.join(prep,'Verify-AppSerial.cjs'));
const raw=fs.readFileSync(path.join(__dirname,'serial-export.txt'));
const decoded=decodeSerial(raw),out=path.join(__dirname,'decoded');fs.mkdirSync(out);
const files=[];
for(const [name,b] of decoded){fs.writeFileSync(path.join(out,name),b,{flag:'wx'});files.push({path:name,bytes:b.length,sha256:crypto.createHash('sha256').update(b).digest('hex')});}
fs.writeFileSync(path.join(__dirname,'decoded-manifest.json'),JSON.stringify({serialSha256:crypto.createHash('sha256').update(raw).digest('hex'),files},null,2)+'\n',{flag:'wx'});
for(const name of ['guest-identity.txt','harness.log','harness-exception.txt','run-exitcode.txt','starter-run.txt','starter-exitcode.txt','starter-smoke.log','workbench-default-run.txt','workbench-default-exitcode.txt','workbench-default-smoke.log','workbench-react-run.txt','workbench-react-exitcode.txt','workbench-react-smoke.log'])if(decoded.has(name))console.log(name+'\n'+decoded.get(name).toString('utf8'));
console.log(JSON.stringify({rawFiles:files.length,serialBytes:raw.length}));
