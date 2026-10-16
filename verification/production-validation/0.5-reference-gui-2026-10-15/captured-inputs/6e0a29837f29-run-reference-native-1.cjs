'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',arch=process.argv[2],attempt=process.argv[3];
assert(['x64','x86'].includes(arch)&&/^[1-9][0-9]*$/.test(attempt));
const reference=root+'/demos/reference-workbench',output='C:/work/sm05-reference-'+arch+'-'+attempt;
const folder=__dirname+'/reference-native-'+arch+'-'+attempt,prefix=root+'/out/install/'+arch;
const id=require(root+'/tools/internal/delivery-identity.cjs'),reader=require(root+'/tools/internal/dependency-evidence.cjs');
const standard=require(root+'/tools/internal/standard-build.cjs');
const request=root+'/out/sdk-'+arch+'/llvm-profile-request-Release.json';
const profile=JSON.parse(fs.readFileSync(request,'utf8')).discovery;
const sdk=require(root+'/tools/internal/build-evidence.cjs').verifyAcceptedBuild(root,arch,'Release',true,{nativeToolchain:'LLVM22MSVC142',profileRequest:request});
const web=JSON.parse(fs.readFileSync(__dirname+'/reference-web-2/ARTIFACTS.json','utf8'));
for(const artifact of web.artifacts)assert.deepEqual(id.snapshot(artifact.root,id.walk(artifact.root)),artifact.identity);
assert(!fs.existsSync(output)&&!fs.existsSync(folder));fs.mkdirSync(output);fs.mkdirSync(folder);
const source=id.snapshot(reference,id.walk(reference,'',(name,leaf)=>id.skipped.has(leaf)));
assert.deepEqual(source,web.source);
const installed=reader.snapshot(prefix),pins=[__filename,request,root+'/source-manifest.json',root+'/out/sdk-'+arch+'/build-Release.json'].map(reader.pin);
const args=['-S',reference,'-B',output,'-G','Ninja Multi-Config','-DCMAKE_TOOLCHAIN_FILE='+root+'/cmake/toolchains/SouiLLVM22.cmake',
 '-DCMAKE_PREFIX_PATH='+prefix,'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_EXE_LINKER_FLAGS:STRING=/MAP',
 '-DSOUI_REFERENCE_NATIVE_TOOLCHAIN=LLVM22MSVC142','-DSOUI_REFERENCE_LLVM_SDK_REQUEST='+request,'-DSOUI_REFERENCE_SDK_CONFIGURATION=Release',
 '-DSOUI_REFERENCE_DISTRIBUTION_ROOT='+root,'-DSOUI_REFERENCE_MINIBLINK='+root+'/third_party/prebuilt/'+arch+'/miniblink/node.dll',
 '-DSOUI_REFERENCE_MODERN=ON','-DBUILD_TESTING=ON',...Object.entries(profile.CMakeDefinitions).map(([k,v])=>'-D'+k+'='+v)];
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json','utf8')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';
env.Path=path.dirname(process.execPath)+';C:\\Program Files\\CMake\\bin;C:\\Windows\\System32;C:\\Windows';
const records=[];
async function stage(name,executable,argv,timeoutMs){
 const c=await standard.capture(folder,name,executable,argv,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:output,env,timeoutMs,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
 records.push({name,natural,result:c.result,terminal:v.terminal});
 fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({arch,source,installed,pins,web,sdkBuildId:sdk.build.buildId,records,nativeHostQualified:false,releaseReady:false},null,2));
 console.log(JSON.stringify({arch,name,natural,exitCode:c.result.exitCode}));assert(natural,name+' failed');
}
(async()=>{
 fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({arch,output,prefix,pins,source,installed,web,args},null,2));
 await stage('configure','C:/Program Files/CMake/bin/cmake.exe',args,360000);
 await stage('build','C:/Program Files/CMake/bin/cmake.exe',['--build',output,'--config','Release','--parallel','2','--verbose'],600000);
 const crtRoot=root+'/third_party/prebuilt/'+arch+'/runtime';
 for(const name of id.walk(crtRoot).filter(name=>name.toLowerCase().endsWith('.dll')))fs.copyFileSync(path.join(crtRoot,name),path.join(output,'Release',path.basename(name)));
 await stage('native-tests','C:/Program Files/CMake/bin/ctest.exe',['--test-dir',output,'-C','Release','--output-on-failure'],300000);
 pins.forEach(reader.checkPin);reader.verifySource(installed,prefix);
 assert.deepEqual(id.snapshot(reference,id.walk(reference,'',(name,leaf)=>id.skipped.has(leaf))),source);
 for(const artifact of web.artifacts)assert.deepEqual(id.snapshot(artifact.root,id.walk(artifact.root)),artifact.identity);
 const apps=['SouiReferenceCompatReact','SouiReferenceCompatVue','SouiReferenceModern'].map(name=>{
  const base=output+'/bin/Release/'+name;return {name,root:base,payload:id.snapshot(base,id.walk(base))};
 });
 fs.writeFileSync(folder+'/ARTIFACTS.json',JSON.stringify({arch,apps,buildFiles:id.walk(output).filter(name=>/\.(exe|lib|map|obj)$/.test(name)).map(name=>reader.pin(path.join(output,name))),source,installed,pins,nativeHostQualified:false,releaseReady:false},null,2));
 console.log(JSON.stringify({arch,nativeBuildAndTestsPassed:true,nativeHostQualified:false}));
})().catch(error=>{console.error(error);process.exitCode=1;});
