'use strict';

// Separate passive manual evidence. Never accepts BurstInput ACKs or native IDs.
const fs = require('node:fs');
const crypto = require('node:crypto');
const {TextDecoder, isDeepStrictEqual} = require('node:util');
const path = require('node:path');
const U64_MAX = 18446744073709551615n;
const MARKER_REGION = [8,336,64,32], CONTENT_PROBE = [10,330], CAPTURE_REGION = [8,328,64,40];
function insist(value, message) { if (!value) throw new Error(message); }
function integer(value, minimum=0, maximum=Number.MAX_SAFE_INTEGER) { return Number.isSafeInteger(value) && value>=minimum && value<=maximum; }
function wide(value, positive=false) {
  insist(typeof value==='string' && /^(0|[1-9][0-9]{0,19})$/.test(value), 'Exact uint64 string required');
  const result=BigInt(value);insist(result<=U64_MAX && (!positive || result>0n), 'uint64 out of range');return result;
}
function keys(value, required, optional=[]) {
  insist(value && typeof value==='object' && !Array.isArray(value), 'Object required');
  insist(required.every(key=>Object.hasOwn(value,key)) && Object.keys(value).every(key=>required.includes(key)||optional.includes(key)), 'Missing or unknown field');
}
function crc16(bytes) {
  let value=65535;
  for(let i=0;i<16;i++) {
    if(i===12||i===13)continue;
    value^=bytes[i]<<8;
    for(let bit=0;bit<8;bit++)value=((value<<1)^((value&32768)?4129:0))&65535;
  }
  return value;
}
function decodeMarkerHex(hex) {
  insist(typeof hex==='string' && /^[a-f0-9]{32}$/.test(hex), 'Manual marker must be 16 lowercase hex bytes');
  const bytes=Buffer.from(hex,'hex');
  insist(bytes[0]===167 && bytes[1]===77, 'Wrong manual marker magic');
  insist(crc16(bytes)===(bytes[12]|(bytes[13]<<8)), 'Manual marker CRC16 mismatch');
  insist((bytes[11]&~3)===0, 'Manual marker reserved bits set');
  const nonce=bytes.readUInt32LE(2), revision=bytes.readUIntLE(6,3), frame=bytes.readUInt16LE(9), flags=bytes[11], pageEpoch=bytes.readUInt16LE(14);
  insist(nonce!==0 && revision<1048576 && frame>0, 'Manual marker capacity/identity invalid');
  insist(pageEpoch!==0 || (flags&1)!==0,'Manual marker has no page epoch');
  return {nonce,revision,frame,flags,pageEpoch,overflow:!!(flags&1),geometryInvalid:!!(flags&2)};
}
function expectedColor(revision) { return [(revision*73)%256,(revision*151)%256,(revision*29)%256]; }
function loadObservation(filename) {
  insist(typeof filename==='string'&&path.isAbsolute(filename)&&!filename.includes('\0'),'Absolute observation path required');
  const stat=fs.lstatSync(filename);insist(stat.isFile()&&!stat.isSymbolicLink()&&stat.nlink===1,'Plain observation file required');
  insist(stat.size>0&&stat.size<=64*1024*1024,'Observation file size invalid');
  const fd=fs.openSync(filename,'r');let bytes;
  try {
    const held=fs.fstatSync(fd);insist(held.ino===stat.ino&&held.dev===stat.dev&&held.size===stat.size,'Observation changed while opening');
    bytes=fs.readFileSync(fd);const after=fs.fstatSync(fd),named=fs.lstatSync(filename);
    insist(after.size===held.size&&after.mtimeMs===held.mtimeMs&&named.ino===held.ino&&named.dev===held.dev,'Observation changed while reading');
  } finally {fs.closeSync(fd);}
  insist(bytes.length===stat.size&&bytes.at(-1)===10,'Observation must end in newline');
  const text=new TextDecoder('utf-8',{fatal:true}).decode(bytes),lines=text.slice(0,-1).split('\n');
  insist(lines.length>=2&&lines.length<=250003,'Observation line count invalid');
  return {rows:lines.map(line=>{insist(line.length>0&&line.length<4096,'Observation line size invalid');return JSON.parse(line);}),
    source:{path:fs.realpathSync(filename),bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')}};
}
function readLines(filename) {return loadObservation(filename).rows;}

const ARTIFACT_ROLES=['host','kernel','pageHtml','pageScript','collector','reader'];
function artifactBindings(binding, revalidate=false) {
  keys(binding,['frozen','before','after']);
  const normalize=rows=>{
    insist(Array.isArray(rows)&&rows.length===ARTIFACT_ROLES.length,'Six explicit artifact roles required');
    const result=new Map();
    for(const row of rows) {
      keys(row,['role','path','bytes','sha256']);
      insist(ARTIFACT_ROLES.includes(row.role)&&!result.has(row.role),'Duplicate/unknown artifact role');
      insist(typeof row.path==='string'&&path.isAbsolute(row.path)&&!row.path.includes('\0')&&integer(row.bytes,1,1024*1024*1024)&&typeof row.sha256==='string'&&/^[a-f0-9]{64}$/.test(row.sha256),'Invalid artifact identity');
      result.set(row.role,row);
    }
    insist(new Set(rows.map(row=>path.resolve(row.path).toLowerCase())).size===rows.length,'Artifact paths alias');
    return ARTIFACT_ROLES.map(role=>result.get(role));
  };
  const frozen=normalize(binding.frozen),before=normalize(binding.before),after=normalize(binding.after);
  insist(isDeepStrictEqual(frozen,before)&&isDeepStrictEqual(frozen,after),'Artifact endpoint identities differ from freeze');
  if(revalidate)for(const row of frozen) {
    const stat=fs.lstatSync(row.path);insist(stat.isFile()&&!stat.isSymbolicLink()&&stat.nlink===1&&stat.size===row.bytes,'Artifact file shape changed');
    const fd=fs.openSync(row.path,'r');
    try {
      const held=fs.fstatSync(fd),state=crypto.createHash('sha256'),buffer=Buffer.allocUnsafe(65536);let size=0,count;
      insist(held.ino===stat.ino&&held.dev===stat.dev&&held.size===stat.size,'Artifact changed while opening');
      while((count=fs.readSync(fd,buffer,0,buffer.length,null))){size+=count;insist(size<=stat.size,'Artifact grew');state.update(buffer.subarray(0,count));}
      const last=fs.fstatSync(fd);insist(size===stat.size&&last.size===stat.size&&last.mtimeMs===held.mtimeMs&&state.digest('hex')===row.sha256,'Artifact bytes changed');
    } finally {fs.closeSync(fd);}
  }
  return {pins:frozen,endpointPinsConsistent:true,currentFilesRevalidated:revalidate,
    continuousIdentityObserved:false,actualLoadedModulesAuthenticated:false};
}

const SCHEMA='soui.manual-desktop-observation/v1';
const COMMON=['schema','event','run_id'];
const SAMPLE_REASONS=['marker-pixels','marker-magic','marker-crc','marker-counter','marker-flags','marker-nonce','marker-epoch',
  'probe-mismatch','cursor-overlap','cursor-unavailable','accumulated-frames','protected-content','content-alias','present-before-phase'];
const FATAL_REASONS=['startup-timeout','target-closed','window-destroyed','window-identity','window-hidden','foreground-lost',
  'geometry-changed','dpi-changed','output-changed','epoch-changed','counter-regressed','present-regressed','graphics-failed',
  'capture-failed','gpu-read-timeout','capture-budget-exceeded','observation-deadline-exceeded','capture-clock-regressed',
  'output-limit','persist-failed','observer-failed','wait-timer-failed','wait-timer-close-failed'];
const SAMPLE_KEYS=COMMON.concat(['phase','sampleIndex','captureBeginQpc','captureEndQpc','lastPresentQpc','accumulatedFrames',
  'pointerOnly','status','reason','markerHex','contentRgb','captureBgraCrc32','revision','frame','flags','pageEpoch',
  'contentChange','markerOnly','frameSequenceGap','revisionDelta']);
const COUNTERS=['samples','startupSamples','captureSamples','validSamples','contentChanges','markerOnlySamples','duplicateSamples',
  'pointerOnlySamples','timeoutPolls','unknownSamples','accumulatedDesktopFrames','unobservedDesktopFrames','frameSequenceGapTotal'];
function analyzeManualObservation(rows, expected) {
  insist(Array.isArray(rows)&&rows.length>=2&&rows.length<=250003,'Observation record count invalid');
  keys(expected,['run_id','targetPid','targetCreationFileTime','observerPid','observerCreationFileTime','hwnd','qpcFrequency','intervalMs','durationMs','startupMs','artifacts'],
    ['phaseBeginQpc','phaseEndQpc','captureBackend']);
  const expectedBackend=expected.captureBackend??'dxgi';
  insist(expectedBackend==='dxgi'||expectedBackend==='gdi-bitblt','Unknown expected capture backend');
  insist(typeof expected.run_id==='string'&&/^[a-f0-9]{32}$/.test(expected.run_id)&&!/^0+$/.test(expected.run_id),'Run identity invalid');
  for(const field of ['targetPid','observerPid'])insist(integer(expected[field],1,4294967295),'PID invalid');
  for(const field of ['targetCreationFileTime','observerCreationFileTime','hwnd','qpcFrequency'])wide(expected[field],true);
  insist(expected.targetPid!==expected.observerPid,'Host and observer must be separate processes');
  insist(integer(expected.intervalMs,1,1000)&&integer(expected.durationMs,1000,120000)&&integer(expected.startupMs,1000,60000),'Invalid requested time');
  const artifacts=artifactBindings(expected.artifacts),first=rows[0],last=rows.at(-1),qpf=wide(expected.qpcFrequency,true);
  const nonce=parseInt(expected.run_id.slice(-8),16)||1;
  insist(qpf>=1000n&&qpf<=U64_MAX/200000n,'Unsupported QPC frequency');
  for(const row of rows)insist(row&&row.schema===SCHEMA&&row.run_id===expected.run_id,'Observation schema/run mismatch');
  keys(first,COMMON.concat(['markerVersion','targetPid','targetCreationFileTime','hwnd','qpcFrequency','observerPid','observerCreationFileTime',
    'startupBeginQpc','startupDeadlineQpc','intervalMs','durationMs','startupMs','markerRegion','contentProbe','captureRegion']),
    ['waitTimerBackend','captureBackend']);
  insist(first.event==='manual-observation.begin'&&first.markerVersion==='manual-feedback-v1','Wrong observation begin/marker version');
  const captureBackend=first.captureBackend??'dxgi';
  insist(captureBackend==='dxgi'||captureBackend==='gdi-bitblt','Unknown capture backend');
  insist(captureBackend===expectedBackend,'Capture backend differs from the expected frozen selection');
  const waitTimerBackend=Object.hasOwn(first,'waitTimerBackend')?first.waitTimerBackend:'unknown';
  insist(!Object.hasOwn(first,'waitTimerBackend')||['high-resolution-waitable','standard-waitable'].includes(waitTimerBackend),'Unknown wait timer backend');
  for(const field of ['targetPid','targetCreationFileTime','hwnd','qpcFrequency','observerPid','observerCreationFileTime','intervalMs','durationMs','startupMs'])
    insist(first[field]===expected[field],'Observation identity/request differs: '+field);
  insist(isDeepStrictEqual(first.markerRegion,MARKER_REGION)&&isDeepStrictEqual(first.contentProbe,CONTENT_PROBE)&&isDeepStrictEqual(first.captureRegion,CAPTURE_REGION),'Observation ROI differs');
  const startupBegin=wide(first.startupBeginQpc,true),startupDeadline=wide(first.startupDeadlineQpc,true);
  insist(startupDeadline===startupBegin+BigInt(first.startupMs)*qpf/1000n,'Startup deadline changed');
  keys(last,COMMON.concat(['phaseBeginQpc','phaseEndQpc','phaseDeadlineQpc',...COUNTERS,'firstContentQpc','lastContentQpc','headTicks','tailTicks',
    'completed','observationQualified','reason','physicalPresentation','inputToPhotonMeasured','performanceGate']),['captureBackend']);
  insist(last.event==='manual-observation.end','Missing observation end');
  insist((last.captureBackend??'dxgi')===captureBackend,'Capture backend changed during observation');
  for(const key of ['completed','observationQualified'])insist(typeof last[key]==='boolean','Missing terminal qualification');
  for(const key of ['physicalPresentation','inputToPhotonMeasured','performanceGate'])insist(last[key]===false,'Unsupported physical/performance claim');
  const unobservableCounters=['pointerOnlySamples','accumulatedDesktopFrames','unobservedDesktopFrames'];
  for(const key of COUNTERS) {
    if(captureBackend==='gdi-bitblt'&&unobservableCounters.includes(key))insist(last[key]===null,'GDI capture fabricated a DXGI counter: '+key);
    else insist(integer(last[key]),'Invalid terminal counter');
  }
  const counts=Object.fromEntries(COUNTERS.map(key=>[key,0]));
  let phase=null,baseline=null,previous=null,previousCaptureEnd=startupBegin,previousPresent=0n;
  let firstContent=null,lastContent=null,pageEpoch=null,maxCaptureGap=0n,previousCapturePhaseEnd=null,lastTrustedPresent=null;
  const events=[],unknownReasons={},nominalOverrunSamples=[];
  let output=null;
  for(const row of rows.slice(1,-1)) {
    if(row.event==='manual-observation.output') {
      keys(row,COMMON.concat(['deviceName','left','top','right','bottom','originX','originY','region']));
      insist(captureBackend==='gdi-bitblt'&&!output&&typeof row.deviceName==='string'&&/^\\\\\.\\DISPLAY[1-9][0-9]*$/.test(row.deviceName)&&
        ['left','top','right','bottom','originX','originY'].every(key=>Number.isSafeInteger(row[key]))&&row.right>row.left&&row.bottom>row.top&&
        isDeepStrictEqual(row.region,CAPTURE_REGION)&&row.originX+8>=row.left&&row.originX+72<=row.right&&
        row.originY+328>=row.top&&row.originY+368<=row.bottom,'Invalid GDI output/ROI binding');
      output=row;continue;
    }
    if(row.event==='manual-observation.phase') {
      keys(row,COMMON.concat(['phase','phaseBeginQpc','phaseDeadlineQpc']));
      insist(!phase&&row.phase==='capture'&&baseline,'Capture phase requires one accepted startup baseline');
      const begin=wide(row.phaseBeginQpc,true),deadline=wide(row.phaseDeadlineQpc,true);
      insist(begin===previousCaptureEnd&&begin<startupDeadline,'Capture phase does not follow baseline within startup deadline');
      insist(deadline===begin+BigInt(first.durationMs)*qpf/1000n,'Capture deadline changed');
      phase={begin,deadline};continue;
    }
    keys(row,SAMPLE_KEYS,captureBackend==='gdi-bitblt'?['visibleQpc']:[]);
    insist(row.event==='manual-observation.sample','Unexpected observation event');
    insist(phase||!baseline,'Accepted baseline must immediately open capture phase');
    insist(row.phase===(phase?'capture':'startup'),'Sample phase/order mismatch');
    insist(row.sampleIndex===++counts.samples,'Observation sample sequence missing/duplicated');
    counts[phase?'captureSamples':'startupSamples']++;
    const begin=wide(row.captureBeginQpc,true),end=wide(row.captureEndQpc,true);
    const present=captureBackend==='dxgi'?wide(row.lastPresentQpc):0n;
    const visible=captureBackend==='gdi-bitblt'?wide(row.visibleQpc,true):present;
    insist(begin>=previousCaptureEnd&&end>=begin&&
      (captureBackend==='dxgi'?present<=end:row.lastPresentQpc===null&&visible<=end&&visible-begin<=qpf/10n),
      'Sample clock order/budget invalid');previousCaptureEnd=end;
    if(captureBackend==='dxgi'&&present>0n){insist(present>previousPresent,'Present QPC repeated/regressed');previousPresent=present;}
    if(captureBackend==='gdi-bitblt'){insist(output&&visible>=begin&&visible<=end&&visible>previousPresent,'GDI visible QPC/output order invalid');previousPresent=visible;}
    if(phase){insist(begin>=phase.begin&&begin<phase.deadline,'Acquisition started outside fixed capture phase');
      if(captureBackend==='gdi-bitblt')insist(end<phase.deadline&&visible<phase.deadline,'GDI capture completed after its fixed deadline');
      if(previousCapturePhaseEnd!==null&&begin-previousCapturePhaseEnd>maxCaptureGap)maxCaptureGap=begin-previousCapturePhaseEnd;
      previousCapturePhaseEnd=end;if(end>phase.deadline)nominalOverrunSamples.push(row.sampleIndex);
    } else {
      insist(begin<startupDeadline,'Startup acquisition started after deadline');
      if(captureBackend==='gdi-bitblt')insist(end<startupDeadline,'GDI startup capture completed after its deadline');
    }
    if(captureBackend==='dxgi')insist(integer(row.accumulatedFrames,0,4294967295)&&typeof row.pointerOnly==='boolean','Invalid acquisition counters');
    else insist(row.accumulatedFrames===null&&row.pointerOnly===null,'GDI capture fabricated DXGI acquisition data');
    insist(['valid','baseline-reject','pointer-only','wait-timeout','unknown'].includes(row.status),'Unknown sample status');
    insist(row.reason===null||SAMPLE_REASONS.includes(row.reason),'Unknown sample reason');
    if(row.status==='unknown'||row.status==='baseline-reject')insist(row.reason!==null,'Rejected acquisition lacks reason');
    for(const key of ['contentChange','markerOnly'])insist(typeof row[key]==='boolean','Invalid content classification');
    for(const key of ['frameSequenceGap','revisionDelta'])insist(integer(row[key],0,1048575),'Invalid sample delta');
    const hasMarker=row.markerHex!==null,hasProbe=row.contentRgb!==null;
    if(hasMarker)insist(typeof row.markerHex==='string'&&/^[a-f0-9]{32}$/.test(row.markerHex),'Invalid raw marker hex');
    if(hasProbe)insist(Array.isArray(row.contentRgb)&&row.contentRgb.length===3&&row.contentRgb.every(value=>integer(value,0,255)),'Invalid content RGB');
    if(row.captureBgraCrc32!==null)insist(typeof row.captureBgraCrc32==='string'&&/^[a-f0-9]{8}$/.test(row.captureBgraCrc32),'Invalid capture CRC32');
    let decoded=null,decodeError=null;
    if(hasMarker){try{decoded=decodeMarkerHex(row.markerHex);}catch(error){decodeError=error;}}
    if(hasMarker){const bytes=Buffer.from(row.markerHex,'hex'),raw={revision:bytes.readUIntLE(6,3),frame:bytes.readUInt16LE(9),flags:bytes[11],pageEpoch:bytes.readUInt16LE(14)};
      for(const key of ['revision','frame','flags','pageEpoch'])insist(row[key]===raw[key],'Raw marker field mismatch: '+key);
    } else for(const key of ['revision','frame','flags','pageEpoch'])insist(row[key]===null,'Unreadable marker fabricates fields');
    if(row.status==='valid') {
      insist(decoded&&!decodeError&&hasProbe&&row.captureBgraCrc32!==null&&row.reason===null&&!row.pointerOnly,'Valid sample lacks checked pixels');
      insist(decoded.nonce===nonce&&decoded.flags===0&&decoded.pageEpoch>0&&visible>0n&&
        (captureBackend==='gdi-bitblt'||row.accumulatedFrames===1),'Valid sample marker identity/flags/acquisition invalid');
      insist(isDeepStrictEqual(row.contentRgb,expectedColor(decoded.revision)),'Valid probe differs from original feedback');
      if(pageEpoch!==null)insist(decoded.pageEpoch===pageEpoch,'Page generation changed');
      if(!phase){baseline={row,decoded};previous=baseline;pageEpoch=decoded.pageEpoch;}
    }
    if(!phase)insist(!row.contentChange&&!row.markerOnly&&row.frameSequenceGap===0&&row.revisionDelta===0,'Startup counted as capture work');
    if(captureBackend==='dxgi'&&(row.status==='wait-timeout'||row.pointerOnly)) {
      insist(!hasMarker&&!hasProbe&&row.captureBgraCrc32===null&&!row.contentChange&&!row.markerOnly&&present===0n,'Empty acquisition fabricates pixels');
      if(row.status==='wait-timeout')insist(!row.pointerOnly&&row.accumulatedFrames===0&&row.reason===null,'Timeout classification mismatch');
      else insist(row.status===(row.accumulatedFrames?(phase?'unknown':'baseline-reject'):'pointer-only')&&row.reason===(row.accumulatedFrames?'accumulated-frames':null),'Pointer-only classification mismatch');
    }
    if(captureBackend==='dxgi'&&row.status==='pointer-only')insist(row.pointerOnly,'Pointer-only flag missing');
    if(captureBackend==='gdi-bitblt')insist(!['pointer-only','wait-timeout'].includes(row.status),'GDI sample cannot be a DXGI wait/pointer record');
    if(row.contentChange)insist(phase&&row.status==='valid'&&row.revisionDelta>0&&
      (captureBackend==='gdi-bitblt'||row.accumulatedFrames<=1)&&!row.markerOnly,'Unqualified content change');
    if(phase) {
      insist(row.status!=='baseline-reject','Baseline rejection outside startup');
      const trusted=decoded&&decoded.nonce===nonce&&decoded.flags===0&&decoded.pageEpoch>0&&hasProbe&&
        isDeepStrictEqual(row.contentRgb,expectedColor(decoded.revision))&&row.captureBgraCrc32!==null&&
        !['cursor-overlap','cursor-unavailable','protected-content'].includes(row.reason);
      let duplicate=false;
      if(trusted) {
        insist(row.reason===null||['accumulated-frames','content-alias','present-before-phase'].includes(row.reason),'Trusted pixels have incompatible rejection reason');
        insist(decoded.pageEpoch===pageEpoch,'Page epoch changed within capture');
        insist(previous&&decoded.frame>=previous.decoded.frame&&decoded.revision>=previous.decoded.revision,'Feedback/frame counter regressed');
        insist(decoded.frame!==previous.decoded.frame||decoded.revision===previous.decoded.revision,'Feedback revision changed without render');
        // A local rAF render can be superseded before desktop observation.
        // Its missing sequence value is not proof of a lost desktop update.
        const gap=Math.max(0,decoded.frame-previous.decoded.frame-1),delta=decoded.revision-previous.decoded.revision;
        const sameContent=isDeepStrictEqual(row.contentRgb,previous.row.contentRgb),sameMarker=row.markerHex===previous.row.markerHex;
        insist(row.frameSequenceGap===gap&&row.revisionDelta===delta,'Trusted sample delta differs from previous anchor');
        insist(row.markerOnly===(!sameMarker&&sameContent),'Marker-only classification differs from actual probe');
         insist(row.contentChange===(row.status==='valid'&&delta>0&&!sameContent&&
           (captureBackend==='gdi-bitblt'||row.accumulatedFrames<=1)&&visible>=phase.begin),'Content classification differs from actual probe');
         if(row.status==='valid')insist((captureBackend==='gdi-bitblt'||row.accumulatedFrames===1)&&visible>=phase.begin&&
           !(delta>0&&sameContent),'Lost or aliased content accepted as valid');
        duplicate=row.status==='valid'&&sameMarker;
        if(lastTrustedPresent&&lastTrustedPresent.present===visible)
          insist(lastTrustedPresent.crc===row.captureBgraCrc32&&lastTrustedPresent.marker===row.markerHex&&isDeepStrictEqual(lastTrustedPresent.rgb,row.contentRgb),'Same visible sample has inconsistent captured pixels');
        lastTrustedPresent={present:visible,crc:row.captureBgraCrc32,marker:row.markerHex,rgb:row.contentRgb};
        previous={row,decoded};
      } else insist(!row.contentChange&&!row.markerOnly&&row.frameSequenceGap===0&&row.revisionDelta===0,'Untrusted sample advances content anchor');
      if(row.status==='valid')counts.validSamples++;
      if(row.pointerOnly)counts.pointerOnlySamples++;
      if(row.status==='wait-timeout')counts.timeoutPolls++;
      if(row.status==='unknown'||row.status==='baseline-reject'){counts.unknownSamples++;unknownReasons[row.reason||'unspecified']=(unknownReasons[row.reason||'unspecified']||0)+1;}
      if(captureBackend==='dxgi') {
        counts.accumulatedDesktopFrames+=row.accumulatedFrames;
        counts.unobservedDesktopFrames+=Math.max(0,row.accumulatedFrames-1);
      }
      counts.frameSequenceGapTotal+=row.frameSequenceGap;
      if(row.status==='valid'&&row.markerOnly)counts.markerOnlySamples++;
      if(row.contentChange){counts.contentChanges++;firstContent??=visible;lastContent=visible;events.push({frame:row.frame,revision:row.revision,pageEpoch:row.pageEpoch,
        ...(captureBackend==='dxgi'?{lastPresentQpc:row.lastPresentQpc}:{visibleQpc:row.visibleQpc}),captureEndQpc:row.captureEndQpc});}
      if(duplicate)counts.duplicateSamples++;
    }
  }
  for(const key of COUNTERS) {
    if(captureBackend==='gdi-bitblt'&&unobservableCounters.includes(key))continue;
    insist(counts[key]===last[key],'Terminal count mismatch: '+key);
  }
  let window=null;
  if(phase) {
    const end=wide(last.phaseEndQpc,true);insist(last.phaseBeginQpc===String(phase.begin)&&last.phaseDeadlineQpc===String(phase.deadline)&&end>=previousCaptureEnd,'Terminal phase bounds differ');
    if(last.completed)insist(end>=phase.deadline,'Completed phase ended before fixed deadline');
    if(Object.hasOwn(expected,'phaseBeginQpc'))insist(last.phaseBeginQpc===expected.phaseBeginQpc,'External phase begin differs');
    if(Object.hasOwn(expected,'phaseEndQpc'))insist(last.phaseEndQpc===expected.phaseEndQpc,'External phase end differs');
    if(firstContent!==null){insist(firstContent>=phase.begin&&lastContent<=end,'Content observations outside phase');
      insist(last.firstContentQpc===String(firstContent)&&last.lastContentQpc===String(lastContent)&&last.headTicks===String(firstContent-phase.begin)&&last.tailTicks===String(end-lastContent),'Content coverage summary differs');
    } else for(const key of ['firstContentQpc','lastContentQpc','headTicks','tailTicks'])insist(last[key]===null,'Missing content coverage cannot be zero');
    window={phaseBeginQpc:last.phaseBeginQpc,phaseEndQpc:last.phaseEndQpc,phaseDeadlineQpc:last.phaseDeadlineQpc,qpcFrequency:first.qpcFrequency,
      nominalOverrunTicks:String(end>phase.deadline?end-phase.deadline:0n),
      source:captureBackend==='dxgi'?'DXGI output duplication acquisition':'synchronous GDI BitBlt software capture'};
  } else {
    insist(!last.completed&&!last.observationQualified&&counts.captureSamples===0,'Missing phase cannot complete');
    for(const key of ['phaseBeginQpc','phaseEndQpc','phaseDeadlineQpc','firstContentQpc','lastContentQpc','headTicks','tailTicks'])insist(last[key]===null,'Missing phase fabricates timing');
  }
  const eligible=!!phase&&last.completed&&counts.contentChanges>=2&&counts.unknownSamples===0&&
    (captureBackend==='gdi-bitblt'||counts.unobservedDesktopFrames===0);
  insist(last.observationQualified===eligible,'Terminal observation qualification contradicts evidence');
  if(last.completed)insist(last.reason===(eligible?null:(counts.unknownSamples||
    (captureBackend==='dxgi'&&counts.unobservedDesktopFrames)?'observation-gaps':'insufficient-content-changes')),'Terminal reason contradicts counts');
  else insist(FATAL_REASONS.includes(last.reason),'Fatal terminal needs explicit reason');
  let observedContentTiming={status:'unavailable',intervalTicks:null,summary:null,changesPerSecond:null};
  if(eligible) {
    const stamp=item=>wide(captureBackend==='dxgi'?item.lastPresentQpc:item.visibleQpc,true);
    const intervals=events.slice(1).map((item,index)=>stamp(item)-stamp(events[index]));
    insist(intervals.length>0&&intervals.every(value=>value>0n),'Distinct content observations need increasing observation timestamps');
    const sorted=intervals.slice().sort((a,b)=>a<b?-1:a>b?1:0),quantile=percent=>String(sorted[Math.ceil(sorted.length*percent/100)-1]);
    observedContentTiming={status:captureBackend==='dxgi'?'observed-content-intervals':'gdi-software-visible-intervals',
      captureBackend,timestampMeaning:captureBackend==='dxgi'?'DXGI LastPresentTime':'QPC immediately after BitBlt and GdiFlush',
      intervalTicks:intervals.map(String),qpcFrequency:String(qpf),
      summary:{samples:intervals.length,minimumTicks:String(sorted[0]),maximumTicks:String(sorted.at(-1)),p50Ticks:quantile(50),p95Ticks:quantile(95),p99Ticks:quantile(99),
        intervalsOver50ms:intervals.filter(value=>value*1000n>qpf*50n).length},
      changesPerSecond:{numerator:String(BigInt(counts.contentChanges)*qpf),denominator:String(wide(last.phaseEndQpc,true)-phase.begin),unit:'observed original-background changes/second',
        includesManualIdle:true,physicalFrameRate:false,workloadComparable:'unknown'}};
  }
  const reportedCounts={...counts};
  if(captureBackend==='gdi-bitblt')for(const key of unobservableCounters)reportedCounts[key]=null;
  return {schema:'soui.manual-frame-analysis/v1',run_id:expected.run_id,captureBackend,
    t7_endpoint:captureBackend==='dxgi'?'DXGI-LastPresentTime':'BitBlt-software-capture-end-not-physical-presentation',
    targetPid:expected.targetPid,targetCreationFileTime:expected.targetCreationFileTime,
    observerPid:expected.observerPid,observerCreationFileTime:expected.observerCreationFileTime,hwnd:expected.hwnd,window,pageEpoch,waitTimerBackend,
    traceQualified:eligible,observationQualified:false,artifactIdentity:artifacts,counts:reportedCounts,contentObservations:events,observedContentTiming,unknownReasons,
    allPageRendersObserved:false,intermediatePageRenderDisposition:'unknown',
    coverage:{headTicks:last.headTicks,tailTicks:last.tailTicks,maxAcquisitionGapTicks:String(maxCaptureGap),nominalOverrunSamples,continuousCoverageProven:false},
    physicalPresentation:false,inputToPhotonMeasured:false,inputReplay:false,nativeInputIds:false,uniqueScreenFrames:null,performanceGate:false,
    sourceScope:{pixelCrc16IndependentlyChecked:true,captureCrc32:'native-computed; raw ROI bytes are not present for independent recomputation',
      geometry:'native collector checks fixed client/DPI; raw stream does not report a continuous geometry series',naturalProcessExitVerified:false}};
}
function readManualObservation(filename, expected) {
  const loaded=loadObservation(filename),result=analyzeManualObservation(loaded.rows,expected);
  result.rawSource=loaded.source;
  result.artifactIdentity=artifactBindings(expected.artifacts,true);
  // A complete end line may precede a failed final native FlushFileBuffers.
  // Only the external held-process supervisor can combine natural exit/Job0
  // with these trace and artifact checks. File contents never substitute for it.
  return result;
}

module.exports={decodeMarkerHex,expectedColor,readLines,artifactBindings,analyzeManualObservation,readManualObservation};
