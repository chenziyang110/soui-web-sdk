'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const trace=require('../../../benchmarks/protocol/input-frame-trace.cjs');
const {analyzeSoftwareFrameLineage}=require('../../../benchmarks/protocol/input-frame-lineage.cjs');
// Authored loss-free traces exercise relation boundaries, never actual input.
function fixture(count=2){
  const points=[],p=(point,qpc,first='0',last=first,extra={})=>{
    const value={event:'trace.point',version:1,point,why:0,outcome:0,flags:0,thread_id:7,producer_slot:1,qpc:String(qpc),
      view_id:'1',view_generation:'2',document_generation:'3',geometry_generation:'4',input_first:first,input_last:last,
      task_id:'0',frame_id:'0',action_first:'0',action_last:'0',value:'0',related_id:'0',...extra};points.push(value);return value;};
  for(let n=1;n<=count;n++){
    const id=String(100+n),start=1000+n*10;
    p(1,start,id,id,{related_id:String(n)});p(2,start+1,id,id,{why:1});p(3,start+2,id);p(4,start+3,id);
  }
  const frame={frame_id:'90',related_id:'40'};
  const end=String(100+count),tail=1000+count*10+10;
  p(27,tail,'0','0',frame);p(25,tail+1,'101',end,{frame_id:'90',flags:1});
  p(6,tail+2,'101',end,{frame_id:'90',flags:1});p(28,tail+4,'0','0',frame);p(8,tail+8,'0','0',frame);
  return {points,p};
}
function check(f){
  const points=[...f.points].sort((a,b)=>BigInt(a.qpc)<BigInt(b.qpc)?-1:BigInt(a.qpc)>BigInt(b.qpc)?1:0);
  const counts=Array(31).fill('0');for(const p of points)counts[p.point]=String(Number(counts[p.point])+1);
  const begin={event:'trace.begin',schema:'soui.input-frame-trace/v1',version:1,run_id:'a'.repeat(32),
    process_id:11,process_incarnation:'123',qpc_frequency:'1000',mode:'detailed',maximum_threads:32,events_per_thread:1024};
  const end={event:'trace.end',complete:true,registered_threads:1,active_producers:0,attempted:String(points.length),recorded:String(points.length),
    dropped:f.loss?'1':'0',queued:'0',unregistered_writes:'0',registration_rejections:'0',causal_lost:'0',counter_saturated:false,
    owner_violation:false,invalid_event:false,clock_invalid:false,stages:counts,counters:Array(16).fill('0'),
    durations:Array.from({length:15},()=>Array(64).fill('0'))};
  const joined=trace.joinTraces([[begin,...points,end]]);
  const map=new Map(points.filter(p=>p.point===1).map(p=>[Number(p.related_id),p]));
  return analyzeSoftwareFrameLineage(joined,map);
}
test('frame/scene context links zero-input submit without inheriting input coverage or visibility',()=>{
  const f=fixture(),r=check(f);assert.equal(r.related_inputs,2);assert.equal(r.distinct_software_frames,1);
  assert.equal(r.host_to_software_submit_ms.p95,28);assert.deepEqual(r.rejections,{});
  assert.equal(f.points.find(p=>p.point===8).input_first,'0');assert.equal(r.stage8_input_coverage_inherited,false);
  assert.equal(r.fullInputToVisibleCausalComplete,false);assert.equal(r.performanceGate,false);
  assert.deepEqual(r.optional_resources_ready,{recorded:0,unobserved:2});
});
test('optional resource transition is validated when present, never synthesized when absent',()=>{
  const f=fixture();f.p(7,1033,'0','0',{frame_id:'90',related_id:'40'});
  assert.deepEqual(check(f).optional_resources_ready,{recorded:2,unobserved:0});
  f.points.find(p=>p.point===7).qpc='1039';assert.equal(check(f).related_inputs,0);
});
test('all four generation components and frame ID isolate submissions',()=>{
  for(const field of ['view_id','view_generation','document_generation','geometry_generation','frame_id']){
    const f=fixture();f.points.find(p=>p.point===8)[field]='999';const r=check(f);
    assert.equal(r.related_inputs,0);assert.equal(r.rejections.submit_missing,2);
  }
});
test('duplicate or multiple same-frame submits are ambiguous instead of choosing earliest',()=>{
  for(const delta of [0,1]){const f=fixture(),p=f.points.find(p=>p.point===8);
    f.points.push({...p,qpc:String(BigInt(p.qpc)+BigInt(delta))});
    assert.equal(check(f).related_inputs,0);assert.equal(check(f).rejections.submit_ambiguous,2);}
});
test('conflicting scene IDs, backwards frame times and missing coverage reject the relation',()=>{
  for(const mutate of [f=>f.points.find(p=>p.point===8).related_id='41',
    f=>f.points.find(p=>p.point===8).qpc='1031',f=>f.points.splice(f.points.findIndex(p=>p.point===25),1),
    f=>f.points.find(p=>p.point===6).flags=0]){
    const f=fixture();mutate(f);assert.equal(check(f).related_inputs,0);
  }
});
test('two candidate processed frames reject an input rather than selecting one',()=>{
  const f=fixture(),p=f.points.find(p=>p.point===6);f.points.push({...p,frame_id:'91',qpc:'1033'});
  assert.equal(check(f).related_inputs,0);assert.equal(check(f).rejections.processed_frame_ambiguous,2);
});
test('source loss prevents frame relation distributions even if remaining events appear unique',()=>{
  const f=fixture();f.loss=true;const r=check(f);assert.equal(r.source_eligible,false);
  assert.equal(r.related_inputs,0);assert.equal(r.host_to_software_submit_ms,null);
});
test('admission after engine start is allowed, rejected admission and unknown geometry are not',()=>{
  const f=fixture();f.points.find(p=>p.point===2).qpc='1012';assert.equal(check(f).related_inputs,2);
  f.points.find(p=>p.point===2).why=2;assert.equal(check(f).related_inputs,1);
  const unknown=fixture();for(const p of unknown.points)p.geometry_generation='0';assert.equal(check(unknown).related_inputs,0);
});
test('uint64 QPC differences remain exact beyond JavaScript integer precision',()=>{
  const f=fixture(),offset=1n<<60n;for(const p of f.points)p.qpc=String(BigInt(p.qpc)+offset);
  const r=check(f);assert.equal(r.related_inputs,2);assert.equal(r.host_to_software_submit_ms.p95,28);
});
test('predating coverage and cancelled handler returns cannot supply normal processed lineage',()=>{
  const old=fixture();old.points.find(p=>p.point===25).qpc='1029';
  assert.equal(check(old).related_inputs,0);assert.equal(check(old).rejections.processed_coverage_predates_scene,2);
  const cancelled=fixture();cancelled.points.find(p=>p.point===4).why=16;
  assert.equal(check(cancelled).related_inputs,1);assert.equal(check(cancelled).rejections.handler_not_normal_return,1);
});
test('diagnostic row cap does not truncate the cohort distributions or counts',()=>{
  const result=check(fixture(1025));assert.equal(result.related_inputs,1025);
  assert.equal(result.samples.length,1024);assert.equal(result.omitted_samples,1);
  assert.equal(result.host_to_software_submit_ms.samples,1025);assert.equal(result.distinct_software_frames,1);
});
