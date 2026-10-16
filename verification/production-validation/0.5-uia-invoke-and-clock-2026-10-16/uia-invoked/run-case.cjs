'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const [arch,buildAttempt,runAttempt,selected,exit]=process.argv.slice(2),expected=Number(exit);
assert(['x64','x86'].includes(arch));for(const value of [buildAttempt,runAttempt,selected])assert(/^[a-z0-9-]+$/.test(value));assert([0,1].includes(expected));
const buildPath=__dirname+'/'+arch+'-'+buildAttempt+'-BUILD-VERIFIED.json',build=JSON.parse(fs.readFileSync(buildPath));assert(build.built&&!build.actualTestsExecuted);
build.pins.forEach(e.checkPin);const pins=[e.pin(__filename),e.pin(buildPath),...build.pins];
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(name=>name.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:\\Windows\\System32;C:\\Windows';
const folder=__dirname+'/'+arch+'-'+runAttempt;assert(!fs.existsSync(folder));fs.mkdirSync(folder);
(async()=>{
  const c=await standard.capture(folder,'events',__dirname+'/out/'+arch+'-'+buildAttempt+'/Release/uia_event_probe.exe',[selected],
    {runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:__dirname,env,timeoutMs:300000,drainMs:2000});
  const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
  const natural=c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeBeforeCleanup===0&&v.terminal.activeProcesses===0;
  const matched=c.result.exitCode===expected&&c.result.supervisorExitCode===expected;pins.forEach(e.checkPin);
  fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({pins,selected,expected,matched,natural,result:c.result,terminal:v.terminal,currentVersionQualified:false},null,2)+'\n',{flag:'wx'});
  console.log(JSON.stringify({selected,expected,matched,natural,exitCode:c.result.exitCode}));assert(matched&&natural);
})().catch(error=>{console.error(error);process.exitCode=1;});
