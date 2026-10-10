'use strict';

function sessionSettings(durationMs, settleMs, timeoutMs, idleMs) {
  for (const [key,value,max] of [['durationMs',durationMs,43200000],['settleMs',settleMs,60000],
    ['timeoutMs',timeoutMs,120000],['idleMs',idleMs,60000]])
    if (!Number.isSafeInteger(value) || value < 0 || value > max) throw Error(`Invalid session ${key}`);
  if (durationMs < 3120000 || timeoutMs < 1 || idleMs < 1) throw Error('Session requires 52 real minutes and positive timing');
  return {mode:'session',durationMs,settleMs,timeoutMs,idleMs};
}

function sessionTranscript(cycles) {
  const rows=[{event:'run.begin'},{event:'sample',phase:'process',cycle:0}];
  const pair=(action,window,cycle)=>rows.push({event:'step.begin',action,window,cycle},{event:'step.complete',action,window,cycle});
  pair('mount',1,0); pair('runtime.reject-busy',0,0);
  rows.push({event:'sample',phase:'anchor',cycle:0});
  for(let cycle=1;cycle<=cycles;++cycle){
    rows.push({event:'cycle.begin',cycle});
    pair('minimize',1,cycle); pair('restore',1,cycle);
    pair('mount',2,cycle); pair('mount',3,cycle); pair('close',3,cycle); pair('close',2,cycle);
    rows.push({event:'sample',phase:'anchor',cycle},{event:'cycle.complete',cycle});
  }
  const final=cycles+1;
  rows.push({event:'sample',phase:'anchor-finished',cycle:final});
  pair('close',1,final); rows.push({event:'sample',phase:'closed',cycle:final});
  pair('runtime.shutdown',0,final); rows.push({event:'sample',phase:'runtime-closed',cycle:final});
  pair('runtime.reject-restart',0,final); pair('runtime.shutdown-idempotent',0,final);
  rows.push({event:'run.complete'}); return rows;
}

const id=value=>typeof value==='string' && /^[A-Za-z0-9_.:-]{1,128}$/.test(value);
const integer=(value,min=0,max=Number.MAX_SAFE_INTEGER)=>Number.isSafeInteger(value) && value>=min && value<=max;
const keys=(value,names)=>value && typeof value==='object' && !Array.isArray(value) &&
  Object.keys(value).sort().join('|')===[...names].sort().join('|');

function validateSessionRecords(records,config,outcome) {
  sessionSettings(config.durationMs,config.settleMs,config.timeoutMs,config.idleMs);
  const owned=outcome.ownedProcess, terminal=owned?.terminal;
  if(config.mode!=='session' || outcome.timedOut!==false || outcome.exitCode!==0 || outcome.signal ||
    !integer(outcome.pid,1) || owned?.pid!==outcome.pid || owned.exitCode!==0 || owned.supervisorExitCode!==0 ||
    owned.passed!==true || owned.closureConfirmed!==true || owned.stopRequested!==false || owned.fallbackUsed!==false || outcome.spawnError || owned.spawnError || owned.reportError || owned.signal ||
    !/^[1-9][0-9]*$/.test(owned.creationFileTime) || typeof owned.creationFileTime!=='string' ||
    terminal?.reason!=='completed' || terminal.rootExitCode!==0 || terminal.rootExited!==true ||
    terminal.event!=="terminal" || !integer(terminal.totalProcesses,1) || terminal.limitTerminatedProcesses!==0 ||
    terminal.activeBeforeCleanup!==0 || terminal.activeProcesses!==0 || terminal.terminatedOwnedJob!==false ||
    terminal.closureConfirmed!==true || terminal.passed!==true) throw Error('Session needs actual natural Owned 0/0 closure');
  if(!Array.isArray(records) || records[0]?.event!=='run.begin' || records.at(-1)?.event!=='run.complete')
    throw Error('Missing session run boundaries');
  const start=records[0];
  if(start.pid!==outcome.pid || start.creationFileTime!==owned.creationFileTime ||
    Object.keys(config).some(key=>start[key]!==config[key])) throw Error('Session parameters or native identity differ');
  const normalized=[], samples=[], windows=new Map(), scopes=new Set(), sessions=new Set(), exits=[];
  let elapsed=-1, anchor=null, ended=null, renewals=0, lastGeneration=0, lastProgress=0, renewalProgress=0;
  let finished=false, currentCycle=0, anchorMountMs=null, lastReportMs=null, lastRenewMs=null, lastCycleMs=null, lastAdvanceMs=null;
  let runningPhase=null;
  const cycleBound=config.idleMs+6*(config.timeoutMs+2000)+config.settleMs;
  for(const [index,record] of records.entries()){
    if(!integer(record.elapsedMs) || record.elapsedMs<elapsed) throw Error('Invalid session timestamp');
    elapsed=record.elapsedMs;
    switch(record.event){
      case 'run.begin': case 'run.complete':
        if(index!==0 && index!==records.length-1) throw Error('Duplicate run boundary');
        normalized.push({event:record.event}); break;
      case 'cycle.begin': case 'cycle.complete':
        if(record.event==='cycle.begin'){
          if(record.elapsedMs-(lastCycleMs ?? anchorMountMs)>cycleBound)throw Error('Background work stopped');
          lastCycleMs=record.elapsedMs; currentCycle=record.cycle;
        }
        normalized.push({event:record.event,cycle:record.cycle}); break;
      case 'step.begin': case 'step.complete': {
        if(record.action==='minimize' && record.event==='step.complete'){
          const window=windows.get(1);
          if(record.window!==1 || !window?.reported || window.minimizedAt!==undefined)
            throw Error('Anchor minimize ownership differs');
          window.minimizedAt=record.elapsedMs;
        }
        if(record.action==='restore'){
          const window=windows.get(1);
          if(record.window!==1 || window?.minimizedAt===undefined ||
            record.elapsedMs-window.minimizedAt<config.settleMs)
            throw Error('Anchor restore is missing its actual settled minimize');
          if(record.event==='step.complete')delete window.minimizedAt;
        }
        if(record.action==='mount'){
          if(record.event==='step.begin'){
            if(windows.has(record.window))throw Error('Window remounted before close');
            windows.set(record.window,{cycle:record.cycle,reported:false,revoked:false});
            if(record.window===1)anchorMountMs=record.elapsedMs;
          } else if(record.window===1 ? !anchor : !windows.get(record.window)?.reported)
            throw Error('Mount completed before authenticated task observation');
        }
        if(record.action==='close' && record.event==='step.complete'){
          if(!windows.get(record.window)?.revoked)throw Error('Close completed without real scope revocation');
          windows.delete(record.window);
        }
        normalized.push({event:record.event,action:record.action,window:record.window,cycle:record.cycle}); break;
      }
      case 'sample':
        for(const key of ['privateBytes','workingSetBytes','handles','threads','gdiObjects','userObjects'])
          if(!integer(record[key]))throw Error(`Missing session resource ${key}`);
        samples.push(record); normalized.push({event:record.event,phase:record.phase,cycle:record.cycle}); break;
      case 'task.handler.exit':
        if(![1,2,3].includes(record.window) || !id(record.operationId) || !id(record.sessionId))
          throw Error('Invalid actual handler exit');
        exits.push(record); break;
      case 'session.report': {
        const data=record.data, window=windows.get(record.window);
        if(!window || !id(record.scopeId) || !id(record.sessionId) ||
          !keys(data,['phase','taskId','generation','progressSequence','pendingRequests','subscriptions','state']) ||
          !id(data.taskId) || !integer(data.generation,1,2147483647) || !integer(data.progressSequence,0,2147483647) ||
          !integer(data.pendingRequests,0,1) || !integer(data.subscriptions,0,1))throw Error('Invalid authenticated session telemetry');
        if(data.phase==='failed')throw Error('Native consumer reported session failure');
        if(record.window!==1){
          if(data.phase!=='background' || window.reported || scopes.has(record.scopeId) || sessions.has(record.sessionId) || data.generation!==1 ||
            data.state!=='cancelled' || data.subscriptions!==0 || data.pendingRequests!==0)
            throw Error('Background cancellation or scope identity differs');
          const actual=exits.filter(exit=>exit.window===record.window && exit.sessionId===record.sessionId);
          if(actual.length!==2 || ['complete-1','cancel-1'].some(operation=>actual.filter(exit=>exit.operationId===operation).length!==1))
            throw Error('Background report preceded actual handler return');
          Object.assign(window,{reported:true,scopeId:record.scopeId,sessionId:record.sessionId}); scopes.add(record.scopeId); sessions.add(record.sessionId);
          break;
        }
        if(data.phase==='started'){
          if(anchor || data.generation!==1 || data.progressSequence<1)throw Error('Anchor did not start once with real progress');
          anchor={scopeId:record.scopeId,sessionId:record.sessionId,taskId:data.taskId,elapsedMs:record.elapsedMs};
          Object.assign(window,{reported:true,...anchor}); scopes.add(record.scopeId); sessions.add(record.sessionId); lastGeneration=1;
        } else if(!anchor || finished || record.scopeId!==anchor.scopeId || record.sessionId!==anchor.sessionId ||
          data.taskId!==anchor.taskId)throw Error('Anchor document, scope or task changed');
        if(lastReportMs!==null && record.elapsedMs-lastReportMs>config.timeoutMs+2000)
          throw Error('Anchor telemetry stopped');
        lastReportMs=record.elapsedMs;
        if(data.progressSequence<lastProgress)throw Error('Actual progress sequence reversed');
        if(data.progressSequence>lastProgress)lastAdvanceMs=record.elapsedMs;
        if(data.state==='running' && (lastAdvanceMs===null || record.elapsedMs-lastAdvanceMs>12000))
          throw Error('Actual subscribed progress stopped');
        if(data.phase==='renewed'){
          if(runningPhase==='renewed')throw Error('Renewal lacks its captured running heartbeat');
          if(data.generation!==lastGeneration+1 || data.progressSequence<=renewalProgress)
            throw Error('Renewal needs one real generation advance and continuing task events');
          if(lastRenewMs===null ? record.elapsedMs-anchorMountMs<1500000 : record.elapsedMs-lastRenewMs<1500000-(config.timeoutMs+2000))
            throw Error('Renewal arrived before the unchanged native policy window');
          lastRenewMs=record.elapsedMs;
          lastGeneration=data.generation; renewalProgress=data.progressSequence; ++renewals;
        } else if(data.generation!==lastGeneration)throw Error('Generation changed without observed renewal');
        if(data.phase==='finished'){
          // finished retains the last running observation's generation; it
          // separately acknowledges cancellation, terminal state and unsubscribe.
          if(runningPhase!=='heartbeat')throw Error('Finish lacks a final actual running heartbeat');
          if(renewals<2 || data.state!=='cancelled' || data.pendingRequests!==0 || data.subscriptions!==0 ||
            data.progressSequence<renewalProgress || record.elapsedMs-anchor.elapsedMs<config.durationMs)
            throw Error('Anchor finished early or without acknowledged task/subscription cleanup');
          if(exits.filter(exit=>exit.window===1 && exit.sessionId===anchor.sessionId && exit.operationId==='session-anchor-1').length!==1)
            throw Error('Finished report preceded actual anchor handler return');
          finished=true;
        } else if(!['started','heartbeat','renewed'].includes(data.phase) || data.state!=='running' || data.subscriptions!==1)
          throw Error('Anchor did not retain its real task subscription');
        lastProgress=data.progressSequence; runningPhase=data.phase; break;
      }
      case 'session.revoked': {
        const window=windows.get(record.window);
        if(!window?.reported || window.revoked || record.scopeId!==window.scopeId ||
          record.active!==false || record.handlerExited!==true || record.window===1 && !finished)
          throw Error('Actual scope/handler cleanup differs');
        const observed=exits.filter(exit=>exit.window===record.window && exit.sessionId===window.sessionId);
        const wanted=record.window===1 ? ['complete-1','cancel-1','session-anchor-1'] : ['complete-1','cancel-1'];
        if(observed.length!==wanted.length || wanted.some(operation=>observed.filter(exit=>exit.operationId===operation).length!==1))
          throw Error('Actual handler exits are incomplete or duplicated');
        window.revoked=true; break;
      }
      case 'session.end':
        if(ended!==null || !finished || record.elapsedMs-anchor.elapsedMs<config.durationMs)throw Error('Incomplete real session duration');
        if(lastCycleMs===null || record.elapsedMs-lastCycleMs>cycleBound)throw Error('Background work ended too early');
        ended=record.elapsedMs; break;
      default: throw Error(`Failed or unknown session event: ${record.event}`);
    }
  }
  if(!anchor || !finished || ended===null || windows.size || currentCycle<1 ||
    JSON.stringify(normalized)!==JSON.stringify(sessionTranscript(currentCycle)))throw Error('Missing or reordered session lifecycle');
  return {measuredCycles:currentCycle,renewals,elapsedMs:ended-anchor.elapsedMs,samples,
    sameDocumentTaskSubscription:true,resourceGate:'not-evaluated',longRunGate:'not-evaluated',releaseReady:false};
}

module.exports={sessionSettings,sessionTranscript,validateSessionRecords};
