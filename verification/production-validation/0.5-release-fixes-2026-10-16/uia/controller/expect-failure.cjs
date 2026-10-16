'use strict';
const fs=require('node:fs'),cp=require('node:child_process'),assert=require('node:assert/strict');
const [exe,resultFile,test,needle]=process.argv.slice(2);
const result=cp.spawnSync(exe,[test],{windowsHide:true,encoding:'utf8',timeout:10000,maxBuffer:1024*1024});
fs.writeFileSync(resultFile,JSON.stringify({executable:exe,test,status:result.status,signal:result.signal,error:result.error?.message||null,stdout:result.stdout,stderr:result.stderr,expectedFailure:true},null,2)+'\n',{flag:'wx'});
process.stdout.write(result.stdout||'');process.stderr.write(result.stderr||'');
assert(!result.error&&result.signal===null&&result.status===1,'Original regression must actually fail with exit 1');
assert(result.stderr.includes(needle||'owner focus uses the configured bounded pump'),'Original failure must be the target regression');
