'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',[arch,variant]=process.argv.slice(2);
assert(['x64','x86'].includes(arch)&&['red','green'].includes(variant));
const e=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const source=__dirname+'/'+variant,output=__dirname+'/build-'+arch+'-'+variant,folder=__dirname+'/evidence-'+arch+'-'+variant;
assert(!fs.existsSync(output)&&!fs.existsSync(folder));fs.mkdirSync(output);fs.mkdirSync(folder);
const snapshot=e.snapshot(source),request=root+'/out/'+arch+'/llvm-profile-request-Release.json',profile=JSON.parse(fs.readFileSync(request)).discovery;
const original=e.snapshot(root+'/out/release-build-history/install-'+arch+'-source19'),prefix=output+'/installed';fs.cpSync(original.root,prefix,{recursive:true,errorOnExist:true,force:false});assert.deepEqual(e.snapshot(prefix).files,original.files);const installed=e.snapshot(prefix);
const zlibSource='C:/work/sm05-refresh-control-'+arch+'-first1/soui_zlib.lib';
assert(fs.existsSync(zlibSource),'need preserved source19 zlib');
const zlib=output+'/soui_zlib.lib';fs.copyFileSync(zlibSource,zlib);const pins=[__filename,request,zlib,root+'/cmake/toolchains/SouiLLVM22.cmake',__dirname+'/DECISION.json'].map(e.pin);
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:\\Windows\\System32;C:\\Windows';
const records=[];
async function stage(name,exe,args,expectedExit=0){
 const c=await standard.capture(folder,name,exe,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:output,env,timeoutMs:120000,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.exitCode===expectedExit&&c.result.supervisorExitCode===expectedExit&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeBeforeCleanup===0&&v.terminal.activeProcesses===0;
 records.push({name,expectedExit,natural,result:c.result,terminal:v.terminal});fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({arch,variant,pins,snapshot,records},null,2));
 console.log(JSON.stringify({arch,variant,name,actual:c.result.exitCode,natural}));assert(natural,name+' did not reach expected natural exit '+expectedExit);
}
(async()=>{
 await stage('configure','C:/Program Files/CMake/bin/cmake.exe',['-DCMAKE_PREFIX_PATH='+prefix,'-DSOUI_PROBE_ZLIB='+zlib,'-S',source,'-B',output,'-G','Ninja Multi-Config','-DCMAKE_TOOLCHAIN_FILE='+root+'/cmake/toolchains/SouiLLVM22.cmake',...Object.entries(profile.CMakeDefinitions).map(([k,v])=>'-D'+k+'='+v)]);
 await stage('build','C:/Program Files/CMake/bin/cmake.exe',['--build',output,'--config','Release','--verbose']);
 const dllSource=root+'/third_party/prebuilt/'+arch+'/miniblink/node.dll',dll=output+'/Release/node.dll';pins.push(e.pin(dllSource));fs.copyFileSync(dllSource,dll);e.checkPin({...e.pin(dllSource),path:dll});pins.push(e.pin(dll));
 fs.cpSync(source+'/resources',output+'/Release/resources',{recursive:true,errorOnExist:true,force:false});const resources=e.snapshot(output+'/Release/resources'),exe=e.pin(output+'/Release/ReleaseFixtureRegression.exe');
 await stage('probe',exe.path,[],variant==='red'?11:0);
 const log=fs.readFileSync(output+'/Release/smoke.log','utf8').trim().split(/\r?\n/);assert.deepEqual(log,['process-start','host-started','application-ready','host-close-complete']);
 pins.forEach(e.checkPin);e.checkPin(exe);e.verifySource(snapshot,source);e.verifySource(installed,prefix);e.verifySource(original,original.root);e.verifySource(resources,resources.root);
 fs.writeFileSync(folder+'/VERIFIED.json',JSON.stringify({arch,variant,observed:true,records,exe,pins,snapshot,installed,original,resources,log,completeReleaseVerification:false},null,2),{flag:'wx'});
})().catch(error=>{console.error(error);process.exitCode=1;});
