'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,source=path.join(root,'staging/source'),id=require(path.join(source,'tools/internal/delivery-identity.cjs'));
const staging=id.readJson(path.join(root,'staging-input.json'));
const env={...process.env,VcpkgEnabled:'false',VcpkgAutoLink:'false',VcpkgEnableManifest:'false',MSBUILDDISABLENODEREUSE:'1',PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
function gate(){const r=cp.spawnSync('powershell.exe',['-NoProfile','-Command',"$busy=@(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$' });$busy|Select-Object ProcessId,Name,CreationDate|ConvertTo-Json -Compress;if($busy.Count){exit 2}"],{windowsHide:true,encoding:'utf8',timeout:20000,env});return{at:new Date().toISOString(),exitCode:r.status,stdout:r.stdout,stderr:r.stderr,error:r.error?.message||null};}
async function main(){
 assert(!fs.existsSync(path.join(root,'sdk-build-result.json')));assert.equal(id.source(source).sha256,staging.sourceSha256);
 const results=[];
 for(const arch of ['x86','x64']){
  const completed=path.join(root,'sdk-'+arch+'-complete.json');
  if(fs.existsSync(completed)){
   const prior=id.readJson(completed),command=id.readJson(path.join(root,'command-logs','sdk-'+arch,'result.json'));
   assert.equal(prior.driverSha256,id.hash(fs.readFileSync(__filename)));assert.equal(command.exitCode,0);assert.equal(command.timedOut,false);assert.equal(command.error,null);
   const build=require(path.join(source,'tools/internal/build-evidence.cjs')).verifyBuild(source,arch,'Release',true);
   assert.equal(build.buildId,prior.sdkBuildId);assert.equal(build.source.sha256,staging.sourceSha256);
   assert.equal(id.snapshot(path.join(source,'out/install',arch),id.walk(path.join(source,'out/install',arch))).sha256,prior.installedSdk.sha256);
   results.push(prior);continue;
  }
  assert(!fs.existsSync(path.join(root,'command-logs','sdk-'+arch)),'An incomplete/failed build must be investigated, not silently restarted');
  const g=gate();fs.writeFileSync(path.join(root,`sdk-${arch}-gate-${Date.now()}.json`),id.encoded(g),{flag:'wx'});if(g.exitCode!==0||g.error){console.log(JSON.stringify({status:'busy-no-compile',gate:g}));process.exitCode=2;return;}
  const args=['-NoProfile','-File',path.join(source,'tools/Build.ps1'),'-Architecture',arch,'-Configuration','Release','-SdkOnly','-Install'];if(results.length)args.push('-SkipWeb');
  const child=cp.spawn(process.execPath,[path.join(root,'Capture-Command.cjs'),'sdk-'+arch,'1200000','powershell.exe',...args],{env,windowsHide:true,stdio:'inherit'});
  fs.writeFileSync(path.join(root,'sdk-build-running.json'),id.encoded({driverPid:process.pid,capturePid:child.pid,architecture:arch,startedAt:new Date().toISOString(),sourceSha256:staging.sourceSha256}));
  const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('close',resolve);});assert.equal(code,0);
  const command=id.readJson(path.join(root,'command-logs','sdk-'+arch,'result.json'));assert.equal(command.exitCode,0);assert.equal(command.timedOut,false);assert.equal(command.error,null);
  const build=require(path.join(source,'tools/internal/build-evidence.cjs')).verifyBuild(source,arch,'Release',true);
  const installedSdk=id.snapshot(path.join(source,'out/install',arch),id.walk(path.join(source,'out/install',arch)));
  const out=path.join(source,'out','sdk-'+arch),commands=id.walk(out).filter(n=>/(CL|Lib|link)\.command\.1\.tlog$/i.test(n));assert(commands.length>1);
  for(const name of commands){const b=fs.readFileSync(path.join(out,name));assert(!/[\\/]vcpkg[\\/]/i.test(b[0]===255&&b[1]===254?b.toString('utf16le'):b.toString('utf8')));}
  const entry={architecture:arch,sdkBuildId:build.buildId,sourceSha256:build.source.sha256,installedSdk,originalExitCode:command.exitCode,compilerCommandFiles:commands.length,driverSha256:id.hash(fs.readFileSync(__filename))};
  fs.writeFileSync(completed,id.encoded(entry),{flag:'wx'});results.push(entry);
 }
 assert.equal(id.source(source).sha256,staging.sourceSha256);
 fs.writeFileSync(path.join(root,'sdk-build-result.json'),id.encoded({status:'two-sdk-installations-built',version:staging.version,sourceSha256:staging.sourceSha256,architectures:results,finishedAt:new Date().toISOString(),guiExecuted:false,releaseReady:false}),{flag:'wx'});
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
