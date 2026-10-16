'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',[arch,attempt]=process.argv.slice(2);assert(['x64','x86'].includes(arch)&&/^[1-9][0-9]*$/.test(attempt));
const source=__dirname+'/reference-gui-controller-1',output='C:/work/sm05-gui-controller-'+arch+'-'+attempt,folder=__dirname+'/reference-gui-build-'+arch+'-'+attempt;
const id=require(root+'/tools/internal/delivery-identity.cjs'),reader=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const request=root+'/out/sdk-'+arch+'/llvm-profile-request-Release.json',profile=JSON.parse(fs.readFileSync(request,'utf8')).discovery;
const sdk=require(root+'/tools/internal/build-evidence.cjs').verifyAcceptedBuild(root,arch,'Release',true,{nativeToolchain:'LLVM22MSVC142',profileRequest:request});
assert(!fs.existsSync(output)&&!fs.existsSync(folder));fs.mkdirSync(output);fs.mkdirSync(folder);
const prefix=root+'/out/install/'+arch,installed=reader.snapshot(prefix),pins=[__filename,source+'/CMakeLists.txt',source+'/controller.cpp',root+'/demos/reference-workbench/tests/public-endpoint-client.h',request,root+'/out/sdk-'+arch+'/build-Release.json'].map(reader.pin);
const args=['-S',source,'-B',output,'-G','Ninja Multi-Config','-DCMAKE_TOOLCHAIN_FILE='+root+'/cmake/toolchains/SouiLLVM22.cmake','-DCMAKE_PREFIX_PATH='+prefix,'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DREFERENCE_SOURCE_ROOT='+root+'/demos/reference-workbench',...Object.entries(profile.CMakeDefinitions).map(([k,v])=>'-D'+k+'='+v)];
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json','utf8')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:\\Windows\\System32;C:\\Windows';
const records=[];
async function stage(name,argv){
 const c=await standard.capture(folder,name,'C:/Program Files/CMake/bin/cmake.exe',argv,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:output,env,timeoutMs:300000,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
 records.push({name,natural,result:c.result,terminal:v.terminal});fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({arch,records,pins,sdkBuildId:sdk.build.buildId,releaseReady:false},null,2));console.log(JSON.stringify({name,natural}));assert(natural,name+' failed');
}
(async()=>{
 fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({arch,source,output,prefix,pins,installed,args},null,2));
 await stage('configure',args);await stage('build',['--build',output,'--config','Release','--parallel','2','--verbose']);
 const crt=root+'/third_party/prebuilt/'+arch+'/runtime';for(const file of id.walk(crt).filter(x=>x.toLowerCase().endsWith('.dll')))fs.copyFileSync(path.join(crt,file),path.join(output,'Release',path.basename(file)));
 pins.forEach(reader.checkPin);reader.verifySource(installed,prefix);
 fs.writeFileSync(folder+'/ARTIFACTS.json',JSON.stringify({arch,pins,installed,sdkBuildId:sdk.build.buildId,executable:reader.pin(output+'/Release/ReferenceGuiController.exe'),map:reader.pin(output+'/ReferenceGuiController.map'),payload:id.snapshot(output+'/Release',id.walk(output+'/Release')),scope:'Actual native finite GUI test coordinator; not SDK or release qualification'},null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
