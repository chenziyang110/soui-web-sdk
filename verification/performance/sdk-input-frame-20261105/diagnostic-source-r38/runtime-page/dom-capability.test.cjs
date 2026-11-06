'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {checkCorrespondence}=require('./sync-input-reader.cjs');
const run='123456789abcdef0123456789abcdef0';
// These contracts call the actual correspondence checker. Synthetic boundary
// receipts do not certify checkTrace(), a native callback, TLS or the desktop.
function readerFixture(unknown=false){
 const points=[],rows=[],calls=[],dom=[],xy=[],firstIds=Array(64).fill(0),firstCalls=Array(64).fill(0),firstDom=Array(64).fill(0);
 const context=(id,point=3)=>({point,value:point===3?'1':'0',input_first:String(id),input_last:String(id),view_id:'1',view_generation:'2',document_generation:'3',geometry_generation:'4',qpc:'0',thread_id:0,producer_slot:0,related_id:'0'});
 const task=context(0,12);
 function stages(id,tag,t){for(const [point,qpc]of [[1,t],[3,t+1],[4,t+80]])points.push({...context(id,point),value:'1',related_id:point===1?String(tag):'0',qpc:String(qpc),why:0,outcome:0});}
 function call(kind,id,begin,c){const ordinal=rows.length+1;calls.push([ordinal,kind,id,1]);rows.push({ordinal,call_kind:kind,begin_qpc:String(begin),end_qpc:String(begin+1),actual_thread_id:7,actual_es:'500',bound_receipt:'8',current_receipt:'8',authorized:true,cookie_match:true,context_stable:true,context_present:c!==null,context:c,returned_id:id,fault_bits:0});return ordinal;}
 function move(seq,id,begin,repeat,c){const ordinal=call(1,id,begin,c===undefined?context(id):c),callback=dom.length+1;dom.push([callback,ordinal,id,callback,99+seq,100,seq,repeat?2:1,-1,'undefined',0]);xy.push({x:99+seq,y:100});if(!repeat){firstIds[seq-1]=id;firstCalls[seq-1]=ordinal;firstDom[seq-1]=callback;}}
 call(0,0,50,task);stages(101,1,1000);move(1,101,1002,false);move(1,101,1004,true);
 const auditCall=call(2,0,1040,task);
 stages(900,0,1050);move(1,unknown?0:900,1052,true,unknown?null:context(900));
 for(let seq=2;seq<=64;seq++){const t=1000+(seq-1)*100;stages(100+seq,seq,t);move(seq,100+seq,t+2,false);}
 const page={schema:'soui.burst-page/v4',protocol:'sync-input-ack64/v4',mode:'native-dispatch-scope-required',absent_is_trusted_remains_unknown:true,arbitrary_page_native_origin_certified:false,run_id:run,kind:'pointer_move',device_pixel_ratio:1,binding_ready:true,initial_zero_drawn:true,invalid:false,rejected:0,overflow:false,first_fault:null,observed:64,latest:64,mask_hex:'ffffffffffffffff',marker_region:[8,8,64,52],content_probe:[10,42],dom_ledger_complete:true,calls_complete:true,dom_ledger:dom,callback_count:dom.length,getter_calls:calls,getter_call_count:calls.length,cold_outside:{completed:true,returned_id:0,getter_call:1},deferred_audit:{done:true,returned_id:0,getter_call:auditCall,page_asserts_outside_scope:false},repeated_coordinates:2,outside_coordinates:0,first_slot_input_ids:firstIds,first_slot_calls:firstCalls,first_slot_callbacks:firstDom};
 Object.assign(page,{listener_opened:true,listener_opened_after_getter_call:1,listener_closed:true,listener_close_reason:'all-slots-and-native-audit',listener_closed_after_callback:dom.length,listener_closed_after_getter_call:calls.length});
 const sync={schema:'soui.sync-input-context-witness/v1',run_id:run,overflow:false,attempted:rows.length,stored:rows.length,capacity:256,rows,installed:true,listener_ready:true,page_ready:true,cold_seen:true,deferred_done:true,failed:false,error_code:0,collector_complete:true,normal_runtime_barrier:true,finalized_while_armed:false,tls_module_is_exe:true,recorder_module_is_exe:true,binding:{receipt:'8',listener_receipt:'8',engine_thread_id:7,per_context_jsFunction:true,process_global_binding_used:false},deferred_control:{page_getter_call:auditCall,returned_id:0,context:task,begin_qpc:'1039',end_qpc:'1042'}};
 return{run,frequency:10000000n,sync,joined:{streams:[{points}]},pageReceipt:{schema:'soui.input-coordinate-page-receipt/v1',run_id:run,accepted:true,capture_complete:true,snapshot:{schema:'soui.input-coordinate-page-witness/v1',run_id:run,overflow:false,invalid:false,first_invalid:null,rows:xy,sync_input_report:page}}};
}

const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const pageSource=fs.readFileSync(path.join(__dirname,'burst-page.js'),'utf8');
function pageFixture(){
 let listener=null,id=0,clock=1;const packets=[];
 const canvas={getContext(){return{fillRect(){},fillText(){}};},addEventListener(type,fn){assert.equal(type,'mousemove');listener=fn;},removeEventListener(type,fn){assert.equal(fn,listener);listener=null;}};
 const w={souiBurstConfig:{run,kind:'pointer_move'},devicePixelRatio:1,performance:{now(){return clock++;}},setTimeout(){return 1;},clearTimeout(){},__souiSyncInputId(){if(id==='throw')throw Error('model getter failure');return id;},
 __souiNativePostMessage(raw){const p=JSON.parse(raw);packets.push(p);if(p.method==='bridge.bootstrap'){assert.equal(p.requestId,'sync-input-witness-ready:'+run);return JSON.stringify({requestId:p.requestId,ok:true,result:{sessionId:'actual-session',origin:'app://test',token:'ephemeral'}});}assert.equal(p.method,'benchmark.coordinateWitness');return JSON.stringify({requestId:p.requestId,ok:true,result:{accepted:true,persisted:false}});}};
 vm.runInNewContext(pageSource,{window:w,document:{getElementById(){return canvas;}},requestAnimationFrame(){}},{timeout:1000});
 w.__souiNativeDispatchEvent({type:'soui.sync-input-witness.installed',run_id:run,request_id:'sync-input-witness-ready:'+run});
 return{packets,fire(seq,nativeId,trust){if(arguments.length<3)trust='absent';id=nativeId;const e={clientX:99+seq,clientY:100};if(trust==='throws')Object.defineProperty(e,'isTrusted',{get(){throw Error('model trust access failure');}});else if(trust!=='absent')e.isTrusted=trust;listener(e);},audit(){id=0;w.__souiSyncDeferredAudit();},result(){return JSON.parse(JSON.stringify(w.souiBurstResult()));}};
}
test('v4 missing property preserves unknown with nonzero native IDs and bounded report',()=>{
 const f=pageFixture();for(let i=1;i<=64;i++)f.fire(i,100+i);f.audit();const r=f.result();
 assert.equal(r.schema,'soui.burst-page/v4');assert.equal(r.mode,'native-dispatch-scope-required');assert.equal(r.invalid,false);assert.equal(r.observed,64);assert.deepEqual(r.dom_ledger[0].slice(8),[-1,'undefined',0]);assert.equal(r.first_slot_input_ids[0],101);
 const p=f.packets.find(x=>x.method==='benchmark.coordinateWitness');assert(p);assert(Buffer.byteLength(p.params.snapshot_json)<=24576);assert(Buffer.byteLength(JSON.stringify(p.params))<=32768);assert.equal(r.absent_is_trusted_remains_unknown,true);assert.equal(r.arbitrary_page_native_origin_certified,false);
});
test('false, null, string, present undefined and trust access exception are all rejected',()=>{
 for(const value of [false,null,'true',undefined,'throws']){const f=pageFixture();f.fire(1,101,value);const r=f.result();assert.equal(r.invalid,true);assert.equal(r.observed,0);assert.equal(r.first_slot_input_ids[0],0);assert.equal(r.dom_ledger[0][7],4);}
});
test('synthetic no-context ID0 or failed getter cannot first-set a visible slot',()=>{
 for(const id of [0,'throw']){const f=pageFixture();f.fire(1,id);const r=f.result();assert.equal(r.invalid,true);assert.equal(r.observed,0);assert.equal(r.mask_hex,'0000000000000000');assert.equal(r.first_slot_calls[0],0);assert.equal(r.dom_ledger.length,1);assert.equal(r.dom_ledger[0][2],0);}
});
test('actual correspondence checker accepts v4 unknown only with complete existing native evidence',()=>{
 const r=checkCorrespondence(readerFixture());assert.equal(r.first.length,64);assert.equal(r.rawDomCallbacks,66);assert.equal(r.dom.every(x=>x.rawIsTrusted===-1&&x.trustedFlagUnknown===true),true);assert.equal(r.unknownScopeCallbacks,0);
});
test('v4 compatibility cannot bypass missing T1, wrong tag or overwritten first setter',()=>{
 const missing=readerFixture();missing.joined.streams[0].points=missing.joined.streams[0].points.filter(x=>!(x.point===1&&x.input_first==='101'));assert.throws(()=>checkCorrespondence(missing),/All64/);
 const tag=readerFixture();tag.joined.streams[0].points.find(x=>x.point===1&&x.input_first==='101').related_id='65';assert.throws(()=>checkCorrespondence(tag),/tag/);
 const setter=readerFixture();setter.pageReceipt.snapshot.sync_input_report.first_slot_input_ids[0]=900;assert.throws(()=>checkCorrespondence(setter),/First setter/);
 const zero=readerFixture(),p=zero.pageReceipt.snapshot.sync_input_report,n=zero.sync.rows[1];n.context_present=false;n.context=null;n.returned_id=0;p.getter_calls[1][2]=0;p.dom_ledger[0][2]=0;assert.throws(()=>checkCorrespondence(zero),/First visible slot/);
});
test('reader rejects v3 and non-missing trust values even behind a valid native ID',()=>{
 const old=readerFixture();old.pageReceipt.snapshot.sync_input_report.schema='soui.burst-page/v3';assert.throws(()=>checkCorrespondence(old),/v4/);
 for(const fields of [[0,'boolean',1],[-1,'object',1],[-1,'string',1],[-1,'undefined',1],[-1,'exception',-1]]){const f=readerFixture();f.pageReceipt.snapshot.sync_input_report.dom_ledger[0].splice(8,3,...fields);assert.throws(()=>checkCorrespondence(f),/trust capability/);}
});
