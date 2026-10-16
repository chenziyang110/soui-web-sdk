'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {sessionSettings,validateSessionRecords}=require('./session-validation.cjs');

// Synthetic transcript data exercises rejection logic only. These tests are
// never evidence of an actual lease, SDK task, native process or eight-hour run.
const config=sessionSettings(3120000,20,20000,60000);
const owned={pid:123,creationFileTime:'134360221699965438',exitCode:0,supervisorExitCode:0,
  passed:true,closureConfirmed:true,stopRequested:false,fallbackUsed:false,
  signal:null,spawnError:null,reportError:null,terminal:{event:"terminal",totalProcesses:1,limitTerminatedProcesses:0,reason:'completed',rootExitCode:0,rootExited:true,activeBeforeCleanup:0,
    activeProcesses:0,terminatedOwnedJob:false,closureConfirmed:true,passed:true}};
const outcome={pid:123,exitCode:0,signal:null,timedOut:false,ownedProcess:owned};
const copy=value=>structuredClone(value);

function transcript(){
  const rows=[];
  const add=(elapsedMs,value)=>rows.push({elapsedMs,...value});
  const step=(time,event,action,window,cycle)=>add(time,{event,action,window,cycle});
  const sample=(time,phase,cycle)=>add(time,{event:'sample',phase,cycle,privateBytes:20000000,
    workingSetBytes:10000000,handles:100,threads:3,gdiObjects:2,userObjects:3});
  const exited=(time,window,sessionId,operationId)=>add(time,{event:'task.handler.exit',window,sessionId,operationId});
  const report=(time,window,scopeId,sessionId,data)=>add(time,{event:'session.report',window,scopeId,sessionId,data});
  const anchor=(phase,generation,progressSequence,state='running')=>({phase,generation,progressSequence,state,
    taskId:'anchor-task',pendingRequests:0,subscriptions:state==='running'?1:0});
  add(0,{event:'run.begin',...config,pid:123,creationFileTime:owned.creationFileTime}); sample(10,'process',0);
  step(100,'step.begin','mount',1,0);
  exited(200,1,'anchor-session','complete-1'); exited(201,1,'anchor-session','cancel-1');
  report(500,1,'soui-scope-1','anchor-session',anchor('started',1,1));
  step(510,'step.complete','mount',1,0); step(512,'step.begin','runtime.reject-busy',0,0);
  step(514,'step.complete','runtime.reject-busy',0,0); sample(515,'anchor',0);
  let cycles=0;
  for(let time=1000;time<3120500;time+=60000){
    const cycle=++cycles; add(time,{event:'cycle.begin',cycle});
    step(time+1,'step.begin','minimize',1,cycle); step(time+2,'step.complete','minimize',1,cycle);
    step(time+22,'step.begin','restore',1,cycle); step(time+23,'step.complete','restore',1,cycle);
    for(const [window,offset] of [[2,24],[3,29]]){
      const scopeId=`soui-scope-${window}-${cycle}`, sessionId=`session-${window}-${cycle}`;
      step(time+offset,'step.begin','mount',window,cycle);
      exited(time+offset+1,window,sessionId,'complete-1'); exited(time+offset+2,window,sessionId,'cancel-1');
      report(time+offset+3,window,scopeId,sessionId,{phase:'background',taskId:`cancelled-${window}-${cycle}`,
        generation:1,progressSequence:0,pendingRequests:0,subscriptions:0,state:'cancelled'});
      step(time+offset+4,'step.complete','mount',window,cycle);
    }
    for(const [window,offset] of [[3,35],[2,38]]){
      step(time+offset,'step.begin','close',window,cycle);
      add(time+offset+1,{event:'session.revoked',window,scopeId:`soui-scope-${window}-${cycle}`,active:false,handlerExited:true});
      step(time+offset+2,'step.complete','close',window,cycle);
    }
    sample(time+41,'anchor',cycle); add(time+42,{event:'cycle.complete',cycle});
  }
  for(let second=1;second<=3120;++second){
    const generation=second<1501?1:second<3001?2:3;
    report(500+second*1000,1,'soui-scope-1','anchor-session',
      anchor(second===1501||second===3001?'renewed':'heartbeat',generation,second+1));
  }
  exited(3120520,1,'anchor-session','session-anchor-1');
  report(3121500,1,'soui-scope-1','anchor-session',anchor('finished',3,3122,'cancelled'));
  add(3121510,{event:'session.end'}); sample(3121511,'anchor-finished',cycles+1);
  step(3121512,'step.begin','close',1,cycles+1);
  add(3121513,{event:'session.revoked',window:1,scopeId:'soui-scope-1',active:false,handlerExited:true});
  step(3121514,'step.complete','close',1,cycles+1); sample(3121515,'closed',cycles+1);
  let time=3121516;
  for(const action of ['runtime.shutdown','runtime.reject-restart','runtime.shutdown-idempotent']){
    step(time++,'step.begin',action,0,cycles+1); step(time++,'step.complete',action,0,cycles+1);
    if(action==='runtime.shutdown')sample(time++,'runtime-closed',cycles+1);
  }
  add(time,{event:'run.complete'}); return rows.sort((a,b)=>a.elapsedMs-b.elapsedMs);
}

test('complete identity-bound data requires a stable anchor through real-policy timing, callbacks, cancellation and closure',()=>{
  const result=validateSessionRecords(transcript(),config,outcome);
  assert.equal(result.renewals,2); assert.equal(result.measuredCycles,52);
  assert.equal(result.sameDocumentTaskSubscription,true);
  assert.equal(result.resourceGate,'not-evaluated'); assert.equal(result.longRunGate,'not-evaluated');
  assert.equal(result.releaseReady,false);
});

test('session settings reject short or malformed requests instead of shortening the production lease',()=>{
  for(const args of [[3119999,20,20000,60000],[43200001,20,20000,60000],[3120000,20,0,60000],
    [3120000,20,20000,0],[3120000,20.5,20000,60000]])assert.throws(()=>sessionSettings(...args));
});

test('runtime failure, forced tails and numeric/mismatched FILETIME cannot satisfy natural Owned closure',()=>{
  for(const patch of [{supervisorExitCode:124},{creationFileTime:134360221699965438},{pid:124},{passed:false},{spawnError:"actual native spawn failed"},{signal:"SIGKILL"},{reportError:"truncated report"},
    {terminal:{...owned.terminal,terminatedOwnedJob:true}},{terminal:{...owned.terminal,activeBeforeCleanup:1}},{terminal:{...owned.terminal,limitTerminatedProcesses:1}},{terminal:{...owned.terminal,totalProcesses:0}}])
    assert.throws(()=>validateSessionRecords(transcript(),config,{...outcome,ownedProcess:{...owned,...patch}}),/Owned|identity/);
  const rows=transcript(); rows[0].creationFileTime='134360221699965439';
  assert.throws(()=>validateSessionRecords(rows,config,outcome),/identity/);
});

const cases=[
  ['task restarted',rows=>rows.find(r=>r.event==='session.report'&&r.data.phase==='heartbeat').data.taskId='different-task'],
  ['document scope changed',rows=>rows.find(r=>r.event==='session.report'&&r.data.phase==='heartbeat').scopeId='soui-scope-replacement'],
  ['background session reused',rows=>rows.find(r=>r.event==='session.report'&&r.window===3).sessionId='anchor-session'],
  ['subscription lost',rows=>rows.find(r=>r.event==='session.report'&&r.data.phase==='heartbeat').data.subscriptions=0],
  ['client backlog exceeded',rows=>rows.find(r=>r.event==='session.report'&&r.data.phase==='heartbeat').data.pendingRequests=2],
  ['subscribed events stopped',rows=>rows.filter(r=>r.event==='session.report'&&r.data.phase==='heartbeat'&&r.elapsedMs<20000).forEach(r=>r.data.progressSequence=1)],
  ['generation skipped',rows=>rows.find(r=>r.event==='session.report'&&r.data.phase==='renewed').data.generation=3],
  ['progress lost across renewal',rows=>rows.find(r=>r.event==='session.report'&&r.data.phase==='renewed').data.progressSequence=1],
  ['scope not actually revoked',rows=>rows.find(r=>r.event==='session.revoked').active=true],
  ['handler still running',rows=>rows.find(r=>r.event==='session.revoked').handlerExited=false],
  ['wrong terminal outcome',rows=>rows.find(r=>r.event==='session.report'&&r.data.phase==='finished').data.state='failed'],
  ['unsubscribe unacknowledged',rows=>rows.find(r=>r.event==='session.report'&&r.data.phase==='finished').data.subscriptions=1],
  ['resource sample absent',rows=>delete rows.find(r=>r.event==='sample'&&r.phase==='anchor').handles],
  ['consumer failed',rows=>rows.find(r=>r.event==='session.report'&&r.data.phase==='heartbeat').data.phase='failed'],
];
for(const [name,mutate] of cases)test(`rejects ${name}`,()=>{
  const rows=transcript(); mutate(rows); assert.throws(()=>validateSessionRecords(rows,config,outcome));
});

test('early generation advances do not prove an unchanged real thirty-minute policy',()=>{
  const rows=transcript(); const early=rows.find(r=>r.event==='session.report'&&r.data.phase==='heartbeat');
  early.data.phase='renewed'; early.data.generation=2;
  assert.throws(()=>validateSessionRecords(rows,config,outcome),/policy window/);
});

test('missing callback returns, stopped telemetry and missing lifecycle work remain failures',()=>{
  for(const keep of [r=>!(r.event==='task.handler.exit'&&r.operationId==='session-anchor-1'),
    r=>!(r.event==='task.handler.exit'&&r.window===2),
    r=>!(r.event==='session.report'&&r.data.phase==='heartbeat'&&r.elapsedMs<1400000),
    r=>!(r.event==='step.begin'&&r.action==='close'&&r.window===3)])
    assert.throws(()=>validateSessionRecords(transcript().filter(keep),config,outcome));
});

test('old mixed semantics, premature duration and extra protocol fields are not inherited',()=>{
  assert.throws(()=>validateSessionRecords(transcript(),{...config,mode:'mixed'},outcome));
  const rows=transcript(); rows.find(r=>r.event==='session.report').data.bearer='never-accepted';
  assert.throws(()=>validateSessionRecords(rows,config,outcome),/telemetry/);
  const longer={...config,durationMs:config.durationMs+60000}, short=transcript(); short[0].durationMs=longer.durationMs;
  assert.throws(()=>validateSessionRecords(short,longer,outcome),/early|duration/);
});

test('anchor minimize/restore must be paired, ordered and actually settled each cycle',()=>{
  for(const action of ['minimize','restore'])
    assert.throws(()=>validateSessionRecords(transcript().filter(r=>r.action!==action),config,outcome));
  const early=transcript(); early.find(r=>r.event==='step.begin'&&r.action==='restore').elapsedMs-=1;
  assert.throws(()=>validateSessionRecords(early,config,outcome),/settled minimize/);
});

test('finished retains the last running generation and cannot count a post-cancel renewal',()=>{
  const rows=transcript();
  assert.equal(validateSessionRecords(rows,config,outcome).renewals,2);
  rows.find(r=>r.event==='session.report'&&r.data.phase==='finished').data.generation=4;
  assert.throws(()=>validateSessionRecords(rows,config,outcome),/Generation changed/);
  const unpaired=transcript();
  unpaired.filter(r=>r.event==='session.report'&&r.window===1&&r.elapsedMs>3001500&&r.data.phase==='heartbeat')
    .forEach(r=>r.data.phase='renewed');
  assert.throws(()=>validateSessionRecords(unpaired,config,outcome));
});
test('a final running renewal/heartbeat may finish without demanding another post-observation task tick',()=>{
  const rows=transcript();
  for(const row of rows)if(row.event==='session.report'&&row.window===1&&
    row.elapsedMs>=3001500&&row.elapsedMs<3120500){
    row.data.generation=2;if(row.data.phase==='renewed')row.data.phase='heartbeat';
  }
  const last=rows.find(row=>row.event==='session.report'&&row.data.phase==='heartbeat'&&row.elapsedMs===3120500);
  last.data.phase='renewed';
  const paired=copy(last);paired.elapsedMs+=1;paired.data.phase='heartbeat';
  rows.push(paired);rows.sort((a,b)=>a.elapsedMs-b.elapsedMs);
  rows.find(row=>row.event==='session.report'&&row.data.phase==='finished').data.progressSequence=last.data.progressSequence;
  assert.equal(validateSessionRecords(rows,config,outcome).renewals,2);
  assert.throws(()=>validateSessionRecords(rows.filter(row=>row!==paired),config,outcome),/final actual running heartbeat/);
});

// Execute the real web runAnchor body after the existing acceptance TypeScript
// compiler removes types. The simulated client only exercises await/snapshot
// interleavings; this is not evidence of native renewal or elapsed real time.
async function exerciseWebBoundary(boundary){
  const fs=require('node:fs'), path=require('node:path'), vm=require('node:vm');
  const ts=require('typescript');
  assert.equal(ts.version,JSON.parse(fs.readFileSync(path.join(__dirname,'package.json'),'utf8')).devDependencies.typescript);
  const filename=path.join(__dirname,'../acceptance-app/web/main.ts');
  const source=ts.createSourceFile(filename,fs.readFileSync(filename,'utf8'),ts.ScriptTarget.Latest,true);
  const anchor=source.statements.filter(node=>ts.isFunctionDeclaration(node)&&node.name?.text==='runAnchor');
  const delay=source.statements.filter(node=>ts.isVariableStatement(node)&&
    node.declarationList.declarations.some(value=>ts.isIdentifier(value.name)&&value.name.text==='delay'));
  assert.equal(anchor.length,1); assert.equal(delay.length,1);
  const javascript=ts.transpileModule(delay[0].getText(source)+'\n'+anchor[0].getText(source),{
    compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.CommonJS}}).outputText;
  let generation=1, reads=0, commands=0, sequence=0, subscriptionCount=0, cancelled=false, onProgress;
  const reports=[], calls=[];
  const client={get sessionLease(){++reads;return {generation};},get pendingRequestCount(){return 0;},
    get subscriptionCount(){return subscriptionCount;},
    subscribe(_task,_event,callback){onProgress=callback;subscriptionCount=1;return {
      ready:Promise.resolve(),unsubscribe:async()=>{subscriptionCount=0;generation=5;}};},
    async call(method,params){
      calls.push(method);
      if(method==='task.start')return {accepted:true,duplicate:false,taskId:'real-body-unit-task'};
      if(method==='task.query'){
        if(!cancelled)onProgress({payload:{state:'running',progress:99,message:`session-tick:${++sequence}`}});
        return {state:cancelled?'cancelled':'running'};
      }
      if(method==='acceptance.session.command')return {finish:++commands===(boundary==='renewed-await'?2:3)};
      if(method==='acceptance.session.probe'){
        if(boundary==='renewed-await'&&commands===1)generation=2;
        return {scopeId:'same-native-scope'};
      }
      if(method==='acceptance.session.report'){
        reports.push(structuredClone(params));
        if(boundary==='renewed-await'&&params.phase==='renewed'&&params.generation===2)generation=3;
        if(boundary==='heartbeat-await'&&params.phase==='heartbeat'&&params.generation<3)generation=params.generation+1;
        return {accepted:true};
      }
      if(method==='task.cancel'){cancelled=true;generation=4;return {cancelRequested:true};}
      if(method==='bridge.probe.report'||method==='acceptance.ready')return {};
      throw Error(`Unexpected real-body unit call ${method}`);
    }};
  const context=vm.createContext({performance:{now:()=>0},setTimeout:callback=>{callback();return 1;},
    app:{textContent:''}});
  vm.runInContext(javascript+'\nthis.runActualAnchor=runAnchor;',context,{filename});
  await context.runActualAnchor(client,'same-native-scope');
  assert.equal(reads,commands+1,'Only admission and each running observation read the live generation');
  assert.equal(reports.at(-1).phase,'finished');assert.equal(reports.at(-1).generation,3);
  assert.equal(generation,5,'Real-body cleanup awaits may advance the simulated current lease');
  assert.equal(reports.at(-2).phase,'heartbeat');assert.equal(reports.at(-2).generation,3);
  for(let index=0;index<reports.length;++index)if(reports[index].phase==='renewed'){
    assert.equal(reports[index+1].phase,'heartbeat');
    assert.equal(reports[index+1].generation,reports[index].generation);
  }
  assert.equal(calls.at(-1),'acceptance.session.report');
  const cancelIndex=calls.indexOf('task.cancel');
  assert.equal(calls[cancelIndex-1],'acceptance.session.report','Final actual running report precedes cancellation');
  return reports;
}
test('real web body keeps renewed/heartbeat paired when renewal advances during report acknowledgement',async()=>{
  const reports=await exerciseWebBoundary('renewed-await');
  assert.deepEqual(reports.filter(row=>row.phase==='heartbeat').map(row=>row.generation),[2,3]);
});
test('real web body does not reread generation during heartbeat or terminal cleanup awaits',async()=>{
  const reports=await exerciseWebBoundary('heartbeat-await');
  assert.deepEqual(reports.filter(row=>row.phase==='heartbeat').map(row=>row.generation),[1,2,3]);
});
