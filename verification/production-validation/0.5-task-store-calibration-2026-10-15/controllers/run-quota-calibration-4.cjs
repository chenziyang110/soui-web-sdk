'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',arch=process.argv[2],attempt=process.argv[3],limit=process.argv[4];assert(/^[1-9][0-9]*$/.test(limit)&&BigInt(limit)<=0xffffffffn);assert(['x64','x86'].includes(arch)&&/^[a-z0-9-]+$/.test(attempt));
const output='C:/work/sm05-quota-calibration-'+arch+'-'+attempt,folder=__dirname+'/quota-calibration-'+arch+'-'+attempt,prefix=root+'/out/install/'+arch;
assert(!fs.existsSync(output)&&!fs.existsSync(folder));fs.mkdirSync(output);fs.mkdirSync(folder);
const reader=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const request=root+'/out/sdk-'+arch+'/llvm-profile-request-Release.json',profile=JSON.parse(fs.readFileSync(request,'utf8')).discovery;
require(root+'/tools/internal/build-evidence.cjs').verifyAcceptedBuild(root,arch,'Release',true,{nativeToolchain:'LLVM22MSVC142',profileRequest:request});
const pins=[__filename,request,root+'/out/sdk-'+arch+'/build-Release.json',root+'/tests/calibration/CMakeLists.txt',root+'/tests/calibration/task-store-quota.cpp'].map(reader.pin),installed=reader.snapshot(prefix);
const args=['-S',root+'/tests/calibration','-B',output,'-G','Ninja Multi-Config','-DCMAKE_TOOLCHAIN_FILE='+root+'/cmake/toolchains/SouiLLVM22.cmake',
 '-DCMAKE_PREFIX_PATH='+prefix,'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',...Object.entries(profile.CMakeDefinitions).map(([k,v])=>'-D'+k+'='+v)];
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json','utf8')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:\\Windows\\System32;C:\\Windows';
const records=[];
async function stage(name,executable,argv,timeoutMs){const c=await standard.capture(folder,name,executable,argv,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:output,env,timeoutMs,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result),natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
 records.push({name,natural,result:c.result,terminal:v.terminal});fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({scope:'Actual independently compiled calibration consumer against explicitly captured installed SDK; not final default qualification',arch,installed,pins,records},null,2));
 console.log(JSON.stringify({arch,name,natural,exitCode:c.result.exitCode}));assert(natural,name+' failed; retain original records');}
(async()=>{
 fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({arch,output,prefix,pins,installed,args,explicitRequestQuota:limit},null,2));
 await stage('configure','C:/Program Files/CMake/bin/cmake.exe',args,300000);
 await stage('build','C:/Program Files/CMake/bin/cmake.exe',['--build',output,'--config','Release','--verbose'],300000);
 function copyRuntime(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,e.name);if(e.isDirectory())copyRuntime(p);else if(e.name.endsWith('.dll'))fs.copyFileSync(p,path.join(output,'Release',e.name));}}copyRuntime(root+'/third_party/prebuilt/'+arch+'/runtime');
 await stage('baseline',output+'/Release/SouiTaskStoreCalibration.exe',[limit],360000);
 pins.forEach(reader.checkPin);reader.verifySource(installed,prefix);
 console.log(JSON.stringify({arch,passed:true,productionDefaultQualified:false}));
})().catch(e=>{console.error(e);process.exitCode=1;});
