'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs'),s=require(root+'/tools/internal/standard-build.cjs');
const [mode,arch,buildAttempt,runAttempt]=process.argv.slice(2);assert(['outer','inner'].includes(mode)&&['x64','x86'].includes(arch)&&[buildAttempt,runAttempt].every(x=>/^[a-z0-9-]+$/.test(x)));
const builtFile=__dirname+'/build-'+arch+'-'+buildAttempt+'/BUILT.json',built=JSON.parse(fs.readFileSync(builtFile));assert(built.built);
const folder=__dirname+'/regression-'+arch+'-'+runAttempt,lease=__dirname+'/desktop-lease.ps1';
const pins=[e.pin(builtFile),...built.pins,...built.executables,built.result,e.pin(__filename),e.pin(lease),e.pin(__dirname+'/expect-failure.cjs')];
function check(){pins.forEach(e.checkPin);e.verifySource(built.native,__dirname+'/native');e.verifySource(built.installed,__dirname+'/sdk-'+arch);}
function exe(name){const row=built.executables.find(x=>path.basename(x.path)===name+'.exe');assert(row);return row.path;}
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(x=>x.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:/Windows/System32;C:/Windows';
const records=[];
async function capture(name,executable,args,expectedExit){check();const c=await s.capture(folder,name,executable,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:root,env,timeoutMs:120000,drainMs:2000});
 const v=s.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const closed=c.result.closureConfirmed&&v.terminal.activeProcesses===0&&!v.terminal.terminatedOwnedJob&&!c.result.stopRequested&&!c.result.fallbackUsed;
 records.push({name,expectedExit,closed,result:c.result,terminal:v.terminal});fs.writeFileSync(folder+'/'+mode+'-RESULT.json',JSON.stringify({records,pins,diagnosticOnly:true,releaseReady:false},null,2)+'\n');
 assert(closed&&c.result.exitCode===expectedExit&&c.result.supervisorExitCode===expectedExit,name+' actual exit/closure differs; original retained');check();return c;
}
(async()=>{
 if(mode==='outer'){fs.mkdirSync(folder);fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({arch,built,pins},null,2)+'\n',{flag:'wx'});
  await capture('desktop','C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe',['-NoProfile','-NonInteractive','-File',lease,'-Node',process.execPath,'-Controller',__filename,'-Architecture',arch,'-BuildAttempt',buildAttempt,'-RunAttempt',runAttempt],0);
 }else{
  await capture('original-owner-focus',process.execPath,[__dirname+'/expect-failure.cjs',exe('NativeUiaOriginalRegression'),folder+'/original-exit.json','owner-focus'],0);
  assert(fs.readFileSync(folder+'/original-owner-focus-stderr.bin','utf8').includes('owner focus uses the configured bounded pump'));
  await capture('original-owner-sync',process.execPath,[__dirname+'/expect-failure.cjs',exe('NativeUiaOriginalRegression'),folder+'/original-sync-exit.json','owner-sync','owner mutation uses the configured bounded pump'],0);
  const names=['properties','mutations','stale-secret','bounded','callback-close','uia-client','owner-focus','uia-focus','owner-sync','uia-mutations','property-events','tree-events','revoked-events','invoke-events','denied-invoke-events','failed-invoke-events','closed-invoke-events','com-initialization','api-close'];
  for(const name of names){
   await capture('candidate-'+name,exe('NativeUiaRegression'),[name],0);
   assert.equal(fs.readFileSync(folder+'/candidate-'+name+'-stdout.bin','utf8').trim(),'case='+name+' passed');
  }
  fs.writeFileSync(folder+'/VERIFIED.json',JSON.stringify({passed:true,arch,originalRegressionReproduced:true,originalExit:1,candidateCases:names,records,pins,installedProviderSourceOverlaid:true,fullPublicBuildExecuted:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
 }
 console.log(JSON.stringify({mode,arch,regressionComplete:true}));
})().catch(error=>{console.error(error);process.exitCode=1;});
