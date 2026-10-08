import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {SouiNativeClientImpl,connectSouiNative} from '../../sdk/web/dist/index.js';

const credentials={sessionId:'lease-session',origin:'app://lease',token:'token-1'};
const lease=(generation=1,expiresInMs=1800000,renewAfterMs=1500000)=>({generation,expiresInMs,renewAfterMs});
const deferred=()=>{let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {promise,resolve,reject};};
const settle=async()=>{for(let i=0;i<20;i++)await Promise.resolve();};

function clock(t){
  let now=0,next=1;const timers=new Map();
  const saved={timeout:globalThis.setTimeout,clear:globalThis.clearTimeout,performance:Object.getOwnPropertyDescriptor(globalThis,'performance')};
  globalThis.setTimeout=(callback,delay=0)=>{const id=next++;timers.set(id,{at:now+Math.max(0,Number(delay)),callback});return id;};
  globalThis.clearTimeout=id=>timers.delete(id);
  Object.defineProperty(globalThis,'performance',{configurable:true,value:{now:()=>now}});
  t.after(()=>{globalThis.setTimeout=saved.timeout;globalThis.clearTimeout=saved.clear;Object.defineProperty(globalThis,'performance',saved.performance);});
  return {get now(){return now;},get size(){return timers.size;},jump(ms){now+=ms;},async fireNextEarly(lead){
    const next=[...timers].sort((a,b)=>a[1].at-b[1].at||a[0]-b[0])[0];assert.ok(next);
    now=Math.max(now,next[1].at-lead);timers.delete(next[0]);next[1].callback();await settle();
  },async advance(ms){
    const end=now+ms;let steps=0;
    for(;;){await settle();const due=[...timers].filter(([,v])=>v.at<=end).sort((a,b)=>a[1].at-b[1].at||a[0]-b[0])[0];
      if(!due)break;if(++steps>10000)throw Error('Unbounded timer loop');now=Math.max(now,due[1].at);timers.delete(due[0]);due[1].callback();}
    now=end;await settle();
  }};
}

function fixture(t,extra={}){
  const requests=[],listeners=new Set(),errors=[];
  const port={request:raw=>{const wait=deferred();requests.push({...wait,request:JSON.parse(raw)});return wait.promise;},
    onEvent:listener=>{listeners.add(listener);return()=>listeners.delete(listener);},dispose:()=>{}};
  const sdk=new SouiNativeClientImpl(port,{...credentials,lease:lease(),onError:error=>errors.push(error.code),...extra});
  t.after(()=>sdk.dispose());
  const reply=(index,result)=>requests[index].resolve(JSON.stringify({requestId:requests[index].request.requestId,ok:true,result}));
  const renewReply=(index,generation=2,remaining=1800000)=>reply(index,{...credentials,token:`token-${generation}`,lease:lease(generation,remaining,Math.max(0,remaining-300000))});
  const deny=(index,code)=>requests[index].resolve(JSON.stringify({requestId:requests[index].request.requestId,ok:false,error:{code,message:code}}));
  return {sdk,requests,errors,reply,renewReply,deny,emit:event=>{for(const listener of listeners)listener(event);}};
}

test('renewal is opt-in, early renewal returns the existing lease, and native rotation is reserved',async t=>{
  clock(t);const f=fixture(t);
  assert.deepEqual(await f.sdk.renewSession(),lease());assert.equal(f.requests.length,0);
  await assert.rejects(f.sdk.call('bridge.session.renew',{operationId:'bypass'}),{code:'E_RESERVED_METHOD'});
  const legacy=fixture(t,{lease:undefined});await assert.rejects(legacy.sdk.renewSession(),{code:'E_RENEWAL_UNAVAILABLE'});
});

test('automatic renewal shares one flight and leaves business dispatch synchronous',async t=>{
  const time=clock(t),f=fixture(t);await time.advance(1500000);
  assert.equal(f.requests.length,1);assert.equal(f.requests[0].request.method,'bridge.session.renew');
  const first=f.sdk.renewSession(),second=f.sdk.renewSession();assert.equal(first,second);
  const work=f.sdk.call('desktop.clipboard.writeText',{text:'gesture'});
  assert.equal(f.requests.length,2);assert.equal(f.requests[1].request.token,'token-1');
  f.renewReply(0);await first;f.reply(1,{applied:true});await work;
  const next=f.sdk.call('app.custom',{});assert.equal(f.requests[2].request.token,'token-2');f.reply(2,'ok');await next;
  assert.equal(f.sdk.sessionLease.generation,2);
});

test('an automatic timer firing slightly early retains a bounded later renewal timer',async t=>{
  const time=clock(t);
  for(const lead of [1,0.5]){
    const f=fixture(t);await time.fireNextEarly(lead);
    assert.equal(f.requests.length,0);assert.equal(time.size,1);
    await time.advance(Math.ceil(lead));assert.equal(f.requests.length,1);
    const renewing=f.sdk.renewSession();f.renewReply(0);await renewing;
    assert.equal(f.sdk.sessionLease.generation,2);f.sdk.dispose();assert.equal(time.size,0);
  }
});

test('renewal has a slot even when business and release capacities are exhausted',async t=>{
  const time=clock(t),f=fixture(t,{maxPendingRequests:1,maxSubscriptions:1,requestTimeoutMs:60000});
  await time.advance(1499999);
  const sub=f.sdk.subscribe('task-1','task.progress',()=>{});f.reply(0,{subscriptionId:'sub-1'});await sub.ready;
  const work=f.sdk.call('app.custom',{}),remove=sub.unsubscribe();assert.equal(f.sdk.pendingRequestCount,2);
  await time.advance(1);assert.equal(f.requests[3].request.method,'bridge.session.renew');assert.equal(f.sdk.pendingRequestCount,3);
  const renewing=f.sdk.renewSession();f.renewReply(3);await renewing;f.reply(2,{removed:true});f.reply(1,'done');await Promise.all([work,remove]);
});

test('bootstrap delay only shortens expiration and disposed renewal cannot restart',async t=>{
  const time=clock(t),f=fixture(t,{leaseElapsedMs:10000});
  assert.equal(f.sdk.sessionLease.expiresInMs,1790000);
  await time.advance(1500000);const renew=f.sdk.renewSession();const rejected=assert.rejects(renew,{code:'E_DISPOSED'});
  f.sdk.dispose();await rejected;f.renewReply(0);await settle();await time.advance(1800000);
  assert.equal(f.sdk.disposed,true);assert.equal(f.requests.length,1);assert.equal(f.sdk.pendingRequestCount,0);
  assert.equal(time.size,0);await assert.rejects(f.sdk.renewSession(),{code:'E_DISPOSED'});
});

test('lease validation and caller mutation cannot increase or replace the authorization lifetime',async t=>{
  clock(t);const port={request:()=>assert.fail('invalid options sent'),onEvent:()=>()=>{}};
  for(const elapsed of [-1,NaN,Infinity,-Infinity])
    assert.throws(()=>new SouiNativeClientImpl(port,{...credentials,lease:lease(),leaseElapsedMs:elapsed}),{code:'E_INVALID_OPTIONS'});
  for(const change of [{generation:0},{generation:1.1},{generation:Infinity},{expiresInMs:0},{expiresInMs:1800001},
    {renewAfterMs:-1},{renewAfterMs:1500001},{expiresInMs:10,renewAfterMs:11}])
    assert.throws(()=>new SouiNativeClientImpl(port,{...credentials,lease:{...lease(),...change}}),{code:'E_INVALID_OPTIONS'});
  assert.throws(()=>new SouiNativeClientImpl(port,{...credentials,lease:lease(),leaseElapsedMs:1800000}),{code:'E_SESSION_EXPIRED'});
  const supplied=lease(),f=fixture(t,{lease:supplied});supplied.generation=55;supplied.expiresInMs=99999999;
  const snapshot=f.sdk.sessionLease;snapshot.generation=77;snapshot.expiresInMs=99999999;
  assert.deepEqual(f.sdk.sessionLease,lease());
});

test('lost acknowledgement recovers the same operation without extending native expiry or replaying work',async t=>{
  const time=clock(t),f=fixture(t);await time.advance(1500000);
  const renewing=f.sdk.renewSession();const operation=f.requests[0].request.params.operationId;
  assert.match(operation,/^[A-Za-z0-9_.:-]{1,128}$/);
  await time.advance(10250);assert.equal(f.requests.length,2);
  assert.equal(f.requests[1].request.params.operationId,operation);assert.equal(f.requests[1].request.token,'token-1');
  assert.notEqual(f.requests[0].request.requestId,f.requests[1].request.requestId);
  // Native minted at the first send. The recovery receipt keeps that expiry.
  await time.advance(500);f.renewReply(1,2,1789750);const recovered=await renewing;
  assert.equal(recovered.expiresInMs,1789250);assert.equal(f.sdk.pendingRequestCount,0);
  const current=f.sdk.call('app.custom',{once:true});assert.equal(f.requests[2].request.token,'token-2');f.reply(2,'done');await current;
  f.renewReply(0,2);await settle();assert.equal(f.sdk.sessionLease.expiresInMs,1789250);
  assert.equal(f.requests.filter(x=>x.request.method==='app.custom').length,1);
});

test('an earlier timed-out attempt can recover the active flight and cancel its retry timer',async t=>{
  const time=clock(t),f=fixture(t);await time.advance(1500000);const renewing=f.sdk.renewSession();
  await time.advance(10050);f.renewReply(0);const result=await renewing;
  assert.equal(result.expiresInMs,1789950);await time.advance(500);
  assert.equal(f.requests.length,1);assert.equal(f.sdk.pendingRequestCount,0);
});

function singleNativeSlot(t,{actualResolver=false,expiresInMs=300000}={}){
  const requests=[],errors=[],hooks=new Map(),target={addEventListener:(name,listener)=>hooks.set(name,listener)};
  let active,completed;
  const header=fs.readFileSync(new URL('../../sdk/src/host/bridge_pending_script.h',import.meta.url),'utf8');
  const source=header.match(/R"JS\(([\s\S]*?)\)JS"/)[1];
  const create=vm.runInNewContext('(function(id,timeoutMs,renewal){return '+source+'(id,timeoutMs,renewal);})',
    {window:target,setTimeout:globalThis.setTimeout,clearTimeout:globalThis.clearTimeout});
  const port={onEvent:()=>()=>{},request:raw=>{
    const request=JSON.parse(raw),wait=deferred();requests.push(request);
    const promise=actualResolver?create(request.requestId,request.timeoutMs,true):wait.promise;
    if(typeof promise==='string')return Promise.resolve(promise);
    const finish=response=>actualResolver?target.__souiBridgePending[request.requestId]?.finish(response):wait.resolve(response);
    if(active)finish(JSON.stringify({requestId:request.requestId,ok:false,error:{code:'E_BUSY',message:'The native renewal slot is still occupied.'}}));
    else if(completed&&request.params.operationId===completed.operationId){
      assert.equal(request.token,'token-1');const remaining=Math.floor(completed.expiresAt-performance.now());
      finish(JSON.stringify({requestId:request.requestId,ok:true,result:{...credentials,token:'token-2',
        lease:lease(2,remaining,Math.max(0,remaining-300000))}}));
    }
    else active={request,finish};
    return promise;
  }};
  const sdk=new SouiNativeClientImpl(port,{...credentials,lease:lease(1,expiresInMs,0),onError:error=>errors.push(error.code)});
  t.after(()=>{sdk.dispose();hooks.get('pagehide')?.();});
  return {sdk,requests,errors,resolverCount:()=>Object.keys(target.__souiBridgePending||{}).length,
    complete:()=>{assert.ok(active);const finished=active;active=undefined;
      completed={operationId:finished.request.params.operationId,expiresAt:performance.now()+1800000};
      finished.finish(JSON.stringify({requestId:finished.request.requestId,ok:true,result:{...credentials,token:'token-2',lease:lease(2)}}));}};
}

test('three attempts with one occupied native slot retain a custom-port late acknowledgement until the deadline',async t=>{
  const time=clock(t),f=singleNativeSlot(t),renewing=f.sdk.renewSession();let completed=false;
  renewing.then(()=>{completed=true;},()=>{completed=true;});
  await time.advance(20000);assert.equal(f.requests.length,3);assert.equal(completed,false);
  assert.equal(f.sdk.pendingRequestCount,1);assert.equal(f.sdk.renewSession(),renewing);
  assert.equal(new Set(f.requests.map(request=>request.params.operationId)).size,1);
  await time.advance(1000);f.complete();assert.equal((await renewing).generation,2);
  assert.equal(f.sdk.pendingRequestCount,0);assert.deepEqual(f.errors,[]);
  await time.advance(4000);assert.equal(f.requests.length,3);assert.equal(f.sdk.sessionLease.generation,2);
});

test('the actual native resolver drops the first acknowledgement and the reserved final attempt retrieves the same operation',async t=>{
  const time=clock(t),f=singleNativeSlot(t,{actualResolver:true}),renewing=f.sdk.renewSession();let completed=false;
  renewing.then(()=>{completed=true;},()=>{completed=true;});
  await time.advance(10250);assert.equal(f.requests.length,2);assert.equal(f.resolverCount(),0);
  await time.advance(750);f.complete();await settle();assert.equal(completed,false);assert.equal(f.sdk.sessionLease.generation,1);
  await time.advance(8999);assert.equal(f.requests.length,2);assert.equal(completed,false);
  await time.advance(1);assert.equal((await renewing).generation,2);assert.equal(f.requests.length,3);
  assert.equal(new Set(f.requests.map(request=>request.params.operationId)).size,1);
  assert.equal(new Set(f.requests.map(request=>request.requestId)).size,3);
  assert.equal(f.requests[2].timeoutMs,5000);assert.equal(f.sdk.sessionLease.expiresInMs,1791000);
  assert.equal(f.resolverCount(),0);assert.equal(f.sdk.pendingRequestCount,0);
});

test('an occupied native slot exhausts three attempts but fails only at the original recovery or lease deadline',async t=>{
  const time=clock(t);
  for(const expiresInMs of [300000,12000]){
    const f=singleNativeSlot(t,{actualResolver:true,expiresInMs}),renewing=f.sdk.renewSession();let completed=false;
    renewing.then(()=>{completed=true;},()=>{completed=true;});const rejected=assert.rejects(renewing,{code:'E_BUSY'});
    await time.advance(10750);assert.equal(f.requests.length,expiresInMs===12000?3:2);assert.equal(f.resolverCount(),0);
    assert.equal(completed,false);assert.equal(f.sdk.pendingRequestCount,1);
    await time.advance(Math.min(25000,expiresInMs)-10751);assert.equal(completed,false);
    await time.advance(1);await rejected;assert.equal(f.sdk.pendingRequestCount,0);
    assert.equal(f.requests.length,3);assert.equal(f.sdk.sessionLease.generation,1);assert.equal(time.size,0);
    f.complete();await settle();assert.equal(f.sdk.sessionLease.generation,1);assert.equal(f.resolverCount(),0);
    await assert.rejects(f.sdk.renewSession(),{code:'E_BUSY'});f.sdk.dispose();
  }
});

test('retry attempts and recovery deadline stay bounded and terminal failure does not rebootstrap',async t=>{
  const time=clock(t),f=fixture(t);await time.advance(1500000);
  const renewing=f.sdk.renewSession(),rejected=assert.rejects(renewing,{code:'E_TIMEOUT'});
  await time.advance(25000);await rejected;
  assert.equal(f.requests.length,3);assert.equal(new Set(f.requests.map(x=>x.request.params.operationId)).size,1);
  assert.equal(f.requests[2].request.timeoutMs,4250);assert.deepEqual(f.errors,['E_TIMEOUT']);
  assert.equal(f.sdk.pendingRequestCount,0);f.renewReply(0);await settle();assert.equal(f.sdk.sessionLease.generation,1);
  await assert.rejects(f.sdk.renewSession(),{code:'E_TIMEOUT'});await time.advance(1800000);assert.equal(f.requests.length,3);
});

test('even an unprocessed timeout cannot admit a late success beyond the original expiry',async t=>{
  const time=clock(t),f=fixture(t,{lease:lease(1,100,0)});
  const renewing=f.sdk.renewSession(),rejected=assert.rejects(renewing,{code:'E_RENEWAL_TIMEOUT'});
  assert.equal(f.requests[0].request.timeoutMs,100);
  time.jump(100);f.renewReply(0);await rejected;assert.equal(f.sdk.sessionLease.generation,1);
  assert.equal(f.sdk.sessionLease.expiresInMs,0);assert.equal(f.sdk.pendingRequestCount,0);
});

test('renewal identity, generation and native denials are checked without adopting partial credentials',async t=>{
  const time=clock(t);
  for(const change of [{sessionId:'other'},{origin:'app://other'},{token:''},{token:'token-1'},
    {lease:lease(1)},{lease:lease(3)},{lease:{...lease(2),expiresInMs:1800001}}]){
    const f=fixture(t,{lease:lease(1,300000,0)}),renewing=f.sdk.renewSession();
    f.reply(0,{...credentials,token:'token-2',lease:lease(2),...change});
    await assert.rejects(renewing,{code:'E_INVALID_RESPONSE'});assert.equal(f.sdk.sessionLease.generation,1);f.sdk.dispose();
  }
  for(const code of ['E_SESSION_EXPIRED','E_UNAUTHORIZED','E_RENEWAL_UNAVAILABLE']){
    const f=fixture(t,{lease:lease(1,300000,0)}),renewing=f.sdk.renewSession();f.deny(0,code);
    await assert.rejects(renewing,{code});await time.advance(1000);assert.equal(f.requests.length,1);f.sdk.dispose();
  }
});

test('retryable native rejection and transport loss retain operation identity',async t=>{
  const time=clock(t),f=fixture(t,{lease:lease(1,300000,0)});const renewing=f.sdk.renewSession();
  f.deny(0,'E_RENEW_TOO_EARLY');await time.advance(250);assert.equal(f.requests.length,2);
  f.requests[1].reject(Error('lost pipe acknowledgement'));await time.advance(19750);assert.equal(f.requests.length,3);
  assert.equal(new Set(f.requests.map(x=>x.request.params.operationId)).size,1);
  f.renewReply(2);assert.equal((await renewing).generation,2);
});

test('in-flight subscription and its sequence survive rotation and late removal uses current credentials',async t=>{
  const time=clock(t),f=fixture(t,{requestTimeoutMs:60000});await time.advance(1499999);
  const delivered=[],sub=f.sdk.subscribe('task-1','task.progress',e=>delivered.push(e.sequence));
  const emit=sequence=>f.emit({sessionId:credentials.sessionId,taskId:'task-1',eventName:'task.progress',sequence,payload:{}});
  emit(4);await time.advance(1);const renewing=f.sdk.renewSession();f.renewReply(1);await renewing;
  f.reply(0,{subscriptionId:'kept-sub'});await sub.ready;emit(3);emit(4);emit(5);assert.deepEqual(delivered,[4,5]);
  const removing=sub.unsubscribe();assert.equal(f.requests[2].request.token,'token-2');assert.deepEqual(f.requests[2].request.params,{subscriptionId:'kept-sub'});
  emit(6);f.reply(2,{removed:true});await removing;assert.deepEqual(delivered,[4,5]);assert.equal(f.sdk.subscriptionCount,0);
});

test('timed-out subscribe cleanup and repeated unsubscribe remain once-only across renewal',async t=>{
  const time=clock(t),f=fixture(t,{requestTimeoutMs:1000});await time.advance(1499999);
  const sub=f.sdk.subscribe('task-1','task.progress',()=>assert.fail('removed subscription event'));
  const rejected=assert.rejects(sub.ready,{code:'E_TIMEOUT'});await time.advance(1);
  const renewing=f.sdk.renewSession();f.renewReply(1);await renewing;await time.advance(999);await rejected;
  f.reply(0,{subscriptionId:'late-sub'});await settle();assert.equal(f.requests[2].request.token,'token-2');
  f.reply(2,{removed:true});await settle();const removal=sub.unsubscribe();await removal;assert.equal(sub.unsubscribe(),removal);
  sub();sub();assert.equal(f.requests.filter(x=>x.request.method==='task.unsubscribe').length,1);
});

test('an old renewal response cannot roll back credentials after a subsequent generation',async t=>{
  const time=clock(t),f=fixture(t);await time.advance(1500000);const first=f.sdk.renewSession();
  await time.advance(10250);f.renewReply(1,2,1789750);await first;
  await time.advance(1489750);assert.equal(f.requests.length,3);const second=f.sdk.renewSession();f.renewReply(2,3);await second;
  f.renewReply(0,2);await settle();assert.equal(f.sdk.sessionLease.generation,3);
  const work=f.sdk.call('app.custom',{});assert.equal(f.requests[3].request.token,'token-3');f.reply(3,3);await work;
});

test('twenty simulated 25-minute rotations retain one subscription and increasing event sequences',async t=>{
  const time=clock(t),f=fixture(t);let delivered=0;
  const sub=f.sdk.subscribe('long-task','task.progress',()=>delivered++);f.reply(0,{subscriptionId:'long-sub'});await sub.ready;
  for(let generation=2;generation<=21;generation++){
    await time.advance(1500000);const renewing=f.sdk.renewSession(),index=f.requests.length-1;
    f.renewReply(index,generation);await renewing;
    f.emit({sessionId:credentials.sessionId,taskId:'long-task',eventName:'task.progress',sequence:generation,payload:{}});
    assert.equal(f.sdk.subscriptionCount,1);assert.equal(f.sdk.sessionLease.generation,generation);
  }
  assert.equal(time.now,30000000);assert.equal(delivered,20);assert.equal(f.requests.filter(x=>x.request.method==='task.subscribe').length,1);
  const removal=sub.unsubscribe();assert.equal(f.requests.at(-1).request.token,'token-21');f.reply(f.requests.length-1,{removed:true});await removal;
});

test('dispose during renewal and subscribe stops callbacks and compensates one late subscription',async t=>{
  const time=clock(t),f=fixture(t,{requestTimeoutMs:60000});await time.advance(1499999);
  const sub=f.sdk.subscribe('task-1','task.progress',()=>assert.fail('disposed event'));
  const readyRejected=assert.rejects(sub.ready,{code:'E_DISPOSED'});await time.advance(1);
  const renewing=f.sdk.renewSession(),renewRejected=assert.rejects(renewing,{code:'E_DISPOSED'});
  f.sdk.dispose();await Promise.all([readyRejected,renewRejected]);f.renewReply(1);f.reply(0,{subscriptionId:'late-disposed'});await settle();
  assert.equal(f.sdk.sessionLease.generation,1);assert.equal(f.requests.filter(x=>x.request.method==='task.unsubscribe').length,1);
  f.reply(2,{removed:true});await settle();assert.equal(f.sdk.pendingRequestCount,0);assert.equal(f.sdk.subscriptionCount,0);
  assert.equal(time.size,0);await time.advance(1800000);assert.equal(f.requests.length,3);
});

test('synchronous transport reentrancy observes the published single flight and disposal wins',async t=>{
  clock(t);let sdk,nested;const requests=[];
  const port={onEvent:()=>()=>{},request:raw=>{const request=JSON.parse(raw);requests.push(request);
    nested=sdk.renewSession();sdk.dispose();return Promise.resolve(JSON.stringify({requestId:request.requestId,ok:true,
      result:{...credentials,token:'token-2',lease:lease(2)}}));}};
  sdk=new SouiNativeClientImpl(port,{...credentials,lease:lease(1,300000,0)});t.after(()=>sdk.dispose());
  const renewal=sdk.renewSession();assert.equal(nested,renewal);await assert.rejects(renewal,{code:'E_DISPOSED'});await settle();
  assert.equal(requests.length,1);assert.equal(sdk.sessionLease.generation,1);assert.equal(sdk.pendingRequestCount,0);
});

function nativeWindow(bootstrap,clock){
  const hooks=new Map(),requests=[];
  const target={addEventListener:(type,listener)=>{if(!hooks.has(type))hooks.set(type,new Set());hooks.get(type).add(listener);},
    removeEventListener:(type,listener)=>hooks.get(type)?.delete(listener),
    __souiNativePostMessage:raw=>{const request=JSON.parse(raw),wait=deferred();requests.push({...wait,request});
      if(request.method==='bridge.bootstrap'){clock?.jump(1000);return JSON.stringify({requestId:request.requestId,ok:true,result:bootstrap});}return wait.promise;}};
  const reply=(index,result)=>requests[index].resolve(JSON.stringify({requestId:requests[index].request.requestId,ok:true,result}));
  return {target,requests,reply,close:()=>{for(const listener of hooks.get('pagehide')||[])listener({type:'pagehide'});},
    hookCount:()=>[...hooks.values()].reduce((count,listeners)=>count+listeners.size,0)};
}

test('standard connector retains one bootstrap and applies lease metadata with elapsed time',async t=>{
  const time=clock(t),f=nativeWindow({...credentials,lease:lease()},time);
  const a=connectSouiNative({target:f.target}),b=connectSouiNative({target:f.target});assert.equal(a,b);
  const sdk=await a;assert.equal(sdk.sessionLease.expiresInMs,1799000);t.after(()=>sdk.dispose());
  await time.advance(1500000);const renewing=sdk.renewSession();f.reply(1,{...credentials,token:'token-2',lease:lease(2)});await renewing;
  assert.equal(f.requests.filter(x=>x.request.method==='bridge.bootstrap').length,1);
  f.close();assert.equal(f.hookCount(),0);await assert.rejects(connectSouiNative({target:f.target}),{code:'E_DISPOSED'});
});

test('connector rejects malformed native lease metadata before creating a client',async t=>{
  clock(t);
  for(const invalid of [null,[],{},lease(0),{...lease(),renewAfterMs:1800000}]){
    const f=nativeWindow({...credentials,lease:invalid});await assert.rejects(connectSouiNative({target:f.target}),{code:'E_INVALID_RESPONSE'});
    assert.equal(f.hookCount(),0);assert.equal(f.requests.length,1);
  }
});

test('Workbench React manual bootstrap passes lease metadata and page exit disposes renewal',async t=>{
  const time=clock(t),f=nativeWindow({...credentials,lease:lease()},time);
  const prior=Object.getOwnPropertyDescriptor(globalThis,'window');Object.defineProperty(globalThis,'window',{configurable:true,value:f.target});
  t.after(()=>{if(prior)Object.defineProperty(globalThis,'window',prior);else delete globalThis.window;});
  const ts=createRequire(new URL('../../sdk/web/package.json',import.meta.url))('typescript');
  const sdkUrl=new URL('../../sdk/web/dist/index.js',import.meta.url).href;
  const source=fs.readFileSync(new URL('../../demos/workbench/web/apps/react/native.ts',import.meta.url),'utf8')
    .replace("'../../packages/sdk/soui_native_client'",JSON.stringify(sdkUrl)).replace('import.meta.env.MODE',"'test'");
  // Transpile the actual integration source; only its module resolution and
  // build-time MODE constant are supplied by this non-browser harness.
  const compiled=ts.transpileModule(source,{compilerOptions:{target:ts.ScriptTarget.ES2015,module:ts.ModuleKind.ES2020}}).outputText;
  const integration=await import(`data:text/javascript;base64,${Buffer.from(compiled).toString('base64')}`);
  const sdk=integration.createReactNativeClient();t.after(()=>sdk.dispose());assert.equal(sdk.sessionLease.expiresInMs,1799000);
  await time.advance(1500000);const renewing=sdk.renewSession();f.reply(1,{...credentials,token:'token-2',lease:lease(2)});await renewing;
  const work=sdk.call('app.custom',{});assert.equal(f.requests[2].request.token,'token-2');f.reply(2,'ok');await work;
  assert.equal(f.hookCount(),2);f.close();assert.equal(sdk.disposed,true);assert.equal(f.hookCount(),0);
  await time.advance(1800000);assert.equal(f.requests.length,3);
});

test('Workbench default Vue bootstrap uses the same lease-aware public client and document cleanup',async t=>{
  const time=clock(t),f=nativeWindow({...credentials,lease:lease()},time);
  const prior=Object.getOwnPropertyDescriptor(globalThis,'window');Object.defineProperty(globalThis,'window',{configurable:true,value:f.target});
  t.after(()=>{if(prior)Object.defineProperty(globalThis,'window',prior);else delete globalThis.window;});
  const ts=createRequire(new URL('../../sdk/web/package.json',import.meta.url))('typescript');
  const sdkUrl=new URL('../../sdk/web/dist/index.js',import.meta.url).href;
  const original=fs.readFileSync(new URL('../../demos/workbench/web/apps/demo/src/main.ts',import.meta.url),'utf8');
  const normalized=original.replace(/\r\n/g,'\n');
  const start=normalized.indexOf('import {\n  SouiBridgeError');
  const end=normalized.indexOf('const port = new SynchronousNativePort();');
  assert.ok(start>=0&&end>start);
  // Exercise the original bootstrap/port declarations without importing Vue,
  // styles or mounting its business UI. No implementation is mirrored here.
  const source=normalized.slice(start,end).replace("'../../../packages/sdk/soui_native_client'",JSON.stringify(sdkUrl))
    .replaceAll('import.meta.env.MODE',"'test'")+'\nexport {createNativeClient,SynchronousNativePort};';
  const compiled=ts.transpileModule(source,{compilerOptions:{target:ts.ScriptTarget.ES2015,module:ts.ModuleKind.ES2020}}).outputText;
  const integration=await import(`data:text/javascript;base64,${Buffer.from(compiled).toString('base64')}`);
  const sdk=integration.createNativeClient(new integration.SynchronousNativePort());t.after(()=>sdk.dispose());
  assert.equal(sdk.sessionLease.expiresInMs,1799000);await time.advance(1500000);
  const renewing=sdk.renewSession();f.reply(1,{...credentials,token:'token-2',lease:lease(2)});await renewing;
  const work=sdk.call('app.custom',{});assert.equal(f.requests[2].request.token,'token-2');f.reply(2,'ok');await work;
  f.close();assert.equal(sdk.disposed,true);assert.equal(f.hookCount(),0);await time.advance(1800000);assert.equal(f.requests.length,3);
});
