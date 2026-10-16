'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const arch=process.argv[2],attempt=process.argv[3];assert(['x86','x64'].includes(arch)&&/^[a-z0-9-]+$/.test(attempt));
const folder=__dirname+'/capture-'+arch+'-'+attempt,output=__dirname+'/output-'+arch+'-'+attempt;
const prepared=JSON.parse(fs.readFileSync(__dirname+'/PREPARED.json'));
for(const name of ['nativeHeader','protocol','source24','script','reader'])e.checkPin(prepared[name]);
e.verifySource(prepared.after,prepared.source);e.verifySource(prepared.original,prepared.originalRoot);
const record=JSON.parse(fs.readFileSync(folder+'/CAPTURE.json'));assert(record.completedWorkload&&record.analysisPending);
record.pins.forEach(e.checkPin);e.checkPin(record.kernelPin);e.checkPin(record.page);
e.verifySource(record.copied,output+'/installed');e.verifySource(record.payload,output+'/build/bin/Release');
const stage=record.records.find(row=>row.name==='calibration');assert(stage?.natural&&stage.result.exitCode===0&&stage.result.supervisorExitCode===0);
const standard=require(root+'/tools/internal/standard-build.cjs');
const rawOwned=fs.readFileSync(folder+'/calibration.ndjson'),rawContext=fs.readFileSync(folder+'/calibration.ndjson.context.json');
const ready=JSON.parse(fs.readFileSync(folder+'/calibration-ready.json'));
standard.verifyStream(rawOwned,JSON.parse(rawContext),ready,stage.result);
const owned=rawOwned.toString('utf8').trim().split('\n').map(JSON.parse),subject=owned[0];
assert.equal(subject.pid,stage.result.pid);assert.equal(subject.creationFileTime,stage.result.creationFileTime);
const rawPath=folder+'/calibration-stdout.bin',raw=fs.readFileSync(rawPath);assert(raw.length>0&&raw.length<16*1024*1024&&raw.at(-1)===10);
const strict=require(root+'/tools/internal/owned-tail-evidence.cjs').parseStrictJson;
const rows=raw.toString('utf8').trim().split(/\r?\n/).map(line=>strict(line));assert(rows.length>0&&rows.every(row=>row.event==='cache.sample'));
const overlayPath='C:/work/sm05-memory-envelope-2/PROPOSAL.json',overlay=JSON.parse(fs.readFileSync(overlayPath));
const readerRow=overlay.files.find(row=>row.relative==='tests/acceptance/process-address-census.cjs');
assert(readerRow&&readerRow.supersedes.sha256===prepared.reader.sha256);e.checkPin(readerRow.after);
const reader=require(readerRow.after.path),observations=[],peaks={private:0n,mapped:0n,image:0n,total:0n,privateUsage:0n};
let previous;const counts={matched:0,partial:0,invalid:0,'not-evaluated':0};
for(const [index,row]of rows.entries()){
  const observed=reader.analyzeAddressSpace(row.addressSpace,{subject,nativeIdentity:row.nativeIdentity,
    captureClock:row.captureClock,pointerBytes:arch==='x86'?4:8,previous});
  ++counts[observed.dataGate];observations.push({index,stage:row.stage,phase:row.phase,elapsedMs:row.elapsedMs,...observed});
  if(observed.dataGate==='matched'){
    previous=row.addressSpace;
    for(const kind of ['private','mapped','image'])peaks[kind]=BigInt(observed.committed[kind])>peaks[kind]?BigInt(observed.committed[kind]):peaks[kind];
    const total=BigInt(row.addressSpace.committedTotalBytes),usage=reader.decimal(row.privateBytes);
    peaks.total=total>peaks.total?total:peaks.total;peaks.privateUsage=usage>peaks.privateUsage?usage:peaks.privateUsage;
  }
}
record.pins.forEach(e.checkPin);e.checkPin(prepared.reader);e.checkPin(readerRow.after);e.verifySource(prepared.after,prepared.source);
const passed=counts.matched===rows.length;
fs.writeFileSync(folder+'/ADDRESS-OBSERVED.json',JSON.stringify({passed,architecture:arch,subject:{pid:subject.pid,creationFileTime:subject.creationFileTime},
  pins:[__filename,__dirname+'/PREPARED.json',overlayPath,readerRow.after.path,folder+'/CAPTURE.json',rawPath,folder+'/calibration.ndjson',folder+'/calibration.ndjson.context.json'].map(e.pin),
  counts,observations,peaksFromMatchedSamples:Object.fromEntries(Object.entries(peaks).map(([name,value])=>[name,value.toString()])),
  scope:prepared.scope,processCommitIsNotPhysicalMemory:true,wholeCacheQualified:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({arch,passed,counts,peaks:Object.fromEntries(Object.entries(peaks).map(([name,value])=>[name,value.toString()]))}));
assert(passed,'Incomplete actual workload address-space observations retained');
