'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',work=__dirname,arch=process.argv[2];assert(['x86','x64'].includes(arch));
const e=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const pins=[__filename,work+'/build.ps1',work+'/native/CMakeLists.txt',work+'/proposal/tests/acceptance-app/process-address-census.h',work+'/proposal/tests/native/process_address_census_tests.cpp',root+'/source-manifest.json'].map(e.pin);
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(name=>name.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:\\Windows\\System32;C:\\Windows';
const records=[];
async function stage(name,exe,args,timeoutMs){
 pins.forEach(e.checkPin);const folder=work+'/'+arch+'-'+name;assert(!fs.existsSync(folder));fs.mkdirSync(folder);
 const c=await standard.capture(folder,name,exe,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:work,env,timeoutMs,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
 records.push({name,natural,result:c.result,terminal:v.terminal});fs.writeFileSync(work+'/'+arch+'-RESULTS.json',JSON.stringify({pins,records},null,2));
 console.log(JSON.stringify({arch,name,natural,exitCode:c.result.exitCode}));assert(natural,name+' failed');pins.forEach(e.checkPin);
}
(async()=>{
 await stage('build',ps,['-NoProfile','-NonInteractive','-File',work+'/build.ps1','-Architecture',arch,'-Attempt','census1'],300000);
 const exe=work+'/out/'+arch+'-census1/Release/SouiAddressCensusTests.exe';pins.push(e.pin(exe));
 await stage('contracts',exe,['contracts'],15000);await stage('actual',exe,['actual'],15000);
 const actual=JSON.parse(fs.readFileSync(work+'/'+arch+'-actual/actual-stdout.bin','utf8'));
 assert.equal(actual.state,'complete');assert.equal(actual.pointerBytes,arch==='x64'?8:4);assert.equal(actual.nonAtomic,true);assert.equal(actual.physicalMemory,false);
 assert(BigInt(actual.coveredBytes)===BigInt(actual.maximumAddress)-BigInt(actual.minimumAddress)+1n);
 assert(BigInt(actual.committedPrivateBytes)>=65536n&&BigInt(actual.committedMappedBytes)>=65536n&&BigInt(actual.committedImageBytes)>0n);
 const actualPin=e.pin(work+'/'+arch+'-actual/actual-stdout.bin');
 fs.writeFileSync(work+'/'+arch+'-VERIFIED.json',JSON.stringify({passed:true,architecture:arch,pins,records,actualPin,scope:'Fresh native collector and true current-process private/mapped/image pages; no SDK workload calibration or memory qualification',releaseReady:false},null,2)+'\n',{flag:'wx'});
})().catch(error=>{console.error(error);process.exitCode=1;});
