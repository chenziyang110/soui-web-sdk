'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const files=[];
function walk(folder){for(const entry of fs.readdirSync(folder,{withFileTypes:true})){
  const file=path.join(folder,entry.name);assert(!entry.isSymbolicLink());
  if(entry.isDirectory())walk(file);else if(entry.isFile())files.push(file);
}}
walk(__dirname+'/proposal');
const proofs=[];
for(const arch of ['x64','x86']){
  const file=__dirname+'/'+arch+'-VERIFIED.json',record=JSON.parse(fs.readFileSync(file));
  assert(record.passed&&record.architecture===arch&&!record.releaseReady);
  record.pins.forEach(e.checkPin);e.checkPin(record.actualPin);
  assert(record.records.length===3&&record.records.every(row=>row.natural&&row.result.exitCode===0&&
    row.result.supervisorExitCode===0&&!row.terminal.terminatedOwnedJob&&row.terminal.activeProcesses===0));
  proofs.push(e.pin(file));
}
for(const [file,count]of [['reader-tests-2.txt',8],['resource-reader-tests-2.txt',43]]){
  const text=fs.readFileSync(__dirname+'/'+file,'utf8');assert(text.includes('pass '+count)&&text.includes('fail 0')&&text.includes('skipped 0'));
  proofs.push(e.pin(__dirname+'/'+file));
}
const changes=files.sort().map(file=>{const relative=path.relative(__dirname+'/proposal',file).replaceAll('\\','/');
  return {relative,before:fs.existsSync(root+'/'+relative)?e.pin(root+'/'+relative):null,after:e.pin(file)};});
assert.equal(changes.length,9);
fs.writeFileSync(__dirname+'/PROPOSAL.json',JSON.stringify({schema:'soui.bounded-address-census-proposal/v1',
  adopted:false,currentSource:e.pin(root+'/source-manifest.json'),files:changes,proofs,
  nativeArchitectures:['x86','x64'],nativeGroups:['contracts','actual-private-mapped-image'],pureTestsPassed:51,
  scope:'Bounded native collector and independent scalar reader; final installed acceptance workload and budget calibration pending',
  wholeCacheQualified:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({sealed:true,files:changes.length,nativeArchitectures:2,pureTests:51,candidateChanged:false}));
