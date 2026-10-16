import {connectSouiNative, SouiBridgeError, type SouiCoreMethodMap, type SouiNativeClientImpl} from '@soui-sdk';

type Methods = SouiCoreMethodMap & {
  'acceptance.echo': {params:{};result:{window:number;bridgeChecks:boolean;sessionRole?:'anchor'|'background'}};
  'acceptance.reflect': {params:{index:number};result:{index:number}};
  'acceptance.delay': {params:{};result:{delayed:true}};
  'acceptance.fail': {params:{};result:never};
  'acceptance.large': {params:{};result:never};
  'acceptance.ready': {params:{};result:{}};
  'acceptance.session.probe': {params:{};result:{scopeId:string}};
  'acceptance.session.command': {params:{};result:{finish:boolean}};
  'acceptance.session.report': {params:SessionReport;result:{accepted:boolean}};
};

type SessionReport = {
  phase:'started'|'heartbeat'|'renewed'|'finished'|'background'|'failed';
  taskId:string; generation:number; progressSequence:number;
  pendingRequests:number; subscriptions:number; state:'running'|'cancelled'|'failed';
};
const delay = (milliseconds:number) => new Promise<void>(resolve => setTimeout(resolve,milliseconds));

async function runAnchor(client:SouiNativeClientImpl<Methods>, scopeId:string) {
  let taskId='session:unavailable', progressSequence=0, lastTick=performance.now(), eventFailed=false;
  let observedGeneration=client.sessionLease?.generation ?? 0;
  const report=async(phase:SessionReport['phase'],state:SessionReport['state'],generation:number) => {
    if(!generation)throw new Error('Real native lease is unavailable.');
    const acknowledgement=await client.call('acceptance.session.report',{phase,taskId,generation,progressSequence,
      pendingRequests:client.pendingRequestCount,subscriptions:client.subscriptionCount,state});
    if(!acknowledgement.accepted)throw new Error('Native observation was rejected.');
  };
  try {
    if(observedGeneration!==1)throw new Error('Initial lease generation differs.');
    const started=await client.call('task.start',{kind:'acceptance.session',operationId:'session-anchor-1',input:{}});
    if(!started.accepted || started.duplicate)throw new Error('Anchor task was not started once.');
    taskId=started.taskId;
    const subscription=client.subscribe(taskId,'task.progress',event=>{
      const payload=event.payload as {state?:unknown;message?:unknown;progress?:unknown};
      if(payload.state!=='running')return;
      // The SDK may publish mark_running before the handler's first tick.
      if(payload.progress===0 && payload.message==='')return;
      const match=typeof payload.message==='string' ? /^session-tick:([1-9][0-9]*)$/.exec(payload.message) : null;
      const next=match ? Number(match[1]) : 0;
      if(!Number.isSafeInteger(next) || next<=progressSequence){eventFailed=true;return;}
      progressSequence=next; lastTick=performance.now();
    });
    await subscription.ready;
    let running=false;
    for(let attempt=0;attempt<200;++attempt){
      const status=await client.call('task.query',{taskId});
      if(status.state==='running' && progressSequence>0){running=true;break;}
      if(status.state==='failed' || status.state==='cancelled' || status.state==='succeeded')
        throw new Error('Anchor task ended during admission.');
      await delay(50);
    }
    if(!running)throw new Error('Actual anchor task and events did not start.');
    await report('started','running',observedGeneration);
    await client.call('bridge.probe.report',{ready:true});
    await client.call('acceptance.ready',{});
    while(true){
      const command=await client.call('acceptance.session.command',{});
      if(eventFailed || performance.now()-lastTick>10000)throw new Error('Actual anchor progress stopped or reordered.');
      const status=await client.call('task.query',{taskId});
      if(status.state!=='running')throw new Error('Anchor task did not remain running.');
      const probe=await client.call('acceptance.session.probe',{});
      if(probe.scopeId!==scopeId)throw new Error('Native authorization scope changed.');
      // Capture once for this actual running observation. Renewal can finish
      // while either report is awaiting acknowledgement; both keep this value.
      const generation=client.sessionLease?.generation;
      if(!generation)throw new Error('Real native lease is unavailable.');
      if(generation!==observedGeneration){
        if(generation!==observedGeneration+1)throw new Error('Actual renewal skipped a generation.');
        observedGeneration=generation;
        await report('renewed','running',generation);
      }
      await report('heartbeat','running',generation);
      // This is the final retained-task observation when finish is requested.
      // Later cancellation/terminal/unsubscribe awaits can renew the lease too.
      if(command.finish)break;
      await delay(1000);
    }
    const requested=await client.call('task.cancel',{taskId});
    if(!requested.cancelRequested)throw new Error('Anchor cancellation was not acknowledged.');
    let cancelled=false;
    for(let attempt=0;attempt<200;++attempt){
      const status=await client.call('task.query',{taskId});
      if(status.state==='cancelled'){cancelled=true;break;}
      if(status.state==='failed' || status.state==='succeeded')throw new Error('Anchor returned the wrong terminal outcome.');
      await delay(50);
    }
    if(!cancelled)throw new Error('Actual anchor handler did not confirm cancellation.');
    await subscription.unsubscribe();
    const probe=await client.call('acceptance.session.probe',{});
    if(probe.scopeId!==scopeId || client.pendingRequestCount!==0 || client.subscriptionCount!==0)
      throw new Error('Anchor identity or acknowledged cleanup differs.');
    if(observedGeneration<3)throw new Error('Fewer than two running renewals were observed.');
    // generation describes the last actual running observation, not the
    // possibly newer lease after cancellation and acknowledged cleanup.
    await report('finished','cancelled',observedGeneration);
    app.textContent='连续文档的真实任务、订阅与续租观察完成；资源门另行判定';
  } catch(error) {
    try { await report('failed','failed',observedGeneration); } catch { /* The outer native/Owned timeout retains failure. */ }
    throw error;
  }
}

const app=document.getElementById('app')!;
app.textContent='正在验证公共 SDK 原生连接…';
const match=window.location.pathname.match(/window-([123])\//);
const expectedWindow=match ? Number(match[1]) : 0;
async function run() {
  if(!expectedWindow)throw new Error('The independent window identity is missing.');
  const options={maxPendingRequests:1024}; // Deliberately above the native limit for overload validation.
  const connection=connectSouiNative<Methods>(options);
  if(connection!==connectSouiNative<Methods>(options))throw new Error('Duplicate connection did not reuse bootstrap.');
  const client=await connection;
  app.textContent='检查窗口身份…';
  const identity=await client.call('acceptance.echo',{});
  if(identity.window!==expectedWindow)throw new Error('Message reached the wrong native window.');
  app.textContent='检查当前窗口的原生能力发现…';
  const expectedCapabilities:{[name:string]:string}={
    'bridge.describe':'bridge.describe', 'bridge.probe.report':'bridge.probe',
    'task.start':'task.start', 'task.query':'task.query', 'task.cancel':'task.cancel',
    'task.subscribe':'task.subscribe', 'task.unsubscribe':'task.subscribe',
    'acceptance.echo':'acceptance.echo', 'acceptance.ready':'acceptance.ready',
  };
  if(identity.bridgeChecks)
    for(const name of ['acceptance.reflect','acceptance.delay','acceptance.fail','acceptance.large'])
      expectedCapabilities[name]=name;
  if(identity.sessionRole){
    for(const method of ['bridge.session.renew','acceptance.session.probe','acceptance.session.command','acceptance.session.report'])
      expectedCapabilities[method]=method;
  }
  const expectedNames=Object.keys(expectedCapabilities).sort();
  const catalog=await client.discover({pageSize:2,maxMethods:32});
  if(catalog.methods.length!==expectedNames.length || catalog.methods.some((method,index)=>
    method.name!==expectedNames[index] || method.capability!==expectedCapabilities[method.name]))
    throw new Error('Discovered methods differ from this window\'s registered capabilities.');
  const firstPage=await client.describe({cursor:0,limit:2});
  if(firstPage.sdkVersion!==catalog.sdkVersion || firstPage.nextCursor!==2 ||
    JSON.stringify(firstPage.methods)!==JSON.stringify(catalog.methods.slice(0,2)))
    throw new Error('Repeated discovery changed within the active document session.');
  let cancelledTaskId='';
  if(identity.bridgeChecks) {
    app.textContent='检查 32 并发…';
    const results=await Promise.all(Array.from({length:32},(_,index)=>client.call('acceptance.reflect',{index})));
    if(results.some((value,index)=>value.index!==index))throw new Error('Concurrent response identity mismatch.');
    let ticks=0;
    app.textContent='检查原生异步等待…';
    const timer=setInterval(()=>{ticks++;},5);
    try {
      const result=await client.call('acceptance.delay',{});
      if(!result.delayed || ticks<1)throw new Error('The page did not advance while native work was running.');
    } finally {clearInterval(timer);}
    app.textContent='检查原生容量与控制槽位…';
    const blocker=client.call('acceptance.delay',{});
    const overloaded=Array.from({length:256},(_,index)=>client.call('acceptance.reflect',{index})
      .then(value=>({index:value.index,code:''}),error=>({index:-1,code:error instanceof SouiBridgeError ? error.code : 'unexpected'})));
    // The public task service must still admit a control request at ordinary capacity.
    const control=client.cancel('fixture:missing').then(()=>{throw new Error('Missing task unexpectedly cancelled.');},error=>{
      if(!(error instanceof SouiBridgeError) || error.code!=='E_TASK_NOT_FOUND')throw error;
    });
    const capacityResults=await Promise.all(overloaded);
    await Promise.all([blocker,control]);
    if(capacityResults.filter(result=>result.code==='E_BUSY').length!==1 ||
       capacityResults.some((result,index)=>result.code ? result.code!=='E_BUSY' : result.index!==index))
      throw new Error('Native capacity mismatch: '+JSON.stringify({busy:capacityResults.filter(result=>result.code==='E_BUSY').length,
        errors:capacityResults.filter(result=>result.code && result.code!=='E_BUSY').map(result=>result.code),
        mismatched:capacityResults.filter((result,index)=>!result.code && result.index!==index).length}));
    for(const [method,code] of [['acceptance.fail','E_ACCEPTANCE_FAILURE'],['acceptance.large','E_LIMIT_MESSAGE_SIZE']] as const) {
      app.textContent='检查错误恢复：'+method;
      try {await client.call(method,{});throw new Error('Native failure was accepted as success.');}
      catch(error) {if(!(error instanceof SouiBridgeError) || error.code!==code)throw error;}
    }
    app.textContent='检查公共 SDK 长任务与进度事件…';
    const started=await client.call('task.start',{kind:'acceptance.count',operationId:'complete-1',input:{steps:10}});
    if(!started.accepted || started.duplicate)throw new Error('Native task did not start once.');
    let progressEvents=0;
    const subscription=client.subscribe(started.taskId,'task.progress',event=>{
      const payload=event.payload as {progress?:unknown};
      if(typeof payload.progress==='number')progressEvents++;
    });
    try {
      await subscription.ready;
      let completed=false;
      for(let attempt=0;attempt<100;++attempt){
        const status=await client.call('task.query',{taskId:started.taskId});
        if(status.state==='succeeded'){
          if(status.progress!==100 || status.result?.steps!==10)throw new Error('Task result changed across Bridge.');
          completed=true;
          break;
        }
        if(status.state==='failed' || status.state==='cancelled')throw new Error('Task failed unexpectedly: '+status.message);
        await new Promise<void>(resolve=>setTimeout(resolve,20));
      }
      if(!completed)throw new Error('Public task did not finish.');
      await new Promise<void>(resolve=>setTimeout(resolve,20));
      if(progressEvents===0)throw new Error('No native progress event reached the page.');
    } finally {await subscription.unsubscribe();}
    app.textContent='检查公共 SDK 协作取消…';
    const cancellable=await client.call('task.start',{kind:'acceptance.count',operationId:'cancel-1',input:{steps:100}});
    let running=false;
    for(let attempt=0;attempt<100;++attempt){
      const status=await client.call('task.query',{taskId:cancellable.taskId});
      if(status.state==='running' && status.progress>0){running=true;break;}
      await new Promise<void>(resolve=>setTimeout(resolve,20));
    }
    if(!running)throw new Error('Cancel target never entered a native worker.');
    const requested=await client.call('task.cancel',{taskId:cancellable.taskId});
    if(!requested.cancelRequested)throw new Error('Task cancellation was not acknowledged.');
    let cancelled=false;
    for(let attempt=0;attempt<100;++attempt){
      const status=await client.call('task.query',{taskId:cancellable.taskId});
      if(status.state==='cancelled'){cancelled=true;break;}
      if(status.state==='succeeded')throw new Error('Cancelled task reported success.');
      await new Promise<void>(resolve=>setTimeout(resolve,20));
    }
    if(!cancelled)throw new Error('Native handler did not confirm cancellation.');
    cancelledTaskId=cancellable.taskId;
    // Capacity and error recovery must leave the same document usable.
    if((await client.call('acceptance.echo',{})).window!==expectedWindow || client.pendingRequestCount!==0)
      throw new Error('Native connection did not recover after failures.');
    if(JSON.stringify(await client.discover({pageSize:2,maxMethods:32}))!==JSON.stringify(catalog))
      throw new Error('Capability discovery changed after task, overload or error recovery.');
  }
  if(identity.sessionRole){
    const probe=await client.call('acceptance.session.probe',{});
    if(!probe.scopeId)throw new Error('Actual native scope correlation is absent.');
    if(identity.sessionRole==='anchor'){await runAnchor(client,probe.scopeId);return;}
    const generation=client.sessionLease?.generation;
    if(!cancelledTaskId || generation!==1 || client.pendingRequestCount!==0 || client.subscriptionCount!==0)
      throw new Error('Background task cancellation did not finish.');
    const accepted=await client.call('acceptance.session.report',{phase:'background',taskId:cancelledTaskId,
      generation,progressSequence:0,pendingRequests:0,subscriptions:0,state:'cancelled'});
    if(!accepted.accepted)throw new Error('Background native observation failed.');
  }
  app.textContent='回报页面就绪…';
  await client.call('bridge.probe.report',{ready:true});
  await client.call('acceptance.ready',{});
  app.textContent=identity.bridgeChecks ? '公共 SDK 连接、能力发现、并发、长任务、进度、取消、限流与错误恢复验证通过' : '公共 SDK 原生连接、能力发现、窗口身份和页面就绪验证通过';
}
void run().catch(error=>{app.textContent='失败：'+String(error);document.title='SDK acceptance failed';});
