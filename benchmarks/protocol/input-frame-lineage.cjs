'use strict';
const {exact,insist,milliseconds,distribution,whole}=require('./input-frame-trace.cjs');
const MAX_RELATIONS=1000000,MAX_SAMPLES=1024;
const generation=p=>[p.view_id,p.view_generation,p.document_generation,p.geometry_generation].join('/');
const frameKey=p=>generation(p)+'/'+p.frame_id;
const inputKey=p=>generation(p)+'/'+p.input_first;
const rangeKey=p=>p.input_first+'/'+p.input_last;
function append(map,key,value){if(!map.has(key))map.set(key,[]);map.get(key).push(value);}
function lowerBound(items,value){let lo=0,hi=items.length;while(lo<hi){const mid=(lo+hi)>>>1;
  if(items[mid].id<value)lo=mid+1;else hi=mid;}return lo;}

// Input is an already checked joinTraces result and its authenticated tag map.
// This supplemental relation never supplies visible input terminals or changes
// the strict native-stage/desktop classifications in the original reader.
function analyzeSoftwareFrameLineage(joined,mapping){
  insist(joined&&Array.isArray(joined.points)&&joined.points.length<=250000&&
    mapping instanceof Map&&mapping.size<=100000,'Frame lineage input bound exceeded');
  const result={schema:'soui.input-frame-lineage/v1',source_eligible:joined.fullCausalEligible===true,
    mapped_inputs:mapping.size,related_inputs:0,distinct_software_frames:0,
    rejections:{},samples:[],omitted_samples:0,host_to_software_submit_ms:null,processed_to_software_submit_ms:null,
    optional_resources_ready:{recorded:0,unobserved:0},
    endpoint:'processed-frame software draw/submit call return; API success and visible content unproven',
    stage8_input_coverage_inherited:false,fullInputToVisibleCausalComplete:false,
    physicalPresentation:false,performanceGate:false,releaseReady:false};
  if(!result.source_eligible){result.unavailable_reason='source-not-loss-free-detailed';return result;}
  const frequency=exact(joined.identity.qpc_frequency,false),points=new Set(joined.points);
  const ordered=new Map(),singletons=new Map(),frames=new Map(),candidates=new Map(),cache=new Map();
  let relations=0;
  const work=()=>insist(++relations<=MAX_RELATIONS,'Frame lineage relation budget exceeded');
  for(const [sequence,p]of mapping){
    insist(whole(sequence,1,100000)&&points.has(p)&&p.point===1&&p.input_first!=='0'&&
      p.input_first===p.input_last&&exact(p.related_id,false)===BigInt(sequence),'Frame lineage tag is not an authenticated joined point');
    append(ordered,generation(p),{id:exact(p.input_first,false),sequence});
  }
  for(const items of ordered.values())items.sort((a,b)=>a.id<b.id?-1:a.id>b.id?1:0);
  for(const p of joined.points){
    if([2,3,4].includes(p.point)&&p.input_first!=='0'&&p.input_first===p.input_last)
      append(singletons,inputKey(p)+'/'+p.point,p);
    if(![6,7,8,25,27,28].includes(p.point)||p.frame_id==='0')continue;
    const fk=frameKey(p);if(!frames.has(fk))frames.set(fk,{byStage:new Map(),coverage:new Map()});
    const f=frames.get(fk);append(f.byStage,p.point,p);
    if(p.point===25)append(f.coverage,rangeKey(p),p);
    if(p.point!==6||p.input_first==='0')continue;
    const items=ordered.get(generation(p))||[],last=exact(p.input_last);
    for(let i=lowerBound(items,exact(p.input_first));i<items.length&&items[i].id<=last;i++){
      work();append(candidates,items[i].sequence,p);
    }
  }
  function one(values,label,errors){
    if(values.length===1)return values[0];
    errors.push(label+(values.length?'_ambiguous':'_missing'));return null;
  }
  function frameRelation(s6){
    if(cache.has(s6))return cache.get(s6);
    const errors=[],f=frames.get(frameKey(s6)),stage=n=>f.byStage.get(n)||[];
    const coverage=one(f.coverage.get(rangeKey(s6))||[],'coverage',errors);
    const pending=one(stage(27),'scene_pending',errors),active=one(stage(28),'scene_activated',errors);
    const submit=one(stage(8),'submit',errors);
    if(!(s6.flags&1)||!coverage||(coverage.flags&1)!==1||exact(coverage.qpc)>exact(s6.qpc))errors.push('processed_coverage_invalid');
    if(pending&&coverage&&exact(coverage.qpc)<exact(pending.qpc))errors.push('processed_coverage_predates_scene');
    let resource=null;
    if(pending&&active&&submit){
      if(pending.related_id==='0'||pending.related_id!==active.related_id||pending.related_id!==submit.related_id)
        errors.push('scene_identity_conflict');
      if(exact(pending.qpc)>exact(s6.qpc)||exact(active.qpc)<exact(pending.qpc)||
        exact(submit.qpc)<exact(s6.qpc)||exact(submit.qpc)<exact(active.qpc))errors.push('frame_time_order_invalid');
      const resources=stage(7).filter(p=>{work();return p.related_id===pending.related_id;});
      if(resources.length>1)errors.push('resources_ready_ambiguous');
      else if(resources.length===1){resource=resources[0];if(exact(resource.qpc)<exact(pending.qpc)||exact(resource.qpc)>exact(active.qpc))
        errors.push('resources_ready_time_invalid');}
    }
    const value={errors,pending,submit,resource};cache.set(s6,value);return value;
  }
  const hostSamples=[],processedSamples=[],acceptedFrames=new Set();
  for(const [sequence,t1]of mapping){
    const errors=[],row={sequence,native_input_id:t1.input_first,generation:generation(t1),related:false};
    const g=generation(t1);if(g.split('/').some(value=>value==='0'))errors.push('unknown_generation');
    const single=stage=>one(singletons.get(inputKey(t1)+'/'+stage)||[],'stage'+stage,errors);
    const admission=single(2),engine=single(3),handler=single(4);
    const s6=one(candidates.get(sequence)||[],'processed_frame',errors);
    if(admission&&engine&&handler&&s6){
      // Admission is recorded after route returns. Engine can start before T2.
      if(admission.why!==1||admission.outcome!==0)errors.push('admission_not_accepted');
      if(handler.why!==0||handler.outcome!==0)errors.push('handler_not_normal_return');
      if(exact(admission.qpc)<exact(t1.qpc)||exact(engine.qpc)<exact(t1.qpc)||exact(handler.qpc)<exact(engine.qpc)||
        exact(s6.qpc)<exact(handler.qpc))errors.push('input_time_order_invalid');
      const relation=frameRelation(s6);errors.push(...relation.errors);
      if(relation.pending&&exact(relation.pending.qpc)<exact(t1.qpc))errors.push('scene_predates_input');
      if(errors.length===0){
        const submit=relation.submit;
        const host=milliseconds(exact(submit.qpc)-exact(t1.qpc),frequency);
        const processed=milliseconds(exact(submit.qpc)-exact(s6.qpc),frequency);
        hostSamples.push(host);processedSamples.push(processed);acceptedFrames.add(frameKey(s6));
        result.related_inputs++;result.optional_resources_ready[relation.resource?'recorded':'unobserved']++;
        Object.assign(row,{related:true,frame_id:s6.frame_id,scene_id:relation.pending.related_id,
          host_qpc:t1.qpc,processed_qpc:s6.qpc,submit_qpc:submit.qpc,
          host_to_software_submit_ms:host,processed_to_software_submit_ms:processed,
          resource_ready_qpc:relation.resource?.qpc??null});
      }
    }
    row.rejected=[...new Set(errors)];for(const reason of row.rejected)result.rejections[reason]=(result.rejections[reason]||0)+1;
    if(result.samples.length<MAX_SAMPLES)result.samples.push(row);else result.omitted_samples++;
  }
  result.distinct_software_frames=acceptedFrames.size;
  result.host_to_software_submit_ms=hostSamples.length?distribution(hostSamples):null;
  result.processed_to_software_submit_ms=processedSamples.length?distribution(processedSamples):null;
  result.relation_operations=relations;
  result.sample_rows_scope='First1024 input diagnostics only; aggregate distributions/counts include the entire mapped cohort';
  return result;
}
module.exports={analyzeSoftwareFrameLineage};
