'use strict';
const trace = require('./input-frame-trace.cjs');
const {isDeepStrictEqual} = require('node:util');
const {insist,exact,whole,milliseconds,distribution} = trace;
function decodeBurstMarkerHex(hex) {
  insist(typeof hex==='string'&&/^[a-f0-9]{32}$/.test(hex),'Invalid burst marker bytes');
  const data=Buffer.from(hex,'hex');let crc=65535;
  for(let n=0;n<16;++n){if(n===12||n===13)continue;crc^=data[n]<<8;for(let bit=0;bit<8;++bit)crc=((crc<<1)^((crc&32768)?4129:0))&65535;}
  insist(data.readUInt16LE(0)===0x53a7&&data.readUInt16LE(12)===crc&&
    (data[11]&15)===1,'Burst marker CRC/magic/flags invalid');
  const result={nonce:data.readUInt32LE(2),ack:data.readUIntLE(6,3),frame:data.readUInt16LE(9),
    scene:data[11]&7,motion:false,deliveredUnits:data.readUInt16LE(14)|((data[11]>>>4)<<16)};
  insist(result.nonce>0&&result.ack<0x100000&&result.deliveredUnits<=result.ack&&result.deliveredUnits<=100000,
    'Burst delivery counter exceeds ACK');return result;
}
function verifyInput(rows) {
  insist(Array.isArray(rows) && rows.length >= 3 && rows.length <= 100002, 'Invalid bounded burst rows');
  const begin = rows[0], end = rows.at(-1);
  insist(begin?.event === 'burst.begin' && begin.schema === 'soui.burst-input/v1' &&
    /^[a-f0-9]{32}$/.test(begin.run_id) && !/^0+$/.test(begin.run_id) &&
    whole(begin.target_pid,1) && whole(begin.injector_pid,1) &&
    [125,500,1000].includes(begin.hz) && whole(begin.requested,1,100000) &&
    begin.requested <= begin.hz*120 && ['pointer_move','wheel'].includes(begin.kind) &&
    begin.concurrent_injectors === 1 && begin.injection_tag_magic === 1396703232 &&
    whole(begin.nonce,1) && begin.content_width === 1100 && begin.content_height === 720,
    'Invalid frozen burst session');
  for (const key of ['process_incarnation','injector_incarnation','qpc_frequency','started_qpc']) exact(begin[key],false);
  insist(begin.nonce === (parseInt(begin.run_id.slice(24),16) || 1), 'Run/marker nonce mismatch');
  insist(end?.event === 'burst.end' && end.completed === true && end.attempted === begin.requested &&
    end.sent === begin.requested && rows.length === begin.requested+2, 'Burst input failed/partial/truncated');
  const inputs = rows.slice(1,-1), frequency = exact(begin.qpc_frequency,false), start = exact(begin.started_qpc,false);
  let previous = start;
  inputs.forEach((row,index)=>{
    insist(row.event === 'input.inject' && exact(row.sequence,false) === BigInt(index+1) &&
      row.sent === 1 && row.client_x === (begin.kind==='wheel'?100:100+index%1000) &&
      row.client_y === (begin.kind==='wheel'?100:100+Math.floor(index/1000)) &&
      row.wheel_delta === (begin.kind==='wheel'?-120:0), 'Burst injection sequence/trajectory/fixed wheel units differ');
    const scheduled = start+BigInt(Math.floor(index/begin.hz))*frequency+
      BigInt(index%begin.hz)*frequency/BigInt(begin.hz);
    const before = exact(row.before_qpc,false), after = exact(row.after_qpc,false);
    insist(exact(row.scheduled_qpc,false) === scheduled && before >= scheduled && before >= previous &&
      after >= before, 'Burst QPC interval/phase reordered');
    previous = after;
  });
  insist(exact(end.ended_qpc,false) >= previous, 'Burst ended before last injection');
  return {begin,end,inputs,frequency};
}
function checkDesktop(rows,input,options) {
  insist(Array.isArray(rows) && rows.length >= 5 && rows.length <= 300010, 'Invalid bounded burst desktop rows');
  const begin=rows[0], end=rows.at(-1), source=input.begin;
  insist(begin?.event === 'desktop.begin' && begin.schema === 'soui.burst-desktop/v1' &&
    begin.run_id === source.run_id && begin.pid === source.target_pid &&
    begin.creationFileTime === source.process_incarnation && begin.qpcFrequency === source.qpc_frequency &&
    begin.collectorPid === source.injector_pid && begin.collectorCreationFileTime === source.injector_incarnation &&
    begin.expectedNonce === source.nonce && begin.dpi === 96 && begin.rotation === 0 &&
    begin.probe === false && begin.scene === 1 && isDeepStrictEqual(begin.region,[8,8,64,36]) &&
    isDeepStrictEqual(begin.contentProbe,[10,42]), 'Burst desktop run/process/QPC/geometry mismatch');
  exact(begin.hwnd,false); exact(begin.startedQpc,false);
  insist(end?.event === 'desktop.end' && end.completed === true &&
    exact(end.endedQpc,false) >= exact(input.end.ended_qpc,false), 'Burst desktop incomplete');
  const calibration = options.calibration;
  insist(calibration && calibration.schema === 'soui.burst-calibration/v1' &&
    calibration.run_id === begin.run_id && calibration.qpc_frequency === begin.qpcFrequency &&
    Number.isFinite(calibration.observation_error_ms) && calibration.observation_error_ms >= 0 &&
    calibration.observation_error_ms <= 1000 && calibration.device_pixel_ratio === 1,
    'Explicit frozen observation error/DPR calibration required');
  const observations=[], pointers=[];
  let output=null,pending=null,ready=false,lastPresent=0n,lastAck=0,lastFrame=0,lastDelivered=0,frameCount=0,omittedImages=0;
  for (const row of rows.slice(1,-1)) {
    if (row.event === 'desktop.output') {
      insist(!output && !pending && typeof row.deviceName === 'string' &&
        /^\\\\\.\\DISPLAY[1-9][0-9]*$/.test(row.deviceName), 'Invalid/duplicate desktop output');
      for (const key of ['left','top','right','bottom','adapterLow','adapterHigh']) insist(Number.isSafeInteger(row[key]), 'Invalid output bounds/LUID');
      insist(row.right>row.left && row.bottom>row.top && row.adapterLow===begin.adapterLow &&
        row.adapterHigh===begin.adapterHigh && begin.originX+8>=row.left && begin.originX+72<=row.right &&
        begin.originY+8>=row.top && begin.originY+44<=row.bottom, 'Desktop output/region mismatch');
      output=row; continue;
    }
    insist(output, 'Desktop output binding missing');
    if (row.event === 'desktop.pointer') {
      insist(!pending && row.lastPresentQpc==='0' && row.accumulatedFrames===0, 'Pointer update is not an image');
      exact(row.observedQpc,false); pointers.push(row); continue;
    }
    if (row.event === 'desktop.acquire') {
      const qpc=exact(row.lastPresentQpc,false);
      insist(!pending && qpc>lastPresent && whole(row.accumulatedFrames,1,4096) && row.protected===false, 'Invalid desktop acquisition');
      pending=row; lastPresent=qpc; continue;
    }
    if (row.event === 'desktop.baseline-reject') {
      insist(!ready && pending && row.lastPresentQpc===pending.lastPresentQpc &&
        exact(row.lastPresentQpc)-exact(begin.startedQpc)<=input.frequency*5n, 'Invalid desktop warmup rejection');
      pending=null; continue;
    }
    if (row.event === 'desktop.ready') {
      insist(!ready && pending && row.lastPresentQpc===pending.lastPresentQpc &&
        row.nonce===source.nonce && row.ack===0, 'Fresh run baseline missing');
      ready=true; continue;
    }
    insist(row.event==='desktop.frame' && ready && pending && row.lastPresentQpc===pending.lastPresentQpc &&
      exact(row.observedQpc,false)>=exact(row.lastPresentQpc,false) && row.nonce===source.nonce &&
      whole(row.ack,lastAck,source.requested) && whole(row.frame,lastFrame,65535) && row.scene===1 &&
      whole(row.deliveredUnits,lastDelivered,row.ack) && (source.kind!=='wheel'||row.deliveredUnits===row.ack) &&
      row.motion===false && row.contentSequence===row.ack && row.width===1100 && row.height===720,
      'Burst marker/content/generation changed');
    const decoded=decodeBurstMarkerHex(row.markerHex);
    insist(decoded.nonce===row.nonce && decoded.ack===row.ack && decoded.frame===row.frame &&
      decoded.scene===row.scene && decoded.motion===row.motion && decoded.deliveredUnits===row.deliveredUnits, 'Raw marker bytes differ');
    if (row.ack>lastAck) {
      insist(row.frame>lastFrame, 'New ACK lacks a completed content frame');
      const injection=input.inputs[row.ack-1], present=exact(row.lastPresentQpc,false);
      insist(present>=exact(injection.before_qpc,false), 'Desktop content predates its input');
      observations.push({sequence:row.ack,present_qpc:row.lastPresentQpc,observed_qpc:row.observedQpc,
        crossed_inputs:row.ack-lastAck-1,accumulated_frames:pending.accumulatedFrames,
        previous_ack:lastAck,delivered_units:row.deliveredUnits,
        complete_image_history:omittedImages+pending.accumulatedFrames-1===0,
        lower_ms:present>=exact(injection.after_qpc)?milliseconds(present-exact(injection.after_qpc),input.frequency):
          -milliseconds(exact(injection.after_qpc)-present,input.frequency),
        upper_ms:milliseconds(present-exact(injection.before_qpc),input.frequency)});
    }
    omittedImages+=pending.accumulatedFrames-1; ++frameCount; lastAck=row.ack; lastFrame=row.frame;lastDelivered=row.deliveredUnits; pending=null;
  }
  insist(!pending && lastAck===source.requested && exact(end.frames)===BigInt(frameCount) &&
    exact(end.pointerOnly)===BigInt(pointers.length), 'Desktop final ACK/accounting incomplete');
  return {observations,omittedImages,frameCount,pointerOnly:pointers.length,deliveredUnits:lastDelivered,
    observationErrorMs:calibration.observation_error_ms};
}
function checkBurst(inputRows,streams,desktopRows,options={}) {
  const input=verifyInput(inputRows);
  insist(Array.isArray(streams) && (streams.length>0 || options.allowUnavailableStages===true),'Missing internal streams without explicit E2E-only mode');
  const joined=streams.length?trace.joinTraces(streams):{identity:{run_id:input.begin.run_id,
    process_id:input.begin.target_pid,process_incarnation:input.begin.process_incarnation,qpc_frequency:input.begin.qpc_frequency},
    points:[],streams:[],fullCausalEligible:false,equal_tick_cross_producer_groups:0,
    equal_tick_order:'internal-stage-evidence-unavailable'};
  const identity=joined.identity;
  insist(input.begin.run_id===identity.run_id && input.begin.target_pid===identity.process_id &&
    input.begin.process_incarnation===identity.process_incarnation && input.begin.qpc_frequency===identity.qpc_frequency,
    'Burst/trace exact identity mismatch');
  insist(options.ownedTarget?.pid===input.begin.target_pid &&
    options.ownedTarget.creationFileTime===input.begin.process_incarnation &&
    options.ownedInjector?.pid===input.begin.injector_pid &&
    options.ownedInjector.creationFileTime===input.begin.injector_incarnation,
    'Native held target/injector identities required');
  const mapping=new Map(), nativeKeys=new Map(), terminalByKey=new Map();
  const key=point=>[point.view_id,point.view_generation,point.document_generation,point.geometry_generation,point.input_first].join('/');
  for (const point of joined.points) {
    if (point.point===trace.stages.host_receive && point.related_id!=='0') {
      const sequence=Number(exact(point.related_id,false));
      insist(whole(sequence,1,input.inputs.length) && !mapping.has(sequence) && point.input_first===point.input_last &&
        point.input_first!=='0' && exact(point.qpc)>=exact(input.inputs[sequence-1].before_qpc),
        'Diagnostic tag is duplicated, outside injection span or lacks actual packet mapping');
      insist(!nativeKeys.has(key(point)), 'Two injections map to one native input');
      mapping.set(sequence,point); nativeKeys.set(key(point),sequence);
    }
    if (point.point===trace.stages.input_terminal && point.input_first===point.input_last) {
      insist(!terminalByKey.has(key(point)), 'Duplicate input terminal'); terminalByKey.set(key(point),point);
    }
  }
  const desktop=checkDesktop(desktopRows,input,options), visible=new Map();
  const nativeStages=[1,2,3,4,6,7,8], byGeneration=new Map(), nativeTimes=new Map();
  for(const [sequence,point] of mapping) {
    const generation=[point.view_id,point.view_generation,point.document_generation,point.geometry_generation].join('/');
    if(!byGeneration.has(generation))byGeneration.set(generation,[]);
    byGeneration.get(generation).push({sequence,native:exact(point.input_first)});
    nativeTimes.set(sequence,new Map([[1,exact(point.qpc)]]));
  }
  for(const members of byGeneration.values())members.sort((a,b)=>a.native<b.native?-1:a.native>b.native?1:0);
  let relations=0;
  for(const point of joined.points) {
    if(![...nativeStages,5].includes(point.point) || point.input_first==='0' ||
      ([6,7,8].includes(point.point)&&point.frame_id==='0'))continue;
    const generation=[point.view_id,point.view_generation,point.document_generation,point.geometry_generation].join('/');
    const members=byGeneration.get(generation)||[], first=exact(point.input_first),last=exact(point.input_last);
    let low=0,high=members.length;
    while(low<high){const middle=(low+high)>>>1;if(members[middle].native<first)low=middle+1;else high=middle;}
    for(let index=low;index<members.length && members[index].native<=last;++index) {
      insist(++relations<=1000000,'Native coverage relation budget exceeded');
      const times=nativeTimes.get(members[index].sequence),tick=exact(point.qpc);
      insist(tick>=times.get(1),'Native stage predates its actual host input');
      if(!times.has(point.point)||tick<times.get(point.point))times.set(point.point,tick);
    }
  }
  for(const point of joined.points) {
    if(point.point!==trace.stages.input_terminal || point.input_first===point.input_last)continue;
    const generation=[point.view_id,point.view_generation,point.document_generation,point.geometry_generation].join('/');
    const members=byGeneration.get(generation)||[],first=exact(point.input_first),last=exact(point.input_last);
    let low=0,high=members.length;
    while(low<high){const middle=(low+high)>>>1;if(members[middle].native<first)low=middle+1;else high=middle;}
    for(let index=low;index<members.length && members[index].native<=last;++index) {
      insist(++relations<=1000000,'Terminal coverage relation budget exceeded');
      const host=mapping.get(members[index].sequence);
      insist(!terminalByKey.has(key(host)) && exact(point.qpc)>=exact(host.qpc),'Duplicate/predating terminal range');
      terminalByKey.set(key(host),point);
    }
  }
  let nativeComplete=0,microtasksComplete=0;
  const stageSamples=Object.fromEntries(nativeStages.map(point=>[String(point),0]));
  const hostToEngine=[],hostToSubmit=[];
  for(const times of nativeTimes.values()) {
    if(nativeStages.every(point=>times.has(point)))++nativeComplete;
    if(times.has(5))++microtasksComplete;
    for(const point of nativeStages)if(times.has(point))++stageSamples[String(point)];
    if(times.has(3))hostToEngine.push(milliseconds(times.get(3)-times.get(1),input.frequency));
    if(times.has(8))hostToSubmit.push(milliseconds(times.get(8)-times.get(1),input.frequency));
  }
  const visualSuperseded=new Map();
  if(options.controlledWorkload===true)for(const observation of desktop.observations) {
    if(!observation.complete_image_history || observation.delivered_units!==observation.sequence)continue;
    for(let sequence=observation.previous_ack+1;sequence<observation.sequence;++sequence)
      visualSuperseded.set(sequence,exact(observation.present_qpc));
  }
  const ages=[]; let oldest=1,newest=0;
  for (const observation of desktop.observations) {
    const present=exact(observation.present_qpc); visible.set(observation.sequence,present);
    while(newest<input.inputs.length && exact(input.inputs[newest].before_qpc)<=present) ++newest;
    while(oldest<=newest) {
      const host=mapping.get(oldest), terminal=host && terminalByKey.get(key(host));
      const ended=visible.get(oldest) ?? visualSuperseded.get(oldest) ??
        (terminal && terminal.outcome!==1 ? exact(terminal.qpc) : undefined);
      if (ended===undefined || ended>present) break;
      ++oldest;
    }
    ages.push({present_qpc:observation.present_qpc,oldest_pending_sequence:oldest<=newest?String(oldest):null,
      oldest_pending_age_ms:oldest<=newest?milliseconds(present-exact(input.inputs[oldest-1].before_qpc),input.frequency):null,
      latest_injected_sequence:String(newest),latest_input_age_ms:milliseconds(present-exact(input.inputs[newest-1].before_qpc),input.frequency)});
  }
  const unresolved=[];let unprovenVisibleTerminals=0;
  const classifications={visible:0,coalesced:0,cancelled:0,stale:0,not_visible:0,rejected:0,unknown:0};
  const outcomes=['unknown','visible','coalesced','cancelled','stale','not_visible','rejected'];
  for(let sequence=1;sequence<=input.inputs.length;++sequence) {
    const host=mapping.get(sequence), terminal=host && terminalByKey.get(key(host));
    if(terminal && host)insist(exact(terminal.qpc)>=exact(host.qpc),'Input terminal predates host processing');
    if(!visible.has(sequence) && terminal?.outcome===1)++unprovenVisibleTerminals;
    const category=terminal&&terminal.outcome!==1?outcomes[terminal.outcome]:visible.has(sequence)?'visible':
      visualSuperseded.has(sequence)?'coalesced':'unknown';
    ++classifications[category];
    if(category==='unknown')unresolved.push(sequence);
  }
  const intervals=desktop.observations.slice(1).map((value,index)=>milliseconds(
    exact(value.present_qpc)-exact(desktop.observations[index].present_qpc),input.frequency));
  const injectionIntervals=input.inputs.slice(1).map((value,index)=>milliseconds(
    exact(value.before_qpc)-exact(input.inputs[index].before_qpc),input.frequency));
  const injectionSpan=exact(input.inputs.at(-1).before_qpc)-exact(input.inputs[0].before_qpc);
  return {schema:'soui.burst-input-check/v1',run_id:input.begin.run_id,
    qpc_frequency:input.begin.qpc_frequency,
    requested_hz:input.begin.hz,actual_span_hz:injectionSpan>0n?
      (input.inputs.length-1)*1000/milliseconds(injectionSpan,input.frequency):null,
    injection_intervals_ms:injectionIntervals.length?distribution(injectionIntervals):null,
    scheduling_lateness_ms:distribution(input.inputs.map(value=>milliseconds(
      exact(value.before_qpc)-exact(value.scheduled_qpc),input.frequency))),
    input_count:input.inputs.length,mapped_host_inputs:mapping.size,
    stage_evidence:streams.length?'available':'unavailable',
    unobserved_injections:streams.length?input.inputs.length-mapping.size:null,
    desktop_samples:desktop.observations.length,unresolved_inputs:unresolved.length,
    input_terminal_counts:classifications,classified_input_count:input.inputs.length-classifications.unknown,
    proven_visual_coalesced_inputs:visualSuperseded.size,delivered_units:desktop.deliveredUnits,
    unproven_visible_terminals:unprovenVisibleTerminals,
    unresolved_input_sequences:unresolved,desktop_omitted_images:desktop.omittedImages,
    ack_crossed_inputs:desktop.observations.reduce((sum,item)=>sum+item.crossed_inputs,0),
    fullCausalEligible:joined.fullCausalEligible,fullCausalComplete:joined.fullCausalEligible &&
      mapping.size===input.inputs.length && unresolved.length===0 && desktop.omittedImages===0 &&
      nativeComplete===input.inputs.length && microtasksComplete===input.inputs.length && unprovenVisibleTerminals===0,
    native_stage_samples:stageSamples,native_path_complete_inputs:nativeComplete,
    proven_microtask_inputs:microtasksComplete,unknown_microtask_inputs:input.inputs.length-microtasksComplete,
    host_to_engine_ms:hostToEngine.length?distribution(hostToEngine):null,
    host_to_submit_ms:hostToSubmit.length?distribution(hostToSubmit):null,
    stage_values_ms:{host_to_engine:hostToEngine,host_to_submit:hostToSubmit},
    // Specific captured-image response; omitted images cannot prove first display.
    captured_image_upper_ms:distribution(desktop.observations.map(value=>value.upper_ms)),
    captured_image_intervals_ms:intervals.length?distribution(intervals):null,
    observation_error_ms:desktop.observationErrorMs,ages,samples:desktop.observations,
    qpc_quantum_ms:milliseconds(1n,input.frequency),
    equal_tick_cross_producer_groups:joined.equal_tick_cross_producer_groups,
    equal_tick_order:joined.equal_tick_order,
    p99_scope:desktop.observations.length>=10000?'sample-count-met':'exploratory-insufficient-visible-samples',
    native_frame_coverage_meaning:'processed-context-only-unless-separate-content-proof',
    t0_endpoint:'SendInput-call-interval-not-physical-device',
    t7_endpoint:'captured-compositor-image-not-scanout',
    physicalPresentation:false,performanceGate:false,releaseReady:false};
}
module.exports={verifyInput,checkDesktop,checkBurst,decodeBurstMarkerHex};
