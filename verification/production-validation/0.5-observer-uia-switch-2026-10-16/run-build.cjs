'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const [arch,attempt]=process.argv.slice(2);assert(['x64','x86'].includes(arch)&&/^[a-z0-9-]+$/.test(attempt));
const folder=__dirname+'/capture-build-'+arch+'-'+attempt;assert(!fs.existsSync(folder));fs.mkdirSync(folder);
const native=e.snapshot(__dirname+'/native'),headers=e.snapshot(root+'/sdk/include');
const pins=[__filename,__dirname+'/build.ps1',__dirname+'/cmake/toolchains/SouiLLVM22.cmake',
  __dirname+'/tools/internal/Invoke-ToolFileIdentity.ps1',__dirname+'/tools/internal/Tool-FileIdentity.ps1',
  root+'/benchmarks/protocol/host64-build-debug-observer.cpp',__dirname+'/before/host64-build-debug-observer.cpp',root+'/source-manifest.json',root+'/demos/reference-workbench/tests/public-endpoint-client.h',root+'/third_party/include/json/json.hpp'].map(e.pin);
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(name=>name.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:/Windows/System32;C:/Windows';
(async()=>{
  const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
  const args=['-NoProfile','-NonInteractive','-File',__dirname+'/build.ps1','-Architecture',arch,'-Attempt',attempt];
  fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({pins,native,headers,executable:ps,args},null,2)+'\n',{flag:'wx'});
  const c=await standard.capture(folder,'build',ps,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:__dirname,env,timeoutMs:300000,drainMs:2000});
  const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
  const natural=c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
  fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({pins,native,headers,natural,result:c.result,terminal:v.terminal,actualUiAutomationExecuted:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
  assert(natural,'Build failed; original attempt retained');pins.forEach(e.checkPin);e.verifySource(native,__dirname+'/native');e.verifySource(headers,root+'/sdk/include');
  const executable=e.pin(__dirname+'/out/'+arch+'-'+attempt+'/Release/ParserAfter.exe');
  fs.writeFileSync(folder+'/BUILT.json',JSON.stringify({built:true,pins,native,headers,executable,result:e.pin(folder+'/RESULT.json'),actualUiAutomationExecuted:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
  console.log(JSON.stringify({arch,built:true,natural,actualUiAutomationExecuted:false}));
})().catch(error=>{console.error(error);process.exitCode=1;});
