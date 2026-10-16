'use strict';
// Independent read-only subreader. No SDK import, actor, file write or budget.
const crypto=require('node:crypto');
const U64=(1n<<64n)-1n,I64=(1n<<63n)-1n,MAX_BYTES=32*1024*1024,MAX_ROWS=8192;
class QuotaError extends Error{constructor(code){super(code);this.code=code;}}
const need=(ok,code)=>{if(!ok)throw new QuotaError(code);};
function reason(error){try{return typeof error?.code==='string'?error.code:'quota-reader-invalid';}catch{return 'quota-reader-invalid';}}
function keys(value,names,code){need(value&&typeof value==='object'&&!Array.isArray(value),code);const actual=Object.keys(value);need(actual.length===names.length&&names.every(n=>Object.hasOwn(value,n)),code);}
function u64(value){need(typeof value==='string'&&/^(?:0|[1-9][0-9]{0,19})$/.test(value),'quota-canonical-u64');const n=BigInt(value);need(n<=U64,'quota-u64-overflow');return n;}
function positive(value,code){const n=u64(value);need(n>0n,code);return n;}
function u32(value,code){const n=positive(value,code);need(n<=0xffffffffn,code);return n;}
function rowInteger(value,code){need(Number.isSafeInteger(value)&&value>=0,code);return BigInt(value);}
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),hash=v=>typeof v==='string'&&/^[a-f0-9]{64}$/.test(v);

// Preserve original UTF-8 bytes and reject duplicate object fields before decode.
// Limits bound parser work; quota counters remain decimal strings, never Number.
function strictJson(text){
  let at=0,nodes=0;const white=()=>{while(/[\x20\t\r\n]/.test(text[at]||'!'))++at;};
  function string(){const start=at++;while(at<text.length){const c=text[at++];if(c==='"'){const s=JSON.parse(text.slice(start,at));for(const ch of s){const cp=ch.codePointAt(0);need(cp<0xd800||cp>0xdfff,'quota-json-unpaired-surrogate');}return s;}if(c==='\\')++at;}throw new QuotaError('quota-json-string');}
  function value(depth){need(depth<=64&&++nodes<=32768,'quota-json-bound');white();const c=text[at];
    if(c==='"')return string();
    if(c==='{'){++at;const obj=Object.create(null),seen=new Set();white();if(text[at]==='}'){++at;return obj;}for(;;){white();need(text[at]==='"','quota-json-object-key');const key=string();need(!seen.has(key),'quota-json-duplicate-field');seen.add(key);white();need(text[at++]===':','quota-json-colon');obj[key]=value(depth+1);white();const end=text[at++];if(end==='}')return obj;need(end===',','quota-json-object-end');}}
    if(c==='['){++at;const arr=[];white();if(text[at]===']'){++at;return arr;}for(;;){need(arr.length<4096,'quota-json-array-bound');arr.push(value(depth+1));white();const end=text[at++];if(end===']')return arr;need(end===',','quota-json-array-end');}}
    for(const [word,v]of [['true',true],['false',false],['null',null]])if(text.startsWith(word,at)){at+=word.length;return v;}
    const m=/^-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?/.exec(text.slice(at));need(m,'quota-json-value');at+=m[0].length;const n=Number(m[0]);need(Number.isFinite(n)&&(!Number.isInteger(n)||Number.isSafeInteger(n)),'quota-json-number-bound');return n;
  }
  const out=value(0);white();need(at===text.length,'quota-json-trailing');return out;
}
function decodeRecords(raw){
  need(Buffer.isBuffer(raw)&&raw.length>0&&raw.length<=MAX_BYTES,'quota-log-byte-bound');
  need(!(raw[0]===0xef&&raw[1]===0xbb&&raw[2]===0xbf),'quota-log-bom');
  let text;try{text=new TextDecoder('utf-8',{fatal:true}).decode(raw);}catch{throw new QuotaError('quota-log-utf8');}
  need(text.endsWith('\n')&&!text.includes('\r'),'quota-log-lf-framing');const lines=text.slice(0,-1).split('\n');need(lines.length<=MAX_ROWS,'quota-log-row-bound');
  return lines.map(line=>{need(line.length>0&&Buffer.byteLength(line,'utf8')<=512*1024,'quota-log-line-bound');const row=strictJson(line);need(row&&typeof row==='object'&&!Array.isArray(row)&&typeof row.event==='string','quota-log-row-shape');return row;});
}
const bindingKeys=['schema','inputsMatched','inputBindingScope','sourceSha256','payloadSha256','consumerBuildId','rawLogSha256','ownedLogSha256','contextSha256','subject','executablePin','expectedPolicy','expectedRun'];
function checkBinding(binding,rawSha){
  keys(binding,bindingKeys,'quota-binding-shape');need(binding.schema==='soui.acceptance-task-store-quota-binding/1'&&binding.inputBindingScope==='complete-acceptance-inputs','quota-binding-schema');
  need(binding.inputsMatched===true,'quota-inputs-unmatched');for(const name of ['sourceSha256','payloadSha256','rawLogSha256','ownedLogSha256','contextSha256'])need(hash(binding[name]),'quota-binding-hash');need(binding.rawLogSha256===rawSha,'quota-raw-log-sha-differs');
  need(typeof binding.consumerBuildId==='string'&&binding.consumerBuildId.length>0&&binding.consumerBuildId.length<=256,'quota-build-identity');
  keys(binding.subject,['pid','creationFileTime'],'quota-subject-shape');need(Number.isSafeInteger(binding.subject.pid)&&binding.subject.pid>0&&binding.subject.pid<=0xffffffff,'quota-subject-pid');positive(binding.subject.creationFileTime,'quota-subject-time');
  keys(binding.executablePin,['path','bytes','sha256'],'quota-executable-pin');const p=binding.executablePin;
  need(typeof p.path==='string'&&/^[A-Za-z]:[\\/]/.test(p.path)&&!/[\x00-\x1f]/.test(p.path)&&!p.path.slice(2).includes(':')&&!p.path.split(/[\\/]/).includes('..')&&/(?:^|[\\/])SouiAcceptance\.exe$/i.test(p.path)&&Number.isSafeInteger(p.bytes)&&p.bytes>0&&hash(p.sha256),'quota-executable-pin');
  const policy=binding.expectedPolicy===null?null:u64(binding.expectedPolicy);
  keys(binding.expectedRun,['mode','cycles','warmup'],'quota-run-binding');need(binding.expectedRun.mode==='reload','quota-run-mode');const cycles=positive(binding.expectedRun.cycles,'quota-run-cycles'),warmup=u64(binding.expectedRun.warmup);need(cycles<=100000n&&warmup<=1000n,'quota-run-bound');
  return {policy,cycles,warmup,subject:binding.subject};
}
const recordKeys=['schema','scope','state','getterStatus','captureState','maxLiveRequestedBytes','poolEpoch','poolEpochValid','creditValid','chargedLiveRequestedBytes','chargedPeakRequestedBytes','successfulLiveRequestedBytes','successfulPeakRequestedBytes','deniedRequests','deniedRequestsExact','diagnosticsServiceEpoch','diagnosticsServiceState','capture','productionDefaultGate','fullCacheBudgetGate','calibrationPin'];
function point(view,parentClock,subject,expectedPolicy){
  const r=view?.taskStoreRequestQuota;need(r!==undefined,'quota-record-missing');keys(r,recordKeys,'quota-record-fields');
  need(r.schema==='soui.acceptance-task-store-request-quota/1'&&r.scope==='task-store-private-request-byte-quota','quota-record-schema');
  need(r.captureState==='valid','quota-native-capture-invalid');
  need(['available','busy','invalid','unavailable'].includes(r.state)&&r.getterStatus===r.state,'quota-status-differs');
  for(const n of ['poolEpochValid','creditValid','deniedRequestsExact'])need(typeof r[n]==='boolean','quota-record-bool');
  need(r.productionDefaultGate==='not-evaluated'&&r.fullCacheBudgetGate==='not-evaluated'&&r.calibrationPin===null,'quota-unproven-budget');
  const epoch=u64(r.poolEpoch),live=u64(r.chargedLiveRequestedBytes),peak=u64(r.chargedPeakRequestedBytes),success=u64(r.successfulLiveRequestedBytes),successfulPeak=u64(r.successfulPeakRequestedBytes),denied=u64(r.deniedRequests),cap=r.maxLiveRequestedBytes===null?null:u64(r.maxLiveRequestedBytes);
  need(r.poolEpochValid===(epoch>0n)&&epoch!==U64,'quota-pool-identity-invalid');
  need(success<=live&&live<=peak&&success<=successfulPeak&&successfulPeak<=peak,'quota-live-peak-invariant');
  const diag=u64(r.diagnosticsServiceEpoch),states=['ready','disabled','busy','not_started','wrong_owner_thread','unavailable'];need(states.includes(r.diagnosticsServiceState),'quota-diagnostic-status');
  const old=view?.bridge?.task_service?.observation;need(old&&old.owner_epoch===r.diagnosticsServiceEpoch&&old.state===r.diagnosticsServiceState,'quota-diagnostic-epoch-differs');
  if(r.diagnosticsServiceState==='ready')need(diag>0n&&diag!==U64,'quota-diagnostic-identity-invalid');
  const c=r.capture;keys(c,['windowId','windowHandle','pid','creationFileTime','captureThreadId','windowThreadId','identityValid','clock'],'quota-capture-fields');
  const window=positive(c.windowId,'quota-window'),handle=positive(c.windowHandle,'quota-window-handle'),thread=u32(c.captureThreadId,'quota-thread'),windowThread=u32(c.windowThreadId,'quota-window-thread');
  need(c.identityValid===true&&u32(c.pid,'quota-pid')===BigInt(subject.pid)&&c.creationFileTime===subject.creationFileTime,'quota-subject-or-native-identity');
  need(thread===windowThread&&view.window===c.windowId&&view.captureThreadId===c.captureThreadId,'quota-window-or-thread-differs');
  const clock=c.clock;keys(clock,['source','unit','frequency','begin','end','valid'],'quota-clock-fields');
  const frequency=positive(clock.frequency,'quota-clock-frequency'),begin=positive(clock.begin,'quota-clock-begin'),end=positive(clock.end,'quota-clock-end');
  need(clock.source==='QueryPerformanceCounter'&&clock.unit==='ticks'&&clock.valid===true&&frequency<=I64&&begin<=I64&&end<=I64&&end>=begin,'quota-clock-invalid');
  keys(parentClock,['valid','qpc','frequency','captureThreadId','phase'],'quota-parent-clock-fields');
  need(parentClock.valid===true&&parentClock.phase==='synchronous-native-read-complete'&&parentClock.captureThreadId===c.captureThreadId&&u64(parentClock.frequency)===frequency&&positive(parentClock.qpc,'quota-parent-qpc')>=end,'quota-parent-clock-differs');
  let state='available',failure=null;
  if(r.state==='busy'||r.state==='unavailable'){
    need(epoch===0n&&!r.poolEpochValid&&!r.creditValid&&!r.deniedRequestsExact&&cap===null&&[live,peak,success,successfulPeak,denied].every(n=>n===0n),'quota-unavailable-facts-not-cleared');state='partial';failure='quota-'+r.state;
  }else{
    need(cap===expectedPolicy,'quota-immutable-policy-differs');
    if(r.state==='available'){need(epoch>0n&&r.creditValid&&r.deniedRequestsExact,'quota-available-invalid');need(cap===null||peak<=cap,'quota-cap-exceeded');}
    else{need(!r.poolEpochValid||!r.creditValid||!r.deniedRequestsExact,'quota-invalid-with-valid-flags');if(!r.deniedRequestsExact)need(denied===U64,'quota-inexact-denial-not-saturated');state='invalid';failure='quota-invalid';}
  }
  return {state,failure,raw:r,window:c.windowId,tuple:{windowHandle:handle.toString(),pid:c.pid,creationFileTime:c.creationFileTime,threadId:thread.toString(),frequency:frequency.toString()},begin,end,epoch,cap,peak,successfulPeak,denied,live,success,diag};
}
function analyzeTaskStoreQuota(raw,binding){
  const out={schema:'soui.task-store-request-quota-observations/1',scope:'task-store-private-request-byte-quota',binding:binding??null,bindingGate:'not-evaluated',dataGate:'partial',points:[],counts:{available:0,busy:0,unavailable:0,invalid:0,missing:0,retired:0},failure:null,observedPolicy:null,productionDefaultGate:'not-evaluated',fullCacheBudgetGate:'not-evaluated',calibrationPin:null,finalDisposalReceiptObserved:false,qualified:false,releaseReady:false};
  let invalid=false,partial=false,firstInvalid=null;const failures=[];const reject=(code,bad=true)=>{failures.push(code);if(bad){invalid=true;firstInvalid??=code;}else partial=true;};
  let records,expected;try{records=decodeRecords(raw);expected=checkBinding(binding,sha(raw));out.bindingGate='matched';}catch(error){out.bindingGate='invalid';out.dataGate='invalid';out.failure=reason(error);return out;}
  const lives=new Map(),usedEpochs=new Set(),samplePhases=new Set(),cycleSamples=new Set();let beginCount=0,endCount=0,reloads=0n,previousFrame=null,previousCaller=null,mountCount=0,closeCount=0,lastSamplePhase=null;
  const observeCallerClock=clock=>{
    keys(clock,['valid','qpc','frequency','captureThreadId','phase'],'quota-parent-clock-fields');
    const qpc=positive(clock.qpc,'quota-parent-qpc'),frequency=positive(clock.frequency,'quota-parent-frequency'),thread=u32(clock.captureThreadId,'quota-parent-thread');
    need(clock.valid===true&&clock.phase==='synchronous-native-read-complete'&&qpc<=I64&&frequency<=I64,'quota-parent-clock-invalid');
    if(previousCaller)need(qpc>=previousCaller.qpc&&frequency===previousCaller.frequency&&thread===previousCaller.thread,'quota-parent-clock-reversed-or-tuple-changed');
    previousCaller={qpc,frequency,thread};
  };
  const add=(view,parentClock,native,retired,row,index)=>{
    const result={eventIndex:index,event:row.event,phase:row.phase??null,cycle:row.cycle??null,window:view?.window??null,retired,state:'partial',failure:null,raw:view?.taskStoreRequestQuota??null};
    try{
      const life=lives.get(view?.window);need(life,'quota-owner-lifetime-missing');
      need(view.ownerAlive===true&&view.hostClosed===retired&&view.lifetime===(retired?'after-host-close-before-owner-destruction':'owner-alive'),'quota-owner-lifetime-differs');
      if(!retired)need(native?.producer==='native-sdk-owner'&&native.pid===String(expected.subject.pid)&&native.creationFileTime===expected.subject.creationFileTime&&native.captureThreadId===view.captureThreadId,'quota-native-subject-differs');
      const p=point(view,parentClock,expected.subject,expected.policy);result.state=p.state;result.failure=p.failure;
      if(previousCaller)need(p.begin>=previousCaller.qpc&&BigInt(p.tuple.frequency)===previousCaller.frequency&&BigInt(p.tuple.threadId)===previousCaller.thread,'quota-frame-predates-prior-caller');
      if(life.tuple)need(JSON.stringify(life.tuple)===JSON.stringify(p.tuple),'quota-caller-tuple-changed');else life.tuple=p.tuple;
      if(previousFrame)need(p.begin>previousFrame.begin&&p.begin>=previousFrame.end&&p.tuple.frequency===previousFrame.frequency&&p.tuple.threadId===previousFrame.threadId,'quota-capture-clock-reversed-or-replayed');
      previousFrame={begin:p.begin,end:p.end,frequency:p.tuple.frequency,threadId:p.tuple.threadId};
      if(p.epoch>0n){
        if(life.epoch!==null)need(p.epoch===life.epoch,'quota-pool-changed-within-owner');else{need(!usedEpochs.has(p.epoch.toString()),'quota-pool-reused-after-owner');life.epoch=p.epoch;usedEpochs.add(p.epoch.toString());}
        if(life.diag!==null&&p.diag>0n)need(p.diag===life.diag,'quota-diagnostic-owner-changed');else if(p.diag>0n)life.diag=p.diag;
        const prior=life.previous;if(prior)need(p.peak>=prior.peak&&p.successfulPeak>=prior.successfulPeak&&p.denied>=prior.denied,'quota-monotonic-counter-reversed');
        life.previous=p;
      }
      result.observation={poolEpoch:p.epoch.toString(),diagnosticsServiceEpoch:p.diag.toString(),policyKind:p.state==='partial'?'unobserved':p.cap===null?'unconfigured':p.cap===0n?'zero':'finite',maxLiveRequestedBytes:p.cap===null?null:p.cap.toString(),chargedLiveRequestedBytes:p.live.toString(),chargedPeakRequestedBytes:p.peak.toString(),successfulLiveRequestedBytes:p.success.toString(),successfulPeakRequestedBytes:p.successfulPeak.toString(),deniedRequests:p.denied.toString(),beginQpc:p.begin.toString(),endQpc:p.end.toString(),callerTuple:p.tuple};
      if(p.state==='available')++out.counts.available;else{reject(p.failure,p.state==='invalid');if(p.state==='invalid')++out.counts.invalid;else ++out.counts[p.raw.state];}
      if(retired)++out.counts.retired;
    }catch(error){result.failure=reason(error);result.state=result.failure==='quota-record-missing'?'partial':'invalid';reject(result.failure,result.state==='invalid');++out.counts[result.state==='invalid'?'invalid':'missing'];}
    out.points.push(result);
  };
  for(const [index,row]of records.entries()){
    try{
      if(['run.failed','step.failed','cleanup.failed'].includes(row.event))reject('quota-prior-native-failure');
      if(row.event==='session.report'&&row.data?.phase==='failed')reject('quota-prior-business-failure');
      if(row.event==='run.begin'){
        need(++beginCount===1&&endCount===0&&row.mode==='reload'&&rowInteger(row.cycles,'quota-run-cycles-shape')===expected.cycles&&rowInteger(row.warmup,'quota-run-warmup-shape')===expected.warmup&&row.pid===expected.subject.pid&&row.creationFileTime===expected.subject.creationFileTime,'quota-run-begin-differs');
      }
      if(row.event==='step.complete'&&row.action==='mount'){
        need(beginCount===1&&endCount===0&&row.window===1&&++mountCount===1&&samplePhases.has('process')&&!samplePhases.has('mounted')&&!lives.has('1'),'quota-mount-identity');lives.set('1',{epoch:null,diag:null,tuple:null,previous:null});
      }
      if(row.event==='step.complete'&&row.action==='reload'){
        need(beginCount===1&&endCount===0&&row.window===1&&lives.has('1')&&rowInteger(row.cycle,'quota-reload-cycle')===reloads+1n,'quota-reload-order');++reloads;
      }
      if(row.event==='sample'){
        need(beginCount===1&&endCount===0,'quota-sample-outside-run');const native=row.nativeSdk;need(Array.isArray(native?.views),'quota-view-roster-missing');
        need(['process','mounted','cycle','closed','runtime-closed'].includes(row.phase),'quota-sample-phase');
        const priorPhase=lastSamplePhase;need(row.phase==='process'?priorPhase===null:row.phase==='mounted'?priorPhase==='process':row.phase==='cycle'?(priorPhase==='mounted'||priorPhase==='cycle'):row.phase==='closed'?priorPhase==='cycle':priorPhase==='closed','quota-sample-phase-order');lastSamplePhase=row.phase;
        if(row.phase==='cycle'){const cycle=rowInteger(row.cycle,'quota-sample-cycle');need(cycle===reloads&&cycle>0n&&!cycleSamples.has(cycle.toString()),'quota-cycle-sample-order');cycleSamples.add(cycle.toString());}
        else{need(!samplePhases.has(row.phase),'quota-sample-phase-replayed');samplePhases.add(row.phase);}
        if(row.phase==='process')need(lives.size===0&&reloads===0n,'quota-process-sample-phase');
        if(row.phase==='mounted')need(lives.size===1&&reloads===0n,'quota-mounted-sample-phase');
        if(row.phase==='closed'||row.phase==='runtime-closed')need(lives.size===0,'quota-closed-sample-phase');
        const seen=new Set();for(const view of native.views){need(typeof view?.window==='string'&&!seen.has(view.window)&&lives.has(view.window),'quota-view-roster-differs');seen.add(view.window);add(view,native.captureClock,native,false,row,index);}
        need(seen.size===lives.size,'quota-view-roster-differs');
        need(native.producer==='native-sdk-owner'&&native.pid===String(expected.subject.pid)&&native.creationFileTime===expected.subject.creationFileTime&&native.captureThreadId===native.captureClock?.captureThreadId,'quota-native-subject-differs');observeCallerClock(native.captureClock);
      }
      if(Object.hasOwn(row,'retiredOwner')){
        need(row.event==='step.complete'&&row.action==='close'&&row.window===1&&lives.has('1')&&++closeCount===1&&mountCount===1&&reloads===expected.cycles+expected.warmup&&row.retiredOwner?.window==='1','quota-retired-replay-or-window');add(row.retiredOwner,row.nativeRenderer?.captureClock,null,true,row,index);lives.delete('1');
      }else if(row.event==='step.complete'&&row.action==='close'){need(row.window===1&&lives.has('1')&&++closeCount===1&&mountCount===1&&reloads===expected.cycles+expected.warmup,'quota-close-order');reject('quota-retired-sample-missing',false);lives.delete(String(row.window));}
      if(row.event==='step.complete'&&row.nativeRenderer?.captureClock)observeCallerClock(row.nativeRenderer.captureClock);
      if(row.event==='run.complete')need(++endCount===1&&beginCount===1&&lives.size===0&&mountCount===1&&closeCount===1&&lastSamplePhase==='runtime-closed'&&reloads===expected.cycles+expected.warmup,'quota-run-complete-differs');
    }catch(error){reject(reason(error));}
  }
  if(beginCount!==1||endCount!==1||mountCount!==1||closeCount!==1||out.counts.available===0||BigInt(cycleSamples.size)!==expected.cycles+expected.warmup||samplePhases.size!==4)reject('quota-complete-reload-samples-missing',false);
  out.observedPolicy=out.counts.available?{kind:expected.policy===null?'unconfigured':expected.policy===0n?'zero':'finite',maxLiveRequestedBytes:expected.policy===null?null:expected.policy.toString()}:null;
  out.reloadsObserved=reloads.toString();out.cycleSamplesObserved=String(cycleSamples.size);
  out.dataGate=invalid?'invalid':partial?'partial':'matched';out.failure=invalid?firstInvalid:failures[0]??null;return out;
}
module.exports={u64,decodeRecords,analyzeTaskStoreQuota};
