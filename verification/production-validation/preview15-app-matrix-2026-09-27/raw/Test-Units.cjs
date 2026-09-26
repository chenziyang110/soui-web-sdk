'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const input=id.readJson(path.join(root,'matrix-input.json'));
const env={...process.env,PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
assert.equal(id.readJson(path.join(root,'matrix-result.json')).status,'four-configurations-compiled');
assert.equal(id.source(repo).sha256,input.source.sha256);
const results=[];
for(const [name,program,args] of [
 ['unit-contract-x64-release','powershell.exe',['-NoProfile','-File',path.join(repo,'tools/Test.ps1'),'-Architecture','x64','-Configuration','Release','-UnitOnly']],
 ...[['x86','Release'],['x86','Debug'],['x64','Debug']].map(([arch,config])=>['ctest-'+arch+'-'+config.toLowerCase(),'ctest.exe',['--test-dir',path.join(repo,'out',arch),'-C',config,'--output-on-failure']])
]){
 const run=cp.spawnSync(process.execPath,[path.join(root,'Capture-Command.cjs'),name,'300000',program,...args],{env,windowsHide:true,stdio:'inherit'});assert.equal(run.status,0,name);
 const result=id.readJson(path.join(root,'command-logs',name,'result.json'));assert.equal(result.exitCode,0);assert.equal(result.timedOut,false);assert.equal(result.error,null);results.push({name,originalExitCode:result.exitCode});
}
assert.equal(id.source(repo).sha256,input.source.sha256);
fs.writeFileSync(path.join(root,'unit-results.json'),id.encoded({version:'0.3.0-preview.15',sourceSha256:input.source.sha256,results,finishedAt:new Date().toISOString(),guiExecuted:false,releaseReady:false}),{flag:'wx'});
