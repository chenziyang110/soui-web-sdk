'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const buildEvidence=require(path.join(repo,'tools/internal/build-evidence.cjs'));
const [arch,kind]=process.argv.slice(2);assert(['x86','x64'].includes(arch));assert(['deps','apps','window-thread-timeout','directory-cancel'].includes(kind));assert.equal(process.argv.length,4);
const expected='b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07';
const env={...process.env,PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};for(const key of Object.keys(env))if(key.startsWith('SOUI_'))delete env[key];
function gate(owned=0){
 const script="$all=@(Get-CimInstance Win32_Process);$own=[Collections.Generic.HashSet[int]]::new();if("+Number(owned)+" -gt 0){[void]$own.Add("+Number(owned)+")};do{$changed=$false;foreach($p in $all){if($own.Contains([int]$p.ParentProcessId) -and $own.Add([int]$p.ProcessId)){$changed=$true}}}while($changed);$busy=@($all|Where-Object{$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$' -and -not $own.Contains([int]$_.ProcessId)});$busy|Select-Object ProcessId,ParentProcessId,Name,CreationDate|ConvertTo-Json -Compress;if($busy.Count){exit 1}";
 const r=cp.spawnSync('powershell.exe',['-NoProfile','-Command',script],{env,windowsHide:true,timeout:20000});return{at:new Date().toISOString(),exitCode:r.status,error:r.error?.message||null,stdout:r.stdout?.toString()||'',stderr:r.stderr?.toString()||''};
}
const write=(base,name,value)=>{const file=path.join(base,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,id.encoded(value),{flag:'wx'});};
async function main(){
 assert.equal(id.source(repo).sha256,expected);const build=buildEvidence.verifyBuild(repo,arch,'Release');assert.equal(build.version,'0.3.0-preview.15');
 const first=gate();if(first.exitCode!==0||first.error){console.log(JSON.stringify({status:'busy-no-launch',first}));process.exitCode=2;return;}
 const run=path.join(root,'runs',`${arch}-${kind}-`+new Date().toISOString().replace(/[:.]/g,'-'));write(run,'gate-before.json',first);write(run,'build-before.json',build);
 const deps=path.join(repo,'out/dependencies',arch),beforeDirs=fs.readdirSync(deps).filter(n=>n.startsWith('runtime-test-'));
 let program,args;
 if(kind==='deps'){program='powershell.exe';args=['-NoProfile','-File',path.join(repo,'tools/Test-Dependencies.ps1'),'-Architecture',arch];}
 else if(kind==='apps'){program='powershell.exe';args=['-NoProfile','-File',path.join(repo,'tools/Test.ps1'),'-Architecture',arch,'-Configuration','Release','-NativeOnly'];}
 else {program=path.join(repo,'out',arch,'tests/native/Release',kind==='directory-cancel'?'soui_directory_dialog_tests.exe':'soui_desktop_unicode_tests.exe');args=[kind==='directory-cancel'?'--cancel':'--window-thread-timeout'];write(run,'probe-input.json',{program,sha256:id.hash(fs.readFileSync(program)),args});}
 const out=fs.openSync(path.join(run,'stdout.log'),'wx'),err=fs.openSync(path.join(run,'stderr.log'),'wx');
 const child=cp.spawn(program,args,{env,cwd:repo,windowsHide:true,stdio:['ignore',out,err]});write(run,'launch.json',{program,args,pid:child.pid,startedAt:new Date().toISOString(),driverSha256:id.hash(fs.readFileSync(__filename))});
 let closed=false,timedOut=false,interference=false,spawnError=null;const gates=[];child.once('error',error=>{spawnError=String(error);});
 function stop(){if(child.pid)cp.spawnSync('taskkill.exe',['/PID',String(child.pid),'/T','/F'],{windowsHide:true,timeout:15000});}
 const monitor=setInterval(()=>{if(closed)return;const g=gate(child.pid);gates.push(g);if(g.exitCode!==0||g.error){interference=true;stop();}},5000);
 const timer=setTimeout(()=>{timedOut=true;stop();},kind==='apps'?180000:kind==='deps'?120000:30000);
 const result=await new Promise(resolve=>child.once('close',(exitCode,signal)=>resolve({exitCode,signal})));closed=true;clearInterval(monitor);clearTimeout(timer);fs.closeSync(out);fs.closeSync(err);
 const after=gate();write(run,'gates-during.json',gates);write(run,'gate-after.json',after);write(run,'result.json',{...result,timedOut,interference,spawnError,finishedAt:new Date().toISOString(),releaseReady:false});
 assert.equal(id.source(repo).sha256,expected);buildEvidence.verifyBuild(repo,arch,'Release');
 if(kind==='deps'){
  const newDirs=fs.readdirSync(deps).filter(n=>n.startsWith('runtime-test-')&&!beforeDirs.includes(n));write(run,'runtime-directories.json',newDirs);
  if(fs.existsSync(path.join(deps,'rebuilt-smoke.json')))fs.copyFileSync(path.join(deps,'rebuilt-smoke.json'),path.join(run,'rebuilt-smoke.json'),fs.constants.COPYFILE_EXCL);
  const outputs=[];for(const dir of newDirs)for(const target of ['SouiStarter','SouiWorkbench']){const runtime=path.join(deps,dir,target);if(fs.existsSync(runtime))outputs.push({runtime,target,payload:id.payload(runtime,target)});}write(run,'rebuilt-payloads.json',outputs);
 }else if(kind==='apps'){
  const smoke=path.join(repo,'out',arch,'smoke-Release.json');if(fs.existsSync(smoke))fs.copyFileSync(smoke,path.join(run,'smoke.json'),fs.constants.COPYFILE_EXCL);
 }
 if(kind==='window-thread-timeout'||kind==='directory-cancel')assert.equal(id.hash(fs.readFileSync(program)),id.readJson(path.join(run,'probe-input.json')).sha256);
 assert.equal(result.exitCode,0);assert.equal(timedOut,false);assert.equal(interference,false);assert.equal(spawnError,null);assert.equal(after.exitCode,0);assert.equal(after.error,null);
 console.log(JSON.stringify({run,architecture:arch,kind,nativeOrScriptExitCode:result.exitCode,interference:false,releaseReady:false}));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
