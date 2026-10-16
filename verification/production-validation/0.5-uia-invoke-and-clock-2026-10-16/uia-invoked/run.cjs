'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',work=__dirname,standard=require(root+'/tools/internal/standard-build.cjs'),e=require(root+'/tools/internal/dependency-evidence.cjs');
const [provider='before',arch='x64',attempt='red1',selected='property-events',expectedText='1']=process.argv.slice(2);
assert(['before','proposal'].includes(provider)&&['x64','x86'].includes(arch)&&/^[a-z0-9-]+$/.test(attempt));
const expected=Number(expectedText),ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];Object.assign(env,policy.set);
env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:/Program Files/CMake/bin;C:/Program Files/Git/cmd;C:/Windows/System32;C:/Windows';
const pins=[__filename,work+'/build.ps1',work+'/native/CMakeLists.txt',work+'/proposal/tests/native/windows_uia_provider_tests.cpp',
 work+'/'+provider+'/sdk/accessibility/uia/windows_uia_provider.cpp',root+'/sdk/src/runtime/bridge/action_contract.cpp',root+'/source-manifest.json'].map(e.pin),records=[];
async function run(name,exe,args,code){
 const output=work+'/'+arch+'-'+attempt+'-'+name;fs.mkdirSync(output);
 const c=await standard.capture(output,name,exe,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:work,env,timeoutMs:300000,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeBeforeCleanup===0&&v.terminal.activeProcesses===0;
 const matched=c.result.exitCode===code&&c.result.supervisorExitCode===code;
 records.push({name,expected:code,natural,matched,result:c.result,terminal:v.terminal});
 fs.writeFileSync(work+'/'+arch+'-'+attempt+'-RESULTS.json',JSON.stringify({pins,records,currentVersionQualified:false},null,2));
 console.log(JSON.stringify({name,expected:code,natural,matched,exitCode:c.result.exitCode}));
 pins.forEach(e.checkPin);assert(matched&&(code!==0||natural),'Actual native result differs: '+output);
}
(async()=>{
 await run('build',ps,['-NoProfile','-NonInteractive','-File',work+'/build.ps1','-Provider',provider,'-Architecture',arch,'-Attempt',attempt],0);
 const exe=work+'/out/'+arch+'-'+attempt+'/Release/uia_event_probe.exe';pins.push(e.pin(exe));
 if(selected==='build-only') {
   fs.writeFileSync(work+'/'+arch+'-'+attempt+'-BUILD-VERIFIED.json',JSON.stringify({built:true,pins,records,actualTestsExecuted:false,currentVersionQualified:false},null,2),{flag:'wx'});
   return;
 } else if(selected==='each') {
   for(const name of ['properties','mutations','stale-secret','bounded','callback-close','uia-client','com-initialization','api-close','property-events','tree-events','revoked-events',
     'invoke-events','denied-invoke-events','failed-invoke-events','closed-invoke-events'])
     await run('case-'+name,exe,[name],expected);
 } else await run('events',exe,[selected],expected);
 fs.writeFileSync(work+'/'+arch+'-'+attempt+'-VERIFIED.json',JSON.stringify({expectedObserved:true,pins,records,currentVersionQualified:false},null,2),{flag:'wx'});
})().catch(error=>{console.error(error);process.exitCode=1;});
