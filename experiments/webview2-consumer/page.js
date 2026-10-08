import { connectSouiNative, createSouiGui, souiToolDefinitions, validateSouiActionInput } from './sdk/index.js';

const status = document.getElementById('status');
function fact(label, value) {
  const term = document.createElement('dt');
  const description = document.createElement('dd');
  term.textContent = label;
  description.textContent = value;
  document.getElementById('facts').append(term, description);
}
async function run() {
  status.textContent = '检查现代网页能力…';
  if (!CSS.supports('display', 'grid') || new TextEncoder().encode('原生').length !== 6)
    throw new Error('Required modern Web features are unavailable.');
  status.textContent = '等待原生 bootstrap…';
  const client = await connectSouiNative();
  if (await connectSouiNative() !== client) throw new Error('Repeated connection did not reuse the client.');
  status.textContent = '等待分页能力发现…';
  const catalog = await client.discover({ pageSize: 2 });
  for (const method of ['app.ping', 'app.delay', 'app.fail', 'bridge.probe.report']) {
    if (!catalog.methods.some(item => item.name === method)) throw new Error(`Missing capability: ${method}`);
  }
  if(catalog.methods.some(method=>method.name==='app.denied'))throw new Error('Unauthorized method was discoverable.');
  const method=catalog.methods.find(entry=>entry.name==='app.greet');
  if(!method?.action || !souiToolDefinitions(catalog).some(tool=>tool.name==='app.greet'))
    throw new Error('Common action contract is unavailable.');
  const gui=createSouiGui();
  window.addEventListener('pagehide',()=>gui.dispose(),{once:true});
  let actionState={phase:'ready',message:''};
  gui.register({id:'greet',role:'button',name:'调用C++问候',element:document.getElementById('greet'),
    state:()=>({enabled:true,state:actionState}),actions:{activate:async input=>{
      validateSouiActionInput(method.action,input);
      actionState={phase:'running',message:''};
      try{
        const result=await client.call('app.greet',input);
        actionState={phase:'ready',message:result.message};
        document.getElementById('greeting').textContent=result.message;
        return result;
      }catch(error){actionState={phase:'failed',message:error.message};throw error;}
    }}});
  document.getElementById('greet').addEventListener('click',()=>{
    gui.perform({id:'greet',action:'activate',input:{name:'Developer'}}).catch(error=>{
      document.getElementById('greeting').textContent=error.message;
    });
  });
  const greeted=await gui.perform({id:'greet',action:'activate',input:{name:'Developer'}});
  if(greeted.message!=='Hello, Developer. This response came from C++.')throw new Error('Common action returned an invalid result.');
  await gui.waitFor({id:'greet',state:{phase:'ready'},timeoutMs:3000});
  if(!gui.snapshot().nodes.some(node=>node.id==='greet'&&node.role==='button'))throw new Error('GUI semantic snapshot failed.');
  for(const [name,input,code] of [['app.greet',{name:''},'E_INVALID_PARAMS'],['app.denied',{},'E_UNAUTHORIZED']]){
    try{await client.call(name,input);throw new Error('Restricted input or capability was accepted.');}
    catch(error){if(error.code!==code)throw error;}
  }
  fact('机器可发现动作', '契约、工具定义、界面语义与C++执行通过');
  status.textContent = '等待16个原生往返…';
  const pings = await Promise.all(Array.from({ length: 16 }, () => client.call('app.ping', {})));
  if (pings.some(result => result.message !== 'pong')) throw new Error('Native ping response did not match.');
  status.textContent = '检查原生错误与参数拒绝…';
  try {
    await client.call('app.fail', {});
    throw new Error('Expected native failure was accepted.');
  } catch (error) { if (error.code !== 'E_EXPERIMENT_FAILURE') throw error; }
  try {
    await client.call('app.delay', { milliseconds: 0 });
    throw new Error('Invalid native parameters were accepted.');
  } catch (error) { if (error.code !== 'E_INVALID_PARAMS') throw error; }
  let ticks = 0;
  status.textContent = '等待后台延时与恢复调用…';
  const beganDelay = performance.now();
  const timer = setInterval(() => { ticks++; }, 10);
  try {
    const delay = await client.call('app.delay', { milliseconds: 120 });
    if (delay.elapsedMs !== 120 || ticks < 2)
      throw new Error(`Native delay probe failed: result=${delay.elapsedMs}; ticks=${ticks}; wallMs=${Math.round(performance.now() - beganDelay)}; visibility=${document.visibilityState}.`);
  } finally { clearInterval(timer); }
  if ((await client.call('app.ping', {})).message !== 'pong') throw new Error('Recovery call failed.');
  fact('SDK 版本', catalog.sdkVersion);
  fact('现代网页能力', 'ES Modules / CSS Grid / TextEncoder');
  fact('发现方法', String(catalog.methods.length));
  fact('并发往返', '16 / 16');
  fact('后台调用', `页面继续运行（${ticks} 次计时回调）`);
  fact('业务与参数错误', '已正确拒绝，后续调用成功');
  await client.call('bridge.probe.report', { ready: true });
  status.dataset.state = 'ready';
  status.textContent = '连接、能力发现与原生往返已通过。';
}
run().catch(error => {
  status.dataset.state = 'failed';
  status.textContent = `验证失败：${error.code || 'E_PAGE_PROBE'} · ${error.message}`;
  console.error(error);
});
