'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-source-validation/preview15-no-git';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs')),evidence=require(path.join(repo,'tools/internal/build-evidence.cjs'));
const sdk=require(path.join(repo,'tools/sdk/soui-sdk.cjs'));
const env={...process.env,MSBUILDDISABLENODEREUSE:'1',VcpkgEnabled:'false',VcpkgAutoLink:'false',VcpkgEnableManifest:'false',PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
const write=(name,value)=>{const f=path.join(root,name);fs.mkdirSync(path.dirname(f),{recursive:true});fs.writeFileSync(f,id.encoded(value),{flag:'wx'});};
const status=value=>fs.writeFileSync(path.join(root,'matrix-status.json'),id.encoded(value));
const copy=(from,to)=>{fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(from,to,fs.constants.COPYFILE_EXCL);};
async function main(){
 assert(!fs.existsSync(path.join(repo,'.git')));
 const relocated=id.readJson(path.join(root,'../relocate-result.json'));
 assert.equal(relocated.archiveSha256,'4c0255ca2a215e819bbda339d7432fc37398c3cef98b7d234f0411fcd4a4dd92');
 assert.equal(id.hash(fs.readFileSync(path.join(repo,'source-manifest.json'))),relocated.sourceManifestSha256);
 assert.equal(id.source(repo).sha256,relocated.applicationSource.sha256);
 const controls=['Build-Matrix.cjs','Capture-Command.cjs'].map(name=>{const b=fs.readFileSync(path.join(root,name));return {path:name,bytes:b.length,sha256:id.hash(b)}});
 write('archive-binding.json',{archiveSha256:relocated.archiveSha256,sourceManifestSha256:relocated.sourceManifestSha256,sdkManifestSha256:relocated.sdkManifestSha256,controls,gitPresent:false});
 assert.equal(evidence.version(repo),'0.3.0-preview.15');assert(!fs.existsSync(path.join(root,'matrix-input.json')));
 const pkg='C:/SOUI-SDK-production-validation/sdk-core-native-backups-preview15-20260927/sdk-package',manifest=sdk.readPackage(pkg);
 assert.equal(manifest.version,'0.3.0-preview.15');
 const source=id.source(repo);write('matrix-input.json',{repo,version:manifest.version,source,sdkManifestSha256:id.hash(fs.readFileSync(path.join(pkg,'sdk-manifest.json'))),
  startedAt:new Date().toISOString(),environment:{VcpkgEnabled:'false',VcpkgAutoLink:'false',VcpkgEnableManifest:'false',PSModulePath:env.PSModulePath},globalSettingsModified:false,guiExecuted:false});
 const matrix=[];
 for(const [arch,config] of [['x86','Release'],['x64','Release'],['x86','Debug'],['x64','Debug']]){
  assert.equal(id.source(repo).sha256,source.sha256);
  const name=`build-${arch}-${config.toLowerCase()}`,args=['-Architecture',arch,'-Configuration',config];if(matrix.length)args.push('-SkipWeb');
  const child=cp.spawn(process.execPath,[path.join(root,'Capture-Command.cjs'),name,'1200000','powershell.exe','-NoProfile','-File',path.join(repo,'tools/Build.ps1'),...args],{env,windowsHide:true,stdio:'inherit'});
  status({status:'running',step:name,driverPid:process.pid,capturePid:child.pid,at:new Date().toISOString(),sourceSha256:source.sha256});
  const result=await new Promise(resolve=>{child.once('error',error=>resolve({error:String(error)}));child.once('close',(code,signal)=>resolve({code,signal}));});
  assert.equal(result.code,0,JSON.stringify(result));
  const command=id.readJson(path.join(root,'command-logs',name,'result.json'));assert.equal(command.exitCode,0);assert.equal(command.timedOut,false);assert.equal(command.error,null);
  const build=evidence.verifyBuild(repo,arch,config),out=path.join(repo,'out',arch),dest=path.join(root,'builds',name);
  copy(path.join(out,`build-${config}.json`),path.join(dest,'build.json'));copy(path.join(repo,'out/web-applications.json'),path.join(dest,'web.json'));
  const payloads={};
  for(const target of ['SouiStarter','SouiWorkbench']){
   const payload=build.payloads[target],from=path.join(out,'bin',config,target),runtime=path.join(dest,target);let peFiles=0;
   for(const f of payload.files){const bytes=fs.readFileSync(path.join(from,f.path));assert.equal(id.hash(bytes),f.sha256);
    if(/\.(exe|dll)$/i.test(f.path)){const pe=bytes.readUInt32LE(0x3c);assert.equal(bytes.readUInt32LE(pe),0x4550);assert.equal(bytes.readUInt16LE(pe+4),arch==='x86'?0x14c:0x8664);++peFiles;}
    copy(path.join(from,f.path),path.join(runtime,f.path));
   }
   id.verifyPayload(runtime,target,payload);payloads[target]={sha256:payload.sha256,files:payload.files.length,bytes:payload.bytes,peFiles};
  }
  const commands=id.walk(out).filter(n=>n.includes('/'+config+'/')&&/(?:CL|link|Lib)\.command\.1\.tlog$/i.test(n));assert(commands.length>2);
  for(const file of commands){const bytes=fs.readFileSync(path.join(out,file)),text=bytes[0]===255&&bytes[1]===254?bytes.toString('utf16le'):bytes.toString('utf8');assert(!/[\\/]vcpkg[\\/]/i.test(text),file);copy(path.join(out,file),path.join(dest,'commands',file));}
  for(const name of fs.readdirSync(out).filter(n=>n.endsWith('.log')||n==='CMakeCache.txt'))copy(path.join(out,name),path.join(dest,'logs',name));
  matrix.push({architecture:arch,configuration:config,buildId:build.buildId,sourceSha256:source.sha256,payloads,compilerLinkCommands:commands.length,originalExitCode:command.exitCode,guiExecuted:false});
  write('builds/'+name+'/review.json',matrix.at(-1));console.log(JSON.stringify(matrix.at(-1)));
 }
 assert.equal(id.source(repo).sha256,source.sha256);
 const result={status:'four-configurations-compiled',version:evidence.version(repo),sourceSha256:source.sha256,matrix,finishedAt:new Date().toISOString(),guiExecuted:false,sourceDeliveryVerified:false,releaseReady:false};write('matrix-result.json',result);status(result);
}
main().catch(error=>{status({status:'failed',at:new Date().toISOString(),error:String(error.stack)});console.error(error.stack);process.exitCode=1;});
