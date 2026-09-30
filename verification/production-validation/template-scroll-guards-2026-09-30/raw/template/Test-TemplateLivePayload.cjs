'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {verifyLivePayload} = require('./Verify-TemplateLivePayload.cjs');
const [attempt] = process.argv.slice(2);
assert.equal(process.argv.length,3);
assert(/^live-payload-verifier-selftest-attempt[0-9]+$/.test(attempt));
const output = path.join(__dirname,attempt);
assert(!fs.existsSync(output),'Attempt already exists');
fs.mkdirSync(output);
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const fixture = {'ScrollStarter.exe':'fixed-executable','kernel.dll':'fixed-kernel',
  'resources/index.html':'fixed-page',
  'resources/legal/a.txt':'notice-a','resources/legal/b.txt':'notice-b',
  'resources/legal/c.txt':'notice-c','resources/legal/d.txt':'notice-d','resources/legal/e.txt':'notice-e'};
const payload = {sha256:'test-payload',files:Object.entries(fixture).map(([file,data])=>({path:file,bytes:Buffer.byteLength(data),sha256:sha(data)}))};
const record = {schema:'soui.template-live-payload-controls/v1',startedAt:new Date().toISOString(),verifierSha256:sha(fs.readFileSync(path.join(__dirname,'Verify-TemplateLivePayload.cjs'))),cases:[],passed:false};
function test(name,mutation,rejection){
  const stage = path.join(output,name);fs.mkdirSync(stage);
  for(const [rel,data] of Object.entries(fixture)){const file=path.join(stage,rel);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,data,{flag:'wx'});}
  const copy=structuredClone(payload);
  mutation(stage,copy);
  if(rejection)assert.throws(()=>verifyLivePayload(stage,copy),rejection);
  else {const result=verifyLivePayload(stage,copy);assert.equal(result.checkedFiles,8);assert.equal(result.legalFiles,5);}
  record.cases.push({name,expected:rejection?'rejected':'accepted',passed:true});
}
try{
  test('original-payload',()=>{},null);
  test('normal-navigation-log',stage=>fs.writeFileSync(path.join(stage,'source-navigation.log'),'allowed=1 url='+pathToFileURL(path.join(stage,'resources/index.html')).href+'\n'),null);
  test('same-exe-changed-kernel',stage=>fs.writeFileSync(path.join(stage,'kernel.dll'),'other-kernel'),/Payload bytes changed/);
  test('same-exe-changed-page',stage=>fs.writeFileSync(path.join(stage,'resources/index.html'),'other-page'),/Payload bytes changed/);
  test('missing-notice',stage=>fs.unlinkSync(path.join(stage,'resources/legal/a.txt')),/ENOENT/);
  test('extra-runtime-dll',stage=>fs.writeFileSync(path.join(stage,'injected.dll'),'extra'),/Unexpected runtime file/);
  test('foreign-navigation',stage=>fs.writeFileSync(path.join(stage,'source-navigation.log'),'allowed=1 url=https://invalid.example/'),/Unexpected runtime navigation/);
  test('manifest-escape',(_stage,copy)=>{copy.files[0].path='../ScrollStarter.exe';},/Unsafe payload path/);
  record.passed=true;
}catch(error){record.error=String(error.stack||error);process.exitCode=1;}
finally{record.finishedAt=new Date().toISOString();fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(record,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(record));}
