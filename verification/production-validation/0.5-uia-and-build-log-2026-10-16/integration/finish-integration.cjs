'use strict';
const fs=require('node:fs'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs'),packet=JSON.parse(fs.readFileSync(__dirname+'/PREPARED.json'));
e.checkPin(packet.currentSource);packet.files.forEach(row=>e.checkPin(row.after));
const original=['tests/acceptance/long-run-task-quota.test.cjs','tests/sdk/long-run-policy.test.cjs',
 'tests/sdk/long-run-cache-observation.test.cjs','tests/sdk/standard-build.test.cjs',
 'tests/sdk/llvm-evidence-readers.test.cjs','tests/acceptance/llvm-installed-entry.test.cjs'];
const file='tests/tooling/devkit-command.test.cjs',output=__dirname+'/'+file.replaceAll('/','--')+'.txt';
const result=cp.spawnSync(process.execPath,[__dirname+'/run-regression.cjs',file],{windowsHide:true,encoding:'utf8',maxBuffer:16*1024*1024});
assert(!result.error);fs.writeFileSync(output,result.stdout+result.stderr,{flag:'wx'});assert.equal(result.status,0);
const records=[...original,file].map(file=>{
 const output=__dirname+'/'+file.replaceAll('/','--')+'.txt',text=fs.readFileSync(output,'utf8');
 assert.match(text,/fail 0\b/);assert.match(text,/skipped 0\b/);
 return {file,passed:Number(text.match(/pass (\d+)/)[1]),output:e.pin(output)};
});
e.checkPin(packet.currentSource);packet.files.forEach(row=>e.checkPin(row.after));
const verified={passed:true,scope:'Combined proposal pure/source regressions; no final native or public build qualification',prepared:e.pin(__dirname+'/PREPARED.json'),
 records,totalPassed:records.reduce((sum,row)=>sum+row.passed,0),
 preservedHarnessFailure:'Initial controller selected tests/sdk/devkit-command.test.cjs, a nonexistent path. That ENOENT is retained; correct existing test lives under tests/tooling. Earlier six passed runs reused unchanged.',releaseReady:false};
fs.writeFileSync(__dirname+'/INTEGRATION-VERIFIED.json',JSON.stringify(verified,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({passed:true,total:verified.totalPassed}));
