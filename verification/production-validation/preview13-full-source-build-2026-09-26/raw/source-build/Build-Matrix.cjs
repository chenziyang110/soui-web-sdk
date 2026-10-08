'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,source=path.join(root,'source'),capture=path.join(root,'Capture-Command.cjs');
const id=require(path.join(source,'tools/internal/delivery-identity.cjs'));
const evidence=require(path.join(source,'tools/internal/build-evidence.cjs'));
const archive=require(path.join(source,'tools/internal/source-archive.cjs'));
const write=(name,value)=>fs.writeFileSync(path.join(root,name),id.encoded(value),{flag:'wx'});
const env={...process.env,VcpkgEnabled:'false',VcpkgAutoLink:'false',VcpkgEnableManifest:'false',PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
function status(value){const temp=path.join(root,'status.next.json');fs.writeFileSync(temp,id.encoded(value));fs.renameSync(temp,path.join(root,'status.json'));}
function copy(from,to){fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(from,to,fs.constants.COPYFILE_EXCL);}
async function command(name,args){const child=cp.spawn(process.execPath,[capture,name,'1200000','powershell.exe','-NoProfile','-File',path.join(source,'tools/Build.ps1'),...args],{env,windowsHide:true,stdio:'inherit'});status({status:'running',step:name,driverPid:process.pid,capturePid:child.pid,at:new Date().toISOString()});const result=await new Promise(resolve=>{child.on('error',error=>resolve({error:String(error)}));child.on('close',(code,signal)=>resolve({code,signal}));});assert.equal(result.code,0,JSON.stringify(result));const actual=id.readJson(path.join(root,'command-logs',name,'result.json'));assert.equal(actual.exitCode,0);assert.equal(actual.timedOut,false);assert.equal(actual.error,null);return actual;}
async function main(){
 assert(!fs.existsSync(path.join(root,'builds')));const full=archive.verify(source),input=id.source(source);assert.equal(full.files.length,33199);assert.equal(input.sha256,'a1007f2188102dfa4de4e79f73a4e0b2c2b8b99cf3e892a3d1e1e3794fd51e68');
 write('input.json',{version:evidence.version(source),source:input,fullSourceFiles:full.files.length,stage:id.readJson(path.join(root,'stage.json')),childEnvironment:{VcpkgEnabled:env.VcpkgEnabled,VcpkgAutoLink:env.VcpkgAutoLink,VcpkgEnableManifest:env.VcpkgEnableManifest,PSModulePath:env.PSModulePath},globalSettingsModified:false,fullDependencyRebuild:false,guiExecuted:false});
 const matrix=[];
 for(const [arch,config] of [['x86','Release'],['x64','Release'],['x86','Debug'],['x64','Debug']]){
  const name='build-'+arch+'-'+config.toLowerCase(),args=['-Architecture',arch,'-Configuration',config];if(matrix.length)args.push('-SkipWeb');
  const commandResult=await command(name,args),build=evidence.verifyBuild(source,arch,config),output=path.join(root,'builds',name);fs.mkdirSync(output,{recursive:true});
  const targets=[];
  for(const [target,payload] of Object.entries(build.payloads)){
   const runtime=path.join(output,target),original=path.join(source,'out',arch,'bin',config,target);let peFiles=0;
   for(const f of payload.files){const file=id.safePath(original,f.path),b=fs.readFileSync(file);assert.equal(id.hash(b),f.sha256);if(/\.(exe|dll)$/i.test(f.path)){const offset=b.readUInt32LE(0x3c);assert.equal(b.readUInt32LE(offset),0x4550);assert.equal(b.readUInt16LE(offset+4),arch==='x86'?0x14c:0x8664);++peFiles;}copy(file,id.safePath(runtime,f.path));}
   id.verifyPayload(runtime,target,payload);targets.push({target,files:payload.files.length,sha256:payload.sha256,peFiles});
  }
  copy(path.join(source,'out',arch,'build-'+config+'.json'),path.join(output,'build.json'));copy(path.join(source,'out/web-applications.json'),path.join(output,'web.json'));
  for(const file of id.walk(path.join(source,'out',arch)).filter(file=>/\.command\.1\.tlog$/i.test(file)&&file.includes('/'+config+'/'))){const from=path.join(source,'out',arch,file),b=fs.readFileSync(from),text=b[0]===255&&b[1]===254?b.toString('utf16le'):b.toString('utf8');assert(!/[\\/]VCPKG[\\/]/i.test(text),file);copy(from,path.join(output,'commands',file));}
  for(const file of ['native-'+config+'.log','sdk-web-install.log','sdk-web-build.log','templates-desktop-app-web-install.log','templates-desktop-app-web-build.log','demos-workbench-web-install.log','demos-workbench-web-build.log','CMakeCache.txt']){const from=path.join(source,'out',arch,file);if(fs.existsSync(from))copy(from,path.join(output,'logs',file));}
  assert.equal(id.source(source).sha256,input.sha256);matrix.push({architecture:arch,configuration:config,buildId:build.buildId,commandExitCode:commandResult.exitCode,targets,guiExecuted:false});write('builds/'+name+'/result.json',matrix[matrix.length-1]);console.log(id.encoded(matrix[matrix.length-1]));
 }
 archive.verify(source);assert.equal(id.source(source).sha256,input.sha256);
 const result={status:'compiled-four-configurations',finishedAt:new Date().toISOString(),version:evidence.version(source),sourceSha256:input.sha256,fullSourceFiles:full.files.length,sourceManifestAfterBuildVerified:true,matrix,ambientVcpkgCommandInputsAbsent:true,fullDependencyRebuild:false,cleanMachineReproduction:false,unitTestsExecuted:false,guiExecuted:false,sourceDeliveryVerified:false,benchmarkValid:false,releaseReady:false};write('result.json',result);status(result);
}
main().catch(error=>{status({status:'failed',at:new Date().toISOString(),error:String(error.stack)});console.error(error.stack);process.exitCode=1;});
