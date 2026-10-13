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
module.exports={count,observation,queue,tasks,requests,permissions,gui,analyzeOwners};
