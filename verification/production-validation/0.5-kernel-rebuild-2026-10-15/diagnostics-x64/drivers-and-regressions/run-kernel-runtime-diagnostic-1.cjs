'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const sdk='C:/work/sm05-release-050',out=__dirname+'/kernel-runtime-x64-1',destination='C:/work/sm05-kernel-runtime-x64-1';
const reader=require(sdk+'/tools/internal/dependency-evidence.cjs'),standard=require(sdk+'/tools/internal/standard-build.cjs');
const runner='F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe';
assert(!fs.existsSync(out)&&!fs.existsSync(destination));fs.mkdirSync(out);fs.mkdirSync(destination);
const build=JSON.parse(fs.readFileSync(sdk+'/out/x64/build-Release.json','utf8'));
const kernel=JSON.parse(fs.readFileSync(__dirname+'/kernel-x64-1/component-proof.json','utf8'));reader.checkPin(kernel.artifacts[0]);
const imports=JSON.parse(fs.readFileSync(sdk+'/third_party/manifests/soui-build-050.json','utf8')).imports.filter(x=>x.architecture==='x64'&&x.kind==='dll');
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json','utf8')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:\\Windows\\System32;C:\\Windows';
(async()=>{
 const results=[];
 for(const [target,scenario,args] of [['SouiStarter','default',['--smoke-test']],['SouiWorkbench','default',['--webview-smoke-test','--arch=x64']],['SouiWorkbench','react',['--webview-smoke-test','--arch=x64','--react']]]){
  const cwd=destination+'/'+target+'-'+scenario;fs.mkdirSync(cwd);
  for(const f of build.payloads[target].files){const source=sdk+'/out/x64/bin/Release/'+target+'/'+f.path;reader.checkPin({...f,path:source});const dest=path.join(cwd,f.path);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(source,dest,fs.constants.COPYFILE_EXCL);}
  fs.copyFileSync(kernel.artifacts[0].path,cwd+'/node.dll');
  for(const item of imports){reader.checkPin({...item,path:sdk+'/'+item.path});fs.copyFileSync(sdk+'/'+item.path,cwd+'/'+path.basename(item.path));}
  const pins=reader.snapshot(cwd),c=await standard.capture(out,target+'-'+scenario,cwd+'/'+target+'.exe',args,{runner,cwd,env,timeoutMs:45000,drainMs:2000});
  const verified=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
  const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!verified.terminal.terminatedOwnedJob&&verified.terminal.activeProcesses===0;
  for(const pin of pins.files)reader.checkPin({...pin,path:path.join(cwd,pin.path)});
  results.push({target,scenario,pins,result:c.result,terminal:verified.terminal,natural});
  fs.writeFileSync(out+'/RESULT.json',JSON.stringify({scope:'Unchanged historical build8 EXE with current rebuilt dependency DLLs; diagnostic, not current SDK acceptance',kernel,results},null,2));
  console.log(JSON.stringify({target,scenario,natural,exitCode:c.result.exitCode}));
 }
 assert(results.every(r=>r.natural),'Actual dependency runtime diagnostic failed');
})().catch(e=>{console.error(e);process.exitCode=1;});
