'use strict';
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const output = path.join(__dirname, 'title-cover-selftest-attempt1');
assert.equal(fs.existsSync(output), false);
const inspector = path.join(__dirname, 'Inspect-TemplateTitleCover.ps1');
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
assert.equal(sha(inspector), '9161f19afbab8ede78676c1a5a818d63d3dcfe5a8ea4b0e9942eda67c30c9436');
const env = {...process.env, PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')};
fs.mkdirSync(output);
const summary = {schema:'soui.title-cover-selftest/v1', inspectorSha256:sha(inspector), startedAt:new Date().toISOString(), cases:[], passed:false};
const save = () => fs.writeFileSync(path.join(output,'result.json'), JSON.stringify(summary,null,2)+'\n');
function run(name, script) {
  const startedAt = new Date().toISOString();
  const r = cp.spawnSync('powershell.exe',['-NoProfile','-Command',script],{env,encoding:'utf8',windowsHide:true,timeout:20000});
  fs.writeFileSync(path.join(output,name+'.stdout.log'),r.stdout||'',{flag:'wx'});
  fs.writeFileSync(path.join(output,name+'.stderr.log'),r.stderr||'',{flag:'wx'});
  const record={name,script,startedAt,finishedAt:new Date().toISOString(),exitCode:r.status,signal:r.signal,error:r.error?String(r.error):null};
  summary.cases.push(record);save();
  assert.equal(record.error,null);
  return r;
}
const quote = s => "'"+s.replace(/'/g,"''")+"'";
try {
  const gate = run('idle-gate',"$ErrorActionPreference='Stop'; $busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object { $_.Name -match 'qemu|soui|ScrollStarter|msbuild|^cl\\.exe$|^link\\.exe$|cdb|InputTrace|ProcessTreeSampler|electron|git\\.exe' }); [ordered]@{busy=$busy.Count}|ConvertTo-Json -Compress; if($busy.Count){exit 2}");
  assert.equal(gate.status,0);assert.equal(gate.stderr.trim(),'');assert.equal(JSON.parse(gate.stdout).busy,0);
  const file=path.join(output,'foreground.json');
  const positive=run('foreground',"$ErrorActionPreference='Stop'; Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class ReadOnlyForeground{[DllImport(\"user32.dll\")]public static extern IntPtr GetForegroundWindow();[DllImport(\"user32.dll\")]public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);}'; $hwnd=[ReadOnlyForeground]::GetForegroundWindow(); if($hwnd -eq [IntPtr]::Zero){throw 'No foreground window available'}; [uint32]$targetPid=0; [void][ReadOnlyForeground]::GetWindowThreadProcessId($hwnd,[ref]$targetPid); & "+quote(inspector)+" -TargetProcessId $targetPid -ExpectedHwnd $hwnd.ToInt64() -OutputJson "+quote(file));
  assert.equal(positive.status,0);assert.equal(positive.stderr.trim(),'');
  const observed=JSON.parse(fs.readFileSync(file,'utf8'));assert.equal(observed.schema,'soui.template-title-cover/v1');assert(observed.targetHwnd>0&&observed.targetProcessId>0);assert.equal(observed.targetRect.length,4);assert.equal(observed.titlePoint.length,2);
  const negativeFile=path.join(output,'invalid-hwnd.json');
  const negative=run('invalid-hwnd',"$ErrorActionPreference='Stop'; & "+quote(inspector)+" -TargetProcessId 1 -ExpectedHwnd 0 -OutputJson "+quote(negativeFile));
  assert.equal(negative.status,1);assert.match(negative.stderr,/Target HWND owner changed/);assert.equal(fs.existsSync(negativeFile),false);
  summary.passed=true;
} catch(error) { summary.error=String(error.stack||error);process.exitCode=1; }
finally {summary.finishedAt=new Date().toISOString();summary.inspectorSha256After=sha(inspector);if(summary.inspectorSha256After!==summary.inspectorSha256){summary.passed=false;process.exitCode=1;}save();console.log(JSON.stringify(summary));}
