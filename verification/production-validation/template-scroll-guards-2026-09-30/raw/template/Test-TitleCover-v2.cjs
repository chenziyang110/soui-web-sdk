'use strict';
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const [attempt] = process.argv.slice(2);
assert.equal(process.argv.length,3);
assert(/^title-cover-selftest-attempt[0-9]+$/.test(attempt));
const output=path.join(__dirname,attempt);
assert.equal(fs.existsSync(output),false,'Attempt already exists');
const inspector=path.join(__dirname,'Inspect-TemplateTitleCover.ps1');
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
assert.equal(sha(inspector),'9161f19afbab8ede78676c1a5a818d63d3dcfe5a8ea4b0e9942eda67c30c9436');
const env={...process.env,PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
fs.mkdirSync(output);
const summary={schema:'soui.title-cover-selftest/v2',attempt,driverSha256:sha(__filename),inspectorSha256:sha(inspector),startedAt:new Date().toISOString(),cases:[],status:'preparing',passed:false};
const save=()=>fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(summary,null,2)+'\n');
function run(name,script){
  const startedAt=new Date().toISOString();
  const r=cp.spawnSync('powershell.exe',['-NoProfile','-Command',script],{env,encoding:'utf8',windowsHide:true,timeout:20000});
  fs.writeFileSync(path.join(output,name+'.stdout.log'),r.stdout||'',{flag:'wx'});
  fs.writeFileSync(path.join(output,name+'.stderr.log'),r.stderr||'',{flag:'wx'});
  const record={name,script,startedAt,finishedAt:new Date().toISOString(),exitCode:r.status,signal:r.signal,error:r.error?String(r.error):null};
  summary.cases.push(record);save();assert.equal(record.error,null);return r;
}
function gate(name){
  const r=run(name,"$ErrorActionPreference='Stop';$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object{$_.Name -match 'qemu|soui|sdk.*(test|consumer)|ScrollStarter|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|V02InputProbe|msbuild|^cl\\.exe$|^link\\.exe$|cdb|InputTrace|ProcessTreeSampler|electron|git\\.exe'}|Select-Object ProcessId,ParentProcessId,Name,CreationDate);[ordered]@{busy=$busy}|ConvertTo-Json -Depth 3 -Compress;if($busy.Count){exit 2}");
  assert.equal(r.stderr.trim(),'');
  const observed=JSON.parse(r.stdout);assert(Array.isArray(observed.busy));
  if(observed.busy.length){assert.equal(r.status,2);summary.status='deferred-external-load';summary.externalLoad=observed.busy;const error=new Error('External workload before diagnostic');error.deferred=true;throw error;}
  assert.equal(r.status,0);
}
const quote=s=>"'"+s.replace(/'/g,"''")+"'";
try{
  gate('before-foreground');
  const file=path.join(output,'foreground.json');
  const r=run('foreground',"$ErrorActionPreference='Stop';Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class ReadOnlyForeground{[DllImport(\"user32.dll\")]public static extern IntPtr GetForegroundWindow();[DllImport(\"user32.dll\")]public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);}';$hwnd=[ReadOnlyForeground]::GetForegroundWindow();if($hwnd -eq [IntPtr]::Zero){throw 'No foreground window available'};[uint32]$targetPid=0;[void][ReadOnlyForeground]::GetWindowThreadProcessId($hwnd,[ref]$targetPid);& "+quote(inspector)+" -TargetProcessId $targetPid -ExpectedHwnd $hwnd.ToInt64() -OutputJson "+quote(file));
  assert.equal(r.status,0);assert.equal(r.stderr.trim(),'');
  const observed=JSON.parse(fs.readFileSync(file,'utf8'));assert.equal(observed.schema,'soui.template-title-cover/v1');assert(observed.targetHwnd>0&&observed.targetProcessId>0);assert.equal(observed.targetRect.length,4);assert.equal(observed.titlePoint.length,2);
  gate('before-invalid-hwnd');
  const negativeFile=path.join(output,'invalid-hwnd.json');
  const negative=run('invalid-hwnd',"$ErrorActionPreference='Stop';& "+quote(inspector)+" -TargetProcessId 1 -ExpectedHwnd 0 -OutputJson "+quote(negativeFile));
  assert.equal(negative.status,1);assert.match(negative.stderr,/Target HWND owner changed/);assert.equal(fs.existsSync(negativeFile),false);
  gate('after-tests');summary.status='passed';summary.passed=true;
}catch(error){summary.error=String(error.stack||error);if(!error.deferred)summary.status='failed';process.exitCode=error.deferred?2:1;}
finally{summary.finishedAt=new Date().toISOString();summary.inspectorSha256After=sha(inspector);summary.driverSha256After=sha(__filename);if(summary.inspectorSha256After!==summary.inspectorSha256||summary.driverSha256After!==summary.driverSha256){summary.passed=false;summary.status='failed';process.exitCode=1;}save();console.log(JSON.stringify(summary));}
