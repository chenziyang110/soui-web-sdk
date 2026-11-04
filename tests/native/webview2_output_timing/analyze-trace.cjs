'use strict';
const trace=require('../../../benchmarks/protocol/input-frame-trace.cjs');
const signature=0x57564f0100000000n,mask=0x03ffffffn;
function decode(value){
  const packed=trace.exact(value);
  if((packed&~mask)!==signature)return null;
  const result={origin:Number((packed>>17n)&7n),executor:Number((packed>>20n)&7n),preparation:Number((packed>>23n)&7n),raw_bytes:Number(packed&0x1ffffn)};
  trace.insist(result.origin>=1&&result.origin<=3&&result.executor>=1&&result.executor<=3&&result.preparation<=4&&result.raw_bytes<=65536,'Invalid private output metadata');
  return result;
}
function analyze(rows){
  const checked=trace.checkTrace(rows),tasks=new Map(),preparation=[],afterPreparation=[];
  const failures={capacity:0,stale:0,stopped:0,invalid:0,other:0};
  let observed=0,delivered=0,incomplete=0;
  for(const event of checked.points){
    const details=decode(event.value);if(!details)continue;
    trace.insist(['input_first','input_last','action_first','action_last','frame_id','view_id','view_generation','geometry_generation','related_id'].every(key=>event[key]==='0')&&event.flags===0,
      'Output timing invented an input/frame relation');
    trace.insist(event.task_id!=='0'&&event.document_generation!=='0','Output timing lacks task/document identity');
    const key=event.task_id;
    if(!tasks.has(key))tasks.set(key,{details,generation:event.document_generation,points:new Map()});
    const task=tasks.get(key);
    trace.insist(task.generation===event.document_generation&&['origin','executor','raw_bytes'].every(name=>task.details[name]===details[name]),'Output identity/provenance changed');
    trace.insist([11,12,13,14].includes(event.point)&&!task.points.has(event.point),'Invalid/duplicate output timing stage');
    task.points.set(event.point,{event,details});++observed;
  }
  // Existing bridge_response.value remains raw bytes, with no private marker.
  // Join only after all producers' preparation points have been collected.
  for(const event of checked.points){
    if(event.point!==18||!tasks.has(event.task_id))continue;
    const task=tasks.get(event.task_id);
    trace.insist(task.generation===event.document_generation&&event.value===String(task.details.raw_bytes)&&!task.points.has(18),
      'Legacy owner observation differs from prepared output identity/bytes');
    trace.insist(['input_first','input_last','action_first','action_last','frame_id','related_id'].every(key=>event[key]==='0')&&event.flags===0,
      'Owner observation invented input/frame relation');
    task.points.set(18,{event,details:task.details});
  }
  const elapsed=(start,end)=>{
    const a=trace.exact(start.event.qpc),b=trace.exact(end.event.qpc);trace.insist(b>=a,'Output clock order inverted');
    return trace.milliseconds(b-a,trace.exact(checked.begin.qpc_frequency,false));
  };
  const groups={};
  for(const task of tasks.values()){
    const posted=task.points.get(11),begin=task.points.get(12),end=task.points.get(13),cancelled=task.points.get(14),response=task.points.get(18);
    // bridge_response is before PostWebMessageAsJson. A later rejected COM
    // operation may coexist; observing the owner is never delivery success.
    const groupKey=[task.details.origin,task.details.executor,(end||response||cancelled||begin||posted).details.preparation].join(':');
    const group=groups[groupKey]||(groups[groupKey]={observed_tasks:0,owner_observations:0,cancelled:0});++group.observed_tasks;
    if(begin&&end)preparation.push(elapsed(begin,end));
    if(end&&response)afterPreparation.push(elapsed(end,response));
    if(response){++delivered;++group.owner_observations;}
    if(cancelled){++group.cancelled;const why=cancelled.event.why;
      ++failures[why===2?'capacity':why===4||why===6?'stale':why===3||why===7||why===16?'stopped':why===14?'invalid':'other'];}
    if(!posted||!begin||!end||(!cancelled&&!response))++incomplete;
  }
  return {schema:'soui.webview2-output-timing-analysis/v1',traceIntegrityAccepted:checked.traceIntegrityAccepted,
    completeTrace:checked.fullCausalEligible,observed_points:observed,observed_tasks:tasks.size,owner_observations:delivered,
    incomplete_task_chains:incomplete,observed_failures:failures,groups,
    preparation_ms:checked.fullCausalEligible&&preparation.length?trace.distribution(preparation):null,
    preparation_end_to_owner_observation_ms:checked.fullCausalEligible&&afterPreparation.length?trace.distribution(afterPreparation):null,
    actual_queue_histogram:checked.end.durations[4],queue_note:'Existing task_queue histogram starts at actual queue admission; end-to-owner includes admission lock/wait.',
    losses:checked.loss,clock:checked.equal_tick_order,input_causality:false,com_completion:false,physicalPresentation:false,performanceGate:false};
}
if(require.main===module){try{trace.insist(process.argv.length===3,'Usage: node analyze-trace.cjs TRACE');
  console.log(JSON.stringify(analyze(trace.readNdjson(process.argv[2])),null,2));
}catch(error){console.error(error.message);process.exitCode=1;}}
module.exports={decode,analyze};
