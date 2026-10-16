'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {analyzeNativeRenderer,u64Names}=require('./native-renderer-validation.cjs');
const {completeResourceAnalysis}=require('./resource-validation.cjs');
const subject={pid:9,creationFileTime:'123'};
const modulePin={path:'C:\\fixture-only\\runtime\\node.dll',bytes:1234,sha256:'a'.repeat(64)};
// These are DTO-only cases, never real renderer/Owned/file evidence.
function fixture() {
  const initial={schemaVersion:1,scope:'bound-dll-completed-compositor-calls',state:'ready',
    modulePath:modulePin.path,moduleMatches:true,identityBound:true,terminalSnapshot:false,
    structSize:168,abiVersion:1,status:1,flags:0x27,lastDrawKind:1,reserved:0,
    ...Object.fromEntries(u64Names.map(name=>[name,'0'])),processId:'9',processCreationFileTime:'123',
    engineOwnerThreadId:'10',qpcFrequency:'1000',recordingEpoch:'100',initializedQpc:'100',
    renderersCreatedTotal:'1',renderersLive:'1',rasterDrawsTotal:'1',drawEventsTotal:'1',
    lastDrawQpc:'110',lastDrawThreadId:'11',lastDrawProcessId:'9'};
  const clock=qpc=>({valid:true,qpc:String(qpc),frequency:'1000',captureThreadId:'17',phase:'synchronous-native-read-complete'});
  const sample=(phase,cycle,value,qpc)=>({event:'sample',phase,cycle,nativeSdk:{pid:'9',creationFileTime:'123',
    captureThreadId:'17',captureClock:clock(qpc),renderer:structuredClone(value)}});
  const step=(action,value,qpc)=>({event:'step.complete',action,nativeRenderer:{renderer:structuredClone(value),captureClock:clock(qpc)}});
  const rows=[sample('mounted',0,initial,120)];
  for(let cycle=1;cycle<=110;cycle++) rows.push(sample('cycle',cycle,{...initial,
    rasterDrawsTotal:String(cycle+1),drawEventsTotal:String(cycle+1),lastDrawQpc:String(110+cycle)},120+cycle));
  const closed={...rows.at(-1).nativeSdk.renderer,state:'closed',status:4,
    renderersLive:'0',renderersClosedTotal:'1',lastRendererCloseQpc:'240',closedSnapshotQpc:'240'};
  rows.push(step('close',closed,250),sample('closed',111,closed,260));
  const terminal={...closed,terminalSnapshot:true,flags:closed.flags|8,engineFinalizedQpc:'270'};
  rows.push(step('runtime.shutdown',terminal,280),sample('runtime-closed',111,terminal,290),
    step('runtime.shutdown-idempotent',terminal,300));
  return rows;
}
const evaluate=rows=>analyzeNativeRenderer(rows,subject,modulePin);
const nativeAt=(rows,index=1)=>rows[index].nativeSdk.renderer;
test('110 bound raster snapshots and unchanged terminal establish only the finite compositor subgate',()=>{
  const observed=evaluate(fixture());
  assert.equal(observed.nativeSoftwareGate,'matched');assert.equal(observed.qualified,false);
  assert.equal(observed.softwareProcessModelGate,'not-evaluated');assert.equal(observed.observations,116);
  assert.equal(observed.lifetimeGpuSkiaDraws,'0');assert.equal(observed.releaseReady,false);
});
test('missing, disabled, busy, old ABI and no actual completed raster draw are not evaluated',()=>{
  for(const edit of [v=>{v.state='disabled';},v=>{v.state='busy';},v=>{v.structSize=160;},
    v=>{v.flags=7;v.rasterDrawsTotal=v.drawEventsTotal='0';v.lastDrawKind=0;}]) {
    const rows=fixture();edit(nativeAt(rows));assert.equal(evaluate(rows).nativeSoftwareGate,'not-evaluated');
  }
  const rows=fixture();delete rows[1].nativeSdk;assert.equal(evaluate(rows).nativeSoftwareGate,'not-evaluated');
});
test('GPU history remains rejected even when a later snapshot reports a raster last draw',()=>{
  const rows=fixture(),v=nativeAt(rows);v.gpuSkiaDrawsTotal='1';v.drawEventsTotal='3';v.flags|=0x40;
  const result=evaluate(rows);assert.equal(result.nativeSoftwareGate,'failed');
  assert.equal(result.failure,'observed-compositor-gpu-history');
});
test('physical module route, Owned identity, initialization epoch and owner changes cannot match',()=>{
  for(const edit of [v=>{v.modulePath='C:\\other\\node.dll';},v=>{v.processCreationFileTime='124';},
    v=>{v.recordingEpoch=v.initializedQpc='101';},v=>{v.engineOwnerThreadId='12';}]) {
    const rows=fixture();edit(nativeAt(rows));assert.equal(evaluate(rows).nativeSoftwareGate,'failed');
  }
});
test('uint64 precision, canonical decimal encoding and real capture clocks are required',()=>{
  for(const edit of [v=>{v.processId=9;},v=>{v.processId='09';},
    v=>{v.processCreationFileTime='18446744073709551616';},v=>{v.rasterDrawsTotal='-1';}]) {
    const rows=fixture();edit(nativeAt(rows));assert.notEqual(evaluate(rows).nativeSoftwareGate,'matched');
  }
  for(const edit of [c=>{c.qpc='109';},c=>{c.frequency='999';},c=>{c.valid=false;}]) {
    const rows=fixture();edit(rows[1].nativeSdk.captureClock);assert.notEqual(evaluate(rows).nativeSoftwareGate,'matched');
  }
});
test('regression, missing cycle or lost final closure cannot match',()=>{
  const rows=fixture();nativeAt(rows,2).rasterDrawsTotal=nativeAt(rows,2).drawEventsTotal='1';
  assert.equal(evaluate(rows).failure,'native-history-regressed');
  const missing=fixture();missing.splice(40,1);assert.equal(evaluate(missing).nativeSoftwareGate,'not-evaluated');
  const unclosed=fixture();unclosed.at(-1).nativeRenderer.renderer.terminalSnapshot=false;
  assert.equal(evaluate(unclosed).nativeSoftwareGate,'not-evaluated');
});
test('repeated shutdown/read cannot overwrite the first terminal Native timestamp',()=>{
  const rows=fixture();rows.at(-1).nativeRenderer.renderer.engineFinalizedQpc='271';
  assert.equal(evaluate(rows).failure,'native-terminal-overwritten');
});
test('complete binding and matched compositor observations leave frozen total resource qualification pending',()=>{
  const analysis={resourceGate:'passed',passed:true,releaseReady:false,subject};
  const complete={inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',nativeScopeObserved:false,
    nativeToolchainProfile:'MSVC143',loadedPayloadModule:modulePin};
  const observed=completeResourceAnalysis(analysis,complete,fixture());
  assert.equal(observed.nativeScopeGate,'matched-compositor-only');assert.equal(observed.resourceRuleObservation,'passed');
  assert.equal(observed.resourceGate,'not-evaluated');assert.equal(observed.passed,false);
  assert.equal(observed.softwareProcessModelGate,'not-evaluated');
  assert.equal(observed.failure,'broader-software-process-workload-not-observed');
});
