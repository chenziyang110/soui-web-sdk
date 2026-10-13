'use strict';
const path=require('node:path');
const scope='bound-dll-completed-compositor-calls';
const u64Names=['processId','processCreationFileTime','engineOwnerThreadId','qpcFrequency','recordingEpoch',
  'renderersCreatedTotal','renderersClosedTotal','renderersLive','rasterDrawsTotal','gpuSkiaDrawsTotal',
  'drawEventsTotal','lastDrawQpc','lastDrawThreadId','lastDrawProcessId','lastRendererCloseQpc',
  'closedSnapshotQpc','engineFinalizedQpc','initializedQpc'];
class NativeObservationError extends Error {
  constructor(code,failed=false){super(code);this.failed=failed;}
}
const need=(condition,code,failed=false)=>{if(!condition)throw new NativeObservationError(code,failed);};
function decimal(value) {
  need(typeof value==='string'&&/^(?:0|[1-9]\d{0,19})$/.test(value),'invalid-native-u64');
  const exact=BigInt(value);need(exact<=0xffffffffffffffffn,'invalid-native-u64');return exact;
}
const samePath=(a,b)=>typeof a==='string'&&typeof b==='string'&&
  path.win32.isAbsolute(a)&&path.win32.isAbsolute(b)&&
  path.win32.normalize(a).toLowerCase()===path.win32.normalize(b).toLowerCase();
function snapshot(observation,clock,subject,modulePin,terminal=false) {
  need(observation?.schemaVersion===1&&observation.scope===scope,'missing-native-renderer');
  need(['ready','closed'].includes(observation.state),'native-renderer-unavailable');
  need(observation.moduleMatches===true&&observation.identityBound===true&&
    samePath(observation.modulePath,modulePin.path),'native-module-identity-differs',true);
  need(observation.structSize===168&&observation.abiVersion===1&&observation.reserved===0&&
    observation.status===(observation.state==='ready'?1:4)&&
    Number.isInteger(observation.flags)&&observation.flags>=0&&observation.flags<=0x1ff&&
    Number.isInteger(observation.lastDrawKind)&&[0,1,2].includes(observation.lastDrawKind),
    'invalid-native-renderer-envelope');
  const values=Object.fromEntries(u64Names.map(name=>[name,decimal(observation[name])]));
  need((observation.flags&7)===7&&(observation.flags&0x190)===0,'native-renderer-identity-clock-or-counter-fault');
  need(values.processId===BigInt(subject.pid)&&values.processCreationFileTime===decimal(subject.creationFileTime)&&
    values.processId>0n&&values.processCreationFileTime>0n&&values.engineOwnerThreadId>0n&&
    values.engineOwnerThreadId<=0xffffffffn,'native-owned-identity-differs',true);
  need(values.qpcFrequency>0n&&values.initializedQpc>0n&&values.recordingEpoch===values.initializedQpc,
    'native-module-epoch-unobserved');
  need(clock?.valid===true&&clock.phase==='synchronous-native-read-complete'&&
    decimal(clock.frequency)===values.qpcFrequency&&decimal(clock.captureThreadId)>0n&&
    decimal(clock.captureThreadId)<=0xffffffffn,'native-capture-clock-unbound');
  const captured=decimal(clock.qpc);
  need(captured>=values.initializedQpc&&['lastDrawQpc','lastRendererCloseQpc','closedSnapshotQpc','engineFinalizedQpc']
    .every(name=>values[name]===0n||(values[name]>=values.initializedQpc&&values[name]<=captured)),
    'native-time-outside-capture',true);
  need(values.renderersCreatedTotal>=values.renderersClosedTotal&&
    values.renderersLive===values.renderersCreatedTotal-values.renderersClosedTotal&&
    values.drawEventsTotal===values.rasterDrawsTotal+values.gpuSkiaDrawsTotal,
    'native-renderer-counts-differ',true);
  need(values.gpuSkiaDrawsTotal===0n&&(observation.flags&0x40)===0&&observation.lastDrawKind!==2,
    'observed-compositor-gpu-history',true);
  need(values.rasterDrawsTotal>0n&&(observation.flags&0x20)!==0&&observation.lastDrawKind===1&&
    values.lastDrawThreadId>0n&&values.lastDrawThreadId<=0xffffffffn&&values.lastDrawProcessId===values.processId&&values.lastDrawQpc>0n,
    'completed-raster-draw-not-observed');
  if(terminal)need(observation.state==='closed'&&observation.terminalSnapshot===true&&
    (observation.flags&8)!==0&&values.renderersLive===0n&&values.closedSnapshotQpc>0n&&
    values.engineFinalizedQpc>=values.closedSnapshotQpc,'native-terminal-snapshot-unobserved');
  return {values,captured,observation};
}
function analyzeNativeRenderer(records,subject,modulePin) {
  const output={schema:'soui.bound-dll-compositor-observation/v1',scope,nativeSoftwareGate:'not-evaluated',
    qualified:false,softwareProcessModelGate:'not-evaluated',releaseReady:false,
    missingCoverage:['other-GPU-WebGL-decoder-paths','whole-software-workload','long-duration-resource-qualification']};
  try {
    need(Array.isArray(records)&&subject&&Number.isSafeInteger(subject.pid)&&subject.pid>0&&subject.pid<=0xffffffff,
      'missing-original-owned-subject');
    decimal(subject.creationFileTime);
    need(modulePin&&path.win32.isAbsolute(modulePin.path)&&Number.isSafeInteger(modulePin.bytes)&&modulePin.bytes>0&&
      typeof modulePin.sha256==='string'&&/^[a-f0-9]{64}$/.test(modulePin.sha256),'missing-verified-module-pin');
    const cycles=records.filter(row=>row.event==='sample'&&row.phase==='cycle');
    need(cycles.length===110&&cycles.every((row,index)=>row.cycle===index+1),'missing-native-cycle-sampling');
    for(const phase of ['mounted','closed','runtime-closed'])
      need(records.filter(row=>row.event==='sample'&&row.phase===phase).length===1,'missing-native-close-sampling');
    for(const action of ['close','runtime.shutdown','runtime.shutdown-idempotent'])
      need(records.filter(row=>row.event==='step.complete'&&row.action===action).length===1,'missing-native-close-step');
    const selected=records.filter(row=>row.event==='sample'&&['mounted','cycle','closed','runtime-closed'].includes(row.phase)||
      row.event==='step.complete'&&['close','runtime.shutdown','runtime.shutdown-idempotent'].includes(row.action));
    let previous=null,terminal=null;
    for(const row of selected) {
      const isSample=row.event==='sample';
      const observation=isSample?row.nativeSdk?.renderer:row.nativeRenderer?.renderer;
      const clock=isSample?row.nativeSdk?.captureClock:row.nativeRenderer?.captureClock;
      const ended=isSample?row.phase==='runtime-closed':row.action.startsWith('runtime.shutdown');
      if(isSample)need(row.nativeSdk?.pid===String(subject.pid)&&
        row.nativeSdk.creationFileTime===subject.creationFileTime&&
        row.nativeSdk.captureThreadId===clock?.captureThreadId,'app-native-capture-identity-differs',true);
      const current=snapshot(observation,clock,subject,modulePin,ended),v=current.values;
      if((isSample&&row.phase==='closed')||(!isSample&&row.action==='close'))
        need(observation.state==='closed'&&v.renderersLive===0n&&v.closedSnapshotQpc>0n,
          'native-last-renderer-close-unobserved');
      if(previous) {
        need(['processId','processCreationFileTime','engineOwnerThreadId','qpcFrequency','recordingEpoch','initializedQpc']
          .every(name=>v[name]===previous.values[name]),'native-module-epoch-or-owner-changed',true);
        need(['renderersCreatedTotal','renderersClosedTotal','rasterDrawsTotal','gpuSkiaDrawsTotal','drawEventsTotal','lastDrawQpc']
          .every(name=>v[name]>=previous.values[name])&&current.captured>=previous.captured,
          'native-history-regressed',true);
        if(terminal)need(JSON.stringify(observation)===JSON.stringify(terminal),'native-terminal-overwritten',true);
      }
      if(ended&&!terminal)terminal=observation;
      previous=current;
    }
    need(terminal!==null,'native-terminal-snapshot-unobserved');
    Object.assign(output,{nativeSoftwareGate:'matched',modulePin:{...modulePin},subject:{...subject},
      observations:selected.length,lifetimeRasterDraws:terminal.rasterDrawsTotal,
      lifetimeGpuSkiaDraws:terminal.gpuSkiaDrawsTotal,recordingEpoch:terminal.recordingEpoch,
      initializedQpc:terminal.initializedQpc,terminalSnapshot:terminal,failure:null});
  } catch(error) {
    output.nativeSoftwareGate=error instanceof NativeObservationError&&error.failed?'failed':'not-evaluated';
    output.failure=error instanceof NativeObservationError?error.message:'invalid-native-renderer-observation';
  }
  return output;
}
module.exports={analyzeNativeRenderer,snapshot,u64Names};
