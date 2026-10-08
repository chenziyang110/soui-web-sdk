'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,project=path.join(root,'project'),output=path.join(root,'builds');
const id=require(path.join(project,'tools/internal/delivery-identity.cjs')),sdk=require(path.join(project,'tools/sdk/soui-sdk.cjs'));
const evidence=require(path.join(project,'tools/internal/build-evidence.cjs'));
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'')),business=read(path.join(root,'business-input.json'));
const write=(name,value)=>fs.writeFileSync(path.join(root,name),id.encoded(value),{flag:'wx'});
function status(value){const temporary=path.join(root,'matrix-status.next.json');fs.writeFileSync(temporary,id.encoded(value));fs.renameSync(temporary,path.join(root,'matrix-status.json'));}
function businessCheck(){for(const f of business.files){const b=fs.readFileSync(path.join(project,f.path));assert.equal(b.length,f.bytes);assert.equal(id.hash(b),f.sha256,f.path);}}
function copyFile(from,to){fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(from,to,fs.constants.COPYFILE_EXCL);}
async function run(name,arguments_){
 const args=[path.join(root,'Capture-Command.cjs'),name,'720000','powershell.exe','-NoProfile','-File',path.join(project,'tools/Build.ps1'),...arguments_];
 const child=cp.spawn(process.execPath,args,{cwd:root,windowsHide:true,stdio:['ignore','inherit','inherit']});
 status({status:'running',step:name,driverPid:process.pid,capturePid:child.pid,at:new Date().toISOString()});
 const result=await new Promise(resolve=>{child.on('error',error=>resolve({error:error.message}));child.on('close',(code,signal)=>resolve({code,signal}));});
 assert.equal(result.code,0,name+' capture failed '+JSON.stringify(result));
 const original=read(path.join(root,'command-logs',name,'result.json'));assert.equal(original.exitCode,0);assert.equal(original.timedOut,false);assert.equal(original.error,null);
 return original;
}
async function main(){
 const recovered=read(path.join(root,'result.json'));assert.equal(recovered.manualRecoveryVerified,true);assert(!fs.existsSync(output));fs.mkdirSync(output);
 const source=id.source(project),lock=fs.readFileSync(path.join(project,'sdk-lock.json'));assert.equal(evidence.version(project),'0.3.0-preview.13');
 write('matrix-input.json',{startedAt:new Date().toISOString(),source,sdkLockSha256:id.hash(lock),businessFiles:business.files.length,toolchainEnvironment:{VCPKG_ROOT:process.env.VCPKG_ROOT||null,CMAKE_TOOLCHAIN_FILE:process.env.CMAKE_TOOLCHAIN_FILE||null},guiExecuted:false});
 const matrix=[];
 for(const [arch,config]of [['x86','Release'],['x64','Release'],['x86','Debug'],['x64','Debug']]){
  businessCheck();sdk.readLock(project);assert.equal(id.source(project).sha256,source.sha256);
  const name='build-'+arch+'-'+config.toLowerCase(),args=['-Architecture',arch,'-Configuration',config];if(matrix.length)args.push('-SkipWeb');
  const command=await run(name,args);businessCheck();assert.deepEqual(fs.readFileSync(path.join(project,'sdk-lock.json')),lock);
  const build=evidence.verifyBuild(project,arch,config),directory=path.join(output,name);fs.mkdirSync(directory);
  const payload=build.payloads.UpgradeLedger,from=path.join(project,'out',arch,'bin',config,'UpgradeLedger'),runtime=path.join(directory,'runtime');
  let peFiles=0;
  for(const f of payload.files){
   const bytes=fs.readFileSync(id.safePath(from,f.path));assert.equal(id.hash(bytes),f.sha256);
   if(/\.(exe|dll)$/i.test(f.path)){const offset=bytes.readUInt32LE(0x3c);assert.equal(bytes.readUInt32LE(offset),0x4550);assert.equal(bytes.readUInt16LE(offset+4),arch==='x86'?0x14c:0x8664,f.path);++peFiles;}
   const target=id.safePath(runtime,f.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});
  }
  id.verifyPayload(runtime,'UpgradeLedger',payload);
  copyFile(path.join(project,'out',arch,'build-'+config+'.json'),path.join(directory,'build.json'));
  copyFile(path.join(project,'out/web-applications.json'),path.join(directory,'web.json'));
  for(const name of ['native-'+config+'.log','sdk-web-install.log','sdk-web-build.log','web-install.log','web-build.log','CMakeCache.txt']){
   const sourceFile=path.join(project,'out',arch,name);if(fs.existsSync(sourceFile))copyFile(sourceFile,path.join(directory,'logs',name));
  }
  matrix.push({architecture:arch,configuration:config,buildId:build.buildId,sourceSha256:build.source.sha256,payloadSha256:payload.sha256,payloadFiles:payload.files.length,peFiles,commandExitCode:command.exitCode,guiExecuted:false});
  write('builds/'+name+'/result.json',matrix[matrix.length-1]);console.log('Verified '+name+' '+build.buildId);
 }
 assert.equal(new Set(matrix.map(v=>v.buildId)).size,4);assert.equal(id.source(project).sha256,source.sha256);businessCheck();
 const result={status:'four-configurations-compiled',finishedAt:new Date().toISOString(),version:evidence.version(project),sourceSha256:source.sha256,matrix,businessFilesPreserved:22,
  fullDependencyRebuild:false,cleanMachineReproduction:false,guiExecuted:false,sourceDeliveryVerified:false,releaseReady:false};
 write('matrix-result.json',result);status(result);console.log(id.encoded(result));
}
main().catch(error=>{status({status:'failed',at:new Date().toISOString(),error:String(error.stack)});console.error(error.stack);process.exitCode=1;});
