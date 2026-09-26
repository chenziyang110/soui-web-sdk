'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const prep='C:/SOUI-SDK-production-validation/preview11-win7-preparation-v3-20260926';
const input=JSON.parse(fs.readFileSync(path.join(prep,'input.json'),'utf8'));
const raw=fs.readFileSync(path.join(__dirname,'serial-export.txt'));
const files=require(path.join(prep,'Verify-AppSerial.cjs')).decodeSerial(raw);
const text=n=>files.get(n)?.toString('utf8').replace(/^\uFEFF/,'');
const check=(ok,msg)=>{if(!ok)throw Error(msg);};
function verifyTsv(name,expected){
 const lines=text(name).trim().split(/\r?\n/).map(s=>s.split('\t'));
 check(lines.length===expected.length,'File count '+name);
 const paths=new Set();for(const [sha,bytes,p] of lines){check(!paths.has(p),'Duplicate path');paths.add(p);const f=expected.find(f=>f.path===p);check(f&&f.sha256.toLowerCase()===sha.toLowerCase()&&f.bytes===Number(bytes),'TSV mismatch '+p);}
}
check(/Version=6\.1\.7601\r?\n/.test(text('guest-identity.txt'))&&/ServicePackMajorVersion=1/.test(text('guest-identity.txt'))&&/PointerBytes=4/.test(text('guest-identity.txt'))&&/PowerShellMajor=2/.test(text('guest-identity.txt')),'Guest identity');
verifyTsv('transfer-observed.tsv',input.transferFiles);
for(const name of ['SouiStarter','SouiWorkbench'])verifyTsv(name.toLowerCase()+'-payload-pre.tsv',input.packages[name].files);
for(const name of ['starter-zip-test','workbench-zip-test','starter-extract','workbench-extract','payload-pre','starter'])check(text(name+'-exitcode.txt').trim()==='0','Pre-step failed '+name);
check(text('starter-smoke.log').trim().replace(/\r\n/g,'\n')==='process-start\nhost-started\napplication-ready\nhost-close-complete','Starter lifecycle');
const wb=text('workbench-default-smoke.log').trim().replace(/\r\n/g,'\n');
check(wb.includes('\napplication-ready\n')&&wb.endsWith('host-close-start'),'Workbench stopping point');
check(/TimedOut=True/.test(text('workbench-default-run.txt'))&&text('workbench-default-exitcode.txt').trim()==='-1','Workbench harness timeout');
check(!files.has('workbench-react-run.txt')&&!files.has('run-complete.txt')&&!files.has('payload-post.log')&&text('run-exitcode.txt').trim()==='1','Failure scope');
const qemu=JSON.parse(fs.readFileSync(path.join(__dirname,'qemu-result.json'),'utf8').replace(/^\uFEFF/,''));
check(qemu.exitCode===0&&!qemu.timedOut&&qemu.diskUnchanged&&qemu.frozenCheckExitCode===0,'Host cleanup');
const summary={schema:'soui.win7-preview11-partial-failure-review/v1',serialSha256:crypto.createHash('sha256').update(raw).digest('hex'),rawFiles:files.size,guest:'Windows 7 SP1 x86, PowerShell 2',transferFilesVerified:input.transferFiles.length,payloadPreFiles:546,legalPreFiles:10,starter:{exitCode:0,ready:true,closeComplete:true,payloadPostVerified:false},workbenchDefault:{ready:true,closeStarted:true,closeComplete:false,timedOut:true,harnessExitCode:-1,nativeExitCode:null},workbenchReactExecuted:false,qemuExitCode:0,baseDiskUnchanged:true,frozenInputUnchanged:true,allApplicationsPassed:false,sourceReproductionClaim:false,releaseReady:false};
fs.writeFileSync(path.join(__dirname,'partial-review.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(summary));
