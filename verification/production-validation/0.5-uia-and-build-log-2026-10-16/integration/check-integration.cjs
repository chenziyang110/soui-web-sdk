'use strict';
const fs=require('node:fs'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const packet=JSON.parse(fs.readFileSync(__dirname+'/PREPARED.json'));
e.checkPin(packet.currentSource);packet.parents.forEach(e.checkPin);packet.files.forEach(row=>{if(row.before)e.checkPin(row.before);e.checkPin(row.after);});
const selected=['tests/acceptance/long-run-task-quota.test.cjs','tests/sdk/long-run-policy.test.cjs',
 'tests/sdk/long-run-cache-observation.test.cjs','tests/sdk/standard-build.test.cjs',
 'tests/sdk/llvm-evidence-readers.test.cjs','tests/acceptance/llvm-installed-entry.test.cjs','tests/sdk/devkit-command.test.cjs'];
const records=[];
for(const file of selected){
 const result=cp.spawnSync(process.execPath,[__dirname+'/run-regression.cjs',file],{windowsHide:true,encoding:'utf8',maxBuffer:16*1024*1024});
 assert(!result.error,result.error?.message);
 const output=__dirname+'/'+file.replaceAll('/','--')+'.txt';fs.writeFileSync(output,result.stdout+result.stderr,{flag:'wx'});
 assert.equal(result.status,0,file+' failed; preserve original output');
 const text=result.stdout+result.stderr;assert.match(text,/fail 0\b/);assert.match(text,/skipped 0\b/);
 records.push({file,passed:Number(text.match(/pass (\d+)/)[1]),output:e.pin(output)});
 console.log(JSON.stringify(records.at(-1)));
}
e.checkPin(packet.currentSource);packet.files.forEach(row=>e.checkPin(row.after));
fs.writeFileSync(__dirname+'/INTEGRATION-VERIFIED.json',JSON.stringify({passed:true,scope:'Combined proposal pure/source regressions; no final native or public build qualification',prepared:e.pin(__dirname+'/PREPARED.json'),records,totalPassed:records.reduce((sum,row)=>sum+row.passed,0),releaseReady:false},null,2)+'\n',{flag:'wx'});
