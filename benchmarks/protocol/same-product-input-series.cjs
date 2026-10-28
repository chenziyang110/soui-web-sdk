'use strict';
const owned=require('./owned-trace-subject.cjs');
const pair=require('./same-product-input-pair.cjs');
const {insist,exact,distribution}=require('./input-frame-trace.cjs');
function nextAction(history,scenario) {
  const related=history.filter(item=>item.scenario===scenario);
  const last=related.at(-1),previous=related.at(-2);
  if(last?.status==='product_failure')return {action:'stop',reason:'product-failure'};
  if(last?.status==='invalid_environment'&&previous?.status==='invalid_environment'&&last.reason===previous.reason)
    return {action:'stop',reason:last.reason};
  return {action:'run',reason:null};
}
function tailUncertainty(values) {
  if(!values.length)return {samples:0,p99:null,interval:null,scope:'unavailable'};
  const description=distribution(values),sorted=[...values].sort((a,b)=>a-b),n=values.length;
  // DKW: P(sup|Fn-F|>epsilon)<=2exp(-2n epsilon^2), confidence95%.
  // An upper rank beyond1 is unbounded; the sample maximum is not a population bound.
  const epsilon=Math.sqrt(Math.log(40)/(2*n));
  const quantile=q=>sorted[Math.max(0,Math.ceil(q*n)-1)];
  return {...description,interval:{method:'DKW-quantile-band-conditional-iid',nominal_confidence_iid:0.95,
    assumptions:'independent-identically-distributed-not-established-for-burst',
    lower_ms:0.99-epsilon<=0?null:quantile(0.99-epsilon),lower_unbounded:0.99-epsilon<=0,
    upper_ms:0.99+epsilon>=1?null:quantile(0.99+epsilon),
    upper_unbounded:0.99+epsilon>=1},scope:n>=10000?'descriptive-tail-sample-count-met':'exploratory'};
}
function checkSeries(root) {
  const record=owned.readRunJson(root,'input-ui-series.json');
  insist(record?.schema==='soui.same-product-input-series/v1'&&typeof record.scenario==='string'&&
    /^[A-Za-z0-9._-]{1,128}$/.test(record.scenario)&&Array.isArray(record.attempts)&&
    record.attempts.length<=64,'Invalid bounded series manifest');
  const history=[],rounds=[],seenPaths=new Set(),seenRuns=new Set();
  let baselineCommit=null,candidateCommit=null,reference=null,lastEnd=0n,pendingPair=null,pairCount=0,classified=0;
  for(const item of record.attempts) {
    insist(item?.scenario===record.scenario&&nextAction(history,item.scenario).action==='run',
      'Scenario continued after its required stop');
    insist(item.ordinal===history.length+1&&['valid','invalid_environment','product_failure'].includes(item.status),
      'Invalid attempt ordinal/status');
    const directory=owned.runDirectory(root,item.path);
    insist(!seenPaths.has(item.path),'Attempt evidence directory reused');seenPaths.add(item.path);
    if(item.status!=='valid') {
      insist(typeof item.reason==='string'&&/^[a-z0-9._-]{1,128}$/.test(item.reason),'Invalid failure reason');
      const failure=owned.readRunJson(directory,'attempt-invalid.json');
      insist(failure?.schema==='soui.input-ui-invalid-attempt/v1'&&failure.scenario===item.scenario&&
        failure.status===item.status&&failure.reason===item.reason&&failure.retained_raw===true,
        'Failed attempt raw-evidence receipt missing');
      insist(Array.isArray(failure.raw_evidence)&&failure.raw_evidence.length>0&&failure.raw_evidence.length<=16,
        'Failed attempt lacks bounded raw evidence descriptors');
      for(const evidence of failure.raw_evidence)pair.verifiedDescriptor(directory,evidence,evidence?.path);
      history.push(item);continue;
    }
    insist(rounds.length<40,'Series valid-run limit exceeded');
    const run=pair.checkRun(directory),pairIndex=Math.floor(rounds.length/2);
    const expectedRole=pairIndex%2===0?(rounds.length%2===0?'baseline':'candidate'):
      (rounds.length%2===0?'candidate':'baseline');
    insist(item.pair===pairIndex+1&&item.role===expectedRole,'Series must alternate baseline/candidate pair order');
    const start=exact(run.closed.started_qpc,false),end=exact(run.closed.ended_qpc,false);
    insist(start>=lastEnd&&end>=start&&!seenRuns.has(run.record.run_id),'Actual host runs overlapped/reused identity');
    lastEnd=end;seenRuns.add(run.record.run_id);
    if(reference) {
      for(const key of ['product_id','renderer','runtime_version','trace_mode','measurement_source_commit'])insist(run.record[key]===reference.record[key],'Series product/runtime/trace mode/measurement source changed');
      insist(run.result.qpc_frequency===reference.frequency,'Series QPC frequency changed');
      for(const key of ['environment','configuration','workload'])
        insist(run.record.evidence[key].sha256===reference.record.evidence[key].sha256,'Series frozen inputs changed');
      for(const key of ['input_sha256','owned_runner_sha256'])insist(run.record.tools[key]===reference.record.tools[key],'Series tool changed');
    }else reference={record:run.record,frequency:run.result.qpc_frequency};
    if(item.role==='baseline'){if(baselineCommit)insist(baselineCommit===run.record.source_commit,'Baseline revision changed');else baselineCommit=run.record.source_commit;}
    else {if(candidateCommit)insist(candidateCommit===run.record.source_commit,'Candidate revision changed');else candidateCommit=run.record.source_commit;}
    if(item.role==='candidate')classified+=run.result.classified_input_count;
    rounds.push({pair:item.pair,role:item.role,run_id:run.record.run_id,
      input_count:run.result.input_count,terminal_counts:run.result.input_terminal_counts,
      classified_inputs:run.result.classified_input_count,stage_samples:run.result.native_stage_samples,
      t7_samples:run.result.desktop_samples,stage_evidence:run.result.stage_evidence,
      fullCausalComplete:run.result.fullCausalComplete,
      t1_to_t2:tailUncertainty(run.result.stage_values_ms.host_to_engine),
      t1_to_t6:tailUncertainty(run.result.stage_values_ms.host_to_submit),
      t0_to_captured_image:tailUncertainty(run.result.samples.map(sample=>sample.upper_ms))});
    if(!pendingPair)pendingPair={item,run};
    else {
      if(pendingPair.item.role==='baseline')pair.compareRuns(pendingPair.run,run);
      else pair.compareRuns(run,pendingPair.run);
      pendingPair=null;++pairCount;
    }
    history.push(item);
  }
  insist(!pendingPair,'Series ends with an unmatched run');
  const stop=nextAction(history,record.scenario),eligible=pairCount>=5&&classified>=10000;
  return {schema:'soui.same-product-input-series-check/v1',scenario:record.scenario,
    baseline_commit:baselineCommit,candidate_commit:candidateCommit,valid_pairs:pairCount,
    candidate_classified_inputs:classified,required_pairs:5,required_classified_inputs:10000,
    repetitionEvidenceAccepted:eligible,scenario_action:stop.action,stop_reason:stop.reason,
    failed_attempts:history.filter(item=>item.status!=='valid').length,
    rounds,
    uncertainty_scope:'per-round-event-order-statistics-not-p99-of-round-means',
    physicalPresentation:false,performanceGate:false,releaseReady:false};
}
if(require.main===module) {
  try{insist(process.argv.length===3,'Usage: node same-product-input-series.cjs SERIES_ROOT');
    const result=checkSeries(process.argv[2]);console.log(JSON.stringify(result,null,2));
    if(!result.repetitionEvidenceAccepted||result.scenario_action==='stop')process.exitCode=2;
  }catch(error){console.error(error.message);process.exitCode=1;}
}
module.exports={nextAction,tailUncertainty,checkSeries};
