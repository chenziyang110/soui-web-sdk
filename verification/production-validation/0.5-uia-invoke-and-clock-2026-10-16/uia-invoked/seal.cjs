'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const e=require('C:/work/sm05-release-050/tools/internal/dependency-evidence.cjs');
const base='C:/work/sm05-source25-integration-2/PREPARED.json',parent=JSON.parse(fs.readFileSync(base));e.checkPin(parent.currentSource);
const red=JSON.parse(fs.readFileSync(__dirname+'/x64-red1/RESULT.json'));assert(red.matched&&red.natural&&red.expected===1);red.pins.forEach(e.checkPin);
assert(fs.readFileSync(__dirname+'/x64-red1/events-stderr.bin','utf8').includes('real client receives Invoked only after committed observable business result'));
const proofs=[e.pin(__dirname+'/x64-red1/RESULT.json'),e.pin(__dirname+'/x64-red1/events-stderr.bin')];
for(const arch of ['x64','x86']){
  const file=__dirname+'/'+arch+'-green1-VERIFIED.json',pass=JSON.parse(fs.readFileSync(file));assert(pass.expectedObserved);pass.pins.forEach(e.checkPin);
  assert.equal(pass.records.length,16);assert.equal(new Set(pass.records.map(row=>row.name)).size,16);
  assert(pass.records.every(row=>row.expected===0&&row.matched&&row.natural&&row.result.exitCode===0&&row.result.supervisorExitCode===0));proofs.push(e.pin(file));
}
const files=['sdk/accessibility/uia/windows_uia_provider.cpp','sdk/include/soui/runtime/accessibility/windows_uia_provider.h',
  'tests/native/windows_uia_provider_tests.cpp','tests/native/uia/CMakeLists.txt','docs/adr/0109-uia-state-notifications.md'].map(relative=>{
  const row=parent.files.find(row=>row.relative===relative);assert(row);e.checkPin(row.after);
  return {relative,supersedes:row.after,after:e.pin(__dirname+'/proposal/'+relative)};
});
fs.writeFileSync(__dirname+'/PROPOSAL.json',JSON.stringify({schema:'soui.proposal-overlay/v1',basePacket:e.pin(base),
  source24:parent.currentSource,files,proofs,actualNativeCasesPassedPerArchitecture:15,
  scope:'Invoked after successful typed UIA dispatch; shared 16-event pump budget; denied, failed and revoked actions emit no success notification',
  adopted:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({sealed:true,files:5,nativeCasesPassed:30,redObserved:true,candidateChanged:false}));
