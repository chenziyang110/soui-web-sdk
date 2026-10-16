'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),{read}=require('./read-task-store-quota.cjs');
const subject={pid:42,creationFileTime:'134365231775831716'};
function rows(){return ['empty','full-history','full-inflight','released-inflight','retired-history'].map((phase,i)=>({schema:'soui.native-task-store-calibration/1',phase,...subject,qpcBegin:String(10+i*10),qpcEnd:String(12+i*10),qpcFrequency:'1000',poolEpoch:'3',configuredLimit:null,chargedLive:'10',chargedPeak:String(20+i),successfulLive:'9',successfulPeak:String(19+i),quotaDenials:'0',storeEntries:i?1024:0,inflight:i===2?32:0,queued:i===2?30:0,handled:[0,1024,1024,1056,1120][i],privateBytes:'1000',handles:20,productionDefaultQualified:false})).concat({schema:'soui.native-task-store-calibration/1',phase:'destructor-returned',postDestructionQuotaRead:false,productionDefaultQualified:false});}
const bytes=value=>Buffer.from(value.map(v=>JSON.stringify(v)).join('\n')+'\n');
test('complete bounded workload preserves exact uint64 values without claiming qualification',()=>{
 const value=rows();for(const row of value.slice(0,5)){row.chargedPeak='9007199254740993';row.successfulPeak='9007199254740992';}
 const result=read(bytes(value),subject);assert.equal(result.chargedPeak,'9007199254740993');assert.equal(result.productionDefaultQualified,false);assert.equal(result.nativeProcessClosureVerified,false);
});
test('original bytes, exact process identity and one retained owner are required',()=>{
 const raw=bytes(rows());assert.throws(()=>read(raw,{...subject,pid:43}));assert.throws(()=>read(raw.subarray(0,-1),subject));assert.throws(()=>read(Buffer.concat([Buffer.from([0xef,0xbb,0xbf]),raw]),subject));
 const invalid=rows();invalid[2].poolEpoch='4';assert.throws(()=>read(bytes(invalid),subject));
 assert.throws(()=>read(Buffer.from(raw.toString().replace('"pid":42','"pid":42,"pid":42')),subject));
});
test('partial load, changed policy, refusals and unproven cleanup are rejected',()=>{
 for(const [index,field,value]of [[1,'storeEntries',1023],[2,'inflight',31],[2,'queued',29],[4,'handled',1119],[4,'quotaDenials','1'],[3,'configuredLimit','100'],[5,'postDestructionQuotaRead',true],[5,'productionDefaultQualified',true]]){const v=rows();v[index][field]=value;assert.throws(()=>read(bytes(v),subject));}
 assert.throws(()=>read(bytes(rows().slice(1)),subject));
});
test('credit, cap and monotonic sampling remain independent constraints',()=>{
 for(const [field,value]of [['chargedLive','100'],['successfulLive','11'],['successfulPeak','100'],['chargedPeak','1'],['qpcBegin','1'],['qpcFrequency','0'],['chargedPeak','18446744073709551616'],['chargedPeak','01']]){const v=rows();v[3][field]=value;assert.throws(()=>read(bytes(v),subject));}
 const capped=rows();for(const row of capped.slice(0,5))row.configuredLimit='30';assert.equal(read(bytes(capped),subject,'30').limit,'30');assert.throws(()=>read(bytes(capped),subject,'20'));
});
