'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const trace=require('../../../benchmarks/protocol/input-frame-trace.cjs');
const burst=require('../../../benchmarks/protocol/burst-input.cjs');
const pair=require('../../../benchmarks/protocol/same-product-input-pair.cjs');
// Authored fixtures only. These are not captured windows, inputs or performance.
const run='00000000000000010000000000000002';
function point(code,qpc,sequence,related=0,outcome=0) {
  return {event:'trace.point',version:1,point:code,why:0,outcome,flags:0,thread_id:7,producer_slot:1,qpc:String(qpc),
    view_id:'1',view_generation:'2',document_generation:'3',geometry_generation:'4',
    input_first:String(sequence),input_last:String(sequence),task_id:'0',frame_id:'0',
    action_first:'0',action_last:'0',value:'0',related_id:String(related)};
}
function traceRows(points) {
  const stages=Array(29).fill('0');
  for(const point of points)stages[point.point]=String(Number(stages[point.point])+1);
  return [{event:'trace.begin',schema:'soui.input-frame-trace/v1',version:1,run_id:run,
    process_id:11,process_incarnation:'134360000000000001',qpc_frequency:'1000',mode:'detailed',
    maximum_threads:32,events_per_thread:1024},...points,{event:'trace.end',complete:true,
    registered_threads:1,active_producers:0,attempted:String(points.length),recorded:String(points.length),
    dropped:'0',queued:'0',unregistered_writes:'0',registration_rejections:'0',causal_lost:'0',
    counter_saturated:false,owner_violation:false,invalid_event:false,clock_invalid:false,
    stages,counters:Array(16).fill('0'),durations:Array.from({length:15},()=>Array(64).fill('0'))}];
}
function marker(ack,frame,delivered=ack) {
  const bytes=Buffer.alloc(16);bytes.writeUInt16LE(0x53a7);bytes.writeUInt32LE(2,2);
  bytes.writeUIntLE(ack,6,3);bytes.writeUInt16LE(frame,9);bytes[11]=1|((delivered>>>16)<<4);
  bytes.writeUInt16LE(delivered&65535,14);
  let crc=65535;
  for(let n=0;n<16;++n){if(n===12||n===13)continue;crc^=bytes[n]<<8;for(let bit=0;bit<8;++bit)crc=((crc<<1)^((crc&32768)?4129:0))&65535;}
  bytes.writeUInt16LE(crc,12);return bytes.toString('hex');
}
function fixture() {
  const input=[{event:'burst.begin',schema:'soui.burst-input/v1',run_id:run,target_pid:11,
    process_incarnation:'134360000000000001',injector_pid:22,injector_incarnation:'134360000000000003',
    qpc_frequency:'1000',started_qpc:'1000',hz:125,requested:3,kind:'pointer_move',concurrent_injectors:1,
    nonce:2,injection_tag_magic:1396703232,content_width:1100,content_height:720}];
  for(let n=0;n<3;++n)input.push({event:'input.inject',sequence:String(n+1),scheduled_qpc:String(1000+n*8),
    before_qpc:String(1000+n*8),after_qpc:String(1001+n*8),sent:1,client_x:100+n,client_y:100,wheel_delta:0});
  input.push({event:'burst.end',completed:true,attempted:3,sent:3,ended_qpc:'1030'});
  const desktop=[{event:'desktop.begin',schema:'soui.burst-desktop/v1',run_id:run,pid:11,
    creationFileTime:'134360000000000001',qpcFrequency:'1000',collectorPid:22,
    collectorCreationFileTime:'134360000000000003',expectedNonce:2,dpi:96,rotation:0,probe:false,
    scene:1,region:[8,8,64,36],contentProbe:[10,42],hwnd:'123',startedQpc:'800',originX:0,originY:0,
    adapterLow:77,adapterHigh:0},{event:'desktop.output',deviceName:'\\\\.\\DISPLAY1',
    left:0,top:0,right:1920,bottom:1080,adapterLow:77,adapterHigh:0}];
  function frame(qpc,ack,number,delivered=ack) {
    desktop.push({event:'desktop.acquire',lastPresentQpc:String(qpc),accumulatedFrames:1,protected:false});
    if(!ack)desktop.push({event:'desktop.ready',lastPresentQpc:String(qpc),nonce:2,ack:0});
    desktop.push({event:'desktop.frame',lastPresentQpc:String(qpc),observedQpc:String(qpc+1),
      nonce:2,ack,frame:number,scene:1,motion:false,contentSequence:ack,deliveredUnits:delivered,
      width:1100,height:720,markerHex:marker(ack,number,delivered)});
  }
  frame(950,0,1);frame(1010,1,2);frame(1020,3,3,2);
  desktop.push({event:'desktop.end',completed:true,frames:'3',pointerOnly:'0',endedQpc:'1031'});
  const points=[point(1,1000,101,1),point(1,1008,102,2),point(1,1016,103,3)];
  const options={ownedTarget:{pid:11,creationFileTime:'134360000000000001'},
    ownedInjector:{pid:22,creationFileTime:'134360000000000003'},controlledWorkload:true,calibration:{schema:'soui.burst-calibration/v1',
      run_id:run,qpc_frequency:'1000',observation_error_ms:0.25,device_pixel_ratio:1}};
  return {input,desktop,streams:[traceRows(points)],options};
}
const check=value=>burst.checkBurst(value.input,value.streams,value.desktop,value.options);
test('canonical uint64 rejects rounding, leading zero, exponent and overflow',()=>{
  assert.equal(trace.exact('18446744073709551615'),18446744073709551615n);
  for(const value of [9007199254740992,'01','1e3','-1','18446744073709551616'])assert.throws(()=>trace.exact(value));
});
test('captured-image samples never inflate visible sample count or full native chain',()=>{
  const result=check(fixture());assert.equal(result.input_count,3);assert.equal(result.desktop_samples,2);
  assert.equal(result.captured_image_upper_ms.samples,2);assert.equal(result.fullCausalComplete,false);
  assert.equal(result.unknown_microtask_inputs,3);assert.equal(result.host_to_submit_ms,null);
  assert.equal(result.physicalPresentation,false);assert.equal(result.performanceGate,false);assert.equal(result.releaseReady,false);
});
test('ACK crossing retains oldest unresolved input age instead of discarding it',()=>{
  const result=check(fixture());assert.equal(result.ack_crossed_inputs,1);
  assert.equal(result.unresolved_inputs,1);assert.equal(result.ages.at(-1).oldest_pending_sequence,'2');
  assert.equal(result.ages.at(-1).oldest_pending_age_ms,12);
});
test('explicit coalesced terminal closes only the named native input',()=>{
  const f=fixture(),points=f.streams[0].slice(1,-1);points.push(point(10,1019,102,0,2));f.streams=[traceRows(points)];
  const result=check(f);assert.equal(result.unresolved_inputs,0);assert.equal(result.fullCausalComplete,false);
});
test('native visible terminal without observed content cannot erase an old pending input',()=>{
  const f=fixture(),points=f.streams[0].slice(1,-1);points.push(point(10,1019,102,0,1));f.streams=[traceRows(points)];
  const result=check(f);assert.equal(result.unproven_visible_terminals,1);
  assert.equal(result.unresolved_inputs,1);assert.equal(result.ages.at(-1).oldest_pending_sequence,'2');
});
test('same PID with adjacent creation tick cannot bind',()=>{
  const f=fixture();f.options.ownedTarget.creationFileTime='134360000000000002';assert.throws(()=>check(f),/held target/);
});
test('OS thread id reuse/interleaved producers do not impose false wire time order',()=>{
  const points=[point(1,1000,101,1),point(1,900,102,2),point(1,1001,103,3)];
  points[1].producer_slot=2;
  const rows=traceRows(points);rows.at(-1).registered_threads=2;
  assert.equal(trace.checkTrace(rows).fullCausalEligible,true);
});
test('same-tick cross-producer order remains explicitly unknown',()=>{
  const points=[point(1,1000,101,1),point(1,1000,102,2)];points[1].producer_slot=2;
  const rows=traceRows(points);rows.at(-1).registered_threads=2;
  const result=trace.checkTrace(rows);
  assert.equal(result.equal_tick_cross_producer_groups,1);
  assert.match(result.equal_tick_order,/unknown/);
});
test('run and QPC clock mismatches refuse stream joins',()=>{
  const f=fixture(),other=structuredClone(f.streams[0]);other[0].qpc_frequency='1001';
  assert.throws(()=>trace.joinTraces([f.streams[0],other]),/identity/);
});
test('overflow, causal loss and unregistered writers disqualify complete causality',()=>{
  for(const key of ['dropped','causal_lost','unregistered_writes','registration_rejections']){
    const f=fixture();f.streams[0].at(-1)[key]='1';assert.equal(check(f).fullCausalEligible,false);
  }
});
test('capture gaps remain explicit even with observed final ACK',()=>{
  const f=fixture();f.desktop.filter(row=>row.event==='desktop.acquire').at(-1).accumulatedFrames=3;
  const result=check(f);assert.equal(result.desktop_omitted_images,2);assert.equal(result.fullCausalComplete,false);
});
test('marker-only update cannot stand in for real feedback content',()=>{
  const f=fixture();f.desktop.find(row=>row.event==='desktop.frame'&&row.ack===3).contentSequence=2;
  assert.throws(()=>check(f),/marker\/content/);
});
test('magic alone cannot create a duplicate injection mapping',()=>{
  const f=fixture(),points=f.streams[0].slice(1,-1);points.push(point(1,1018,104,1));f.streams=[traceRows(points)];
  assert.throws(()=>check(f),/duplicated/);
});
test('missing host observation is counted without pretending it was accepted',()=>{
  const f=fixture();f.streams=[traceRows(f.streams[0].slice(1,-1).filter(point=>point.related_id!=='2'))];
  assert.equal(check(f).unobserved_injections,1);assert.equal(check(f).fullCausalComplete,false);
});
test('explicit E2E-only baseline reports missing internal stages without forging host loss',()=>{
  const f=fixture();f.streams=[];f.options.allowUnavailableStages=true;
  const result=check(f);assert.equal(result.stage_evidence,'unavailable');
  assert.equal(result.unobserved_injections,null);assert.equal(result.fullCausalEligible,false);
  assert.equal(result.captured_image_upper_ms.samples,2);
});
test('fixed wheel units bind cumulative ACK across coalesced native observations',()=>{
  const f=fixture();f.input[0].kind='wheel';
  for(const row of f.input.slice(1,-1)){row.client_x=100;row.client_y=100;row.wheel_delta=-120;}
  for(const row of f.desktop.filter(row=>row.event==='desktop.frame')){
    row.deliveredUnits=row.ack;row.markerHex=marker(row.ack,row.frame,row.ack);
  }
  f.streams=[traceRows(f.streams[0].slice(1,-1).filter(point=>point.related_id!=='2'))];
  const result=check(f);assert.equal(result.desktop_samples,2);assert.equal(result.unobserved_injections,1);
  assert.equal(result.ack_crossed_inputs,1);assert.equal(result.fullCausalComplete,false);
});
test('delivery counter plus complete captured history proves visual coalescing, not extra T7 samples',()=>{
  const f=fixture(),last=f.desktop.filter(row=>row.event==='desktop.frame').at(-1);
  last.deliveredUnits=3;last.markerHex=marker(3,last.frame,3);
  const result=check(f);assert.equal(result.classified_input_count,3);
  assert.equal(result.input_terminal_counts.coalesced,1);assert.equal(result.desktop_samples,2);
});
test('delivery counter CRC covers upper20-bit boundary and cannot be changed alone',()=>{
  assert.equal(burst.decodeBurstMarkerHex(marker(100000,3,100000)).deliveredUnits,100000);
  const bytes=Buffer.from(marker(3,3,2),'hex');bytes[14]=3;
  assert.throws(()=>burst.decodeBurstMarkerHex(bytes.toString('hex')),/CRC/);
});
test('partial SendInput, altered rate and concurrent injectors fail',()=>{
  for(const mutate of [f=>f.input[1].sent=0,f=>f.input[0].hz=240,f=>f.input[0].concurrent_injectors=2]){
    const f=fixture();mutate(f);assert.throws(()=>check(f));
  }
});
test('unproven microtasks and truncated logs are rejected',()=>{
  const rows=traceRows([point(5,1000,101)]);assert.throws(()=>trace.checkTrace(rows),/microtask/);
  assert.throws(()=>trace.parseNdjson(Buffer.from('{}')),/truncated/);
});
test('same-product helper refuses a descriptor pointing outside the run before reading',()=>{
  assert.throws(()=>pair.verifiedDescriptor('.', {path:'../escape',bytes:1,sha256:'a'.repeat(64)},'input-ui-run.json'),/fixed/);
});
