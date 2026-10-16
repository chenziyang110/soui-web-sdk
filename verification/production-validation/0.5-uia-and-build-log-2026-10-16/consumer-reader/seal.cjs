'use strict';
const fs=require('node:fs'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const predecessor='C:/work/sm05-uia-delivery-2/PROPOSAL.json',packet=JSON.parse(fs.readFileSync(predecessor));
e.checkPin(packet.currentSource);packet.files.forEach(row=>e.checkPin(row.after));
assert.match(fs.readFileSync(__dirname+'/red.txt','utf8'),/Missing expected exception/);
const green=fs.readFileSync(__dirname+'/green.txt','utf8');assert.match(green,/pass 16\b/);assert.match(green,/fail 0\b/);assert.match(green,/skipped 0\b/);
assert.deepEqual(fs.readFileSync(__dirname+'/proposal/tools/internal/configure-log-evidence.cjs'),fs.readFileSync('C:/work/sm05-uia-delivery-2/proposal/tools/internal/configure-log-evidence.cjs'));
const files=['tools/internal/llvm-installed-acceptance.cjs','tests/acceptance/llvm-installed-entry.test.cjs'].map(relative=>({relative,before:e.pin(root+'/'+relative),after:e.pin(__dirname+'/proposal/'+relative)}));
for(const row of files){
 const run=cp.spawnSync('C:/Program Files/Git/cmd/git.exe',['-c','core.autocrlf=false','-c','core.whitespace=cr-at-eol','diff','--no-index','--check','--',row.before.path,row.after.path],{encoding:'utf8',windowsHide:true});
 assert([0,1].includes(run.status)&&run.stdout===''&&run.stderr==='',run.stdout+run.stderr);
}
fs.writeFileSync(__dirname+'/PROPOSAL.json',JSON.stringify({schema:'soui.private-change-proposal/1',adopted:false,currentSource:packet.currentSource,
 scope:'Bind newly captured immutable CMake logs for both installed acceptance and release consumers; legacy evidence checks unchanged',
 requires:e.pin(predecessor),files,regressions:16,skipped:0,evidence:['red.txt','green.txt','run-regression.cjs'].map(name=>e.pin(__dirname+'/'+name)),
 actualPublicConsumerRun:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({sealed:true,files:files.length,passed:16,adopted:false}));
