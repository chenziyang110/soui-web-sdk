'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',work=__dirname;
const standard=require(root+'/tools/internal/standard-build.cjs'),e=require(root+'/tools/internal/dependency-evidence.cjs');
const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];Object.assign(env,policy.set);
env.ComSpec='C:/Windows/System32/cmd.exe';env.Path=path.dirname(process.execPath)+';C:/Program Files/CMake/bin;C:/Program Files/Git/cmd;C:/Windows/System32;C:/Windows';
const pins=[__filename,work+'/native/main.cpp',work+'/native/CMakeLists.txt',work+'/build-native.ps1',root+'/out/install/x64/lib/Release/soui_sdk.lib'].map(e.pin),records=[];
async function run(name,exe,args,expected=0){
 const output=work+'/'+name;fs.mkdirSync(output);
 const c=await standard.capture(output,name,exe,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:work,env,timeoutMs:300000,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const matched=c.result.exitCode===expected&&c.result.supervisorExitCode===expected&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&v.terminal.activeProcesses===0;
 const natural=!v.terminal.terminatedOwnedJob&&v.terminal.activeBeforeCleanup===0;
 const text=fs.readFileSync(output+'/'+name+'-stdout.bin','utf8').trim();
 records.push({name,args,expected,matched,natural,text,result:c.result,terminal:v.terminal});
 fs.writeFileSync(work+'/NATIVE-RESULTS.json',JSON.stringify({pins,records,currentVersionQualified:false},null,2));
 console.log(JSON.stringify({name,matched,natural,exitCode:c.result.exitCode,text:text.slice(-300)}));
 assert(matched&&(expected!==0||natural),'Native check failed: '+name);
 pins.forEach(e.checkPin);return text;
}
(async()=>{
 await run('build',ps,['-NoProfile','-NonInteractive','-File',work+'/build-native.ps1']);
 const exe=work+'/native-build/Release/legal_native_probe.exe';pins.push(e.pin(exe));
 assert.equal(await run('original-denied',exe,[work+'/original/app/resources'],1),'E_REMOTE_RESOURCE');
 assert.equal(await run('proposed-accepted',exe,[work+'/proposed/app/resources']),'accepted');
 for(const [name,filename,body,code] of [
  ['remote-script','app.js','fetch("https://example.invalid/runtime.js")','E_REMOTE_RESOURCE'],
  ['remote-manifest','app.json','{"url":"https://example.invalid/runtime.js"}','E_REMOTE_RESOURCE'],
  ['remote-css','app.css','body{background:url(https://example.invalid/image.png)}','E_REMOTE_RESOURCE'],
  ['frame','frame.html','<iframe src="local.html"></iframe>','E_UNTRUSTED_FRAME'],
  ['legal-remote','legal/custom.json','{"url":"https://example.invalid/runtime.js"}','E_REMOTE_RESOURCE']]){
  const target=work+'/negative-'+name;fs.mkdirSync(target);fs.writeFileSync(target+'/index.html','<div>local</div>');
  fs.mkdirSync(path.dirname(target+'/'+filename),{recursive:true});fs.writeFileSync(target+'/'+filename,body);
  assert.equal(await run(name,exe,[target],1),code);
 }
 fs.writeFileSync(work+'/NATIVE-VERIFIED.json',JSON.stringify({passed:true,pins,records,scope:'Unchanged Source22 installed host resource validator; inert metadata packaging only',currentVersionQualified:false},null,2),{flag:'wx'});
})().catch(error=>{console.error(error);process.exitCode=1;});
