'use strict';
// Independent exact-integer reader. Legacy serial-ACK readers are unchanged.
const fs = require('node:fs');
const {TextDecoder} = require('node:util');
const maximum = 18446744073709551615n;
const stages = Object.freeze({host_receive:1, admission:2, engine_begin:3, handler_return:4,
  microtasks_complete:5, commit_complete:6, resources_ready:7, submit:8,
  desktop_observed:9, input_terminal:10, task_posted:11, task_begin:12, task_end:13,
  task_cancelled:14, wake_requested:15, wake_consumed:16, bridge_request:17,
  bridge_response:18, layout_begin:19, layout_end:20, record_begin:21, record_end:22,
  composite_begin:23, composite_end:24, frame_coverage:25, resource_wait:26,
  scene_pending:27, scene_activated:28});
function insist(condition, message) { if (!condition) throw new Error(message); }
function exact(value, zero = true) {
  insist(typeof value === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(value) &&
    BigInt(value) <= maximum && (zero || value !== '0'), 'Invalid canonical uint64 string');
  return BigInt(value);
}
function whole(value, minimum = 0, maximumValue = 0xffffffff) {
  return Number.isSafeInteger(value) && value >= minimum && value <= maximumValue;
}
function readNdjson(filename) {
  const before = fs.lstatSync(filename);
  insist(before.isFile() && !before.isSymbolicLink() && before.size > 0 &&
    before.size <= 64 * 1024 * 1024, 'Trace must be a bounded ordinary file');
  const file = fs.openSync(filename, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    const held = fs.fstatSync(file);
    insist(held.isFile() && held.size === before.size && held.dev === before.dev &&
      held.ino === before.ino, 'Trace changed while opening');
    const raw = readHeldFile(file,held.size);
    insist(raw.length === held.size, 'Trace changed while reading');
    return parseNdjson(raw);
  } finally { fs.closeSync(file); }
}
function readHeldFile(file,size) {
  insist(Number.isSafeInteger(size) && size>0 && size<=64*1024*1024,'File byte limit exceeded');
  const raw=Buffer.allocUnsafe(size);let offset=0,calls=0;
  while(offset<size && ++calls<=2048) {
    const count=fs.readSync(file,raw,offset,Math.min(65536,size-offset),null);
    if(!count)break;
    offset+=count;
  }
  const extra=Buffer.allocUnsafe(1);
  insist(offset===size && fs.readSync(file,extra,0,1,null)===0,'File grew/truncated or read budget exceeded');
  return raw;
}
function parseNdjson(raw) {
  insist(Buffer.isBuffer(raw) && raw.length>0 && raw.length<=64*1024*1024 && raw.at(-1)===10,'Trace is truncated/too large');
  const lines=new TextDecoder('utf-8',{fatal:true}).decode(raw).trimEnd().split('\n');
  insist(lines.length>=2 && lines.length<=1000000,'Trace record limit exceeded');
  return lines.map(line=>JSON.parse(line));
}
const ids = ['qpc','view_id','view_generation','document_generation','geometry_generation',
  'input_first','input_last','task_id','frame_id','action_first','action_last','value','related_id'];
function checkTrace(rows) {
  insist(Array.isArray(rows) && rows.length >= 2 && rows.length <= 1000000, 'Invalid bounded trace rows');
  const begin = rows[0], end = rows.at(-1);
  insist(begin?.event === 'trace.begin' && begin.schema === 'soui.input-frame-trace/v1' &&
    begin.version === 1 && /^[a-f0-9]{32}$/.test(begin.run_id) && !/^0+$/.test(begin.run_id) &&
    whole(begin.process_id,1) && ['scalar','detailed'].includes(begin.mode) &&
    begin.maximum_threads === 32 && begin.events_per_thread === 1024, 'Invalid trace session header');
  exact(begin.process_incarnation, false); exact(begin.qpc_frequency, false);
  insist(end?.event === 'trace.end' && end.complete === true &&
    whole(end.registered_threads,0,32) && end.active_producers === 0, 'Trace did not close all producers');
  for (const key of ['attempted','recorded','dropped','queued','unregistered_writes','registration_rejections','causal_lost']) exact(end[key]);
  insist(exact(end.queued) === 0n && exact(end.recorded) === BigInt(rows.length - 2) &&
    exact(end.attempted) >= exact(end.recorded), 'Trace event/queue accounting differs');
  for (const key of ['counter_saturated','owner_violation','invalid_event','clock_invalid'])
    insist(typeof end[key] === 'boolean', 'Trace fault flag missing');
  insist(Array.isArray(end.stages) && end.stages.length === 29 &&
    Array.isArray(end.counters) && end.counters.length === 16 &&
    Array.isArray(end.durations) && end.durations.length === 15, 'Scalar arrays have wrong dimensions');
  for (const count of [...end.stages, ...end.counters]) exact(count);
  for (const histogram of end.durations) {
    insist(Array.isArray(histogram) && histogram.length === 64, 'Duration histogram is not bounded');
    for (const count of histogram) exact(count);
  }
  const perProducer = new Map(), tickProducers=new Map(), actualStages=Array(29).fill(0n), points = rows.slice(1,-1);
  for (const point of points) {
    insist(point?.event === 'trace.point' && point.version === 1 &&
      whole(point.point,1,28) && whole(point.why,0,16) && whole(point.outcome,0,6) &&
      whole(point.flags,0,7) && whole(point.thread_id,1) &&
      whole(point.producer_slot,1,end.registered_threads), 'Invalid trace POD fields');
    for (const key of ids) exact(point[key], key !== 'qpc');
    const tick = exact(point.qpc, false), previous = perProducer.get(point.producer_slot);
    insist(previous === undefined || (previous.thread_id===point.thread_id && tick>=previous.qpc),
      'Per-producer owner/QPC changed');
    perProducer.set(point.producer_slot,{thread_id:point.thread_id,qpc:tick});
    if(!tickProducers.has(point.qpc))tickProducers.set(point.qpc,new Set());
    tickProducers.get(point.qpc).add(point.producer_slot);++actualStages[point.point];
    for (const [first,last] of [['input_first','input_last'],['action_first','action_last']])
      insist((point[first] === '0' && point[last] === '0') ||
        (exact(point[first]) > 0n && exact(point[last]) >= exact(point[first])), 'Invalid explicit coverage range');
    if (point.input_first !== '0') insist(point.view_id !== '0' && point.view_generation !== '0' &&
      point.document_generation !== '0', 'Input relation lacks generation identity');
    if (point.point === stages.microtasks_complete) insist((point.flags & 4) !== 0, 'Unproven microtask relation');
    if (point.point === stages.input_terminal) insist(point.input_first !== '0' && point.outcome !== 0, 'Input terminal is missing');
    if (point.point === stages.frame_coverage) insist((point.flags & 1) !== 0, 'Frame coverage was inferred');
    if (point.flags & 1) insist(point.frame_id !== '0' && point.input_first !== '0', 'Coverage has no frame/input');
  }
  insist(perProducer.size <= end.registered_threads, 'Trace has unregistered producers');
  insist(begin.mode === 'detailed' || points.length === 0, 'Scalar mode contains detailed events');
  const loss = exact(end.dropped) + exact(end.unregistered_writes) + exact(end.registration_rejections) + exact(end.causal_lost);
  const faults = ['counter_saturated','owner_violation','invalid_event','clock_invalid'].filter(key=>end[key]);
  if(begin.mode==='detailed' && loss===0n && faults.length===0)
    insist(actualStages.every((count,index)=>exact(end.stages[index])===count),'Detailed stage counts differ from recorded points');
  return {begin,end,points,traceIntegrityAccepted:true,
    fullCausalEligible:begin.mode === 'detailed' && loss === 0n && faults.length === 0,
    equal_tick_cross_producer_groups:[...tickProducers.values()].filter(owners=>owners.size>1).length,
    equal_tick_order:'producer-wire-order-only-cross-producer-unknown',
    loss:loss.toString(), faults, physicalPresentation:false, performanceGate:false, releaseReady:false};
}
function sameIdentity(left,right) {
  for (const key of ['run_id','process_id','process_incarnation','qpc_frequency'])
    insist(left[key] === right[key], 'Trace run/process/QPC identity mismatch');
}
function joinTraces(streams) {
  insist(Array.isArray(streams) && streams.length > 0 && streams.length <= 4, 'Invalid trace stream count');
  const checked = streams.map(checkTrace);
  for (const trace of checked.slice(1)) sameIdentity(checked[0].begin,trace.begin);
  const points=checked.flatMap(trace=>trace.points);
  insist(points.length<=250000,'Joined trace point budget exceeded');
  const ties=new Map();
  checked.forEach((stream,index)=>stream.points.forEach(point=>{
    if(!ties.has(point.qpc))ties.set(point.qpc,new Set());
    ties.get(point.qpc).add(index+':'+point.producer_slot);
  }));
  return {identity:checked[0].begin,points,
    fullCausalEligible:checked.every(trace=>trace.fullCausalEligible),streams:checked,
    equal_tick_cross_producer_groups:[...ties.values()].filter(owners=>owners.size>1).length,
    equal_tick_order:'per-stream-producer-wire-only-cross-producer-unknown'};
}
function milliseconds(ticks,frequency) {
  insist(ticks >= 0n && ticks <= BigInt(Number.MAX_SAFE_INTEGER) &&
    frequency > 0n && frequency <= BigInt(Number.MAX_SAFE_INTEGER), 'Unsafe interval conversion');
  return Number(ticks) * 1000 / Number(frequency);
}
function distribution(samples) {
  insist(samples.length > 0 && samples.every(Number.isFinite), 'No finite samples');
  const sorted = [...samples].sort((a,b)=>a-b);
  const q = p=>sorted[Math.max(0,Math.ceil(p*sorted.length)-1)];
  return {samples:samples.length,p50:q(.5),p95:q(.95),p99:q(.99),min:sorted[0],max:sorted.at(-1)};
}
if (require.main === module) {
  try {
    insist(process.argv.length >= 3 && process.argv.length <= 6, 'Usage: node input-frame-trace.cjs TRACE.ndjson [TRACE.ndjson ...]');
    const result = joinTraces(process.argv.slice(2).map(readNdjson));
    console.log(JSON.stringify({schema:'soui.input-frame-trace-check/v1',run_id:result.identity.run_id,
      points:result.points.length,streams:result.streams.length,fullCausalEligible:result.fullCausalEligible,
      losses:result.streams.map(trace=>trace.loss),physicalPresentation:false,performanceGate:false,releaseReady:false},null,2));
    if (!result.fullCausalEligible) process.exitCode = 2;
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
module.exports = {stages,exact,whole,insist,readNdjson,readHeldFile,parseNdjson,checkTrace,sameIdentity,joinTraces,milliseconds,distribution};
