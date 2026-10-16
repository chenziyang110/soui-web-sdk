'use strict';
const zero='0',U64=0xffffffffffffffffn;
class OwnerError extends Error{constructor(code,failed=false){super(code);this.failed=failed;}}
const need=(ok,code,failed=false)=>{if(!ok)throw new OwnerError(code,failed);};
function count(value){need(typeof value==='string'&&/^(?:0|[1-9]\d{0,19})$/.test(value),'missing-owner-integer');const n=BigInt(value);need(n<=U64,'owner-integer-overflow');return n;}
function observation(value,history,key){
  const o=value?.observation;
  need(o?.state==='ready','owner-observation-'+(o?.state||'missing'));
  const epoch=count(o.owner_epoch),begin=count(o.begin_monotonic_us),end=count(o.end_monotonic_us);
  need(epoch>0n&&begin>0n&&end>=begin&&o.clock_invalid===false&&o.counter_saturated===false,'owner-clock-or-counter-unobserved');
  const identity=key+'|'+epoch,prior=history.get(identity);
  need(!prior||begin>=prior.begin&&end>=prior.end,'owner-clock-reversed',true);
  history.set(identity,{begin,end});return epoch;
}
function bounded(value,name,limit){need(count(value[name])<=BigInt(limit),'owner-cap-exceeded-'+name,true);}
function exact(value,name,limit){need(count(value[name])===BigInt(limit),'owner-source-cap-differs-'+name,true);}
function empty(value,names){for(const name of names)need(count(value[name])===0n,'owner-drain-incomplete-'+name,true);}
function queue(value,history,key,retired=false){
  if(retired&&value?.observation?.state==='not_started')return {state:'not_started',scope:'stopped-host-worker-not-a-zero-snapshot'};
  observation(value,history,key);
  need(value.has_capacity_bound===true,'queue-cap-unobserved');exact(value,'capacity_bound',580);
  bounded(value,'queued',580);bounded(value,'high_water',580);bounded(value,'executing',1);
  if(retired)empty(value,['queued','executing']);
  return {state:'ready'};
}
function tasks(value,history,key,retired){
  observation(value,history,key);
  for(const [name,limit] of Object.entries({worker_limit:2,inflight_limit:32,subscription_limit:128,store_limit:1024}))exact(value,name,limit);
  for(const [name,limit] of Object.entries({worker_slots:2,active_handlers:2,inflight:32,queued:32,inflight_high_water:32,queued_high_water:32,subscriptions:128,subscription_high_water:128,store_entries:1024}))bounded(value,name,limit);
  need(count(value.active_handlers)<=count(value.worker_slots),'active-handlers-exceed-workers',true);
  for(const name of ['terminal_entries','queued_entries','running_entries','timeout_observed_entries','cancel_requested_entries'])
    need(count(value[name])<=count(value.store_entries),'task-store-category-exceeds-store',true);
  need(['terminal_entries','queued_entries','running_entries','timeout_observed_entries','cancel_requested_entries']
    .reduce((total,name)=>total+count(value[name]),0n)===count(value.store_entries),'task-store-state-partition-differs',true);
  for(const [size,capacity] of [['store_string_size_bytes','store_string_capacity_bytes'],['result_string_size_bytes','result_string_capacity_bytes']])
    need(count(value[size])<=count(value[capacity]),'owned-string-size-exceeds-capacity',true);
  // These are observed counts, not independently budgeted byte/heap caches.
  for(const name of ['owner_entries','operation_entries','result_cache_entries'])count(value[name]);
  if(retired)empty(value,['active_handlers','inflight','queued','subscriptions','queued_entries','running_entries','timeout_observed_entries','cancel_requested_entries']);
}
function requests(value,history,key,retired){
  observation(value,history,key);
  for(const [name,limit] of [['ordinary',256],['control',128],['renewal',1]]){
    const lane=value[name];exact(lane,'limit',limit);bounded(lane,'outstanding',limit);bounded(lane,'high_water',limit);
    const acquired=count(lane.acquired_total),released=count(lane.released_total);
    need(acquired>=released&&acquired-released===count(lane.outstanding),'request-lane-accounting-differs',true);
    if(retired)empty(lane,['outstanding']);
  }
}
function permissions(value,history,key,retired){
  observation(value,history,key);need(value.has_scope_capacity_bound===true,'permission-cap-unobserved');
  for(const [name,limit] of Object.entries({stored_scope_limit:128,committed_alias_limit:256,renewal_alias_transient_limit:257,module_scope_limit:512}))exact(value,name,limit);
  need(count(value.stored_scope_owners)+count(value.pending_scope_reservations)<=128n,'scope-cap-exceeded',true);
  need(count(value.bearer_alias_entries)+count(value.pending_alias_reservations)<=256n,'alias-transient-cap-exceeded',true);
  bounded(value,'bearer_alias_high_water',257);bounded(value,'module_scope_slots_used',512);
  if(retired)empty(value,['stored_scope_owners','bearer_alias_entries','pending_scope_reservations','pending_alias_reservations']);
}
function gui(value,history,key,retired){
  if(value?.observation?.state==='not_started')return {state:'not_started',scope:'optional-gui-coordinator-not-instantiated'};
  observation(value,history,key);exact(value,'context_limit',16);exact(value,'effect_queue_limit',16);
  for(const name of ['context_slots_used','pending_live_contexts','retained_weak_slots','queued_effects','effect_queue_high_water'])bounded(value,name,16);
  if(retired){
    need(value.closed===true&&value.owner_poll_active===false,'gui-owner-not-closed',true);
    empty(value,['context_slots_used','pending_live_contexts','queued_effects','effect_borrows']);
  }
  return {state:'ready'};
}
function analyzeOwners(records){
  const history=new Map(),processHistory=new Map(),retiredEpochs=new Map();
  let samples=0,retiredOwners=0,processTerminal=null,processEpoch=null;const optional=[],lifetimes=new Map();
  try{
    for(const row of records){
      if(row.event==='step.complete'&&row.action==='mount'){
        const key=count(String(row.window)).toString();need(!lifetimes.has(key),'owner-window-mounted-twice',true);lifetimes.set(key,new Map());
      }
      if(row.event==='sample'&&row.phase!=='process'){
        const native=row.nativeSdk,process=native?.process;
        need(native?.producer==='native-sdk-owner'&&native.processStatus==='ready'&&process?.scope==='process','process-owner-unobserved');
        queue(process.engine_fifo,processHistory,'process-engine');
        const epoch=count(process.engine_fifo.observation.owner_epoch).toString();
        if(processEpoch===null)processEpoch=epoch;else need(processEpoch===epoch,'global-engine-owner-epoch-changed',true);
        bounded(process,'mounted_hosts',3);
        if(row.phase==='runtime-closed'){
          need(process.terminal_snapshot===true&&process.retired===true&&process.initialized===false,'process-terminal-unobserved');
          empty(process,['mounted_hosts']);empty(process.engine_fifo,['queued','executing']);
          if(processTerminal)need(JSON.stringify(process)===JSON.stringify(processTerminal),'first-process-terminal-overwritten',true);
          else processTerminal=process;
        }
        need(Array.isArray(native.views),'native-owner-array-unobserved');
        const windows=native.views.map(view=>count(view.window).toString());
        need(new Set(windows).size===windows.length&&JSON.stringify([...windows].sort())===JSON.stringify([...lifetimes.keys()].sort()),
          'current-live-view-owner-roster-differs',true);
        for(const view of native.views)checkView(view,false);
        ++samples;
      }
      if(row.event==='step.complete'&&row.action==='close'){
        need(row.retiredOwner?.hostClosed===true&&row.retiredOwner.ownerAlive===true&&
          row.retiredOwner.lifetime==='after-host-close-before-owner-destruction','retired-owner-unobserved');
        need(count(row.retiredOwner.window)===count(String(row.window)),'retired-owner-window-identity-differs',true);
        checkView(row.retiredOwner,true);lifetimes.delete(String(row.window));++retiredOwners;
      }
    }
    need(samples>0&&retiredOwners>0&&processTerminal,'complete-owner-terminal-evidence-missing');
    return {schema:'soui.long-native-owner-capacity/v1',capacityGate:'passed',terminalDrainGate:'passed',
      samples,retiredOwners,processTerminal,optionalGui:optional,
      scope:'Published native component capacities and observed contractual drain; independent owner locks',
      cacheBudgetGate:'not-evaluated',cacheGaps:['Task owner/operation/result-cache byte bounds','allocator/container/external-borrow bytes','Blink/V8/Skia and other third-party caches'],
      notRequiredZero:['retired Task worker_slots/store history','module_scope_slots_used with externally held revoked owners','GUI retained weak slots'],failure:null};
  }catch(error){return {schema:'soui.long-native-owner-capacity/v1',capacityGate:error.failed?'failed':'not-evaluated',
    terminalDrainGate:error.failed?'failed':'not-evaluated',samples,retiredOwners,cacheBudgetGate:'not-evaluated',
    failure:error instanceof OwnerError?error.message:'invalid-owner-observation'};}
  function checkView(view,retired){
    need(view.bridgeStatus==='ready'&&view.adapterStatus===(retired?'not_started':'ready'),'native-view-owner-unobserved');
    const window=count(view.window).toString(),prefix='window-'+window,lifetime=lifetimes.get(window);
    need(lifetime&&view.ownerAlive===true&&view.hostClosed===retired,'current-view-lifetime-differs',true);
    tasks(view.bridge?.task_service,history,prefix+'-tasks',retired);
    permissions(view.bridge?.permission_store,history,prefix+'-permissions',retired);
    need(!(lifetime.has('gui')&&view.bridge?.gui_commands?.observation?.state==='not_started'),
      'previously-observed-gui-owner-now-unobserved');
    optional.push(gui(view.bridge?.gui_commands,history,prefix+'-gui',retired));
    const adapter=view.adapter;need(adapter?.process_engine_scope==='process','shared-engine-scope-differs',true);
    need(count(adapter.context_receipt_before)===count(adapter.context_receipt_after),'adapter-context-read-unstable');
    requests(adapter.requests,history,prefix+'-requests',retired);
    queue(adapter.bridge_worker_fifo,history,prefix+'-bridge-fifo',retired);
    if(retired)need(adapter.bridge_accepting===false,'retired-bridge-still-accepting',true);
    // Epochs are fixed separately for each actual view lifetime; components are not one atomic ledger.
    for(const [component,value] of [['tasks',view.bridge.task_service],['permissions',view.bridge.permission_store],
      ['gui',view.bridge.gui_commands],['requests',adapter.requests],['bridge-fifo',adapter.bridge_worker_fifo]]){
      if(value?.observation?.state!=='ready')continue;
      const actual=count(value.observation.owner_epoch).toString();
      if(lifetime.has(component))need(lifetime.get(component)===actual,'component-owner-epoch-changed-within-live-host',true);
      else lifetime.set(component,actual);
    }
    const epoch=view.bridge.task_service.observation.owner_epoch;
    const key=prefix+'|'+epoch;
    if(retiredEpochs.has(key))need(JSON.stringify(view)===JSON.stringify(retiredEpochs.get(key)),'retired-owner-snapshot-overwritten',true);
    if(retired)retiredEpochs.set(key,view);
  }
}
const privateScope='task-store-private-allocator-requests';
const requestFields=['current_bytes','high_water_bytes','current_allocations','high_water_allocations',
  'allocations_total','deallocations_total','failed_allocations_total','allocated_bytes_total','deallocated_bytes_total'];
const cumulativeFields=requestFields.slice(4),domainNames=['map_requests','private_string_requests','aggregate_requests'];
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
function privateBinding(result,subject,executablePin,logs={},inputsMatched=false){
  return {inputsMatched,inputBindingScope:'complete-acceptance-inputs',sourceSha256:result?.sourceSha256??null,
    payloadSha256:result?.payloadSha256??null,consumerBuildId:result?.consumerBuildId??null,
    rawLogSha256:result?.logSha256??null,ownedLogSha256:logs.ownedLogSha256??null,
    contextSha256:logs.contextSha256??null,subject:subject?{pid:subject.pid,creationFileTime:subject.creationFileTime}:null,
    executablePin:executablePin??null};
}
function privateBindingValid(binding){
  try{
    const pin=binding?.executablePin,sha=value=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value);
    return binding?.inputsMatched===true&&binding.inputBindingScope==='complete-acceptance-inputs'&&
      ['sourceSha256','payloadSha256','rawLogSha256','ownedLogSha256','contextSha256'].every(name=>sha(binding[name]))&&
      typeof binding.consumerBuildId==='string'&&binding.consumerBuildId.length>0&&
      Number.isSafeInteger(binding.subject?.pid)&&binding.subject.pid>0&&binding.subject.pid<=0xffffffff&&count(binding.subject.creationFileTime)>0n&&
      pin&&typeof pin.path==='string'&&/^(?:[A-Za-z]:[\\/]|\/)/.test(pin.path)&&
      /(?:^|[\\/])SouiAcceptance\.exe$/i.test(pin.path)&&Number.isSafeInteger(pin.bytes)&&pin.bytes>0&&sha(pin.sha256);
  }catch{return false;}
}
function privateStoragePoint(view){
  const raw=view?.taskStorePrivateStorage??null,getterStatus=view?.taskStorePrivateStorageStatus??null;
  const out={raw,getterStatus,state:'partial',reason:null,derived:null};
  try{
    need(raw&&typeof raw==='object'&&!Array.isArray(raw),'private-storage-missing');
    const required=['schemaVersion','scope','observation','coverage_mask','gap_mask','task_entries','operation_entries','scope_entries',
      ...domainNames,'privateStringAllocatorRequestsCovered','requestsIncludePrivatePreparationAndRetirement',
      'publicCopiesObserved','phasePartitionObserved','wholeHeapObserved','finalDisposalReceiptObserved',
      'cacheBudgetGate','configuration','budgets','quota','calibrationPin'];
    need(getterStatus!==null&&required.every(key=>Object.hasOwn(raw,key)),'private-storage-fields-missing');
    need(raw.schemaVersion===1&&raw.scope===privateScope,'private-storage-schema-or-scope',true);
    const o=raw.observation;
    need(o&&['state','owner_epoch','begin_monotonic_us','end_monotonic_us','counter_saturated','clock_invalid']
      .every(key=>Object.hasOwn(o,key)),'private-storage-fields-missing');
    need(o&&['ready','busy','disabled','unavailable','not_started','wrong_owner_thread'].includes(o.state)&&
      ['ready','busy','disabled','unavailable','not_started','wrong_owner_thread'].includes(getterStatus),
      'private-storage-status-shape',true);
    for(const key of ['counter_saturated','clock_invalid'])need(typeof o[key]==='boolean','private-storage-bool-'+key,true);
    const epoch=count(o.owner_epoch),begin=count(o.begin_monotonic_us),end=count(o.end_monotonic_us);
    const masks={coverage:count(raw.coverage_mask),gaps:count(raw.gap_mask)};
    const entries=['task_entries','operation_entries','scope_entries'].map(key=>count(raw[key]));
    const domains={};
    for(const name of domainNames){
      const value=raw[name];need(value&&typeof value==='object'&&!Array.isArray(value),'private-storage-domain-missing',true);
      need([...requestFields,'current_bytes_exact','current_allocations_exact'].every(key=>Object.hasOwn(value,key)),
        'private-storage-fields-missing');
      for(const key of ['current_bytes_exact','current_allocations_exact'])need(typeof value[key]==='boolean','private-storage-exact-shape',true);
      domains[name]=Object.fromEntries(requestFields.map(key=>[key,count(value[key])]));
    }
    for(const [key,expected] of Object.entries({privateStringAllocatorRequestsCovered:(masks.coverage&2n)!==0n,
      requestsIncludePrivatePreparationAndRetirement:true,publicCopiesObserved:false,phasePartitionObserved:false,
      wholeHeapObserved:false,finalDisposalReceiptObserved:false}))need(raw[key]===expected,'private-storage-scope-flags',true);
    need(raw.cacheBudgetGate==='not-evaluated'&&['configuration','budgets','quota','calibrationPin'].every(key=>raw[key]===null),
      'private-storage-unproven-budget',true);
    if(getterStatus==='ready')need(o.state==='ready','private-storage-status-differs',true);
    if(getterStatus!=='ready'||o.state!=='ready'){out.reason='private-storage-'+getterStatus;return out;}
    need(masks.coverage===7n&&masks.gaps===31n,'private-storage-mask-differs',true);
    if(o.counter_saturated||entries.includes(U64)||Object.values(domains).some(value=>Object.values(value).includes(U64))){out.reason='private-storage-saturated';return out;}
    if(o.clock_invalid||epoch===0n||begin===0n||end<begin){out.reason='private-storage-clock-unobserved';return out;}
    if(domainNames.some(name=>raw[name].current_bytes_exact!==true||raw[name].current_allocations_exact!==true)){
      out.reason='private-storage-inexact';return out;
    }
    for(const value of Object.values(domains)){
      need(value.allocations_total>=value.deallocations_total&&value.allocations_total-value.deallocations_total===value.current_allocations&&
        value.allocated_bytes_total>=value.deallocated_bytes_total&&value.allocated_bytes_total-value.deallocated_bytes_total===value.current_bytes&&
        value.high_water_bytes>=value.current_bytes&&value.high_water_allocations>=value.current_allocations,
        'private-storage-refund-accounting',true);
    }
    for(const key of ['current_bytes','current_allocations',...cumulativeFields])need(
      domains.aggregate_requests[key]===domains.map_requests[key]+domains.private_string_requests[key],
      'private-storage-aggregate-accounting',true);
    for(const key of ['high_water_bytes','high_water_allocations'])need(
      domains.aggregate_requests[key]>=domains.map_requests[key]&&domains.aggregate_requests[key]>=domains.private_string_requests[key]&&
      domains.aggregate_requests[key]<=domains.map_requests[key]+domains.private_string_requests[key],
      'private-storage-event-peak-accounting',true);
    out.state='ready';out.derived={ownerEpoch:epoch.toString(),beginMonotonicUs:begin.toString(),endMonotonicUs:end.toString(),
      domains:Object.fromEntries(domainNames.map(name=>[name,Object.fromEntries(requestFields.map(key=>[key,domains[name][key].toString()]))])),
      cumulativeDelta:null};return out;
  }catch(error){out.state=['private-storage-missing','private-storage-fields-missing'].includes(error.message)?'partial':'invalid';
    out.reason=error.message||'private-storage-invalid';return out;}
}
function privateStorageObservations(records,binding){
  const bound=privateBindingValid(binding),points=[],lifetimes=new Map(),retired=new Map();let nextLifetime=0;
  const counts={missing:0,busy:0,invalid:0,retired:0,repeated:0,fresh:0};
  const add=(row,eventIndex,view,life,isRetired,native)=>{
    const point={eventIndex,event:row.event,phase:row.phase??null,cycle:row.cycle??null,elapsedMs:row.elapsedMs??null,
      window:view?.window??null,lifetime:life?.id??null,ownerLifetime:view?.lifetime??null,retired:isRetired,
      ...privateStoragePoint(view)};
    if(point.raw===null)++counts.missing;if(point.getterStatus==='busy')++counts.busy;if(isRetired)++counts.retired;
    let repeated=false;const repeat=()=>{if(!repeated){++counts.repeated;repeated=true;}};
    const reject=(reason,invalid=false)=>{if(point.state==='invalid'&&!invalid)return;
      point.state=invalid?'invalid':'partial';point.reason=reason;point.derived=null;};
    try{
      if(!life)reject('private-storage-owner-lifetime-missing');
      else{
        need(!life.invalid,'private-storage-duplicate-mount',true);
        need(count(view.window)>0n,'private-storage-window-shape',true);
        if(point.raw?.observation?.state==='ready'&&point.raw.observation.owner_epoch!==undefined&&count(point.raw.observation.owner_epoch)>0n){
          const epoch=point.raw.observation.owner_epoch;
          need(!life.epoch||life.epoch===epoch,'private-storage-epoch-changed-within-owner',true);life.epoch=epoch;
        }
        need(view.ownerAlive===true&&view.hostClosed===isRetired&&view.lifetime===(isRetired?
          'after-host-close-before-owner-destruction':'owner-alive'),'private-storage-owner-lifetime-differs',true);
        if(point.derived){
          const epoch=point.derived.ownerEpoch;
          need(!life.epoch||life.epoch===epoch,'private-storage-epoch-changed-within-owner',true);life.epoch=epoch;
          const prior=life.previous;
          if(prior){
            need(count(point.derived.beginMonotonicUs)>=count(prior.derived.beginMonotonicUs)&&
              count(point.derived.endMonotonicUs)>=count(prior.derived.endMonotonicUs),'private-storage-clock-reversed',true);
            for(const name of domainNames)for(const key of ['high_water_bytes','high_water_allocations',...cumulativeFields])need(count(point.derived.domains[name][key])>=
              count(prior.derived.domains[name][key]),'private-storage-total-reversed',true);
            if(same(point.raw,prior.raw)){repeat();reject('private-storage-repeated-copy');}
            else point.derived.cumulativeDelta=Object.fromEntries(domainNames.map(name=>[name,Object.fromEntries(cumulativeFields.map(key=>
              [key,(count(point.derived.domains[name][key])-count(prior.derived.domains[name][key])).toString()]))]));
          }
        }
      }
      if(isRetired){
        if(row.action==='close')need(String(row.window)===view.window,'private-storage-retired-window-differs',true);
        const prior=retired.get(view.window);
        if(prior){need(same(prior.raw,point.raw)&&prior.getterStatus===point.getterStatus,'private-storage-retired-copy-changed',true);repeat();}
        else retired.set(view.window,{...point,life});
        if(point.state!=='invalid')reject('private-storage-retired-copy');
      }else{
        if(!native||native.producer===undefined||native.pid===undefined||native.creationFileTime===undefined)
          reject('private-storage-native-subject-missing');
        else if(binding?.subject)need(native.producer==='native-sdk-owner'&&native.pid===String(binding.subject.pid)&&
          native.creationFileTime===binding.subject.creationFileTime,'private-storage-native-subject-differs',true);
        const clock=native?.captureClock;
        if(clock)need(typeof clock.valid==='boolean','private-storage-capture-clock-bool',true);
        if(!clock||clock.valid!==true)reject('private-storage-capture-clock-unobserved');
        else need(count(clock.qpc)>0n&&count(clock.frequency)>0n,'private-storage-capture-clock-shape',true);
      }
      if(!bound&&point.state!=='invalid')reject('private-storage-complete-binding-missing');
      if(point.derived){++counts.fresh;life.previous=structuredClone(point);}
    }catch(error){reject(error.message||'private-storage-lifetime-invalid',true);}
    if(point.state==='invalid')++counts.invalid;points.push(point);
  };
  for(const [eventIndex,row] of records.entries()){
    if(row.event==='step.complete'&&row.action==='mount'){
      const window=String(row.window),invalid=lifetimes.has(window)||!Number.isSafeInteger(row.window)||row.window<1;
      lifetimes.set(window,{id:++nextLifetime,epoch:null,previous:null,invalid});retired.delete(window);
    }
    if(row.event==='sample'&&row.phase!=='process'){
      const views=row.nativeSdk?.views;
      if(!Array.isArray(views)||views.length===0&&lifetimes.size){add(row,eventIndex,null,null,false,row.nativeSdk);continue;}
      const seen=new Set();
      for(const view of views||[]){
        const window=view?.window,life=lifetimes.get(window);
        add(row,eventIndex,view,seen.has(window)?{...life,invalid:true}:life,false,row.nativeSdk);seen.add(window);
      }
      for(const window of lifetimes.keys())if(!seen.has(window))add(row,eventIndex,{window},null,false,row.nativeSdk);
    }
    if(row.retiredOwner){const window=row.retiredOwner.window;
      add(row,eventIndex,row.retiredOwner,lifetimes.get(window)||retired.get(window)?.life,true,null);
      if(row.action==='close')lifetimes.delete(window);
    }
  }
  return {schema:'soui.task-store-private-storage-observations/v1',scope:privateScope,binding:binding??null,bindingGate:bound?'matched':'not-evaluated',
    dataGate:counts.invalid?'invalid':counts.fresh&&points.every(point=>point.state==='ready'||point.retired)?'matched':'partial',
    points,counts,fullCacheBudget:'not-evaluated',calibratedByteBudget:'not-evaluated',
    configuration:null,budgets:null,quota:null,calibrationPin:null,qualified:false,releaseReady:false};
}
module.exports={count,observation,queue,tasks,requests,permissions,gui,analyzeOwners,
  requestFields,domainNames,privateScope,privateBinding,privateBindingValid,privateStoragePoint,privateStorageObservations};
