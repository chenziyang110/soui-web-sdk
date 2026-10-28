'use strict';
// Profile 2: fixed 860 x 580 client and observed work-area geometry.
// Offline files and arithmetic only. Never launches a host, DLL, browser or VM.
// Base: external profile2 verifier SHA-256
// f68c88e25fa32b0a3c4099fd241e74297fe7724945496562c4416b28a4778a23.
// --check-only revalidates the adjacent recorded evidence without external inputs.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const assert=require('node:assert/strict');
const {TextDecoder}=require('node:util');
const BUCKETS=['baseline-x86','candidate-x86','baseline-x64','candidate-x64'];
const PHASES=['host-start-begin','document-ready','host-close-begin','host-close-complete','runtime-close-begin','runtime-close-complete'];
const SOURCE_PINS={
  'web-source/main.ts':'dc0342127a82ab047876385a728033d80e9f00d97c40ce22b20811636c0c344e',
  'decode-serial.cjs':'94b7aee2c5a9cdba8f8489a9305ff8fb8d481757488dcf5534613284bd37f051',
  'launcher/launcher.cpp':'7b66d99f9bf963132b81ee8aaa64bab86b9fb3d845b0f66673d2092fa537e20b',
  'source/main.cpp':'43fa675dd8bd9e5695d0221c192fe27c74ec7f35e6716a5402d5a2293d886435'
};
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const id=(b,label)=>({artifactLabel:label,bytes:b.length,sha256:hash(b)});
function safeInteger(n,label){assert.ok(Number.isSafeInteger(n)&&n>=0,label);}
function finite(n,label){assert.equal(typeof n,'number',label);assert.ok(Number.isFinite(n)&&n>=0,label);}
function near(a,b,label){finite(a,label);finite(b,label);assert.ok(Math.abs(a-b)<=Math.max(1e-6,Math.abs(b)*1e-9),label);}
function keys(value,names,label){assert.ok(value&&typeof value==='object'&&!Array.isArray(value),label);assert.deepEqual(Object.keys(value).sort(),[...names].sort(),label);}
function json(bytes,label){
  const text=new TextDecoder('utf-8',{fatal:true}).decode(bytes).replace(/^\uFEFF/,'');
  const result=JSON.parse(text); // Syntax/escapes first; then reject duplicate decoded object keys.
  let at=0;
  const ws=()=>{while(/[\t\n\r ]/.test(text[at]||'!'))++at;};
  const string=()=>{const start=at++;while(at<text.length){const c=text[at++];if(c==='\\')++at;else if(c==='"')return JSON.parse(text.slice(start,at));}throw Error(label+': unterminated string');};
  const value=()=>{ws();const c=text[at];if(c==='"'){string();return;}
    if(c==='{'){++at;ws();const seen=new Set();if(text[at]==='}'){++at;return;}
      for(;;){ws();assert.equal(text[at],'"',label);const key=string();assert.ok(!seen.has(key),label+': duplicate key '+key);seen.add(key);ws();assert.equal(text[at++],':',label);value();ws();const end=text[at++];if(end==='}')return;assert.equal(end,',',label);}}
    if(c==='['){++at;ws();if(text[at]===']'){++at;return;}for(;;){value();ws();const end=text[at++];if(end===']')return;assert.equal(end,',',label);}}
    const token=/^(?:true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)/.exec(text.slice(at));
    assert.ok(token,label+': scalar');at+=token[0].length;};
  value();ws();assert.equal(at,text.length,label+': complete JSON');return result;
}
function decodeTransport(raw){
  assert.ok(raw.length>0&&raw.length<=160*1024*1024,'bounded raw capture');
  const prefix=Buffer.from('\r\n{"record":"throughput-serial-begin","schema":"soui-neutral-throughput-guest/1"}\r\n');
  assert.ok(raw.subarray(0,prefix.length).equals(prefix),'exact unique serial begin');
  const latin=raw.toString('latin1');
  assert.equal((latin.match(/"record":"throughput-serial-begin"/g)||[]).length,1,'one serial begin');
  assert.equal((latin.match(/"record":"throughput-serial-end"/g)||[]).length,1,'one serial end');
  const match=/\r\n(\{"record":"throughput-serial-end","bytes":([0-9]+),"complete":(true|false)\})\r\n$/.exec(latin);
  assert.ok(match,'complete serial suffix');assert.equal(match[3],'true','serial export complete');
  const suffix=Buffer.from(match[0],'latin1'),payload=raw.subarray(prefix.length,raw.length-suffix.length);
  const declared=Number(match[2]);safeInteger(declared,'declared serial bytes');assert.equal(payload.length,declared,'raw payload byte accounting');
  assert.ok(payload.subarray(-2).equals(Buffer.from('\r\n')),'no truncated report line');
  const lines=payload.toString('utf8').split(/\r*\n/);assert.equal(lines.pop(),'');
  const rows=lines.map((line,i)=>{assert.ok(line.length,'no blank report line');return json(Buffer.from(line,'utf8'),'report line '+(i+1));});
  assert.equal(rows.filter(r=>r.record==='throughput-launcher-begin').length,1);
  assert.equal(rows.filter(r=>r.record==='throughput-launcher-end').length,1);
  const begin=rows[0],terminal=rows.at(-1);
  assert.equal(begin.record,'throughput-launcher-begin');assert.equal(begin.schema,'soui-neutral-throughput-guest/1');
  assert.equal(begin.plannedArms,4);assert.equal(begin.childBoundMs,45000);
  assert.equal(begin.pageReader,'controlled-root-fields-only-not-general-json-validation');
  assert.equal(begin.endToEndInputMeasurement,false);assert.equal(begin.productionQualification,false);
  assert.equal(typeof begin.output,'string');assert.ok(path.win32.isAbsolute(begin.output),'guest unique output path');
  const arms=[];let active=null,artifact=null,kernelBase=null;
  for(const row of rows.slice(1,-1)){
    if(row.record==='throughput-arm-begin'){
      assert.equal(active,null,'nonoverlapping arms');assert.equal(artifact,null);
      assert.equal(row.bucket,BUCKETS[arms.length],'fixed unique arm order');assert.equal(row.boundMs,45000);
      assert.equal(typeof row.kernel,'string');assert.ok(path.win32.isAbsolute(row.kernel));
      assert.equal(path.win32.basename(row.kernel),'node.dll');assert.equal(path.win32.basename(path.win32.dirname(row.kernel)),row.bucket);
      const base=path.win32.dirname(path.win32.dirname(row.kernel));if(kernelBase===null)kernelBase=base;else assert.equal(base,kernelBase,'same deployed bundle');
      active={bucket:row.bucket,artifacts:new Map()};
    }else if(row.record==='artifact-begin'){
      assert.ok(active,'artifact in active arm');assert.equal(artifact,null,'sequential artifacts');assert.equal(row.bucket,active.bucket);
      const expectedKind=active.artifacts.size===0?'child-log':'page-result';assert.equal(row.kind,expectedKind,'exact two artifact order');
      assert.ok(active.artifacts.size<2);safeInteger(row.bytes,'artifact byte count');assert.ok(row.bytes<=8*1024*1024);
      assert.equal(row.encoding,'hex');assert.equal(typeof row.path,'string');
      const arm=path.win32.join(begin.output,active.bucket);
      if(row.kind==='child-log')assert.equal(row.path,path.win32.join(arm,'child.log'));
      else{assert.equal(path.win32.dirname(row.path),path.win32.join(arm,'page-results'));assert.match(path.win32.basename(row.path),/^soui-[1-9][0-9]*-[0-9]+-1\.json$/);}
      artifact={kind:row.kind,bytes:row.bytes,offset:0,chunks:[]};
    }else if(row.record==='artifact-chunk'){
      assert.ok(active&&artifact);assert.equal(row.bucket,active.bucket);assert.equal(row.kind,artifact.kind);assert.equal(row.offset,artifact.offset);
      assert.equal(typeof row.hex,'string');assert.match(row.hex,/^(?:[0-9a-f]{2})+$/);assert.ok(row.hex.length<=8192,'4096 byte source chunk');
      const bytes=Buffer.from(row.hex,'hex');assert.ok(bytes.length>0&&bytes.length<=Math.min(4096,artifact.bytes-artifact.offset),'bounded source chunk');
      artifact.offset+=bytes.length;assert.ok(artifact.offset<=artifact.bytes);artifact.chunks.push(bytes);
    }else if(row.record==='artifact-end'){
      assert.ok(active&&artifact);assert.equal(row.bucket,active.bucket);assert.equal(row.kind,artifact.kind);assert.equal(row.complete,true);
      assert.equal(row.bytes,artifact.bytes);assert.equal(artifact.offset,artifact.bytes);
      active.artifacts.set(artifact.kind,Buffer.concat(artifact.chunks));artifact=null;
    }else if(row.record==='throughput-arm-end'){
      assert.ok(active);assert.equal(artifact,null);assert.equal(row.bucket,active.bucket);assert.equal(active.artifacts.size,2);
      for(const key of ['created','assignedBeforeResume','resumed','exitObserved','jobDrained','jobHandleClosed','childLogIncluded','pageResultIncluded','controlledPageProtocolMatched','executionCapturePassed','passed'])assert.equal(row[key],true,active.bucket+': '+key);
      for(const key of ['timeout','forcedCleanup','cleanupSucceeded'])assert.equal(row[key],false,active.bucket+': '+key);
      assert.equal(row.actualExit,0);assert.equal(row.waitResult,0);assert.equal(row.error,0);assert.equal(row.pageResultFiles,1);
      safeInteger(row.elapsedMs,'arm elapsed');assert.ok(row.elapsedMs<=45000,'arm 45 second bound');
      active.execution=row;arms.push(active);active=null;
    }else throw Error('Unknown/invalid report record: '+row.record);
  }
  assert.equal(active,null);assert.equal(artifact,null);assert.equal(arms.length,4);
  assert.equal(new Set(arms.map(r=>r.bucket)).size,4);
  assert.deepEqual(terminal,{record:'throughput-launcher-end',arms:4,passed:4,logHealthy:true,
    allChildrenNaturalZeroAndDrained:true,allChildClosureObserved:true,performanceAccepted:false,releaseReady:false});
  return {arms,begin,terminal,transport:{...id(raw,'raw-serial.bin'),rawBytesPreserved:true,
    prefixBytes:prefix.length,payloadBytes:payload.length,payloadSha256:hash(payload),suffixBytes:suffix.length,
    rawCrCrLfCount:(latin.match(/\r\r\n/g)||[]).length,complete:true}};
}
function ready(value,load){keys(value,['load','domReadyMs','bridgeReadyMs','readyState'],'navigation ready');
  assert.equal(value.load,load);finite(value.domReadyMs,'DOM ready sample');finite(value.bridgeReadyMs,'Bridge ready sample');
  assert.ok(value.bridgeReadyMs>=value.domReadyMs,'Bridge after DOM sample');assert.ok(['interactive','complete'].includes(value.readyState));}
function animation(value){
  keys(value,['durationRequestedMs','durationObservedMs','frameCount','gapCount','medianGapMs','p95GapMs','maxGapMs','gapsMs',
    'pageMessagesTarget','pageMessagesReceived','pageMessagesSpanMs'],'animation fields');
  assert.equal(value.durationRequestedMs,5000);finite(value.durationObservedMs,'observed animation');assert.ok(value.durationObservedMs>=5000);
  safeInteger(value.frameCount,'frame count');assert.ok(value.frameCount>0);safeInteger(value.gapCount,'gap count');
  assert.ok(Array.isArray(value.gapsMs));assert.equal(value.gapCount,value.gapsMs.length);
  assert.ok(value.frameCount<=4096,'gap series hit source cap; cannot verify complete duration');assert.equal(value.frameCount,value.gapCount);
  for(const gap of value.gapsMs)finite(gap,'frame gap');
  const sorted=value.gapsMs.slice().sort((a,b)=>a-b),p=f=>sorted[Math.min(sorted.length-1,Math.floor(sorted.length*f))];
  assert.equal(value.medianGapMs,p(0.5),'source floor-index median');assert.equal(value.p95GapMs,p(0.95),'source floor-index p95');assert.equal(value.maxGapMs,p(1),'source maximum');
  const sum=value.gapsMs.reduce((a,b)=>a+b,0);near(sum,value.durationObservedMs,'all gap intervals telescope to observed duration');
  assert.equal(value.pageMessagesTarget,256);assert.equal(value.pageMessagesReceived,256);finite(value.pageMessagesSpanMs,'message completion span');
  assert.ok(value.pageMessagesSpanMs<=value.durationObservedMs,'messages complete inside animation');
  return {gapSumMs:sum,gapSumErrorMs:sum-value.durationObservedMs,completeGapSeries:true,
    jsRafCallbacksPerObservedSecond:value.frameCount*1000/value.durationObservedMs,
    nominalFiveSecondOvershootMs:value.durationObservedMs-5000,configuredWatchdogWallTimeExceeded:value.durationObservedMs>15000};
}
function geometry(protocol){
  const one=name=>{const rows=protocol.filter(r=>r.record===name);assert.equal(rows.length,1,'one '+name);return rows[0];};
  const request=one('neutral-window-request'),actual=one('neutral-window-actual');
  keys(request,['record','clientWidth','clientHeight','outerWidth','outerHeight','workLeft','workTop','workWidth','workHeight','screenWidth','screenHeight','line'],'window request fields');
  keys(actual,['record','clientQueried','clientWidth','clientHeight','outerQueried','outerLeft','outerTop','outerWidth','outerHeight','line'],'window actual fields');
  for(const row of [request,actual])for(const [name,value]of Object.entries(row))if(!['record','clientQueried','outerQueried'].includes(name))assert.ok(Number.isSafeInteger(value),'integer window '+name);
  assert.equal(request.clientWidth,860);assert.equal(request.clientHeight,580);
  for(const name of ['outerWidth','outerHeight','workWidth','workHeight','screenWidth','screenHeight'])assert.ok(request[name]>0,'positive '+name);
  assert.ok(request.outerWidth>=request.clientWidth&&request.outerHeight>=request.clientHeight,'non-client frame encloses client');
  assert.ok(request.outerWidth<=request.workWidth&&request.outerHeight<=request.workHeight,'requested frame fits work area');
  assert.equal(actual.clientQueried,true,'GetClientRect observed');assert.equal(actual.outerQueried,true,'GetWindowRect observed');
  for(const name of ['clientWidth','clientHeight','outerWidth','outerHeight'])assert.equal(actual[name],request[name],'actual equals requested '+name);
  assert.equal(actual.outerLeft,request.workLeft+Math.floor((request.workWidth-request.outerWidth)/2),'centered window x');
  assert.equal(actual.outerTop,request.workTop+Math.floor((request.workHeight-request.outerHeight)/2),'centered window y');
  assert.ok(actual.outerLeft>=request.workLeft&&actual.outerTop>=request.workTop,'frame starts in work area');
  assert.ok(actual.outerLeft+actual.outerWidth<=request.workLeft+request.workWidth&&actual.outerTop+actual.outerHeight<=request.workTop+request.workHeight,'frame ends in work area');
  const start=protocol.filter(r=>r.record==='neutral-native-phase'&&r.phase==='host-start-begin');assert.equal(start.length,1);
  assert.ok(request.line<actual.line&&actual.line<start[0].line,'geometry observed before host start');
  const clean=row=>Object.fromEntries(Object.entries(row).filter(([key])=>!['record','line'].includes(key)));
  return {request:clean(request),actual:clean(actual),fixedClientSizeMatched:true,frameFitsWorkArea:true,centeringArithmeticMatched:true};
}
function validateArm(arm){
  const pageBytes=arm.artifacts.get('page-result');assert.ok(pageBytes.length<=128*1024,'page writer 128 KiB bound');
  const page=json(pageBytes,'page result');keys(page,['schema','hostKind','diagnosticOnly','syntheticInput','physicalPresentation','completed','loadCount','navigation','animation','refresh','scope'],'page root');
  assert.equal(page.schema,'soui.neutral-throughput-smoke/v1');assert.equal(page.hostKind,'soui-miniblink');
  assert.equal(page.diagnosticOnly,true);assert.equal(page.syntheticInput,false);assert.equal(page.physicalPresentation,'not-measured');
  assert.equal(page.completed,true);assert.equal(page.loadCount,2);assert.ok(Array.isArray(page.navigation));assert.equal(page.navigation.length,2);
  ready(page.navigation[0],1);ready(page.navigation[1],2);const arithmetic=animation(page.animation);
  keys(page.refresh,['requested','documentReady','bridgeReconnected','nativeMonotonicTiming'],'refresh fields');
  for(const key of ['requested','documentReady','bridgeReconnected'])assert.equal(page.refresh[key],true);
  assert.equal(page.refresh.nativeMonotonicTiming,'See refresh-requested and page-ready tickMs records in host output.');
  assert.equal(page.scope,'Same-resource 5-second page scheduling and refresh smoke; no input latency or production qualification.');
  const childBytes=arm.artifacts.get('child-log'),lines=childBytes.toString('utf8').split(/\r*\n/),protocol=[];
  for(let index=0;index<lines.length;++index){const line=lines[index];if(!line.includes('"record":"neutral-'))continue;
    const row=json(Buffer.from(line,'utf8'),'native protocol line');assert.ok(['neutral-native-phase','neutral-page-status','neutral-window-request','neutral-window-actual'].includes(row.record));
    if(['neutral-native-phase','neutral-page-status'].includes(row.record))safeInteger(row.tickMs,'native monotonic tick');protocol.push({...row,line:index+1});}
  const windowGeometry=geometry(protocol);
  const statuses=protocol.filter(r=>r.record==='neutral-page-status');assert.equal(statuses.length,3,'exactly two loads and one refresh request');
  assert.deepEqual(statuses.map(r=>[r.data.phase,r.data.load]),[['page-ready',1],['refresh-requested',1],['page-ready',2]]);
  for(let index=0;index<2;++index){const native=statuses[index===0?0:2].data;keys(native,['phase','load','domReadyMs','bridgeReadyMs','readyState'],'native ready status');
    const {phase,...actual}=native;assert.deepEqual(actual,page.navigation[index],'native status matches saved/reloaded page receipt');}
  keys(statuses[1].data,['phase','load','observedAnimationMs'],'refresh status');assert.equal(statuses[1].data.observedAnimationMs,page.animation.durationObservedMs);
  const phases=protocol.filter(r=>r.record==='neutral-native-phase');for(const row of phases)assert.ok(PHASES.includes(row.phase),'known native phase');
  const one=name=>{const matches=phases.filter(r=>r.phase===name);assert.equal(matches.length,1,'one native '+name);return matches[0];};
  const start=one('host-start-begin'),closeBegin=one('host-close-begin'),closeEnd=one('host-close-complete'),runtimeBegin=one('runtime-close-begin'),runtimeEnd=one('runtime-close-complete');
  const sequence=[start,statuses[0],statuses[1],statuses[2],closeBegin,closeEnd,runtimeBegin,runtimeEnd];
  for(let i=1;i<sequence.length;++i){assert.ok(sequence[i].line>sequence[i-1].line,'native/status semantic phase order');assert.ok(sequence[i].tickMs>=sequence[i-1].tickMs,'native monotonic phase ticks');}
  assert.ok(statuses[1].tickMs-statuses[0].tickMs+32>=page.animation.durationObservedMs,'coarse native interval covers page animation');
  assert.equal(lines.filter(r=>r==='benchmark.result.begin').length,1,'one result sink begin');assert.equal(lines.filter(r=>r==='benchmark.result.commit').length,1,'one result sink commit');
  return {bucket:arm.bucket,execution:arm.execution,page,windowGeometry,
    artifacts:[id(childBytes,arm.bucket+'/child.log'),id(pageBytes,arm.bucket+'/page-result.json')],arithmetic,
    nativeDurationsMs:{hostStartToInitialReady:statuses[0].tickMs-start.tickMs,
      initialReadyToRefreshRequest:statuses[1].tickMs-statuses[0].tickMs,
      refreshRequestToReady:statuses[2].tickMs-statuses[1].tickMs,
      hostClose:closeEnd.tickMs-closeBegin.tickMs,runtimeClose:runtimeEnd.tickMs-runtimeBegin.tickMs},
    nativeDocumentReadyObservationCount:phases.filter(r=>r.phase==='document-ready').length,
    protocolMonotonicTicks:sequence.map(r=>({phase:r.phase||r.data.phase,load:r.data?.load||null,tickMs:r.tickMs}))};
}
function verifyInputs(deploy,inputs){
  const sourcePins=[];for(const [relative,expected]of Object.entries(SOURCE_PINS)){const bytes=fs.readFileSync(path.join(inputs,relative));assert.equal(hash(bytes),expected,'inspected source changed: '+relative);sourcePins.push(id(bytes,relative));}
  const sealBytes=fs.readFileSync(path.join(deploy,'immutable-manifest.json')),seal=json(sealBytes,'immutable manifest');
  assert.equal(hash(sealBytes),'b320cc1092e2a4e74848e74f560d8774c235de04e831e38fe9aa508da7c03b8b','reviewed profile2 seal');
  assert.ok(fs.readFileSync(path.join(inputs,'immutable-manifest.json')).equals(sealBytes),'top and deployed seal byte equality');
  assert.equal(seal.schema,'soui.neutral-throughput-immutable-inputs/v1');assert.equal(seal.revision,2);assert.equal(seal.sdkVersion,'0.5.0');
  assert.equal(seal.files.length,28);assert.equal(new Set(seal.files.map(r=>r.path)).size,28,'unique sealed payloads');
  assert.deepEqual(seal.mutableKernelSlots,BUCKETS.map(b=>b+'/node.dll'));
  const files=[];for(const row of seal.files){assert.ok(!path.isAbsolute(row.path)&&!row.path.split(/[\\/]/).includes('..'),'relative payload path');const bytes=fs.readFileSync(path.join(deploy,row.path));assert.equal(bytes.length,row.bytes);assert.equal(hash(bytes),row.sha256,'sealed file changed: '+row.path);files.push(row);}
  const kernelBytes=fs.readFileSync(path.join(deploy,'kernel-inputs.json')),kernels=json(kernelBytes,'kernel bindings');
  assert.equal(hash(kernelBytes),'c0ae23448eb77c6a8bdc4a3f4662c92bf96ce437fdad44a7fe9302ff13c74e00','reviewed profile2 kernel manifest');
  assert.equal(kernels.schema,'soui.neutral-throughput-kernel-inputs/v1');assert.equal(kernels.revision,2);assert.equal(kernels.immutableManifestSha256,hash(sealBytes));assert.equal(kernels.candidateInputsReady,true);
  for(const flag of ['candidatesFromBuild3','baselinesFromFrozen050'])assert.equal(kernels[flag],true);
  assert.equal(kernels.performanceAccepted,false);assert.equal(kernels.releaseReady,false);assert.equal(kernels.files.length,4);
  const externalReferences=[];
  function reference(row,label){assert.ok(path.isAbsolute(row.path),'external reference path');const bytes=fs.readFileSync(row.path);assert.equal(bytes.length,row.bytes,label+' byte identity');assert.equal(hash(bytes),row.sha256,label+' hash identity');externalReferences.push({...id(bytes,label),externalHeld:true});return json(bytes,label);}
  const build=reference(kernels.finalKernelBuildSummary,'final-kernel-builds.json');
  assert.equal(build.schema,'soui.input-latency-kernel-builds/v1');assert.equal(build.source.identity,kernels.finalKernelBuildSummary.sourceIdentity,'final source identity bound');assert.equal(build.passed,true);
  const frozen=reference(kernels.frozenSdkDeliveryManifest,'frozen-sdk-0.5.0-manifest.json');assert.equal(frozen.schema,'soui.source-sdk/v1');assert.equal(frozen.version,'0.5.0');
  const portableKernels=[];
  for(const bucket of BUCKETS){const matches=kernels.files.filter(row=>row.path===bucket+'/node.dll');assert.equal(matches.length,1);const row=matches[0],bytes=fs.readFileSync(path.join(deploy,row.path));
    assert.equal(bytes.length,row.bytes);assert.equal(hash(bytes),row.sha256);const pe=bytes.readUInt32LE(0x3c);assert.equal(bytes.readUInt16LE(0),0x5a4d);assert.equal(bytes.readUInt32LE(pe),0x4550);
    assert.equal(bytes.readUInt16LE(pe+4),bucket.endsWith('x86')?0x14c:0x8664);assert.equal(row.machine,bytes.readUInt16LE(pe+4));
    assert.ok(path.isAbsolute(row.sourcePath));assert.ok(fs.readFileSync(row.sourcePath).equals(bytes),'actual provenance DLL equals deployed '+bucket);
    const arch=bucket.endsWith('x86')?'x86':'x64',portable={path:row.path,bytes:row.bytes,sha256:row.sha256,machine:row.machine,sourceArtifactLabel:bucket+'/source-dll',externalSourceByteEqual:true};
    if(bucket.startsWith('candidate-')){assert.ok(row.buildReceipt);const receipt=reference(row.buildReceipt,bucket+'/build-Miniblink.json');assert.equal(receipt.schema,'soui.dependency-build/v1');assert.equal(receipt.architecture,arch);assert.equal(receipt.status,'built');assert.equal(receipt.component,'Miniblink');assert.equal(receipt.kernelLlvm.sourceImmutable,true);assert.equal(receipt.kernelLlvm.inputFilesStable,true);
      const a=receipt.artifacts.filter(r=>r.kind==='miniblink');assert.equal(a.length,1);assert.equal(a[0].sha256.toLowerCase(),row.sha256);assert.equal(path.normalize(a[0].path),path.normalize(row.sourcePath));
      const final=build.builds.filter(r=>r.architecture===arch);assert.equal(final.length,1);assert.equal(final[0].sourceIdentity,build.source.identity);assert.deepEqual(final[0].candidate,{bytes:row.bytes,sha256:row.sha256,machine:row.machine});
      portable.buildReceiptArtifactLabel=bucket+'/build-Miniblink.json';
    }else{assert.equal(row.buildReceipt,null);const a=frozen.files.filter(r=>r.path==='third_party/prebuilt/'+arch+'/miniblink/node.dll');assert.equal(a.length,1);assert.equal(a[0].bytes,row.bytes);assert.equal(a[0].sha256,row.sha256);}
    portableKernels.push(portable);
  }
  for(const arch of ['x86','x64'])assert.notEqual(kernels.files.find(r=>r.path==='baseline-'+arch+'/node.dll').sha256,kernels.files.find(r=>r.path==='candidate-'+arch+'/node.dll').sha256);
  const mediaBytes=fs.readFileSync(path.join(inputs,'media-manifest.json')),media=json(mediaBytes,'media manifest');assert.equal(media.schema,'soui.neutral-throughput-media/v1');assert.equal(media.revision,2);assert.equal(media.passed,true);
  const iso=fs.readFileSync(media.iso.path);assert.equal(iso.length,140705792);assert.equal(hash(iso),'10acd494027b60c83a255d21b236d220b66803e008ba4c42eca573ee87a5d8ee');assert.equal(iso.length,media.iso.bytes);assert.equal(hash(iso),media.iso.sha256);
  assert.equal(media.immutableManifest.sha256,hash(sealBytes));assert.equal(media.kernelInputs.sha256,hash(kernelBytes));assert.equal(media.allIsoFilesMatchDeployment,true);assert.equal(media.allSealsMatchIso,true);
  const expected=[...files,...kernels.files.map(({path,bytes,sha256})=>({path,bytes,sha256})),{path:'immutable-manifest.json',bytes:sealBytes.length,sha256:hash(sealBytes)},{path:'kernel-inputs.json',bytes:kernelBytes.length,sha256:hash(kernelBytes)}].sort((a,b)=>a.path.localeCompare(b.path));
  assert.deepEqual([...media.files].sort((a,b)=>a.path.localeCompare(b.path)),expected,'ISO verifier lists exact 34 deployed files');
  return {profile:2,immutableManifest:id(sealBytes,'immutable-manifest.json'),kernelBindings:id(kernelBytes,'kernel-inputs.json'),sourcePins,sealedFileCount:files.length,kernels:portableKernels,
    sourceIdentity:build.source.identity,externalReferences,externalReferencesVerifiedFromActualFiles:true,
    iso:id(iso,'throughput-smoke-2.iso'),mediaVerification:id(mediaBytes,'media-manifest.json'),isoContentsVerifiedByBoundExternalReport:true,isoContentsIndependentlyReparsedByThisAnalyzer:false,
    sameHostPerArchitecture:true,samePageResourcesAcrossFourArms:true};
}
function comparison(results){return ['x86','x64'].map(architecture=>{const before=results.find(r=>r.bucket==='baseline-'+architecture),after=results.find(r=>r.bucket==='candidate-'+architecture);
  const metrics=r=>({frameCount:r.page.animation.frameCount,observedAnimationMs:r.page.animation.durationObservedMs,
    jsRafCallbacksPerObservedSecond:r.arithmetic.jsRafCallbacksPerObservedSecond,medianGapMs:r.page.animation.medianGapMs,
    p95GapMs:r.page.animation.p95GapMs,maxGapMs:r.page.animation.maxGapMs,pageMessagesSpanMs:r.page.animation.pageMessagesSpanMs,
    initialBridgeReadySampleMs:r.page.navigation[0].bridgeReadyMs,reloadBridgeReadySampleMs:r.page.navigation[1].bridgeReadyMs,
    ...r.nativeDurationsMs,wholeChildElapsedMs:r.execution.elapsedMs});const baseline=metrics(before),candidate=metrics(after),delta={};
  for(const key of Object.keys(baseline))delta[key]=candidate[key]-baseline[key];
  return {architecture,baseline,candidate,deltaCandidateMinusBaseline:delta};});}
function selfTest(){
  assert.deepEqual(json(Buffer.from('{"a":[1,true,null]}'),'self-test'),{a:[1,true,null]});
  assert.throws(()=>json(Buffer.from('{"a":1,"a":2}'),'self-test'));
  assert.throws(()=>json(Buffer.from('{"a":{"b":1,"\\u0062":2}}'),'self-test'));
  near([1,2,3].reduce((a,b)=>a+b,0),6,'gap arithmetic unit');assert.throws(()=>near(6,7,'gap arithmetic negative unit'));
  const gaps=[3,1,4,2],sorted=gaps.slice().sort((a,b)=>a-b);assert.equal(sorted[Math.floor(gaps.length*.5)],3);assert.equal(sorted[Math.floor(gaps.length*.95)],4);
  process.stdout.write(JSON.stringify({offlineHelperChecksPassed:true,guestDataAnalyzed:false,nativeExecuted:false,performanceAccepted:false})+'\n');
}
function checkOnly(summaryPath=path.join(__dirname,'neutral-throughput-results.json'),rawPath=path.join(__dirname,'raw/neutral-throughput-serial.bin')){
  const summaryBytes=fs.readFileSync(summaryPath),published=json(summaryBytes,'published neutral throughput results');
  assert.equal(published.schema,'soui.neutral-throughput-guest-results/v1');assert.equal(published.profile,2);
  assert.equal(published.analysisStatus,'four-arms-protocol-and-arithmetic-validated');
  const raw=fs.readFileSync(rawPath),decoded=decodeTransport(raw),results=decoded.arms.map(validateArm);
  for(const result of results.slice(1))assert.deepEqual(result.windowGeometry,results[0].windowGeometry,'same observed geometry across all four arms');
  const transport={...decoded.transport,artifactLabel:'raw/neutral-throughput-serial.bin'};
  for(const [key,value]of Object.entries(transport))assert.deepEqual(published.rawSerial[key],value,'published transport '+key);
  assert.equal(published.rawSerial.lineEndingTransformationApplied,false);
  assert.equal(published.rawSerial.crCount,[...raw].filter(value=>value===13).length,'raw CR count');
  assert.equal(published.rawSerial.lfCount,[...raw].filter(value=>value===10).length,'raw LF count');
  assert.deepEqual(published.terminal,decoded.terminal,'published exact launcher terminal');
  assert.deepEqual(published.results,results,'every published arm field matches recorded raw');
  assert.deepEqual(published.fixedGeometryAcrossAllFourArms,results[0].windowGeometry,'published fixed geometry');
  assert.deepEqual(published.comparison,comparison(results),'published comparison derived from recorded arms');
  assert.equal(published.totalChildElapsedMs,results.reduce((sum,result)=>sum+result.execution.elapsedMs,0),'published total child elapsed');
  assert.deepEqual(published.assertions,{
    oneSerialBeginAndEnd:true,declaredPayloadByteCountExact:true,
    oneLauncherBeginAndEnd:true,uniqueArmsInFixedOrder:true,
    exactOneChildLogAndOnePageResultPerArm:true,naturalZeroExitAndJobEmptyPerArm:true,
    noTimeoutOrForcedCleanup:true,allMessagesReceivedPerArm:256,loadsPerArm:2,
    requestedAnimationMsPerArm:5000,allGapSeriesComplete:true,
    sourceQuantilesAndGapDurationArithmeticMatched:true,
    nativeAndSavedPageStatusMatched:true,refreshAndBothClosurePhasesObserved:true,
    fixedClientAndCenteringMatched:true
  },'published assertions correspond to the strict checks');
  assert.equal(published.allFourArmsStrictlyCompleted,true);assert.equal(published.benchmarkFunctionalPass,true);
  assert.equal(published.allFunctionalArmsPassed,true);assert.equal(published.functionalArmsPassed,results.length);
  assert.equal(published.runtimeExecutedInGuest,true);assert.equal(published.nativeSubjectsExecutedByThisAnalyzer,false);
  assert.equal(published.performanceAccepted,false);assert.equal(published.releaseReady,false);
  assert.equal(published.launcherProcessExitObserved,false);
  return {mode:'check-only',recordedEvidenceRevalidated:true,arms:results.length,
    rawSerial:id(raw,'raw/neutral-throughput-serial.bin'),publishedSummary:id(summaryBytes,'neutral-throughput-results.json'),
    transportLifecycleAndPageArithmeticRevalidated:true,externalInputsReverified:false,
    previousRunExternalRawReverified:false,nativeExecuted:false,
    scope:'Current recorded transport, four-arm lifecycle, geometry and page arithmetic only; external DLL/source origins and prior external raw are not reverified',
    performanceAccepted:false,releaseReady:false,launcherProcessExitObserved:false};
}
function main(){
  const args=process.argv.slice(2);if(args.length===1&&args[0]==='--self-test'){selfTest();return;}
  if(args.length===1&&args[0]==='--check-only'){process.stdout.write(JSON.stringify(checkOnly(),null,2)+'\n');return;}
  const opts={};for(let i=0;i<args.length;i+=2){assert.ok(['--serial','--output','--deploy','--inputs'].includes(args[i]),'known named option');assert.ok(args[i+1]&&!opts[args[i]],'option value/uniqueness');opts[args[i]]=args[i+1];}
  for(const required of ['--serial','--output','--deploy'])assert.ok(opts[required]&&path.isAbsolute(opts[required]),'required absolute '+required);
  assert.ok(!fs.existsSync(opts['--output']),'fresh output directory only');
  const inputs=opts['--inputs']||path.dirname(opts['--deploy']);assert.ok(path.isAbsolute(inputs));
  const bindings=verifyInputs(opts['--deploy'],inputs),raw=fs.readFileSync(opts['--serial']),decoded=decodeTransport(raw),results=decoded.arms.map(validateArm);
  for(const result of results.slice(1))assert.deepEqual(result.windowGeometry,results[0].windowGeometry,'same observed geometry across all four arms');
  const summary={schema:'soui.neutral-throughput-strict-analysis/v1',profile:2,allFourArmsStrictlyCompleted:true,
    rawSerial:decoded.transport,inputBindings:bindings,terminal:decoded.terminal,results,comparison:comparison(results),
    scope:'Neutral 5-second page scheduling, 256 page messages, one reload and native closure smoke only',
    limitations:{fixedArmOrder:BUCKETS,repetitionsPerArm:1,randomized:false,orderBalanced:false,
      cacheAndWarmupEffectsSeparated:false,physicalPresentationMeasured:false,endToEndInputMeasured:false,
      sourceClockComparison:'Navigation performance.now clocks reset on reload; refresh duration uses native tickMs only',
      gapMeaning:'JavaScript requestAnimationFrame callback intervals; not physical presented frame intervals'},
    performanceAccepted:false,releaseReady:false,launcherProcessExitObserved:false};
  fs.mkdirSync(opts['--output']);fs.writeFileSync(path.join(opts['--output'],'raw-serial.bin'),raw,{flag:'wx'});
  for(const arm of decoded.arms){const dir=path.join(opts['--output'],arm.bucket);fs.mkdirSync(dir);fs.writeFileSync(path.join(dir,'child.log'),arm.artifacts.get('child-log'),{flag:'wx'});fs.writeFileSync(path.join(dir,'page-result.json'),arm.artifacts.get('page-result'),{flag:'wx'});}
  fs.writeFileSync(path.join(opts['--output'],'strict-summary.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});
  const table=['| 架构 | 版本 | rAF 回调数 | 观察时长 ms | gap 中位数 ms | gap p95 ms | gap 最大值 ms | 256 消息 ms | 刷新至就绪 ms | 宿主关闭 ms | runtime 关闭 ms |','|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|'];
  const f=n=>Number.isInteger(n)?String(n):n.toFixed(3);
  for(const pair of summary.comparison)for(const variant of ['baseline','candidate']){const r=pair[variant];table.push(`| ${pair.architecture} | ${variant} | ${r.frameCount} | ${f(r.observedAnimationMs)} | ${f(r.medianGapMs)} | ${f(r.p95GapMs)} | ${f(r.maxGapMs)} | ${f(r.pageMessagesSpanMs)} | ${r.refreshRequestToReady} | ${r.hostClose} | ${r.runtimeClose} |`);}
  table.push('','固定顺序、每臂单次的中性短测。rAF 是 JavaScript 回调间隔；未测物理呈现或端到端输入延迟。performanceAccepted=false，releaseReady=false。','');
  fs.writeFileSync(path.join(opts['--output'],'comparison.md'),table.join('\n'),{flag:'wx'});
  process.stdout.write(JSON.stringify({output:opts['--output'],allFourArmsStrictlyCompleted:true,performanceAccepted:false,releaseReady:false,comparison:summary.comparison},null,2)+'\n');
}
if(require.main===module){try{main();}catch(error){process.stderr.write('Strict throughput analysis failed: '+error.message+'\n');process.exitCode=1;}}
module.exports={decodeTransport,validateArm,animation,json,verifyInputs,geometry,checkOnly};
