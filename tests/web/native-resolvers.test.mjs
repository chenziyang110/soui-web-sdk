import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const header=fs.readFileSync(new URL('../../sdk/src/host/bridge_pending_script.h',import.meta.url),'utf8');
const source=header.match(/R"JS\(([\s\S]*?)\)JS"/)[1];
function fixture(t) {
  const listeners=new Map(),timers=new Set();
  const target={addEventListener:(type,fn)=>{const list=listeners.get(type)||[];list.push(fn);listeners.set(type,list);}};
  const close=()=>{for(const listener of listeners.get('pagehide')||[])listener();};
  // Match jsEval's function body, including the native return statement. A
  // leading newline after return would otherwise silently produce undefined.
  const create=vm.runInNewContext('(function(id, timeoutMs, renewal){return '+source+'(id,timeoutMs,renewal);})',{window:target,
    setTimeout:(fn,ms)=>{const timer=setTimeout(()=>{timers.delete(timer);fn();},ms);timers.add(timer);return timer;},
    clearTimeout:timer=>{clearTimeout(timer);timers.delete(timer);}});
  t.after(()=>{close();assert.equal(timers.size,0);});
  return {target,timers,listeners,create,close,pending:()=>Object.keys(target.__souiBridgePending||{}).length};
}
test('native resolver completion clears its timer and repeated completion cannot settle a later request',async t=>{
  const f=fixture(t),first=f.create('same-id',1000),entry=f.target.__souiBridgePending['same-id'];
  entry.finish('first');assert.equal(await first,'first');assert.equal(f.pending(),0);assert.equal(f.timers.size,0);
  const next=f.create('same-id',1000);entry.finish('obsolete');assert.equal(f.pending(),1);
  f.target.__souiBridgePending['same-id'].finish('second');assert.equal(await next,'second');
});
test('duplicate identities and resolver overflow return correlated errors without adding timers',async t=>{
  const f=fixture(t),waits=[];
  for(let i=0;i<384;i++)waits.push(f.create('id:'+i,1000));
  assert.equal(f.pending(),384);assert.equal(f.timers.size,384);
  assert.deepEqual(JSON.parse(f.create('id:0',1000)),{requestId:'id:0',ok:false,error:{code:'E_DUPLICATE_REQUEST',message:'This document already has a pending request with this identity.'}});
  assert.equal(JSON.parse(f.create('overflow',1000)).error.code,'E_BUSY');assert.equal(f.timers.size,384);
  f.close();const results=await Promise.all(waits);assert.ok(results.every(raw=>JSON.parse(raw).error.code==='E_DISPOSED'));
  assert.equal(f.pending(),0);assert.equal(f.listeners.get('pagehide').length,1);
});
test('native response timeout removes the resolver and explicitly leaves running work uncancelled',async t=>{
  const f=fixture(t),wait=f.create('timeout',15);
  const result=JSON.parse(await wait);assert.equal(result.requestId,'timeout');assert.equal(result.error.code,'E_TIMEOUT');
  assert.match(result.error.message,/not cancelled/);assert.equal(f.pending(),0);assert.equal(f.timers.size,0);
});
test('document exit resolves all waits, is idempotent and blocks future resolver creation',async t=>{
  const f=fixture(t),a=f.create('__proto__',1000),b=f.create('constructor',1000);
  f.close();f.close();for(const result of await Promise.all([a,b]))assert.equal(JSON.parse(result).error.code,'E_DISPOSED');
  assert.equal(JSON.parse(f.create('after-close',1000)).error.code,'E_DISPOSED');assert.equal(f.pending(),0);assert.equal(f.timers.size,0);
});

test('a reserved renewal resolver survives all 384 ordinary waits and releases independently',async t=>{
  const f=fixture(t),ordinary=[];
  for(let i=0;i<384;i++)ordinary.push(f.create('ordinary:'+i,1000));
  const renewal=f.create('renew:1',1000,true);assert.equal(typeof renewal.then,'function');
  assert.equal(f.pending(),385);assert.equal(f.timers.size,385);
  assert.equal(JSON.parse(f.create('renew:2',1000,true)).error.code,'E_BUSY');
  assert.equal(JSON.parse(f.create('ordinary-overflow',1000,false)).error.code,'E_BUSY');
  assert.equal(JSON.parse(f.create('ordinary:0',1000,true)).error.code,'E_DUPLICATE_REQUEST');
  assert.equal(JSON.parse(f.create('renew:1',1000,false)).error.code,'E_DUPLICATE_REQUEST');
  const stale=f.target.__souiBridgePending['renew:1'];stale.finish('renewed');assert.equal(await renewal,'renewed');
  assert.equal(f.pending(),384);const replacement=f.create('renew:1',1000,true);stale.finish('obsolete');assert.equal(f.pending(),385);
  f.target.__souiBridgePending['ordinary:0'].finish('ordinary done');assert.equal(await ordinary[0],'ordinary done');
  assert.equal(f.pending(),384);const nextOrdinary=f.create('new-ordinary',1000,false);assert.equal(f.pending(),385);
  f.close();for(const raw of await Promise.all([...ordinary.slice(1),replacement,nextOrdinary]))
    assert.equal(JSON.parse(raw).error.code,'E_DISPOSED');
  assert.equal(f.pending(),0);assert.equal(f.timers.size,0);
  assert.equal(JSON.parse(f.create('closed-renew',1000,true)).error.code,'E_DISPOSED');
});

test('renewal resolver timeout releases its reserved slot without releasing business waits',async t=>{
  const f=fixture(t),ordinary=f.create('business',1000),renewal=f.create('renew-timeout',15,true);
  assert.equal(JSON.parse(await renewal).error.code,'E_TIMEOUT');assert.equal(f.pending(),1);
  const next=f.create('renew-next',1000,true);f.target.__souiBridgePending['renew-next'].finish('ok');assert.equal(await next,'ok');
  assert.equal(f.pending(),1);f.close();assert.equal(JSON.parse(await ordinary).error.code,'E_DISPOSED');
});
