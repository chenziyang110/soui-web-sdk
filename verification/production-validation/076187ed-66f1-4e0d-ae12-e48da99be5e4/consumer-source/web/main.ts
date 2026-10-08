import {connectSouiNative, SouiBridgeError, type SouiCoreMethodMap} from '@soui-sdk';

type Methods = SouiCoreMethodMap & {
  'acceptance.echo': {params:{};result:{window:number;bridgeChecks:boolean}};
  'acceptance.reflect': {params:{index:number};result:{index:number}};
  'acceptance.delay': {params:{};result:{delayed:true}};
  'acceptance.fail': {params:{};result:never};
  'acceptance.large': {params:{};result:never};
  'acceptance.ready': {params:{};result:{}};
};

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
    // This fixture intentionally has no native task service. An unknown-method
    // response proves reserved admission, not support for task cancellation.
    const control=client.cancel('fixture:missing').then(()=>{throw new Error('Unregistered task service unexpectedly succeeded.');},error=>{
      if(!(error instanceof SouiBridgeError) || error.code!=='E_UNKNOWN_METHOD')throw error;
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
    // Capacity and error recovery must leave the same document usable.
    if((await client.call('acceptance.echo',{})).window!==expectedWindow || client.pendingRequestCount!==0)
      throw new Error('Native connection did not recover after failures.');
  }
  app.textContent='回报页面就绪…';
  await client.call('bridge.probe.report',{ready:true});
  await client.call('acceptance.ready',{});
  app.textContent=identity.bridgeChecks ? '公共 SDK 连接、并发、异步等待、原生限流与错误恢复验证通过' : '公共 SDK 原生连接、窗口身份和页面就绪验证通过';
}
void run().catch(error=>{app.textContent='失败：'+String(error);document.title='SDK acceptance failed';});
