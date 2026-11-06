'use strict';
// V5 trajectory admission with the unchanged v2 persistent-mask pixel encoding.
const trace=require('./input-frame-trace.cjs');
const cells=require('./cells.cjs');
const {insist:need,exact,whole,milliseconds:ms,distribution}=trace;
const eq=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const gen=p=>[p.view_id,p.view_generation,p.document_generation,p.geometry_generation].join('/');
function natural(r){return r&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed&&r.terminal?.reason==='completed'&&r.terminal.rootExitCode===0&&r.terminal.rootExited&&r.terminal.activeBeforeCleanup===0&&r.terminal.activeProcesses===0&&!r.terminal.terminatedOwnedJob&&r.terminal.limitTerminatedProcesses===0;}
function decode(hex){
 need(typeof hex==='string'&&/^[a-f0-9]{48}$/.test(hex),'V2 requires exactly24 marker bytes; legacy rejected');
 const b=Buffer.from(hex,'hex');let crc=65535;for(let n=0;n<24;n++){if(n===12||n===13)continue;crc^=b[n]<<8;for(let bit=0;bit<8;bit++)crc=((crc<<1)^((crc&32768)?4129:0))&65535;}
 need(b.readUInt16LE(0)===0x53a7&&(b[11]&15)===2&&b.readUInt16LE(12)===crc,'V2 marker magic/version/CRC invalid');
 const mask=b.subarray(16),ack=b.readUIntLE(6,3),delivered=b.readUInt16LE(14)|((b[11]>>>4)<<16);let count=0;for(const x of mask)for(let bit=0;bit<8;bit++)count+=(x>>>bit)&1;
 need(b.readUInt32LE(2)>0&&ack<=64&&delivered===ack&&count===delivered,'V2 mask/count/ACK inconsistent');
 return{nonce:b.readUInt32LE(2),ack,frame:b.readUInt16LE(9),mask,delivered};
}
function verifyInput(rows){
 need(Array.isArray(rows)&&rows.length===66,'Exactly64 complete injection rows required');const b=rows[0],e=rows.at(-1);
 need(b.event==='burst.begin'&&b.schema==='soui.spatial-cell-input/v5'&&b.protocol==='spatial-cell-feedback64/v5'&&cells.matchesGeometry(b.cell_geometry)&&/^[a-f0-9]{32}$/.test(b.run_id)&&!/^0+$/.test(b.run_id)&&b.hz===125&&b.requested===64&&b.kind==='pointer_move'&&b.concurrent_injectors===1&&b.injection_tag_magic===0x53400000&&b.content_width===1100&&b.content_height===720&&['dxgi','gdi-bitblt'].includes(b.capture_backend),'Frozen v5 spatial-cell input identity differs; legacy input rejected');
 need(whole(b.target_pid,1)&&whole(b.injector_pid,1)&&b.nonce===(parseInt(b.run_id.slice(24),16)||1),'Input PID/nonce differs');
 const frequency=exact(b.qpc_frequency,false),start=exact(b.started_qpc,false);exact(b.process_incarnation,false);exact(b.injector_incarnation,false);
 let prior=start;const inputs=rows.slice(1,-1);inputs.forEach((x,i)=>{const before=exact(x.before_qpc,false),after=exact(x.after_qpc,false),scheduled=start+BigInt(i)*frequency/125n;
 need(x.event==='input.inject'&&exact(x.sequence,false)===BigInt(i+1)&&x.sent===1&&x.injection_tag===(0x53400000|(i+1))&&x.client_x===40+16*i&&x.client_y===128&&x.wheel_delta===0&&exact(x.scheduled_qpc,false)===scheduled&&before>=scheduled&&before>=prior&&after>=before,'V5 input trajectory/tag/clock mismatch');prior=after;});
 need(e.event==='burst.end'&&e.completed===true&&e.attempted===64&&e.sent===64&&exact(e.ended_qpc,false)>=prior,'Input failed or truncated');return{begin:b,end:e,inputs,frequency,start};
}
function desktop(rows,input){
 need(Array.isArray(rows)&&rows.length>=5&&rows.length<=300010,'Bounded desktop trace required');const b=rows[0],e=rows.at(-1),s=input.begin;
 need(b.event==='desktop.begin'&&b.schema==='soui.persistent-ack-desktop/v2'&&b.run_id===s.run_id&&b.pid===s.target_pid&&b.creationFileTime===s.process_incarnation&&b.collectorPid===s.injector_pid&&b.collectorCreationFileTime===s.injector_incarnation&&b.qpcFrequency===s.qpc_frequency&&b.expectedNonce===s.nonce&&b.dpi===96&&b.rotation===0&&b.scene===1&&b.probe===false&&eq(b.region,[8,8,64,52])&&eq(b.contentProbe,[10,42]),'Desktop identity/QPC/geometry differs');
 const backend=b.captureBackend??'dxgi';need(backend===s.capture_backend,'Capture backend mismatch');exact(b.hwnd,false);exact(b.startedQpc,false);
 let output=null,ready=false,pending=null,previous=null,frames=0,pointers=0,lastPresent=0n,baseline=null,lastEnd=0n;const first=Array(64).fill(null),negative=Array(64).fill(null);
 for(const x of rows.slice(1,-1)){
  if(x.event==='desktop.output'){need(!output&&!ready&&/^\\\\\.\\DISPLAY[1-9][0-9]*$/.test(x.deviceName),'Invalid output identity');for(const k of ['left','top','right','bottom'])need(Number.isSafeInteger(x[k]),'Output bounds invalid');need(x.right>x.left&&x.bottom>x.top&&b.originX+8>=x.left&&b.originX+72<=x.right&&b.originY+8>=x.top&&b.originY+60<=x.bottom,'ROI outside output');if(backend==='dxgi')need(Number.isSafeInteger(x.adapterLow)&&Number.isSafeInteger(x.adapterHigh)&&x.adapterLow===b.adapterLow&&x.adapterHigh===b.adapterHigh,'Output LUID mismatch');else need(x.adapterLow===null&&x.adapterHigh===null&&b.adapterLow===null&&b.adapterHigh===null,'GDI cannot assert LUID');output=x;continue;}
  need(output,'Output missing');
  if(x.event==='desktop.pointer'){need(backend==='dxgi'&&!pending&&x.lastPresentQpc==='0'&&x.accumulatedFrames===0,'Invalid pointer-only row');exact(x.observedQpc,false);pointers++;continue;}
  if(x.event==='desktop.acquire'){const q=exact(x.lastPresentQpc,false);need(backend==='dxgi'&&!pending&&q>lastPresent&&whole(x.accumulatedFrames,1,4096)&&x.protected===false,'Invalid acquisition');pending=x;lastPresent=q;continue;}
  if(x.event==='desktop.ready'){need(!ready&&x.nonce===s.nonce&&x.ack===0,'Fresh zero ACK missing');ready=true;continue;}
  // Any decode/CRC rejection, including warmup, prevents qualified evidence.
  need(x.event==='desktop.frame'&&ready,'Rejected/unknown desktop row; no relaxed CRC acceptance');
  const begin=exact(x.captureBeginQpc,false),end=exact(x.captureEndQpc,false),observed=exact(x.observedQpc,false);
  need(begin<=end&&end>=lastEnd&&end-begin<=input.frequency/10n&&observed>=end&&x.captureSequence===frames+1&&x.width===1100&&x.height===720&&x.scene===1&&x.motion===false,'Capture interval/geometry invalid');lastEnd=end;
  if(backend==='dxgi'){need(pending&&x.lastPresentQpc===pending.lastPresentQpc&&end>=exact(x.lastPresentQpc,false),'Acquisition/frame mismatch');pending=null;}else need(x.lastPresentQpc===null&&x.accumulatedFrames===null&&x.protected===null,'GDI cannot assert DXGI metadata');
  const m=decode(x.markerHex);need(m.nonce===s.nonce&&m.ack===x.ack&&m.frame===x.frame&&m.delivered===x.deliveredUnits&&x.contentSequence===m.ack&&m.frame>0,'Decoded marker differs');
  if(!previous){need(m.ack===0&&m.mask.equals(Buffer.alloc(8))&&end<=input.start,'Initial mask is not pre-input zero');baseline={begin:x.captureBeginQpc,end:x.captureEndQpc};}
  else {need(m.ack>=previous.ack&&m.frame>=previous.frame&&(m.ack===previous.ack||m.frame>previous.frame),'ACK/frame reverted or new ACK lacks frame');for(let n=0;n<8;n++)need((m.mask[n]&previous.mask[n])===previous.mask[n],'Persistent bit reverted');}
  for(let i=0;i<64;i++){const set=(m.mask[i>>>3]&(1<<(i&7)))!==0;if(set&&!first[i]){need(end>=exact(input.inputs[i].before_qpc,false),'Positive capture predates injection');first[i]={captureSequence:x.captureSequence,beginQpc:x.captureBeginQpc,endQpc:x.captureEndQpc,observedQpc:x.observedQpc,lastNegative:negative[i]};}else if(!set)negative[i]={beginQpc:x.captureBeginQpc,endQpc:x.captureEndQpc};}
  previous=m;frames++;
 }
 need(!pending&&baseline&&previous?.ack===64&&previous.mask.equals(Buffer.alloc(8,255))&&first.every(Boolean)&&e.event==='desktop.end'&&e.completed===true&&exact(e.frames)===BigInt(frames)&&exact(e.endedQpc,false)>=exact(input.end.ended_qpc,false),'Desktop final mask/accounting incomplete');
 if(backend==='dxgi')need(exact(e.pointerOnly)===BigInt(pointers),'Pointer accounting differs');else need(e.pointerOnly===null&&exact(e.captureSamples)===BigInt(frames),'GDI sample accounting differs');return{begin:b,baseline,first,frames,backend,output};
}
function checkPersistent64({inputRows,desktopRows,sdkRows,kernelRows,native,pageReceipt,hostResult,hostOwned,inputOwned}){
 const input=verifyInput(inputRows),d=desktop(desktopRows,input),s=input.begin;
 need(natural(hostOwned)&&natural(inputOwned)&&hostOwned.pid===s.target_pid&&hostOwned.creationFileTime===s.process_incarnation&&inputOwned.pid===s.injector_pid&&inputOwned.creationFileTime===s.injector_incarnation,'Held processes did not close naturally');
 need(hostResult.run_id===s.run_id&&hostResult.process_id===s.target_pid&&hostResult.process_incarnation===s.process_incarnation&&hostResult.normal_runtime_close===true&&hostResult.trace_complete===true&&hostResult.coordinate_witness_complete===true&&hostResult.compiled_sdk_version==='0.7.0-preview.4','Host/compiled SDK/collector close mismatch');
 const joined=trace.joinTraces([sdkRows,kernelRows]);need(joined.fullCausalEligible&&joined.identity.run_id===s.run_id&&joined.identity.process_id===s.target_pid&&joined.identity.process_incarnation===s.process_incarnation&&joined.identity.qpc_frequency===s.qpc_frequency,'Full traces have loss/fault/identity mismatch');
 need(native.schema==='soui.input-coordinate-native-witness/v1'&&native.run_id===s.run_id&&native.capture_complete===true&&native.attached===true&&native.detached_on_nc_destroy===true&&!native.detach_failed&&!native.overflow&&!native.clock_failed&&!native.tagged_coordinate_mismatch&&native.events_seen===native.events_stored&&native.rows.length===native.events_seen&&native.capacity===256,'Native witness incomplete');
 exact(native.child_hwnd,false);
 let prior=0n;native.rows.forEach((r,i)=>{const t=exact(r.qpc,false);need(r.ordinal===i+1&&t>=prior&&r.hwnd===native.child_hwnd&&r.message===512,'Native row gap/order/child mismatch');prior=t;});
 const start=exact(d.baseline.end,false);const cohort=native.rows.filter(r=>exact(r.qpc,false)>=start); // Complete post-zero lifetime, not selected tags.
 need(cohort.length===64,'Extra/tag0/missing callback in complete post-zero native cohort');
 const validXY=r=>r.client_y===100&&whole(r.client_x,100,163);need(!native.rows.some(r=>exact(r.qpc,false)<start&&validXY(r)),'Pre-arm coordinate collision prevents closed cohort');
 cohort.forEach((r,i)=>need(r.decoded_tag===i+1&&exact(r.extra_info)===(0x53400000n|BigInt(i+1))&&r.client_x===100+i&&r.client_y===100&&exact(r.qpc,false)>=exact(input.inputs[i].before_qpc,false),'Native tag/coordinate/FIFO mismatch; no tag0 filtering'));
 need(pageReceipt.schema==='soui.input-coordinate-page-receipt/v1'&&pageReceipt.run_id===s.run_id&&pageReceipt.accepted===true&&pageReceipt.capture_complete===true,'Page was not accepted and persisted by original collector');
 const wrapper=pageReceipt.snapshot,page=wrapper?.persistent_ack_report;need(wrapper.schema==='soui.input-coordinate-page-witness/v1'&&wrapper.run_id===s.run_id&&!wrapper.overflow&&!wrapper.invalid&&wrapper.first_invalid===null&&wrapper.rows?.length===64,'Original page transport container incomplete');
 need(page?.schema==='soui.burst-page/v2'&&page.protocol==='persistent-ack64'&&page.run_id===s.run_id&&page.nonce===s.nonce&&page.kind==='pointer_move'&&page.latest===64&&page.observed===64&&page.rejected===0&&page.invalid===false&&page.callback_count===64&&page.dom_ledger_complete===true&&page.dom_ledger_overflow===false&&page.dom_ledger.length===64&&page.mask_hex==='ffffffffffffffff'&&page.first_fault===null&&page.device_pixel_ratio===1&&eq(page.marker_region,[8,8,64,52])&&eq(page.content_probe,[10,42]),'Complete64 DOM report mismatch');
 let js=-1;page.dom_ledger.forEach((r,i)=>{need(r.length===9&&r[0]===i+1&&Number.isFinite(r[1])&&r[1]>=js&&r[2]===100+i&&r[3]===100&&r[4]===i+1&&r[5]===i&&r[6]===i+1&&r[7]===1&&r[8]===1&&wrapper.rows[i].x===r[2]&&wrapper.rows[i].y===r[3],'DOM extra/duplicate/unknown/untrusted/coordinate/order mismatch');js=r[1];});
 const t1s=joined.streams[0].points.filter(x=>x.point===1&&x.value==='1'&&exact(x.qpc,false)>=start);need(t1s.length===64,'Extra/untagged SDK pointer T1 or missing T1');
 need(t1s.every(x=>x.producer_slot===t1s[0].producer_slot&&x.thread_id===t1s[0].thread_id),'SDK T1 UI owner changed');
 const generations=new Set(),ids=new Set(),rows=[];
 t1s.forEach((t,i)=>{need(t.related_id===String(i+1)&&t.input_first===t.input_last&&t.input_first!=='0'&&exact(t.qpc,false)>=exact(cohort[i].qpc,false)&&exact(t.qpc,false)>=exact(input.inputs[i].before_qpc,false),'T1 lacks unique native tag/order relation');const g=gen(t);generations.add(g);need(!ids.has(g+'/'+t.input_first),'Duplicate SDK input identity');ids.add(g+'/'+t.input_first);const positive=d.first[i],q=exact(t.qpc,false),a=exact(positive.beginQpc,false),b=exact(positive.endQpc,false);need(b>=q,'Capture predates SDK T1');rows.push({sequence:i+1,nativeQpc:cohort[i].qpc,inputId:t.input_first,generation:g,t1Qpc:t.qpc,firstPositiveCapture:positive,captureIntervalAfterT1Ms:{lower:ms(a>q?a-q:0n,input.frequency),upper:ms(b-q,input.frequency)},sendInputCallToCaptureUpperMs:ms(b-exact(input.inputs[i].before_qpc,false),input.frequency)});});
 need(generations.size===1,'Document/view/geometry changed');
 return{schema:'soui.persistent-ack64-analysis/v2',run_id:s.run_id,strictCohortAndMaskAccepted:true,count:64,frequencyHz:125,captureBackend:d.backend,captureSamples:d.frames,independentPresentedFrames:null,rows,t1ToFirstPositiveCaptureUpperMs:distribution(rows.map(x=>x.captureIntervalAfterT1Ms.upper)),endpoint:'First positive software capture interval; capture end is upper observation time, not physical first visibility',cohort:'All native callbacks from pre-input zero capture end through normal lifetime close; no tag0/extra filtering; pre-arm coordinate collision rejected',nativeDomRelation:'Exact complete same-order native tags/unique coordinates and DOM coordinates; not temporal-nearest pairing',physicalT8:null,physicalInputLatency:null,performanceGate:false,releaseReady:false};
}
module.exports={decode,verifyInput,desktop}; // R39 exposes no legacy point-cohort checker.
