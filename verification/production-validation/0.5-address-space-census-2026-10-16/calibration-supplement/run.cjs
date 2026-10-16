'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',arch=process.argv[2],attempt=process.argv[3];
assert.equal(process.argv.length,4);assert(['x64','x86'].includes(arch)&&/^[a-z0-9-]+$/.test(attempt));
const source=__dirname+'/source',output=__dirname+'/output-'+arch+'-'+attempt,folder=__dirname+'/capture-'+arch+'-'+attempt;
assert(!fs.existsSync(output)&&!fs.existsSync(folder));fs.mkdirSync(output);fs.mkdirSync(folder);
const evidence=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const request=root+'/out/'+arch+'/llvm-profile-request-Release.json',profile=JSON.parse(fs.readFileSync(request)).discovery;
require(root+'/tools/internal/build-evidence.cjs').verifyAcceptedBuild(root,arch,'Release',false,{nativeToolchain:'LLVM22MSVC142',profileRequest:request});
const originalPrefix=root+'/out/install/'+arch,prefix=output+'/installed',original=evidence.snapshot(originalPrefix);
fs.cpSync(originalPrefix,prefix,{recursive:true,errorOnExist:true,force:false});
evidence.verifySource(original,originalPrefix);const copied=evidence.snapshot(prefix);
assert.deepEqual(copied.files,original.files,'Relocated installed SDK bytes differ');
const snapshot=evidence.snapshot(source);
const pins=[__filename,request,root+'/source-manifest.json',root+'/out/'+arch+'/build-Release.json',source+'/CALIBRATION.json'].map(evidence.pin);
const kernel=output+'/node.dll';fs.copyFileSync(root+'/third_party/prebuilt/'+arch+'/miniblink/node.dll',kernel,fs.constants.COPYFILE_EXCL);
const kernelPin=evidence.pin(kernel);assert.equal(kernelPin.sha256,evidence.pin(root+'/third_party/prebuilt/'+arch+'/miniblink/node.dll').sha256);
const args=['-S',source,'-B',output+'/build','-G','Ninja Multi-Config','-DCMAKE_TOOLCHAIN_FILE='+root+'/cmake/toolchains/SouiLLVM22.cmake',
 '-DCMAKE_PREFIX_PATH='+prefix,'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',...Object.entries(profile.CMakeDefinitions).map(([k,v])=>'-D'+k+'='+v)];
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:\\Windows\\System32;C:\\Windows';
const records=[];
function result(){return {scope:'Finite image/font/cache observation; concurrent external work is not isolated performance evidence',arch,pins,original,copied,snapshot,kernelPin,records,fullCacheQualified:false,releaseReady:false};}
async function stage(name,executable,argv,timeoutMs){
 const c=await standard.capture(folder,name,executable,argv,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:output,env,timeoutMs,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeBeforeCleanup===0&&v.terminal.activeProcesses===0;
 records.push({name,natural,result:c.result,terminal:v.terminal});fs.writeFileSync(folder+'/RESULT.json',JSON.stringify(result(),null,2));
 console.log(JSON.stringify({arch,name,natural,exitCode:c.result.exitCode}));assert(natural,name+' failed; retain all records');
}
(async()=>{
 fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({...result(),source,output,args},null,2),{flag:'wx'});
 await stage('configure','C:/Program Files/CMake/bin/cmake.exe',args,300000);
 await stage('build','C:/Program Files/CMake/bin/cmake.exe',['--build',output+'/build','--config','Release','--verbose'],300000);
 const binaryDir=output+'/build/bin/Release';
 function copyRuntime(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const item=path.join(dir,entry.name);if(entry.isDirectory())copyRuntime(item);else if(entry.name.endsWith('.dll'))fs.copyFileSync(item,path.join(binaryDir,entry.name));}}
 copyRuntime(root+'/third_party/prebuilt/'+arch+'/runtime');const payload=evidence.snapshot(binaryDir);
 fs.mkdirSync(output+'/page-results');
 await stage('calibration',binaryDir+'/SouiCacheCalibration.exe',['--resources='+source+'/web','--miniblink='+kernel,'--result-dir='+output+'/page-results','--exit-on-result'],210000);
 pins.forEach(evidence.checkPin);evidence.checkPin(kernelPin);evidence.verifySource(snapshot,source);evidence.verifySource(copied,prefix);evidence.verifySource(payload,binaryDir);
 const names=fs.readdirSync(output+'/page-results');assert.equal(names.length,1);
 const page=evidence.pin(output+'/page-results/'+names[0]),value=JSON.parse(fs.readFileSync(page.path));
 assert.equal(value.completed,true);assert.equal(value.cycles.length,12);
 value.cycles.forEach((row,i)=>assert.deepEqual(row,{cycle:i+1,loaded:24,width:512,height:512,fontRows:12,scaledCanvasDraws:24,scaledCanvasWidth:384,scaledCanvasHeight:384,renderedImageWidth:160,renderedImageHeight:160}));
 fs.writeFileSync(folder+'/CAPTURE.json',JSON.stringify({...result(),page,payload,completedWorkload:true,analysisPending:true},null,2),{flag:'wx'});
 console.log(JSON.stringify({arch,completedWorkload:true,fullCacheQualified:false}));
})().catch(error=>{console.error(error);process.exitCode=1;});
