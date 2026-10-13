'use strict';
const assert = require('node:assert/strict');
const {isDeepStrictEqual} = require('node:util');
const evidence = require('./owned-trace-subject.cjs');
const maximum = 18446744073709551615n;
function insist(value, reason) { if (!value) throw new Error(reason); }
function whole(value, max) { return Number.isSafeInteger(value) && value >= 0 && value <= max; }
function exact(value, zero = false) {
  insist(typeof value === 'string' && (zero ? /^(0|[1-9][0-9]{0,19})$/ : /^[1-9][0-9]{0,19}$/).test(value) && BigInt(value) <= maximum, 'Invalid exact desktop counter');
  return BigInt(value);
}
function decodeMarkerHex(hex) {
  insist(typeof hex === 'string' && /^[a-f0-9]{32}$/.test(hex), 'Invalid marker bytes');
  const data = Buffer.from(hex, 'hex');
  let crc = 0xffff;
  for (let n = 0; n < 12; ++n) {
    crc ^= data[n] << 8;
    for (let b = 0; b < 8; ++b) crc = ((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0)) & 0xffff;
  }
  insist(data.readUInt16LE(0) === 0x53a7 && data.readUInt16LE(12) === crc && data.readUInt16LE(14) === 0, 'Marker CRC/magic/reserved bits differ');
  const result = {nonce:data.readUInt32LE(2), ack:data.readUIntLE(6,3), frame:data.readUInt16LE(9), scene:data[11]&7, motion:(data[11]&8)!==0};
  insist(result.nonce !== 0 && result.ack < 0x100000 && result.scene >= 1 && result.scene <= 5 && (data[11]&0xf0)===0, 'Marker fields exceed contract');
  return result;
}
function distribution(samples) {
  insist(samples.length > 0 && samples.every(Number.isFinite), 'Missing desktop samples');
  const sorted = [...samples].sort((a,b)=>a-b);
  const q = p => sorted[Math.max(0,Math.ceil(p*sorted.length)-1)];
  return {samples:samples.length,p50:q(.5),p95:q(.95),p99:q(.99),min:sorted[0],max:sorted.at(-1)};
}
function checkDesktopTrace(input, rows, scene, options = {}) {
  insist(Array.isArray(input) && Array.isArray(rows) && rows.length >= 5 && rows.length <= 60010, 'Invalid bounded desktop records');
  const begin = rows[0], end = rows.at(-1), inputBegin = input[0], inputEnd = input.at(-1);
  insist(['S01','S02','S04','S05'].includes(scene), 'Unsupported desktop scene');
  const probe=options.probe===true;
  insist(begin?.event==='desktop.begin' && begin.schema==='soui.desktop-visual-trace/v1' && begin.scene===Number(scene.slice(2)) && begin.probe===probe && begin.dpi===96 && begin.rotation===0 && isDeepStrictEqual(begin.region,[8,8,64,32]), 'Invalid full desktop header');
  const hz = exact(begin.qpcFrequency), start = exact(begin.startedQpc);
  insist(hz<=BigInt(Number.MAX_SAFE_INTEGER),'Desktop QPC frequency exceeds safe conversion');
  exact(begin.creationFileTime); exact(begin.collectorCreationFileTime); exact(begin.hwnd);
  insist(whole(begin.pid,0xffffffff)&&begin.pid>0 && whole(begin.collectorPid,0xffffffff)&&begin.collectorPid>0 && Number.isSafeInteger(begin.originX)&&Number.isSafeInteger(begin.originY), 'Invalid desktop owner');
  insist(end?.event==='desktop.end' && end.completed===true && exact(end.endedQpc)>=start, 'Desktop collection failed/incomplete');
  const minimum=probe?3000:scene==='S05'?123000:61000;
  insist((exact(end.endedQpc)-start)*1000n >= hz*BigInt(minimum), 'Desktop collection is shorter than full input');
  insist(inputBegin?.schema==='soui.external-input-trace/v2' && inputBegin.scene===scene && inputBegin.mode===(probe?'probe':'full') && inputBegin.pid===begin.pid && inputBegin.processCreationTime100ns===begin.creationFileTime && inputBegin.expectedCreationTime100ns===begin.creationFileTime && BigInt(inputBegin.qpcFrequency)===hz, 'Desktop/input identities or QPC differ');
  insist(inputEnd?.event==='run.end' && inputEnd.elapsedMs >= minimum && inputEnd.actions===input.length-2, 'Input trace is incomplete');
  const frames=[], pointers=[];
  let pending=null, ready=null, output=null, previousPresent=0n, previousFrame=null;
  for (const row of rows.slice(1,-1)) {
    if (row.event==='desktop.output') {
      insist(!output&&!pending&&!ready&&typeof row.deviceName==='string'&&/^\\\\\.\\DISPLAY[1-9][0-9]*$/.test(row.deviceName), 'Invalid/duplicate desktop output');
      for(const name of ['left','top','right','bottom','adapterLow','adapterHigh'])insist(Number.isSafeInteger(row[name]), 'Invalid desktop output field');
      insist(row.right>row.left&&row.bottom>row.top&&begin.originX+8>=row.left&&begin.originX+72<=row.right&&begin.originY+8>=row.top&&begin.originY+40<=row.bottom&&row.adapterLow===begin.adapterLow&&row.adapterHigh===begin.adapterHigh, 'Desktop region/adapter is outside its output');
      output=row;continue;
    }
    insist(output,'Desktop output binding missing');
    if (row.event==='desktop.pointer') {
      insist(!pending && row.lastPresentQpc==='0' && row.accumulatedFrames===0, 'Pointer-only update was promoted to desktop image');
      exact(row.observedQpc); pointers.push(row); continue;
    }
    if (row.event==='desktop.acquire') {
      const present = exact(row.lastPresentQpc);
      insist(!pending && present>previousPresent && row.accumulatedFrames===1 && row.protected===false, 'Merged/protected/reordered desktop image');
      previousPresent=present;pending=row;continue;
    }
    if (row.event==='desktop.baseline-reject') {
      insist(!ready && pending && row.lastPresentQpc===pending.lastPresentQpc && exact(row.lastPresentQpc)-start<=hz*5n, 'Invalid baseline warmup');
      pending=null;continue;
    }
    if (row.event==='desktop.ready') {
      insist(!ready && pending && row.lastPresentQpc===pending.lastPresentQpc && whole(row.nonce,0xffffffff)&&row.nonce>0 && whole(row.ack,0xfffff), 'Duplicate/invalid baseline');
      ready=row;continue;
    }
    insist(row.event==='desktop.frame' && ready && pending && row.lastPresentQpc===pending.lastPresentQpc, 'Missing frame acquisition or unknown record');
    const marker=decodeMarkerHex(row.markerHex);
    for(const name of Object.keys(marker))insist(row[name]===marker[name], 'Decoded marker differs from raw bytes');
    insist(row.nonce===ready.nonce && exact(row.observedQpc)+1n>=exact(row.lastPresentQpc), 'Marker incarnation/time changed');
    insist((row.width===1100&&row.height===720)||(scene==='S04'&&row.width===1060&&row.height===700), 'Unexpected desktop geometry');
    if(previousFrame)insist(row.ack>=previousFrame.ack && row.ack<=previousFrame.ack+1 && row.frame>=previousFrame.frame, 'Visual ACK/counter skipped or reversed');
    else insist(row.ack===ready.ack, 'First frame differs from baseline');
    frames.push(row);previousFrame=row;pending=null;
  }
  insist(!pending && frames.length===end.frames && pointers.length===end.pointerOnly && frames.length>0, 'Desktop count/acquisition is incomplete');
  const firstAck = new Map(); for(const frame of frames)if(!firstAck.has(frame.ack))firstAck.set(frame.ack,frame);
  const samples=[]; let expected=ready.ack+1, previousAfter=0n, previousResize={clientX:1100,clientY:720};
  for(const action of input.slice(1,-1)) {
    insist(action.event==='input', 'Unexpected input record');
    if(action.action==='finish-scene'){insist(action===input.at(-2)&&action.desktopEventId===undefined, 'Finish must follow observer closure');continue;}
    if(action.desktopEventId===undefined) {
      insist(scene==='S04'&&action.action==='window-resize'&&action.clientX===previousResize.clientX&&action.clientY===previousResize.clientY, 'A measured operation lacks visual ACK');continue;
    }
    insist(action.desktopEventId===expected++, 'Input event IDs are not contiguous');
    const before=exact(action.desktopQpcBefore),after=exact(action.desktopQpcAfter);
    insist(after>=before && before>=previousAfter && Number.isSafeInteger(action.qpcBefore)&&Number.isSafeInteger(action.qpcAfter)&&BigInt(action.qpcBefore)===before&&BigInt(action.qpcAfter)===after, 'Input exact QPC differs/reorders');previousAfter=after;
    const visible=firstAck.get(action.desktopEventId);
    insist(visible && visible.scene===begin.scene, 'Expected scene/visual ACK was never observed');
    const present=exact(visible.lastPresentQpc);
    insist(present+1n>=before && (present-before)*1000n<=hz*500n, 'Visual ACK precedes injection or exceeded timeout');
    // Retain signed lower bounds: SendInput can return after the visible ACK.
    samples.push({eventId:action.desktopEventId,action:action.action,presentQpc:visible.lastPresentQpc,
      lowerTicks:(present-after).toString(),upperTicks:(present-before).toString(),
      lowerMs:Number(present-after)*1000/Number(hz),upperMs:Number(present-before)*1000/Number(hz)});
    if(action.action==='window-resize')previousResize=action;
  }
  insist(samples.length>=3 && frames.at(-1).ack===expected-1, 'Extra/missing visual input ACK');
  let bound=false;
  if(options.ownerSubject!==undefined||options.collectorSubject!==undefined){
    insist(options.ownerSubject?.pid===begin.pid && options.ownerSubject?.creationFileTime===begin.creationFileTime && options.collectorSubject?.pid===begin.collectorPid && options.collectorSubject?.creationFileTime===begin.collectorCreationFileTime, 'Desktop evidence differs from actual owned subjects');bound=true;
  }
  if(bound||options.pageMarker!==undefined)insist(options.pageMarker?.schema==='soui.visual-marker/v1'&&options.pageMarker.nonce===ready.nonce&&options.pageMarker.devicePixelRatio===1&&isDeepStrictEqual(options.pageMarker.region,begin.region), 'Page marker/DPR differs from observed pixels');
  const motion=frames.filter(row=>row.motion&&row.scene===5);
  const intervals=[];let skipped=0,previous=null;
  for(const row of motion){if(previous&&row.frame!==previous.frame){intervals.push(Number(exact(row.lastPresentQpc)-exact(previous.lastPresentQpc))*1000/Number(hz));skipped+=row.frame-previous.frame-1;}if(!previous||row.frame!==previous.frame)previous=row;}
  if(scene==='S05')insist(intervals.length>=(probe?1:100) && motion.length>=(probe?2:101), 'S05 has no continuous observed marker workload');
  return {schema:'soui.desktop-visual-verdict/v1',desktopObservationAccepted:bound&&!probe,desktopProbeAccepted:bound&&probe,observationScope:probe?'probe':'full',exactOwnedSubjectsBound:bound,
    samples,visualResponseUpperMs:distribution(samples.map(row=>row.upperMs)),
    outputBinding:output,markerFrameIntervals:intervals,markerFrameIntervalsMs:intervals.length?distribution(intervals):null,markerFramesSkipped:skipped,
    markerLongIntervalsOver33_33ms:intervals.filter(value=>value>33.33).length,
    metricMeaning:'SendInput/SetWindowPos interval to first desktop image containing visual ACK; S05 instrumented rAF marker cadence',
    physicalPresentation:false,opticalCalibration:false,performanceGate:false,releaseReady:false};
}
function readDesktopRun(root) {
  const record=evidence.readRunJson(root,'run.json');
  insist(record.desktopObservation===true&&record.status==='completed','Run did not complete explicit desktop observation');
  const verified=evidence.readOwnedRun(root,record);
  require('./check-run-traces.cjs').checkRunTraces(evidence.readRunLines(root,'input.ndjson'),
    evidence.readRunLines(root,'process-tree.ndjson'),record.scene,{ownedSubject:verified.subject});
  assert.deepEqual(evidence.describe(root,'input.ndjson.desktop.ndjson'),record.traceEvidence?.desktop,'Desktop raw hash differs');
  const page=evidence.readRunJson(root,record.pageResult.path);
  assert.deepEqual(evidence.describe(root,record.pageResult.path),
    {path:record.pageResult.path,bytes:record.pageResult.bytes,sha256:record.pageResult.sha256},'Page raw hash differs');
  return checkDesktopTrace(evidence.readRunLines(root,'input.ndjson'),evidence.readRunLines(root,'input.ndjson.desktop.ndjson'),record.scene,
    {ownerSubject:verified.subject,collectorSubject:verified.processes.input.subject,pageMarker:page.visualMarker});
}
function readDesktopSeries(root) {
  // Reuse complete raw/page/input/resources/chronology/interference validation.
  const preflight=require('./summarize-series.cjs').summarizeDirectory(root);
  const series=evidence.readRunJson(root,'series.json');
  insist(series.desktopObservation===true&&preflight.acceptedPairs>=5,'Five complete desktop pairs are required');
  const runs={soui:[],electron:[]};let output=null;
  for(const pair of series.pairs)for(const host of ['soui','electron']) {
    const result=readDesktopRun(evidence.runDirectory(root,pair.runs[host]));
    insist(result.desktopObservationAccepted,'A desktop run is unbound');
    if(output)assert.deepEqual(result.outputBinding,output,'Display output changed across rounds');else output=result.outputBinding;
    runs[host].push(result);
  }
  const metrics={};
  for(const host of ['soui','electron']) {
    const responses=runs[host].flatMap(run=>run.samples.map(sample=>sample.upperMs));
    const frames=runs[host].flatMap(run=>run.markerFrameIntervals);
    metrics[host]={rounds:runs[host].length,visualResponseUpperMs:distribution(responses),
      markerFrameIntervalsMs:frames.length?distribution(frames):null,
      perRoundResponseP95:runs[host].map(run=>run.visualResponseUpperMs.p95),
      markerFramesSkipped:runs[host].reduce((sum,run)=>sum+run.markerFramesSkipped,0)};
  }
  return {schema:'soui.desktop-visual-series/v1',desktopObservationAccepted:true,scene:series.scene,
    metrics,outputBinding:output,sourceIdentity:preflight.sourceIdentity,
    physicalPresentation:false,opticalCalibration:false,performanceGate:false,releaseReady:false,
    remainingEvidence:['M0 frozen device/workload/measurement budget','actual output configured rate and instrumentation overhead','optical input-to-photon','Win7/PE separate capture path']};
}
if(require.main===module){try{const series=process.argv[2]==='--series';insist(process.argv.length===(series?4:3),'Usage: node desktop-visual.cjs [--series] ABS_RESULT_DIRECTORY');console.log(JSON.stringify(series?readDesktopSeries(process.argv[3]):readDesktopRun(process.argv[2]),null,2));}catch(error){console.error(error.message);process.exitCode=1;}}
module.exports={decodeMarkerHex,distribution,checkDesktopTrace,readDesktopRun,readDesktopSeries};
