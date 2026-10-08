'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),cp=require('node:child_process');
const repo='C:/SOUI-SDK-development/preview14-scheduler';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const dest=path.join(repo,'verification/production-validation/v02-continuous-task-preparation-2026-09-27');
const read=name=>id.readJson(path.join(__dirname,name));
for(const name of ['red-configure-x64','red-build-x64','red-driver','sdk-core-verify-source','demo-audit']){
  const r=read(`command-logs/${name}/result.json`);assert.equal(r.exitCode,0);assert(!r.timedOut&&!r.error);
}
const red=read('command-logs/red-run-x64/result.json');assert.equal(red.exitCode,1);assert(!red.timedOut&&!red.error);
assert.match(fs.readFileSync(path.join(__dirname,'command-logs/red-run-x64/stderr.log'),'utf8'),/Demo interaction task kind is not available through task.start/);
assert.equal(id.hash(fs.readFileSync(path.join(__dirname,'red-input/soui_workbench_interaction_tests.exe'))),read('red-executable.json').Hash.toLowerCase());
const audit=read('command-logs/demo-audit/stdout.log');assert.equal(audit.metadata.vulnerabilities.total,0);
const source=id.source(repo);
const gate=cp.spawnSync('powershell.exe',['-NoProfile','-Command',"$ErrorActionPreference='Stop'; @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$'} | Select-Object ProcessId,Name,CreationDate) | ConvertTo-Json -Depth 4 -Compress"],{encoding:'utf8',windowsHide:true,env:{...process.env,PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')}});
if(gate.error||gate.status!==0||gate.stderr.trim())throw Error('Cannot obtain real workload inventory');
const summary={at:new Date().toISOString(),baseCommit:'aedd8d223f8a73a6b9f11f9ba49456381815d5cb',branch:'codex/sdk-v02-continuous-task',
 status:'implemented-awaiting-green-and-desktop',red:{configurationExitCode:0,compilationExitCode:0,behaviorExitCode:1},
 sourceSha256:source.sha256,sourceFiles:source.files.length,sdkCoreSourceMatchesFrozenPackage:true,demoProductionAuditVulnerabilities:0,
 activeWorkloads:gate.stdout.trim()?JSON.parse(gate.stdout):[],greenExecuted:false,newApplicationBuildExecuted:false,guiExecuted:false,releaseReady:false,
 notes:['Initial test array input expectation corrected to reject at public task.start admission; original test preserved.',
 'An exploratory node --check Run-Green.ps1 failed due unsupported file extension; the proper PowerShell parser and node --check Bind-Inputs.cjs subsequently passed; neither was an SDK test.',
 'Outer gate blocked green-x64-attempt1-driver before launch; that attempt name remains unused.']};
fs.writeFileSync(path.join(__dirname,'preparation-review.json'),id.encoded(summary),{flag:'wx'});
fs.mkdirSync(path.join(dest,'raw'),{recursive:true});
const files=id.walk(__dirname).filter(n=>!n.endsWith('.exe')&&!n.endsWith('.pdb'));
const manifest=[];
for(const name of files){const original=path.join(__dirname,name),rel='raw/'+name,to=path.join(dest,rel),data=fs.readFileSync(original);
 fs.mkdirSync(path.dirname(to),{recursive:true});fs.writeFileSync(to,data,{flag:'wx'});manifest.push({path:rel,original,bytes:data.length,sha256:id.hash(data)});
}
fs.writeFileSync(path.join(dest,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files:manifest}),{flag:'wx'});
console.log(JSON.stringify({rawFiles:manifest.length,sourceSha256:source.sha256,status:summary.status,activeWorkloads:summary.activeWorkloads}));
