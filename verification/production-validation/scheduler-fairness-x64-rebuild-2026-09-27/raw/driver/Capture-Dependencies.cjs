'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),crypto=require('node:crypto');
const {pipeline}=require('node:stream/promises');
const [name,limitText,program,...args]=process.argv.slice(2),limit=Number(limitText);
if(!/^[a-z0-9-]+$/.test(name||'')||!Number.isSafeInteger(limit)||limit<1000||limit>7200000||!program)throw Error('Usage: Capture-Command.cjs NAME TIMEOUT_MS PROGRAM ARGS...');
const root=path.join(__dirname,'command-logs',name);fs.mkdirSync(path.dirname(root),{recursive:true});fs.mkdirSync(root);
const out=path.join(root,'stdout.log'),err=path.join(root,'stderr.log');
const record={program,args,cwd:__dirname,startedAt:new Date().toISOString(),pid:null,exitCode:null,signal:null,timedOut:false,error:null};
const child=cp.spawn(program,args,{cwd:__dirname,windowsHide:true,stdio:['ignore','pipe','pipe']});
record.pid=child.pid??null;
const fail=e=>{record.error??=String(e.stack||e)};
child.on('error',fail);
const copies=[pipeline(child.stdout,fs.createWriteStream(out,{flags:'wx'})).catch(fail),pipeline(child.stderr,fs.createWriteStream(err,{flags:'wx'})).catch(fail)];
const timer=setTimeout(()=>{record.timedOut=true;if(child.exitCode===null&&child.signalCode===null&&child.pid){
 const killed=cp.spawnSync(path.join(process.env.SystemRoot,'System32/taskkill.exe'),['/PID',String(child.pid),'/T','/F'],{windowsHide:true,timeout:15000});
 record.cleanupExitCode=killed.status; if(killed.error)fail(killed.error);
}},limit);
child.on('close',async(code,signal)=>{
 clearTimeout(timer);record.exitCode=code;record.signal=signal;
 await Promise.all(copies);record.finishedAt=new Date().toISOString();
 for(const [key,file]of [['stdout',out],['stderr',err]]){
  try {const fd=fs.openSync(file,'r+');try{fs.fsyncSync(fd)}finally{fs.closeSync(fd)}const b=fs.readFileSync(file);record[key]={bytes:b.length,sha256:crypto.createHash('sha256').update(b).digest('hex')};}
  catch(error){fail(error)}
 }
 fs.writeFileSync(path.join(root,'result.json'),JSON.stringify(record,null,2)+'\n',{flag:'wx'});
 console.log(JSON.stringify({name,root,exitCode:code,timedOut:record.timedOut,error:record.error}));
 process.exitCode=code===0&&!record.timedOut&&!record.error?0:1;
});
