'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const pin=f=>{const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:crypto.createHash('sha256').update(b).digest('hex')};};
assert.equal(process.argv[2],'ROOT_REVIEWED_CURRENT_WORKBENCH_SOURCE_V1');
const frozen=JSON.parse(fs.readFileSync(__dirname+'/FrozenSource.actual.json')),header=frozen.sourceFiles.find(f=>f.relative==='demos/workbench/app/resident_shutdown_policy.h');assert(header);
const before=pin(header.source.path);assert.equal(before.sha256,header.source.sha256);
const folder=__dirname+'/policy-source';assert(!fs.existsSync(folder));assert(!fs.existsSync(__dirname+'/PolicySource.actual.json'));fs.mkdirSync(folder);
const rows=[];for(const relative of ['tests/native/resident_shutdown/CMakeLists.txt','tests/native/resident_shutdown/policy_tests.cpp','demos/workbench/app/resident_shutdown_policy.h']){
  const source=pin('C:/sc07/'+relative),target=path.join(folder,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source.path,target,fs.constants.COPYFILE_EXCL);const copied=pin(target);assert.equal(source.bytes,copied.bytes);assert.equal(source.sha256,copied.sha256);assert.deepEqual(pin(source.path),source);rows.push({relative,source,copy:copied});
}
assert.equal(rows[2].copy.sha256,header.copy.sha256);
const record={schema:'soui.current-workbench-two-policy-case-source/v1',producer:pin(__filename),workbenchFreeze:pin(__dirname+'/FrozenSource.actual.json'),sourceFiles:rows,hook:pin(__dirname+'/PolicyTarget.cmake'),cases:['reload-pending','runtime-pending'],unchangedNineCasesReexecuted:false,originalPerCaseTimeoutMs:10000,drainMs:2000,cppTranslationUnits:1,consoleOnly:true,executed:false,productExecuted:false,releaseReady:false};
fs.writeFileSync(__dirname+'/PolicySource.actual.json',JSON.stringify(record,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({policySource:pin(__dirname+'/PolicySource.actual.json'),executed:false}));
