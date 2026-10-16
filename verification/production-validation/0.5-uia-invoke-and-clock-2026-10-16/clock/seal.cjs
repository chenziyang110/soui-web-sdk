'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const e=require('C:/work/sm05-release-050/tools/internal/dependency-evidence.cjs');
const base='C:/work/sm05-memory-envelope-1/PROPOSAL.json',parent=JSON.parse(fs.readFileSync(base));e.checkPin(parent.currentSource);
const files=['tests/acceptance/process-address-census.cjs','tests/acceptance/process-address-census.test.cjs'].map(relative=>{
  const row=parent.files.find(row=>row.relative===relative);assert(row);e.checkPin(row.after);
  return {relative,supersedes:row.after,after:e.pin(__dirname+'/proposal/'+relative)};
});
const red=fs.readFileSync(__dirname+'/red.txt','utf8'),green=fs.readFileSync(__dirname+'/green.txt','utf8');
assert(red.includes("+ 'matched'")&&red.includes("- 'invalid'")&&red.includes('fail 1'));
assert(green.includes('pass 9')&&green.includes('fail 0')&&green.includes('skipped 0'));
fs.writeFileSync(__dirname+'/PROPOSAL.json',JSON.stringify({schema:'soui.proposal-overlay/v1',basePacket:e.pin(base),
  source24:parent.currentSource,files,proofs:['red.txt','green.txt'].map(name=>e.pin(__dirname+'/'+name)),
  scope:'Reject uint64-encoded QPC values that cannot originate from signed Windows LARGE_INTEGER; native collector and all budgets unchanged',
  adopted:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({sealed:true,files:2,testsPassed:9,nativeChanged:false}));
