'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const reader=require('../../../benchmarks/protocol/manual-frame-observation.cjs');
function marker({nonce=1,revision=0,frame=1,flags=0,pageEpoch=1}={}) {
  const value=Buffer.alloc(16);value[0]=167;value[1]=77;value.writeUInt32LE(nonce,2);value.writeUIntLE(revision,6,3);value.writeUInt16LE(frame,9);value[11]=flags;
  value.writeUInt16LE(pageEpoch,14);return checksum(value);
}
function checksum(bytes) {
  let crc=65535;
  for(let offset=0;offset<16;offset++)if(offset!==12&&offset!==13) {
    crc^=bytes[offset]<<8;
    for(let bit=0;bit<8;bit++)crc=((crc<<1)^((crc&32768)?4129:0))&65535;
  }
  bytes.writeUInt16LE(crc,12);return bytes.toString('hex');
}
test('manual CRC decoder keeps local counters distinct from old ACK and bounded wire fields',()=>{
  assert.deepEqual(reader.decodeMarkerHex(marker({nonce:4294967295,revision:1048575,frame:65535,flags:3})),
    {nonce:4294967295,revision:1048575,frame:65535,flags:3,pageEpoch:1,overflow:true,geometryInvalid:true});
  for(const value of [marker({nonce:0}),marker({frame:0}),marker({revision:1048576}),marker({flags:4}),marker({pageEpoch:0})])assert.throws(()=>reader.decodeMarkerHex(value));
  const old=Buffer.from(marker(),'hex');old[1]=83;assert.throws(()=>reader.decodeMarkerHex(checksum(old)),/magic/);
  assert.equal(reader.decodeMarkerHex(marker({pageEpoch:0,flags:1})).pageEpoch,0);
  const bad=Buffer.from(marker(),'hex');bad[6]^=1;assert.throws(()=>reader.decodeMarkerHex(bad.toString('hex')),/CRC16/);
  for(const value of [null,'',marker().toUpperCase(),marker()+'00'])assert.throws(()=>reader.decodeMarkerHex(value));
  assert.deepEqual(reader.expectedColor(1),[73,151,29]);assert.deepEqual(reader.expectedColor(257),[73,151,29]);
});
function artifactFixture(t) {
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'soui-manual-frame-contract-'));
  t.after(()=>fs.rmSync(directory,{recursive:true,force:false}));
  const roles=['host','kernel','pageHtml','pageScript','collector','reader'];
  const rows=roles.map((role,index)=>{
    const file=path.join(directory,role+'.bin'),bytes=Buffer.from('contract-only-'+index);fs.writeFileSync(file,bytes,{flag:'wx'});
    return {role,path:file,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')};
  });
  return {directory,binding:{frozen:rows,before:structuredClone(rows),after:structuredClone(rows)}};
}
test('artifact identity requires the six frozen/before/after roles and actual files when requested',t=>{
  const {binding}=artifactFixture(t);
  const cold=reader.artifactBindings(binding);assert.equal(cold.endpointPinsConsistent,true);assert.equal(cold.currentFilesRevalidated,false);
  const held=reader.artifactBindings(binding,true);assert.equal(held.currentFilesRevalidated,true);assert.equal(held.actualLoadedModulesAuthenticated,false);
  const changed=structuredClone(binding);changed.after[0].sha256='0'.repeat(64);assert.throws(()=>reader.artifactBindings(changed),/identities/);
  const missing=structuredClone(binding);missing.frozen.pop();assert.throws(()=>reader.artifactBindings(missing),/Six/);
  const duplicate=structuredClone(binding);duplicate.frozen[1]=duplicate.frozen[0];assert.throws(()=>reader.artifactBindings(duplicate),/Duplicate/);
  fs.appendFileSync(binding.frozen[0].path,'changed');assert.throws(()=>reader.artifactBindings(binding,true),/shape/);
});
test('observation text refuses truncation, malformed UTF-8, extra blank lines and non-JSON input',t=>{
  const {directory}=artifactFixture(t),file=path.join(directory,'observation.ndjson');
  fs.writeFileSync(file,'{}\n{}\n{}\n');assert.equal(reader.readLines(file).length,3);
  for(const bytes of [Buffer.from('{}\n{}\n{}'),Buffer.from('{}\n\n{}\n'),Buffer.from('{}\nno\n{}\n'),Buffer.from([123,125,10,255,10,123,125,10])]) {
    fs.writeFileSync(file,bytes);assert.throws(()=>reader.readLines(file));
  }
});

const schema='soui.manual-desktop-observation/v1',run='0'.repeat(31)+'1';
function observationFixture(t) {
  const artifacts=artifactFixture(t),common={schema,run_id:run};
  const expected={run_id:run,targetPid:10,targetCreationFileTime:'100',observerPid:20,observerCreationFileTime:'200',hwnd:'300',qpcFrequency:'1000',intervalMs:10,durationMs:1000,startupMs:1000,artifacts:artifacts.binding};
  const begin={...common,event:'manual-observation.begin',markerVersion:'manual-feedback-v1',targetPid:10,targetCreationFileTime:'100',observerPid:20,observerCreationFileTime:'200',hwnd:'300',qpcFrequency:'1000',intervalMs:10,durationMs:1000,startupMs:1000,startupBeginQpc:'1000',startupDeadlineQpc:'2000',markerRegion:[8,336,64,32],contentProbe:[10,330],captureRegion:[8,328,64,40]};
  const baseline={...common,event:'manual-observation.sample',phase:'startup',sampleIndex:1,captureBeginQpc:'1010',captureEndQpc:'1011',lastPresentQpc:'1009',accumulatedFrames:1,pointerOnly:false,status:'valid',reason:null,markerHex:marker(),contentRgb:[0,0,0],captureBgraCrc32:'11111111',revision:0,frame:1,flags:0,pageEpoch:1,contentChange:false,markerOnly:false,frameSequenceGap:0,revisionDelta:0};
  const phase={...common,event:'manual-observation.phase',phase:'capture',phaseBeginQpc:'1011',phaseDeadlineQpc:'2011'};
  const first={...baseline,phase:'capture',sampleIndex:2,captureBeginQpc:'1020',captureEndQpc:'1021',lastPresentQpc:'1019',markerHex:marker({revision:1,frame:2}),contentRgb:[73,151,29],captureBgraCrc32:'22222222',revision:1,frame:2,contentChange:true,revisionDelta:1};
  const second={...first,sampleIndex:3,captureBeginQpc:'1040',captureEndQpc:'1041',lastPresentQpc:'1039',markerHex:marker({revision:2,frame:3}),contentRgb:[146,46,58],captureBgraCrc32:'33333333',revision:2,frame:3};
  const end={...common,event:'manual-observation.end',phaseBeginQpc:'1011',phaseEndQpc:'2012',phaseDeadlineQpc:'2011',samples:3,startupSamples:1,captureSamples:2,validSamples:2,contentChanges:2,markerOnlySamples:0,duplicateSamples:0,pointerOnlySamples:0,timeoutPolls:0,unknownSamples:0,accumulatedDesktopFrames:2,unobservedDesktopFrames:0,frameSequenceGapTotal:0,firstContentQpc:'1019',lastContentQpc:'1039',headTicks:'8',tailTicks:'973',completed:true,observationQualified:true,reason:null,physicalPresentation:false,inputToPhotonMeasured:false,performanceGate:false};
  return {...artifacts,expected,rows:[begin,baseline,phase,first,second,end]};
}
test('passive reader binds exact identities and the native phase without converting it into physical FPS',t=>{
  const {directory,expected,rows}=observationFixture(t),result=reader.analyzeManualObservation(rows,expected);
  assert.equal(result.traceQualified,true);assert.equal(result.observationQualified,false);assert.equal(result.counts.contentChanges,2);
  assert.deepEqual(result.contentObservations.map(x=>x.revision),[1,2]);assert.equal(result.pageEpoch,1);
  assert.equal(result.window.phaseBeginQpc,'1011');assert.equal(result.window.phaseEndQpc,'2012');assert.equal(result.window.nominalOverrunTicks,'1');
  assert.equal(result.coverage.headTicks,'8');assert.equal(result.coverage.tailTicks,'973');assert.equal(result.coverage.continuousCoverageProven,false);
  for(const key of ['physicalPresentation','inputToPhotonMeasured','inputReplay','nativeInputIds','performanceGate'])assert.equal(result[key],false);
  assert.equal(result.uniqueScreenFrames,null);assert.equal(result.sourceScope.naturalProcessExitVerified,false);
  const file=path.join(directory,'valid.ndjson');fs.writeFileSync(file,rows.map(x=>JSON.stringify(x)).join('\n')+'\n');
  const checked=reader.readManualObservation(file,expected);assert.equal(checked.observationQualified,false);assert.equal(checked.traceQualified,true);assert.equal(checked.artifactIdentity.currentFilesRevalidated,true);
  assert.equal(checked.rawSource.sha256,crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'));
  const edge=structuredClone(rows);edge[3].lastPresentQpc='1011';edge[5].firstContentQpc='1011';edge[5].headTicks='0';
  assert.equal(reader.analyzeManualObservation(edge,expected).coverage.headTicks,'0');
});

test('wait timer backend compatibility preserves old unknown and never grants observation qualification',t=>{
  const {expected,rows}=observationFixture(t);
  assert.equal(reader.analyzeManualObservation(rows,expected).waitTimerBackend,'unknown');
  for(const backend of ['high-resolution-waitable','standard-waitable']) {
    const tagged=structuredClone(rows);tagged[0].waitTimerBackend=backend;
    const result=reader.analyzeManualObservation(tagged,expected);
    assert.equal(result.waitTimerBackend,backend);assert.equal(result.traceQualified,true);
    assert.equal(result.observationQualified,false);assert.equal(result.performanceGate,false);
    Object.assign(tagged[5],{completed:false,observationQualified:false,reason:'window-destroyed'});
    assert.equal(reader.analyzeManualObservation(tagged,expected).traceQualified,false);
  }
  for(const backend of ['unknown','',null,1,'high-resolution']) {
    const invalid=structuredClone(rows);invalid[0].waitTimerBackend=backend;
    assert.throws(()=>reader.analyzeManualObservation(invalid,expected),/wait timer backend/);
  }
});

test('wait timer failures remain fatal despite previously valid content endpoints',t=>{
  const {expected,rows}=observationFixture(t);
  for(const reason of ['wait-timer-failed','wait-timer-close-failed']) {
    const failed=structuredClone(rows);failed[0].waitTimerBackend='standard-waitable';
    Object.assign(failed[5],{completed:false,observationQualified:false,reason});
    const result=reader.analyzeManualObservation(failed,expected);
    assert.equal(result.traceQualified,false);assert.equal(result.observationQualified,false);
    assert.equal(result.observedContentTiming.status,'unavailable');assert.equal(result.performanceGate,false);
    failed[5].completed=true;
    assert.throws(()=>reader.analyzeManualObservation(failed,expected));
    failed[5].completed=false;failed[5].observationQualified=true;
    assert.throws(()=>reader.analyzeManualObservation(failed,expected));
  }
});
test('wrong run, process incarnation, HWND, QPF, resource endpoints and fixed phase are rejected',t=>{
  const {expected,rows}=observationFixture(t);
  for(const mutate of [r=>{r[0].targetCreationFileTime='101';},r=>{r[0].observerCreationFileTime='201';},r=>{r[0].hwnd='301';},
    r=>{r[0].qpcFrequency='1001';},r=>{r[3].run_id='1'.repeat(32);},r=>{r[2].phaseDeadlineQpc='2012';},
    r=>{r[0].startupDeadlineQpc='2001';},r=>{r[5].phaseEndQpc='2010';},r=>{r[3].captureBeginQpc='1008';},
    r=>{r[3].lastPresentQpc='1022';},r=>{r[4].sampleIndex=2;},r=>{r[4].lastPresentQpc='1019';},
    r=>{r[1].accumulatedFrames=2;},r=>{r[5].contentChanges=3;},r=>{r[5].physicalPresentation=true;},r=>{r[5].headTicks='0';},r=>{r[5].extra=true;}]) {
    const changed=structuredClone(rows);mutate(changed);assert.throws(()=>reader.analyzeManualObservation(changed,expected));
  }
  const wrong=structuredClone(expected);wrong.targetCreationFileTime=100;assert.throws(()=>reader.analyzeManualObservation(rows,wrong));
  wrong.targetCreationFileTime='0100';assert.throws(()=>reader.analyzeManualObservation(rows,wrong));
  const endpoints=structuredClone(expected);endpoints.artifacts.after[3].sha256='0'.repeat(64);assert.throws(()=>reader.analyzeManualObservation(rows,endpoints));
});
test('marker-only and repeated original probe observations do not become content frames',t=>{
  const {expected,rows}=observationFixture(t);
  Object.assign(rows[3],{markerHex:marker({frame:2}),contentRgb:[0,0,0],revision:0,contentChange:false,markerOnly:true,revisionDelta:0});
  Object.assign(rows[4],{markerHex:marker({frame:2}),contentRgb:[0,0,0],frame:2,revision:0,contentChange:false,markerOnly:false,revisionDelta:0});
  Object.assign(rows[5],{contentChanges:0,markerOnlySamples:1,duplicateSamples:1,firstContentQpc:null,lastContentQpc:null,headTicks:null,tailTicks:null,observationQualified:false,reason:'insufficient-content-changes'});
  const result=reader.analyzeManualObservation(rows,expected);assert.equal(result.counts.contentChanges,0);assert.equal(result.counts.markerOnlySamples,1);assert.equal(result.counts.duplicateSamples,1);assert.equal(result.traceQualified,false);assert.equal(result.coverage.headTicks,null);
});
test('local render jumps preserve observed endpoints while aliases remain unknown',t=>{
  const fixture=observationFixture(t),{expected}=fixture,alias=structuredClone(fixture.rows);
  Object.assign(alias[4],{markerHex:marker({revision:257,frame:3}),contentRgb:[73,151,29],revision:257,revisionDelta:256,status:'unknown',reason:'content-alias',contentChange:false,markerOnly:true});
  Object.assign(alias[5],{validSamples:1,contentChanges:1,unknownSamples:1,lastContentQpc:'1019',tailTicks:'993',observationQualified:false,reason:'observation-gaps'});
  const a=reader.analyzeManualObservation(alias,expected);assert.equal(a.counts.contentChanges,1);assert.equal(a.counts.markerOnlySamples,0);assert.equal(a.unknownReasons['content-alias'],1);
  const gap=structuredClone(fixture.rows);
  // Two page rAF renders precede one captured desktop endpoint. Only that
  // endpoint's changed background is observed; the intermediate fate is unknown.
  Object.assign(gap[3],{markerHex:marker({revision:2,frame:3}),revision:2,frame:3,contentRgb:[146,46,58],revisionDelta:2,frameSequenceGap:1});
  Object.assign(gap[4],{markerHex:marker({revision:3,frame:4}),revision:3,frame:4,contentRgb:[219,197,87]});
  gap[5].frameSequenceGapTotal=1;
  const g=reader.analyzeManualObservation(gap,expected);
  assert.equal(g.traceQualified,true);assert.equal(g.observationQualified,false);
  assert.equal(g.counts.frameSequenceGapTotal,1);assert.equal(g.counts.unobservedDesktopFrames,0);assert.equal(g.counts.unknownSamples,0);
  assert.equal(g.counts.contentChanges,2);assert.deepEqual(g.contentObservations.map(x=>x.revision),[2,3]);
  assert.deepEqual(g.observedContentTiming.intervalTicks,['20']);assert.equal(g.observedContentTiming.changesPerSecond.numerator,'2000');
  assert.equal(g.allPageRendersObserved,false);assert.equal(g.intermediatePageRenderDisposition,'unknown');
  assert.equal(g.observedContentTiming.changesPerSecond.physicalFrameRate,false);assert.equal(g.uniqueScreenFrames,null);
  const missing=structuredClone(gap);missing[3].frameSequenceGap=0;assert.throws(()=>reader.analyzeManualObservation(missing,expected),/delta/);
  const legacyReason=structuredClone(gap);legacyReason[3].reason='frame-gap';assert.throws(()=>reader.analyzeManualObservation(legacyReason,expected),/reason/);
});

test('DXGI accumulated frames remain unqualified even with local render jumps',t=>{
  const {expected,rows}=observationFixture(t);
  Object.assign(rows[3],{markerHex:marker({revision:2,frame:3}),revision:2,frame:3,contentRgb:[146,46,58],revisionDelta:2,frameSequenceGap:1,
    accumulatedFrames:2,status:'unknown',reason:'accumulated-frames',contentChange:false});
  Object.assign(rows[4],{markerHex:marker({revision:3,frame:4}),revision:3,frame:4,contentRgb:[219,197,87]});
  Object.assign(rows[5],{validSamples:1,contentChanges:1,unknownSamples:1,accumulatedDesktopFrames:3,unobservedDesktopFrames:1,frameSequenceGapTotal:1,
    firstContentQpc:'1039',headTicks:'28',observationQualified:false,reason:'observation-gaps'});
  const result=reader.analyzeManualObservation(rows,expected);
  assert.equal(result.traceQualified,false);assert.equal(result.observationQualified,false);assert.equal(result.counts.contentChanges,1);
  assert.equal(result.counts.unobservedDesktopFrames,1);assert.equal(result.counts.frameSequenceGapTotal,1);assert.equal(result.unknownReasons['accumulated-frames'],1);
  assert.equal(result.observedContentTiming.status,'unavailable');assert.equal(result.observedContentTiming.changesPerSecond,null);
  const falselyValid=structuredClone(rows);Object.assign(falselyValid[3],{status:'valid',reason:null,contentChange:true});
  assert.throws(()=>reader.analyzeManualObservation(falselyValid,expected),/Valid sample marker identity\/flags\/acquisition invalid/);
  const falselyQualified=structuredClone(rows);falselyQualified[5].observationQualified=true;
  assert.throws(()=>reader.analyzeManualObservation(falselyQualified,expected),/qualification/);
});
test('reload epoch, counter rollback, CRC corruption and probe disagreement cannot pass as valid frames',t=>{
  const {expected,rows}=observationFixture(t);
  for(const mutate of [r=>{r[4].pageEpoch=2;r[4].markerHex=marker({revision:2,frame:3,pageEpoch:2});},
    r=>{r[4].revision=0;r[4].markerHex=marker({revision:0,frame:3});r[4].contentRgb=[0,0,0];},
    r=>{r[4].frame=2;r[4].markerHex=marker({revision:2,frame:2});},
    r=>{r[4].markerHex=r[4].markerHex.slice(0,-2)+'ff';},r=>{r[4].contentRgb=[1,2,3];},
    r=>{r[4].captureBgraCrc32='not-crc';},r=>{r[4].flags=1;r[4].markerHex=marker({revision:2,frame:3,flags:1});}]) {
    const changed=structuredClone(rows);mutate(changed);assert.throws(()=>reader.analyzeManualObservation(changed,expected));
  }
});
test('timeout has no fabricated pixels and fatal phase cannot become a completed observation',t=>{
  const {expected,rows}=observationFixture(t);
  Object.assign(rows[3],{status:'wait-timeout',markerHex:null,contentRgb:null,captureBgraCrc32:null,revision:null,frame:null,flags:null,pageEpoch:null,lastPresentQpc:'0',accumulatedFrames:0,contentChange:false,revisionDelta:0});
  Object.assign(rows[4],{markerHex:marker({revision:2,frame:2}),frame:2,revisionDelta:2});
  Object.assign(rows[5],{validSamples:1,contentChanges:1,timeoutPolls:1,accumulatedDesktopFrames:1,firstContentQpc:'1039',headTicks:'28',observationQualified:false,reason:'insufficient-content-changes'});
  const result=reader.analyzeManualObservation(rows,expected);assert.equal(result.counts.timeoutPolls,1);assert.equal(result.traceQualified,false);
  rows[5].completed=false;rows[5].reason='foreground-lost';assert.equal(reader.analyzeManualObservation(rows,expected).traceQualified,false);
  rows[5].observationQualified=true;assert.throws(()=>reader.analyzeManualObservation(rows,expected));
});
test('observed content timing uses exact QPC intervals and preserves manual-idle and invalid-trace limits',t=>{
  const {expected,rows}=observationFixture(t),end=rows.pop(),base=rows[4];
  for(const [revision,begin,finish,present] of [[3,'1050','1051','1049'],[4,'1120','1121','1119'],[5,'1900','1901','1899']])
    rows.push({...base,sampleIndex:revision+1,revision,frame:revision+1,captureBeginQpc:begin,captureEndQpc:finish,lastPresentQpc:present,
      markerHex:marker({revision,frame:revision+1}),contentRgb:reader.expectedColor(revision)});
  Object.assign(end,{samples:6,captureSamples:5,validSamples:5,contentChanges:5,accumulatedDesktopFrames:5,lastContentQpc:'1899',tailTicks:'113'});rows.push(end);
  const timing=reader.analyzeManualObservation(rows,expected).observedContentTiming;
  assert.deepEqual(timing.intervalTicks,['20','10','70','780']);
  assert.deepEqual(timing.summary,{samples:4,minimumTicks:'10',maximumTicks:'780',p50Ticks:'20',p95Ticks:'780',p99Ticks:'780',intervalsOver50ms:2});
  assert.equal(timing.changesPerSecond.numerator,'5000');assert.equal(timing.changesPerSecond.denominator,'1001');
  assert.equal(timing.changesPerSecond.includesManualIdle,true);assert.equal(timing.changesPerSecond.physicalFrameRate,false);assert.equal(timing.changesPerSecond.workloadComparable,'unknown');
  end.completed=false;end.observationQualified=false;end.reason='foreground-lost';
  const missing=reader.analyzeManualObservation(rows,expected).observedContentTiming;
  assert.equal(missing.status,'unavailable');assert.equal(missing.intervalTicks,null);assert.equal(missing.summary,null);assert.equal(missing.changesPerSecond,null);
});
