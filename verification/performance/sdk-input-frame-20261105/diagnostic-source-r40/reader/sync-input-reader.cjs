'use strict';
// Exact synchronous SDK context receipts, not nearest-time/coordinate pairing.
const trace=require('./input-frame-trace.cjs'),wire=require('./persistent-ack-reader.cjs');
const cells=require('./cells.cjs');
const {insist:need,exact,whole,milliseconds:ms,distribution}=trace;
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const tuple=e=>[e.view_id,e.view_generation,e.document_generation,e.geometry_generation].join('/');
const key=e=>tuple(e)+'/'+e.input_first;
function natural(r){return r&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed&&r.terminal?.reason==='completed'&&r.terminal.rootExited&&r.terminal.rootExitCode===0&&r.terminal.activeBeforeCleanup===0&&r.terminal.activeProcesses===0&&!r.terminal.terminatedOwnedJob&&r.terminal.limitTerminatedProcesses===0;}
function checkExpectedSdkVersion(hostResult,expectedSdkVersion){
 need(typeof expectedSdkVersion==='string'&&['0.6.0','0.7.0-preview.4'].includes(expectedSdkVersion),'An explicit frozen expectedSdkVersion is required');
 need(hostResult&&hostResult.compiled_sdk_version===expectedSdkVersion,'Host compiled SDK version differs from the frozen expectedSdkVersion');
 return true;
}
function checkNativeCells({native,summary,pageReceipt,run}){
 need(native.schema==='soui.input-coordinate-native-witness/v1'&&native.run_id===run&&native.capture_complete===true&&native.attached===true&&native.detached_on_nc_destroy===true&&native.overflow===false&&native.clock_failed===false&&native.detach_failed===false&&native.capacity===256&&native.events_seen===native.events_stored&&native.rows?.length===native.events_stored,'Original full native ledger incomplete');
 exact(native.child_hwnd,false);let prior=0n,legacyMismatch=false;const tags=new Map();
 native.rows.forEach((r,i)=>{const q=exact(r.qpc,false),extra=exact(r.extra_info),decoded=(extra&0xfff00000n)===0x53400000n?Number(extra&0xfffffn):0;
  need(r.ordinal===i+1&&q>=prior&&r.message===512&&r.hwnd===native.child_hwnd&&Number.isSafeInteger(r.client_x)&&Number.isSafeInteger(r.client_y)&&r.decoded_tag===decoded,'Raw native row/order/tag decode differs');prior=q;
  if(decoded){legacyMismatch=legacyMismatch||decoded>64||r.client_x!==100+(decoded-1)%1000||r.client_y!==100+Math.floor((decoded-1)/1000);
   need(whole(decoded,1,64)&&decoded===tags.size+1&&!tags.has(decoded)&&extra===(0x53400000n|BigInt(decoded))&&cells.sequence(r.client_x,r.client_y)===decoded,'Native tag order or declared cell mismatch');tags.set(decoded,r);}
 });
 need(tags.size===64&&native.tagged_events_seen===64,'All64 original native tags required; untagged rows remain raw');
 need(native.tagged_coordinate_mismatch===legacyMismatch,'Legacy adjacent-grid mismatch flag was altered');
 const observed=legacyMismatch||pageReceipt.snapshot.invalid;
 need(summary?.schema==='soui.input-coordinate-witness-summary/v1'&&summary.run_id===run&&summary.capture_complete===true&&summary.coordinate_mismatch_observed===observed&&summary.coordinate_mismatch_is_product_bug===null&&summary.diagnostic_status===(observed?'coordinate-mismatch-observed':'no-coordinate-mismatch-in-captured-snapshot'),'Original legacy summary was altered or incomplete');
 return{tags,legacyMismatch};
}
function checkCorrespondence({pageReceipt,sync,joined,run,frequency,native,summary}){
 need(pageReceipt.schema==='soui.input-coordinate-page-receipt/v1'&&pageReceipt.run_id===run&&pageReceipt.accepted===true&&pageReceipt.capture_complete===true,'Page report was not persisted by the actual Host');
 const wrapper=pageReceipt.snapshot,page=wrapper?.sync_input_report;
 need(wrapper.schema==='soui.input-coordinate-page-witness/v1'&&wrapper.run_id===run&&wrapper.overflow===false&&wrapper.invalid===false&&wrapper.first_invalid===null,'Original transport wrapper is invalid');
 need(page?.schema==='soui.burst-page/v5'&&page.protocol==='spatial-cell-feedback64/v5'&&cells.matchesGeometry(page.cell_geometry)&&page.mode==='native-dispatch-scope-required'&&page.absent_is_trusted_remains_unknown===true&&page.arbitrary_page_native_origin_certified===false&&page.run_id===run&&page.kind==='pointer_move'&&page.device_pixel_ratio===1&&page.binding_ready===true&&page.initial_zero_drawn===true&&page.invalid===false&&page.rejected===0&&page.overflow===false&&page.first_fault===null&&page.observed===64&&page.latest===64&&page.mask_hex==='ffffffffffffffff'&&equal(page.marker_region,[8,8,64,52])&&equal(page.content_probe,[10,42]),'Incomplete/invalid v5 spatial-cell page; old v4 rejected');
 const nativeInfo=checkNativeCells({native,summary,pageReceipt,run});
 need(page.dom_ledger_complete===true&&page.calls_complete===true&&Array.isArray(page.dom_ledger)&&page.dom_ledger.length===page.callback_count&&page.callback_count>=64&&page.callback_count<=128&&Array.isArray(page.getter_calls)&&page.getter_calls.length===page.getter_call_count&&page.getter_call_count===page.callback_count+2&&page.getter_call_count<=128&&wrapper.rows?.length===page.callback_count,'Page callback/call cardinality or bounds differ');
 need(page.listener_closed===true&&typeof page.listener_close_reason==='string'&&page.listener_closed_after_callback===page.callback_count&&page.listener_closed_after_getter_call===page.getter_call_count,'DOM measurement window was not closed before its snapshot');
 need(page.listener_opened===true&&page.listener_opened_after_getter_call===1,'DOM admission began before authenticated cold0');
 need(sync.schema==='soui.sync-input-context-witness/v1'&&sync.run_id===run&&sync.overflow===false&&sync.attempted===sync.stored&&sync.stored===page.getter_call_count&&sync.rows?.length===sync.stored&&sync.capacity===256,'Native getter history is missing, filtered or overflowed');
 need(sync.installed===true&&sync.listener_ready===true&&sync.page_ready===true&&sync.cold_seen===true&&sync.deferred_done===true&&sync.failed===false&&sync.error_code===0&&sync.collector_complete===true&&sync.normal_runtime_barrier===true&&sync.finalized_while_armed===false&&sync.tls_module_is_exe===true&&sync.recorder_module_is_exe===true,'Native installation/negative/lifetime/same-image proof incomplete');
 need(sync.binding?.receipt===sync.binding?.listener_receipt&&exact(sync.binding.receipt,false)>0n&&sync.binding.per_context_jsFunction===true&&sync.binding.process_global_binding_used===false&&whole(sync.binding.engine_thread_id,1),'Binding document/owner differs');
 const t1=new Map(),begins=new Map(),returns=new Map(),tagged=new Map();
 for(const e of joined.streams[0].points){
  if(e.input_first==='0'||e.input_first!==e.input_last)continue;
  if(e.point===1&&e.value==='1'){
   need(!t1.has(key(e)),'Duplicate native input identity');t1.set(key(e),e);
   if(e.related_id!=='0'){const tag=Number(exact(e.related_id,false));need(whole(tag,1,64)&&tag===tagged.size+1&&!tagged.has(tag),'Unexpected/duplicate/out-of-order injection tag');const raw=nativeInfo.tags.get(tag);need(raw&&exact(raw.qpc,false)<=exact(e.qpc,false),'T1 predates its tagged native receipt');tagged.set(tag,e);}
  }else if(e.point===3&&e.value==='1'){need(!begins.has(key(e)),'Duplicate handler begin');begins.set(key(e),e);}
  else if(e.point===4&&e.value==='1'&&e.why===0&&e.outcome===0){need(!returns.has(key(e)),'Duplicate normal handler return');returns.set(key(e),e);}
 }
 need(tagged.size===64,'All64 exact tagged T1 records required');
 const generations=new Set([...tagged.values()].map(tuple));need(generations.size===1,'Tagged document/view/geometry changed');
 const calls=new Map();let priorEnd=0n;
 for(let i=0;i<sync.rows.length;i++){
  const n=sync.rows[i],p=page.getter_calls[i],begin=exact(n.begin_qpc,false),end=exact(n.end_qpc,false);
  need(p.length===4&&p[0]===i+1&&whole(p[1],0,2)&&whole(p[2],0,2147483647)&&p[3]===1&&n.ordinal===i+1&&n.call_kind===p[1]&&n.returned_id===p[2]&&begin>=priorEnd&&end>=begin,'Native/page getter receipt/order mismatch');priorEnd=end;
  need(n.actual_thread_id===sync.binding.engine_thread_id&&whole(n.actual_thread_id,1)&&exact(n.actual_es,false)>0n&&n.authorized===true&&n.cookie_match===true&&n.context_stable===true&&n.bound_receipt===n.current_receipt&&n.bound_receipt===sync.binding.receipt&&n.fault_bits===0,'Getter owner/context authorization failed');
  let owner=null;const c=n.context;
  need(typeof n.context_present==='boolean'&&(n.context_present?c&&typeof c==='object':c===null),'Missing explicit TLS presence');
  if(n.context_present){
   const id=exact(c.input_first);need(exact(c.input_last)>=id,'Invalid TLS input range');
   const eligible=c.point===3&&c.value==='1'&&id>0n&&c.input_first===c.input_last&&id<=2147483647n&&['view_id','view_generation','document_generation','geometry_generation'].every(k=>exact(c[k])>0n);
   need(n.returned_id===(eligible?Number(id):0),'Stale, range or asynchronous scope returned an input ID');
   if(eligible){owner=t1.get(key(c));const b=begins.get(key(c)),r=returns.get(key(c));need(owner&&b&&r&&generations.has(tuple(c))&&exact(owner.qpc,false)<=exact(b.qpc,false)&&exact(b.qpc,false)<=begin&&end<=exact(r.qpc,false),'Getter does not lie inside its exact normal input handler');}
  }else need(n.returned_id===0,'No-context call returned a stale ID');
  calls.set(i+1,{native:n,page:p,t1:owner});
 }
 need(page.cold_outside?.completed===true&&page.cold_outside.returned_id===0&&page.cold_outside.getter_call===1&&page.getter_calls[0][1]===0,'Cold outside negative missing');
 need(page.deferred_audit?.done===true&&page.deferred_audit.returned_id===0&&page.deferred_audit.page_asserts_outside_scope===false,'Deferred result missing or assumed');
 const audit=calls.get(page.deferred_audit.getter_call);
 need(audit&&audit.native.call_kind===2&&audit.native.context_present===true&&audit.native.context.point===12&&audit.native.returned_id===0,'Deferred negative is not an actual Engine task');
 need(sync.deferred_control?.page_getter_call===page.deferred_audit.getter_call&&sync.deferred_control.returned_id===0&&sync.deferred_control.context?.point===12&&equal(sync.deferred_control.context,audit.native.context)&&exact(sync.deferred_control.begin_qpc,false)<=exact(audit.native.begin_qpc,false)&&exact(audit.native.end_qpc,false)<=exact(sync.deferred_control.end_qpc,false),'Deferred receipt lacks actual native Engine-task bracket');
 need(page.getter_calls.filter(c=>c[1]===0).length===1&&page.getter_calls.filter(c=>c[1]===2).length===1,'Duplicate/unknown control calls');
 const first=Array(64).fill(null),taggedDom=new Set(),dom=[],usedCalls=new Set([1,page.deferred_audit.getter_call]);let js=-1,repeated=0,outside=0,priorDomCall=1,firstCount=0;
 for(let i=0;i<page.dom_ledger.length;i++){
  const r=page.dom_ledger[i],entry=calls.get(r[1]);
  const trustEvidence=(r[8]===1&&r[9]==='boolean'&&r[10]===1)||(r[8]===-1&&r[9]==='undefined'&&r[10]===0);
  need(r.length===11&&r[0]===i+1&&entry&&entry.native.call_kind===1&&r[1]>priorDomCall&&!usedCalls.has(r[1])&&entry.native.returned_id===r[2]&&Number.isFinite(r[3])&&r[3]>=js&&Number.isFinite(r[4])&&Number.isFinite(r[5])&&trustEvidence&&wrapper.rows[i].x===r[4]&&wrapper.rows[i].y===r[5],'DOM callback receipt/coordinates/trust capability mismatch');js=r[3];priorDomCall=r[1];usedCalls.add(r[1]);
  const seq=cells.sequence(r[4],r[5]),slot=seq!==null;
  need(r[6]===seq,'Page coordinate sequence was fabricated');
  if(entry.t1&&entry.t1.related_id!=='0'&&slot&&Number(entry.t1.related_id)===seq)taggedDom.add(seq);
  if(!slot){need(r[7]===3,'Outside callback was hidden or used as a slot');outside++;}
  else if(first[seq-1]){need(r[7]===2,'Repeated callback was hidden or rewrote a slot');repeated++;}
  else {need(seq===firstCount+1,'First setter order must be exactly1..64');need(r[7]===1&&entry.t1&&entry.t1.related_id===String(seq),'First visible slot does not come from its exact injection-tagged input');const raw=nativeInfo.tags.get(seq);need(raw&&raw.client_x===r[4]&&raw.client_y===r[5],'First setter DOM/native actual coordinates differ');first[seq-1]={callback:r[0],call:r[1],inputId:r[2],t1:entry.t1,native:entry.native,actualX:r[4],actualY:r[5]};++firstCount;}
  dom.push({callback:r[0],getterCall:r[1],nativeInputId:r[2],coordinateSequence:seq,injectionTag:entry.t1?.related_id??null,contextEligible:entry.t1!==null,outcome:r[7],rawIsTrusted:r[8],isTrustedTypeof:r[9],isTrustedPropertyPresent:r[10],trustedFlagUnknown:r[8]===-1});
 }
 need(usedCalls.size===calls.size&&taggedDom.size===64&&first.every(Boolean)&&page.repeated_coordinates===repeated&&page.outside_coordinates===outside,'All raw calls/64 tagged DOM relations required');
 need(page.first_slot_input_ids?.length===64&&page.first_slot_calls?.length===64&&page.first_slot_callbacks?.length===64,'Missing actual first setter arrays');
 first.forEach((x,i)=>need(page.first_slot_input_ids[i]===x.inputId&&page.first_slot_calls[i]===x.call&&page.first_slot_callbacks[i]===x.callback,'First setter was overwritten or inferred'));
 const firstPositiveDom=dom.find(x=>x.nativeInputId>0);need(firstPositiveDom&&exact(audit.native.begin_qpc,false)>=exact(calls.get(firstPositiveDom.getterCall).native.end_qpc,false),'Deferred audit did not follow an actual positive callback');
 return{page,sync,first,tagged,dom,nativeInfo,rawDomCallbacks:dom.length,rawGetterCalls:calls.size,repeatedCoordinates:repeated,unknownScopeCallbacks:dom.filter(x=>!x.contextEligible).length,untaggedScopeCallbacks:dom.filter(x=>x.injectionTag==='0').length,frequency};
}
function checkSync64({inputRows,desktopRows,sdkRows,kernelRows,native,summary,sync,pageReceipt,hostResult,hostOwned,inputOwned,expectedSdkVersion}){
 checkExpectedSdkVersion(hostResult,expectedSdkVersion);
 const input=wire.verifyInput(inputRows),capture=wire.desktop(desktopRows,input),s=input.begin;
 need(natural(hostOwned)&&natural(inputOwned)&&hostOwned.pid===s.target_pid&&hostOwned.creationFileTime===s.process_incarnation&&inputOwned.pid===s.injector_pid&&inputOwned.creationFileTime===s.injector_incarnation,'Owned natural closure/held identity missing');
 need(hostResult.run_id===s.run_id&&hostResult.process_id===s.target_pid&&hostResult.process_incarnation===s.process_incarnation&&hostResult.normal_runtime_close===true&&hostResult.trace_complete===true&&hostResult.coordinate_witness_complete===true,'Host/compiled SDK/trace close mismatch');
 const joined=trace.joinTraces([sdkRows,kernelRows]);need(joined.fullCausalEligible&&joined.identity.run_id===s.run_id&&joined.identity.process_id===s.target_pid&&joined.identity.process_incarnation===s.process_incarnation&&joined.identity.qpc_frequency===s.qpc_frequency,'Full trace loss/fault/identity mismatch');
 need(sync.identity?.run_id===s.run_id&&sync.identity.process_id===s.target_pid&&sync.identity.process_incarnation===s.process_incarnation&&sync.identity.qpc_frequency===s.qpc_frequency&&sync.identity.run_id_high===BigInt('0x'+s.run_id.slice(0,16)).toString()&&sync.identity.run_id_low===BigInt('0x'+s.run_id.slice(16)).toString()&&sync.json_byte_budget===540672&&sync.concurrent_call===false,'Actual recorder/witness run/PID/birth/QPF or bounded payload differs');
 const relation=checkCorrespondence({pageReceipt,sync,joined,run:s.run_id,frequency:input.frequency,native,summary}),nativeTags=relation.nativeInfo.tags;
 const rows=relation.first.map((first,i)=>{const t1=first.t1,raw=nativeTags.get(i+1),positive=capture.first[i],t=exact(t1.qpc,false),a=exact(positive.beginQpc,false),b=exact(positive.endQpc,false);
  need(exact(raw.qpc,false)>=exact(input.inputs[i].before_qpc,false)&&exact(raw.qpc,false)<=t&&b>=exact(first.native.end_qpc,false),'Tagged native/callback/capture ordering mismatch');
  const target=cells.center(i+1);return{sequence:i+1,inputId:t1.input_first,generation:tuple(t1),nativeMessageQpc:raw.qpc,t1Qpc:t1.qpc,cellBounds:cells.bounds(i+1),requestedCenter:target,actualNativeXY:[raw.client_x,raw.client_y],actualFirstSetterDomXY:[first.actualX,first.actualY],requestedCenterDelta:[raw.client_x-target[0],raw.client_y-target[1]],exactRequestedCenterReached:raw.client_x===target[0]&&raw.client_y===target[1],firstDomCallback:first.callback,firstGetterCall:first.call,firstPositiveCapture:positive,captureIntervalAfterT1Ms:{lower:ms(a>t?a-t:0n,input.frequency),upper:ms(b-t,input.frequency)}};
 });
 return{schema:'soui.spatial-cell-feedback-analysis/v5',protocol:'spatial-cell-feedback64/v5',cellGeometry:cells.geometry,legacyAdjacentMismatchObserved:relation.nativeInfo.legacyMismatch,oldPrecisePointTestRepaired:false,equivalentToOldAdjacentTrajectory:false,mode:'native-dispatch-scope-required',run_id:s.run_id,exactInputContextAndMaskAccepted:true,count:64,frequencyHz:125,captureSamples:capture.frames,rows,
  rawNativeMouseCallbacks:native.rows.length,rawDomCallbacks:relation.rawDomCallbacks,rawGetterCalls:relation.rawGetterCalls,repeatedCoordinates:relation.repeatedCoordinates,unknownScopeCallbacks:relation.unknownScopeCallbacks,untaggedScopeCallbacks:relation.untaggedScopeCallbacks,
  allDomCallbacks:relation.dom,unknownIsTrustedCallbacks:relation.dom.filter(r=>r.trustedFlagUnknown).length,arbitraryPageNativeOriginCertified:false,t1ToFirstPositiveCaptureUpperMs:distribution(rows.map(r=>r.captureIntervalAfterT1Ms.upper)),independentPresentedFrames:null,
  endpoint:'First valid positive software capture interval; end is an upper observation time, not exact first visibility',domAdmissionWindow:'Authenticated binding through explicit listener removal before snapshot; all admitted callbacks retained. Later UI/native/trace events remain raw and outside this DOM window.',fullCausalComplete:false,physicalT8:null,performanceGate:false,releaseReady:false};
}
module.exports={checkNativeCells,checkCorrespondence,checkExpectedSdkVersion,checkSync64};
