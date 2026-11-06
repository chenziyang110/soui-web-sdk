'use strict';

// Passive process-resource evidence only. QPC brackets select whole measured
// intervals; this module never crops lifetime CPU by a guessed duration ratio.
const fs=require('node:fs');
const {TextDecoder}=require('node:util');
const U64=(1n<<64n)-1n, CAPABILITY='qpc-interval-v1';
function need(value,message){if(!value)throw new Error(message);}
function object(value){return value&&typeof value==='object'&&!Array.isArray(value);}
function uint(value,minimum=0,maximum=Number.MAX_SAFE_INTEGER){return Number.isSafeInteger(value)&&value>=minimum&&value<=maximum;}
function exact(value,positive=false){
  need(typeof value==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(value),'Expected canonical decimal uint64 string');
  const result=BigInt(value);need(result<=U64&&(!positive||result>0n),'uint64 outside range');return result;
}
function identity(value){need(object(value)&&uint(value.pid,1,0xffffffff),'Invalid collector PID');exact(value.creationFileTime,true);}
function bounds(expected){
  need(object(expected)&&/^[a-f0-9]{32}$/.test(expected.run_id)&&!/^0+$/.test(expected.run_id),'Invalid resource run');
  need(uint(expected.targetPid,1,0xffffffff),'Invalid target PID');exact(expected.targetCreationFileTime,true);exact(expected.hwnd,true);
  const frequency=exact(expected.qpcFrequency,true),begin=exact(expected.phaseBeginQpc,true),end=exact(expected.phaseEndQpc,true);
  need(end>begin,'Invalid resource phase bounds');
  if(expected.samplerCollector!==undefined)identity(expected.samplerCollector);
  return {frequency,begin,end};
}
function distribution(values){
  if(!values.length)return null;
  const sorted=values.slice().sort((a,b)=>a-b),quantile=fraction=>sorted[Math.max(0,Math.ceil(sorted.length*fraction)-1)];
  return {samples:sorted.length,minimum:sorted[0],observedMaximum:sorted.at(-1),p50:quantile(0.5),p95:quantile(0.95)};
}
const processKey=value=>value.pid+':'+value.creationTime100ns;
function members(sample,first,collector,withCpu){
  need(Array.isArray(sample.processes)&&sample.processes.length>0&&sample.processes.length<=65536,'Missing bounded process members');
  const seen=new Set(),result=new Map(),sum={privateBytes:0,workingSetBytes:0,handles:0,threads:0};
  for(const member of sample.processes){
    need(object(member)&&uint(member.pid,1,0xffffffff)&&uint(member.parentPid,0,0xffffffff)&&!seen.has(member.pid),'Invalid/duplicate process member');
    seen.add(member.pid);exact(member.creationTime100ns,true);
    need(!collector||member.pid!==collector.pid,'Sampler collector was counted in application tree');
    for(const key of Object.keys(sum)){
      need(uint(member[key]),'Invalid process resource count');sum[key]+=member[key];need(uint(sum[key]),'Resource sum exceeds exact integer range');
    }
    const cpu=withCpu?exact(member.cpuTime100ns):null;result.set(processKey(member),{member,cpu});
  }
  const root=sample.processes[0];
  need(root.pid===first.rootPid&&root.creationTime100ns===first.rootCreationTime100ns,'Sample root identity differs');
  for(const key of Object.keys(sum))need(sample[key]===sum[key],'Sample resource total differs: '+key);
  return result;
}
function analyzeResourceWindow(records,expected){
  const window=bounds(expected);
  need(Array.isArray(records)&&records.length>=3&&records.length<=1000000,'Missing bounded resource stream');
  const first=records[0],last=records.at(-1),samples=records.slice(1,-1);
  need(first?.event==='run.begin'&&first.schema==='soui.process-tree-samples/v2','Exact v2 process-tree identity required');
  need(first.rootPid===expected.targetPid&&first.rootCreationTime100ns===expected.targetCreationFileTime&&
    first.expectedCreationTime100ns===expected.targetCreationFileTime,'Resource target identity differs');
  exact(first.rootCreationTime100ns,true);exact(first.startedAtUtcFiletime100ns,true);
  need(uint(first.intervalMs,1,60000)&&uint(first.durationMs,1,86400000),'Invalid sampler timing');
  const result={schema:'soui.manual-observation-resources/v1',run_id:expected.run_id,targetPid:expected.targetPid,
    targetCreationFileTime:expected.targetCreationFileTime,hwnd:expected.hwnd,
    window:{phaseBeginQpc:expected.phaseBeginQpc,phaseEndQpc:expected.phaseEndQpc,qpcFrequency:expected.qpcFrequency,
      source:'native passive observer actual phase'},
    status:'unavailable',reason:null,cpu:null,memory:null,membershipChanges:[],
    completeScope:'observed-processes-at-poll',allLifetimeProcessesObserved:false,sharedBrowserAttributionVerified:false,
    hwndAttribution:'observer phase association only; process resources include all target-process Hosts',
    workingSetIncludesSharedPages:true,gpuMemoryMeasured:false,physicalPresentationMeasured:false,
    recordDataQualified:false,windowDataQualified:false,observationQualified:false,collectionCompletionVerified:false,
    qualification:'Raw record/window consistency only; final flush/close and natural owned sampler completion require held external receipts',
    performanceVerified:false,releaseReady:false};
  if(first.clockCapability===undefined){
    // Old files retain their original lifetime meaning. Never invent QPC from
    // UTC/GetTickCount or transfer their CPU distribution into this phase.
    result.reason='qpc-capability-unavailable';return result;
  }
  need(first.clockCapability===CAPABILITY&&first.run_id===expected.run_id,'Unknown resource clock capability/run');
  need(first.qpcFrequency===expected.qpcFrequency,'Resource/observer QPC frequencies differ');
  const runBegin=exact(first.runBeginQpc,true);
  identity({pid:first.collectorPid,creationFileTime:first.collectorCreationFileTime});
  need(expected.samplerCollector!==undefined,'Held sampler collector identity required');
  need(first.collectorPid===expected.samplerCollector.pid&&first.collectorCreationFileTime===expected.samplerCollector.creationFileTime,
    'Resource collector differs from held receipt');
  need(first.collectorPid!==first.rootPid,'Collector cannot be the target');
  need(last?.event==='run.end'&&last.durationExpired===false&&last.rootExitCode===0&&last.incompleteSamples===0&&
    last.samples===samples.length&&uint(last.elapsedMs),'Resource stream is incomplete/expired or target failed');
  const runEnd=exact(last.runEndQpc,true);need(runEnd>=runBegin,'Resource run QPC reversed');
  const metrics={privateBytes:[],workingSetBytes:[],handles:[],threads:[]},intervals=[];
  const excluded={outsidePhaseIntervals:0,edgeIntervals:0,membershipIntervals:0,zeroSystemDeltaIntervals:0,outsideMemoryPolls:0,edgeMemoryPolls:0};
  let previous=null,elapsed=-1,totalCpu=0n,totalSystem=0n;
  const maximumGap=first.intervalMs*3+500;
  for(let index=0;index<samples.length;++index){
    const sample=samples[index];
    need(sample?.event==='sample'&&sample.rootPid===first.rootPid&&sample.complete===true&&
      sample.completeScope==='observed-processes-at-poll'&&sample.allLifetimeProcessesObserved===false&&sample.sharedBrowserAttributionVerified===false,
      'Incomplete or overclaimed process-tree poll');
    need(object(sample.enumeration)&&sample.enumeration.complete===true&&sample.enumeration.failure==='none'&&sample.enumeration.win32Error===0&&
      sample.enumeration.closeFailed===false&&sample.enumeration.closeError===0,'Incomplete process enumeration');
    need(uint(sample.elapsedMs)&&sample.elapsedMs>=elapsed&&sample.elapsedMs-Math.max(0,elapsed)<=maximumGap,'Resource poll elapsed clock/gap invalid');
    elapsed=sample.elapsedMs;
    const begin=exact(sample.captureBeginQpc,true),end=exact(sample.captureEndQpc,true),system=exact(sample.systemTime100ns);
    need(begin>=runBegin&&end>=begin&&end<=runEnd&&(!previous||begin>=previous.end),'Resource capture QPC reversed/outside run');
    const currentMembers=members(sample,first,expected.samplerCollector,true);
    if(begin>=window.begin&&end<=window.end){
      for(const key of Object.keys(metrics))metrics[key].push(sample[key]);
    }else if(end<window.begin||begin>window.end)++excluded.outsideMemoryPolls;
    else ++excluded.edgeMemoryPolls;
    if(!previous){
      need(sample.cpuDelta100ns===null&&sample.systemDelta100ns===null&&sample.cpuIntervalBeginQpc===null&&sample.cpuIntervalEndQpc===null&&
        sample.cpuDeltaComplete===false&&sample.cpuPercent===null,'First resource poll fabricated a CPU delta');
    }else{
      const intervalBegin=exact(sample.cpuIntervalBeginQpc,true),intervalEnd=exact(sample.cpuIntervalEndQpc,true);
      need(intervalBegin===previous.begin&&intervalEnd===end,'CPU interval does not bracket adjacent captures');
      need(system>=previous.system,'System CPU counter reversed');
      const systemDelta=system-previous.system;
      need(exact(sample.systemDelta100ns)===systemDelta,'System CPU delta differs from raw counters');
      const newlyObserved=[...currentMembers.keys()].filter(key=>!previous.members.has(key));
      const noLongerObserved=[...previous.members.keys()].filter(key=>!currentMembers.has(key));
      const stable=!newlyObserved.length&&!noLongerObserved.length;
      let cpuDelta=0n;
      for(const [key,item] of currentMembers){
        const prior=previous.members.get(key);if(!prior)continue;
        need(item.cpu>=prior.cpu,'Process CPU counter reversed');cpuDelta+=item.cpu-prior.cpu;need(cpuDelta<=U64,'CPU delta overflow');
      }
      need(sample.cpuDeltaComplete===stable,'CPU completeness differs from adjacent observed cohort');
      if(stable){
        need(exact(sample.cpuDelta100ns)===cpuDelta,'CPU delta differs from raw process counters');
        if(systemDelta){
          const percent=100*Number(cpuDelta)/Number(systemDelta);
          need(typeof sample.cpuPercent==='number'&&Number.isFinite(sample.cpuPercent)&&sample.cpuPercent>=0&&Math.abs(sample.cpuPercent-percent)<=0.0000011,
            'CPU percentage differs from raw deltas');
        }else need(sample.cpuPercent===null,'Zero system interval fabricated CPU percentage');
      }else{
        need(sample.cpuDelta100ns===null&&sample.cpuPercent===null,'Changed cohort fabricated a complete CPU delta');
        result.membershipChanges.push({fromSample:index-1,toSample:index,newlyObserved,noLongerObserved,
          meaning:'Observed membership difference; absence is not proof of process exit and short-lived processes may be missed'});
      }
      if(intervalEnd<window.begin||intervalBegin>window.end)++excluded.outsidePhaseIntervals;
      else if(intervalBegin<window.begin||intervalEnd>window.end)++excluded.edgeIntervals;
      else if(!stable)++excluded.membershipIntervals;
      else if(!systemDelta)++excluded.zeroSystemDeltaIntervals;
      else{
        totalCpu+=cpuDelta;totalSystem+=systemDelta;
        intervals.push({fromSample:index-1,toSample:index,captureStartEarliestQpc:String(previous.begin),captureStartLatestQpc:String(previous.end),
          captureEndEarliestQpc:String(begin),captureEndLatestQpc:String(end),cpuDelta100ns:String(cpuDelta),systemDelta100ns:String(systemDelta)});
      }
    }
    previous={begin,end,system,members:currentMembers};
  }
  need(last.elapsedMs>=elapsed&&last.elapsedMs-elapsed<=maximumGap&&runEnd>=previous.end,'Resource terminal coverage invalid');
  const head=intervals.length?BigInt(intervals[0].captureStartEarliestQpc)-window.begin:null;
  const tail=intervals.length?window.end-BigInt(intervals.at(-1).captureEndLatestQpc):null;
  result.status=intervals.length||metrics.privateBytes.length?'observed-window-samples':'unavailable';
  result.recordDataQualified=true;result.windowDataQualified=result.status==='observed-window-samples';
  result.reason=result.status==='unavailable'?'no-fully-contained-resource-interval-or-poll':null;
  result.cpu={available:intervals.length>0,scope:'fully-contained adjacent stable observed-cohort counter intervals',phaseComplete:false,
    intervals:intervals.length,cpuDelta100ns:String(totalCpu),systemDelta100ns:String(totalSystem),
    machinePercent:totalSystem?100*Number(totalCpu)/Number(totalSystem):null,selectedIntervals:intervals,
    unobservedHeadQpc:head===null?null:String(head),unobservedTailQpc:tail===null?null:String(tail),
    clockBoundary:'Per-process and system counter endpoints lie within capture brackets; no interpolation at phase edges'};
  result.memory={scope:'complete observed process-tree polls wholly within phase; no interpolation or lifetime peak claim',
    polls:metrics.privateBytes.length,...Object.fromEntries(Object.entries(metrics).map(([key,values])=>[key,distribution(values)]))};
  result.excluded=excluded;
  result.limits=['CPU excludes phase edges and observed cohort changes; this is not complete phase/lifetime CPU.',
    'Newly observed and no-longer-observed processes are poll observations, not complete birth/death tracking.',
    'Short-lived processes between polls and external/shared browser ownership are unverified.',
    'Summed working set may count shared pages more than once; GPU memory and physical presentation are unmeasured.',
    'Caller must bind the observer phase and both collector identities to their frozen launch/terminal receipts.'];
  return result;
}
function readJson(filename,ndjson=false){
  const before=fs.lstatSync(filename);need(before.isFile()&&!before.isSymbolicLink()&&before.nlink===1&&before.size>0&&before.size<=64*1024*1024,'Expected bounded ordinary resource evidence');
  const raw=fs.readFileSync(filename),after=fs.lstatSync(filename);
  need(raw.length===before.size&&after.size===before.size&&after.mtimeMs===before.mtimeMs&&after.ino===before.ino,'Resource evidence changed during read');
  const text=new TextDecoder('utf-8',{fatal:true}).decode(raw);
  if(!ndjson)return JSON.parse(text);
  need(text.endsWith('\n'),'Resource NDJSON lacks closed final line');return text.trimEnd().split(/\r?\n/).map(line=>JSON.parse(line));
}
if(require.main===module){
  try{need(process.argv.length===4,'Usage: manual-observation-resources.cjs SAMPLER.ndjson EXPECTED.json');
    console.log(JSON.stringify(analyzeResourceWindow(readJson(process.argv[2],true),readJson(process.argv[3])),null,2));
  }catch(error){console.error(error.message);process.exitCode=1;}
}
module.exports={analyzeResourceWindow,readJson,CAPABILITY};
