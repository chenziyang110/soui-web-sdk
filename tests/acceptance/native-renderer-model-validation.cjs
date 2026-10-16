'use strict';
const compositor=require('./native-renderer-validation.cjs');
const scope='bound-compatibility-backend-workload';
const countNames=['mediaFactoryAttempts','pluginFactoryAttempts','offscreenFactoryAttempts',
  'sharedContextFactoryAttempts','externalFactoryAttempts','platformMismatchTotal','unknownRouteTotal'];
class ModelError extends Error { constructor(code,failed=false){super(code);this.failed=failed;} }
const need=(ok,code,failed=false)=>{if(!ok)throw new ModelError(code,failed);};
function decimal(value) {
  need(typeof value==='string'&&/^(?:0|[1-9]\d{0,19})$/.test(value),'invalid-model-u64');
  const result=BigInt(value);need(result<=0xffffffffffffffffn,'invalid-model-u64');return result;
}
function analyzeNativeModel(records,subject,modulePin) {
  const output={schema:'soui.bound-compatibility-backend-observation/v2',scope,
    softwareWorkloadGate:'not-evaluated',qualified:false,releaseReady:false,
    excludes:'No claim of OS DWM, all-process GPU activity, injected code or other workloads'};
  try {
    need(subject?.entireJobProcesses===1&&subject.naturalClosure===true&&subject.forcedClosure===false&&
      subject.nativeExitCode===0&&subject.supervisorExitCode===0,'original-single-process-natural-job-not-bound');
    const raster=compositor.analyzeNativeRenderer(records,subject,modulePin);
    need(raster.nativeSoftwareGate==='matched',raster.failure||'compositor-unobserved',raster.nativeSoftwareGate==='failed');
    const selected=records.filter(row=>row.event==='sample'&&['mounted','cycle','closed','runtime-closed'].includes(row.phase)||
      row.event==='step.complete'&&['close','runtime.shutdown','runtime.shutdown-idempotent'].includes(row.action));
    let previous=null,terminal=null;
    for(const row of selected) {
      const sample=row.event==='sample',ended=sample?row.phase==='runtime-closed':row.action.startsWith('runtime.shutdown');
      const model=sample?row.nativeSdk?.rendererModel:row.nativeRenderer?.model;
      const clock=sample?row.nativeSdk?.captureClock:row.nativeRenderer?.captureClock;
      need(model?.schemaVersion===2&&model.scope===scope&&['ready','closed'].includes(model.state),
        'complete-native-model-unobserved');
      need(model.structSize===264&&model.abiVersion===2&&model.status===(model.state==='ready'?1:4)&&
        model.flags===19&&model.providerId===1&&model.backendId===1&&model.factoryCoverageMask===127,
        'unsupported-native-model-envelope');
      need(model.moduleMatches===true&&model.identityBound===true&&model.modulePath===model.compositor?.modulePath&&
        model.state===model.compositor.state&&model.status===model.compositor.status&&
        model.terminalSnapshot===model.compositor.terminalSnapshot,'model-compositor-identity-unbound',true);
      const observed=compositor.snapshot(model.compositor,clock,subject,modulePin,ended);
      if((sample&&row.phase==='closed')||(!sample&&row.action==='close'))
        need(model.state==='closed'&&observed.values.renderersLive===0n&&observed.values.closedSnapshotQpc>0n,
          'model-last-renderer-close-unobserved');
      const initialization=decimal(model.backendInitializationQpc);
      need(initialization>0n&&initialization<=observed.values.initializedQpc,'model-initialization-unobserved');
      need(model.compiledGpuSupport===0,'gpu-support-or-build-state-unqualified');
      const counts=Object.fromEntries(countNames.map(name=>[name,decimal(model[name])]));
      need(countNames.every(name=>counts[name]===0n),'extra-or-unknown-rendering-factory-observed');
      if(previous)need(initialization===previous.initialization&&
        ['processId','processCreationFileTime','engineOwnerThreadId','qpcFrequency','recordingEpoch','initializedQpc']
          .every(name=>observed.values[name]===previous.values[name])&&
        ['renderersCreatedTotal','renderersClosedTotal','rasterDrawsTotal','gpuSkiaDrawsTotal','drawEventsTotal','lastDrawQpc']
          .every(name=>observed.values[name]>=previous.values[name])&&observed.captured>=previous.captured&&
        countNames.every(name=>counts[name]>=previous.counts[name]),'model-epoch-or-history-changed',true);
      if(terminal)need(JSON.stringify(model)===JSON.stringify(terminal),'native-model-terminal-overwritten',true);
      if(ended) {
        need(model.terminalSnapshot===true,'native-model-terminal-unobserved');
        if(!terminal)terminal=model;
      }
      previous={initialization,values:observed.values,captured:observed.captured,counts};
    }
    need(terminal!==null,'native-model-terminal-unobserved');
    Object.assign(output,{softwareWorkloadGate:'matched',observations:selected.length,modulePin:{...modulePin},
      subject:{...subject},recordingEpoch:terminal.compositor.recordingEpoch,
      backendInitializationQpc:terminal.backendInitializationQpc,terminalSnapshot:terminal,failure:null});
  } catch(error) {
    output.softwareWorkloadGate=error instanceof ModelError&&error.failed?'failed':'not-evaluated';
    output.failure=error instanceof ModelError?error.message:'invalid-native-model-observation';
  }
  return output;
}
module.exports={analyzeNativeModel,countNames,scope};
