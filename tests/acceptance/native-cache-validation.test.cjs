'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const crypto=require('node:crypto');
const {scope,u64Names,decimal,analyzeNamedCache}=require('./native-cache-validation.cjs');
const root=path.resolve(__dirname,'../..');
const input={subject:{pid:9,creationFileTime:'123'},modulePin:{path:'C:\\fixture\\node.dll',bytes:1234,sha256:'a'.repeat(64)},
  captureClock:{valid:true,qpc:'120',frequency:'1000'}};
function fixture(){return {schemaVersion:1,scope,state:'sampled',hasCompletedSample:true,identityBound:true,moduleMatches:true,
  modulePath:input.modulePin.path,inflight:false,runtimeRetired:false,structSize:216,abiVersion:1,status:1,reserved:0,
  blinkReserved:0,glyphReserved:0,blinkStatus:1,glyphStatus:1,resourceStatus:1,resourceDiscardable:0,
  ...Object.fromEntries(u64Names.map(name=>[name,'0'])),processId:'9',processCreationFileTime:'123',engineOwnerThreadId:'10',
  recordingEpoch:'100',initializedQpc:'100',sampleQpc:'110',qpcFrequency:'1000',resourceByteLimit:'1000',resourceEffectiveSingleLimit:'1000'};}
test('named cache DTO completion is data only and closes no resource gate',()=>{
  const result=analyzeNamedCache(fixture(),input);assert.equal(result.dataGate,'matched');
  assert.equal(result.passed,false);assert.equal(result.qualified,false);assert.equal(result.resourceGate,'not-evaluated');
});
test('uint64 remains exact and rejects numbers, signs, exponents and overflow',()=>{
  assert.equal(decimal('18446744073709551615'),18446744073709551615n);
  for(const value of [1,'01','-1','1e3','18446744073709551616'])assert.throws(()=>decimal(value));
  for(const name of u64Names){const v=fixture();v[name]=1;assert.equal(analyzeNamedCache(v,input).dataGate,'invalid');}
});
test('partial, busy and missing owners cannot become known zero',()=>{
  const v=fixture();v.glyphStatus=2;v.resourceStatus=0;
  const result=analyzeNamedCache(v,input);assert.equal(result.dataGate,'partial');
  assert.deepEqual(result.components,{blink:'ready',glyph:'busy',resource:'not_started'});
  v.hasCompletedSample=false;v.state='not_started';assert.equal(analyzeNamedCache(v,input).dataGate,'not-evaluated');
});
test('wrong ABI, status, module, identity, epoch or clock cannot match',()=>{
  const cases=[{structSize:215},{abiVersion:2},{status:2},{reserved:1},{blinkStatus:4},{resourceStatus:99},
    {modulePath:'C:\\other\\node.dll'},{moduleMatches:false},{identityBound:false},{processId:'8'},
    {processCreationFileTime:'124'},{recordingEpoch:'99'},{engineOwnerThreadId:'0'},{sampleQpc:'121'},{qpcFrequency:'999'}];
  for(const change of cases)assert.equal(analyzeNamedCache({...fixture(),...change},input).dataGate,'invalid');
  assert.equal(analyzeNamedCache(fixture(),{...input,modulePin:{...input.modulePin,sha256:null}}).dataGate,'invalid');
});
test('contradictory completion flags and unavailable mode fields are rejected',()=>{
  for(const change of [{hasCompletedSample:false},{hasCompletedSample:'true'},
    {state:'disabled'},{inflight:true},{runtimeRetired:true},{resourceStatus:3,resourceDiscardable:2}])
    assert.equal(analyzeNamedCache({...fixture(),...change},input).dataGate,'invalid');
});
test('soft targets and current usage may decrease or be exceeded',()=>{
  const old=fixture();old.blinkLiveBytes='9000';old.glyphAttachedCount='40';old.glyphByteLimit='9000';
  const v=fixture();v.blinkLiveBytes='5000';v.blinkCapacity='1';v.glyphAttachedCount='2';v.glyphByteLimit='1';
  assert.equal(analyzeNamedCache(v,{...input,previous:old}).dataGate,'matched');
  v.sampleQpc='109';assert.equal(analyzeNamedCache(v,{...input,previous:old}).dataGate,'invalid');
  v.sampleQpc='110';v.engineOwnerThreadId='11';assert.equal(analyzeNamedCache(v,{...input,previous:old}).dataGate,'invalid');
});
test('single allocation policy preserves zero/default and discardable semantics',()=>{
  const v=fixture();v.resourceRawSingleLimit='2000';assert.equal(analyzeNamedCache(v,input).dataGate,'matched');
  v.resourceEffectiveSingleLimit='2000';assert.equal(analyzeNamedCache(v,input).dataGate,'invalid');
  v.resourceDiscardable=1;v.resourceByteLimit='0';v.resourceRawSingleLimit='0';v.resourceEffectiveSingleLimit='0';
  const result=analyzeNamedCache(v,input);assert.equal(result.dataGate,'matched');
  assert.deepEqual(result.resourcePolicy,{byteBudget:'absent',singleAllocationCap:'not-explicit'});
});
test('retired and pending copies retain original time and may retain nonzero caches',()=>{
  const v=fixture();v.runtimeRetired=true;v.state='unavailable';v.glyphAttachedBytes='99';
  const result=analyzeNamedCache(v,input);assert.equal(result.dataGate,'matched');assert.equal(result.retainedSnapshot,true);
  assert.equal(result.freshRequestCompleted,false);assert.equal(v.sampleQpc,'110');
  v.state='pending';v.inflight=true;assert.equal(analyzeNamedCache(v,input).freshRequestCompleted,false);
});
function body(source,name){const start=source.indexOf(name);assert.ok(start>=0,name);const open=source.indexOf('{',start);
  let depth=1,end=open+1;for(;end<source.length&&depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
  assert.equal(depth,0);return source.slice(open+1,end-1);}
test('production UI getter copies only; no posting, wait or Native call',()=>{
  const source=fs.readFileSync(path.join(root,'sdk/src/host/soui_miniblink_host.cpp'),'utf8');
  const getter=body(source,'diagnostics::cache_status read_miniblink_cache_metrics(');
  assert.match(getter,/std::try_to_lock/);assert.match(getter,/out = runtime.cache_completed/);
  assert.doesNotMatch(getter,/GetProcAddress|sampleExistingCaches|engine_admission::post|invoke_lifecycle|future|pump\(|initialize/);
  const request=body(source,'diagnostics::cache_status request_miniblink_cache_sample()');
  assert.match(request,/engine_lane::ordinary/);assert.match(request,/runtime\.cache_slot\.finish\(token\)/);
  assert.match(request,/runtime\.module != module/);assert.match(request,/runtime\.thread\.get\(\) != engine/);
});
test('native peeks use published owners and never lazy create, purge or walk',()=>{
  const native=path.join(root,'third_party/sources/miniblink');
  const memory=body(fs.readFileSync(path.join(native,'third_party/WebKit/Source/core/fetch/MemoryCache.cpp'),'utf8'),'MemoryCacheObservation peekMemoryCache()');
  assert.match(memory,/WTF::isMainThread\(\)/);assert.match(memory,/gMemoryCache/);
  assert.doesNotMatch(memory,/memoryCache\(|getStatistics|prune|for\s*\(/);
  const glyph=body(fs.readFileSync(path.join(native,'third_party/skia/src/core/SkGlyphCache.cpp'),'utf8'),'SkGlyphCacheObservation SkPeekAttachedGlyphCache()');
  assert.match(glyph,/globals\.peek\(\)/);assert.match(glyph,/tryAcquire\(\)/);assert.doesNotMatch(glyph,/globals\.get\(|get_globals\(|Purge|for\s*\(/);
  const resource=body(fs.readFileSync(path.join(native,'third_party/skia/src/core/SkResourceCache.cpp'),'utf8'),'SkResourceCacheObservation SkResourceCache::PeekRetainedGlobalCache()');
  assert.match(resource,/gMutex\.tryAcquire\(\)/);assert.match(resource,/gResourceCache/);
  assert.doesNotMatch(resource,/get_cache\(|checkMessages|purgeAsNeeded|Find\(|for\s*\(/);
});
test('new API is exported separately and mirror raw bytes agree',()=>{
  const native=path.join(root,'third_party/sources/miniblink/wke');
  assert.deepEqual(fs.readFileSync(path.join(native,'wke_cache_diagnostics.h')),
    fs.readFileSync(path.join(root,'sdk/include/soui/diagnostics/cache_observation_abi.h')));
  assert.match(fs.readFileSync(path.join(native,'wkedefine.h'),'utf8'),/ITERATOR1\(const wkeCacheDiagnosticsApi\*, wkeGetCacheDiagnosticsApi/);
});
test('old 168/264 byte ABI header sources retain their exact baseline bytes',()=>{
  const old={
    'sdk/include/soui/diagnostics/renderer_observation_abi.h':'18fb3a800a32bbc964e5278c80bdc0f64ce85e347d90c7ee0e992c95db88b90d',
    'sdk/include/soui/diagnostics/renderer_model_abi.h':'ad973b1eae3ae56aaa695580c8cb849c90da02d6b1d7c86be7691b1b6dea268a',
    'third_party/sources/miniblink/wke/wke_renderer_diagnostics.h':'18fb3a800a32bbc964e5278c80bdc0f64ce85e347d90c7ee0e992c95db88b90d',
    'third_party/sources/miniblink/wke/wke_renderer_model_diagnostics.h':'ea801356bb59261b8f7a8ef3b4ace1e3404778da0c1be0d39ad279f0195f059a'};
  for(const [relative,hash]of Object.entries(old))assert.equal(crypto.createHash('sha256').update(fs.readFileSync(path.join(root,relative))).digest('hex'),hash);
});
test('App opt-in adds one request after existing sample without a wait or default route change',()=>{
  const source=fs.readFileSync(path.join(root,'tests/acceptance-app/main.cpp'),'utf8');
  const sample=body(source,'void sample(unsigned cycle, const char* phase)');
  assert.equal((sample.match(/request_miniblink_cache_sample\(\)/g)||[]).length,1);
  assert.ok(sample.indexOf('log_.emit')<sample.indexOf('request_miniblink_cache_sample'));
  assert.doesNotMatch(sample,/until\(|MsgWait|pump\(|sleep/);
  assert.match(source,/bool cache_observation\{false\}/);
  assert.match(source,/seen\[8\] != \(argc == 8\)/); // Optional flag cannot replace a required existing argument.
});
test('finalized/invalid Native identity precedes the initialized wrong-owner classification',()=>{
  const native=fs.readFileSync(path.join(root,'third_party/sources/miniblink/wke/wke.cpp'),'utf8');
  const sample=body(native,'uint32_t WKE_CACHE_CALL sample_existing_caches(');
  assert.match(sample,/faults =[^;]*WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED/);
  assert.match(sample,/if \(identity\.flags & faults\)\s*\{\s*value\.status = WKE_CACHE_UNAVAILABLE/);
  assert.ok(sample.indexOf('if (identity.flags & faults)')<sample.indexOf('identity.engineOwnerThreadId == GetCurrentThreadId()'));
});
test('cache owner admission distinguishes real preinit from initialized identity without a draw',()=>{
  const native=fs.readFileSync(path.join(root,'third_party/sources/miniblink/wke/wke.cpp'),'utf8');
  const sample=body(native,'uint32_t WKE_CACHE_CALL sample_existing_caches(');
  assert.match(sample,/required =[^;]*IDENTITY_VALID[^;]*CLOCK_VALID[^;]*ENGINE_INITIALIZED/);
  assert.match(sample,/else if \(\(identityStatus == WKE_RENDER_DIAGNOSTICS_READY \|\| identityStatus == WKE_RENDER_DIAGNOSTICS_CLOSED\s*\|\| identityStatus == WKE_RENDER_DIAGNOSTICS_NOT_STARTED\) && \(identity\.flags & required\) == required\)/);
  assert.doesNotMatch(sample,/drawEventsTotal|rasterDrawsTotal|OBSERVED_RASTER/);
  assert.match(sample,/value\.status = identityStatus == WKE_RENDER_DIAGNOSTICS_BUSY \? WKE_CACHE_BUSY : WKE_CACHE_NOT_STARTED/);
  assert.match(sample,/else if \(identityStatus != WKE_RENDER_DIAGNOSTICS_NOT_STARTED\s*&& identityStatus != WKE_RENDER_DIAGNOSTICS_BUSY\) value\.status = WKE_CACHE_UNAVAILABLE/);
  const preinit={...fixture(),state:'not_started',hasCompletedSample:false,status:0,
    ...Object.fromEntries(u64Names.map(name=>[name,'0'])),blinkStatus:0,glyphStatus:0,resourceStatus:0};
  assert.equal(analyzeNamedCache(preinit,input).dataGate,'not-evaluated');
  // Cache DTO requires initialized identity/clock, never a compositor draw DTO.
  assert.equal(analyzeNamedCache({...fixture(),blinkStatus:0,glyphStatus:0,resourceStatus:0},input).dataGate,'partial');
});
