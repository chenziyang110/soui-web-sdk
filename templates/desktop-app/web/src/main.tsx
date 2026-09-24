import React, {useEffect,useState} from 'react';
import {createRoot} from 'react-dom/client';
import {SouiBridgeError, type SouiNativeClientImpl} from '@soui-sdk';
import {connectNative, type AppMethods} from './native';
import './style.css';
function describeError(error:unknown):string {
  return error instanceof SouiBridgeError && error.code==='E_NATIVE_UNAVAILABLE'
    ? '请从桌面程序启动，以连接 C++ 服务。' : String(error);
}
function App(){
  const [client,setClient]=useState<SouiNativeClientImpl<AppMethods>|null>(null);
  const [name,setName]=useState('Developer');
  const [message,setMessage]=useState('正在连接原生服务…');
  const [busy,setBusy]=useState(false);
  const [taskId,setTaskId]=useState('');
  const [taskBusy,setTaskBusy]=useState(false);
  const [taskStatus,setTaskStatus]=useState('尚未启动');
  const [clipboardStatus,setClipboardStatus]=useState('等待点击复制');
  const [windowTitle,setWindowTitle]=useState('SouiStarter');
  const [windowStatus,setWindowStatus]=useState('可修改原生窗口');
  useEffect(()=>{
    let active=true;
    void connectNative().then(async connected=>{
      if(!active)return;
      const result=await connected.call('app.greet',{name:'Developer'});
      if(!active)return;
      await connected.call('bridge.probe.report',{ready:true});
      if(active){setClient(connected);setMessage(result.message);}
    }).catch(error=>{if(active)setMessage(describeError(error));});
    // The SDK owns this document's connection and releases it on page exit.
    return ()=>{active=false;};
  },[]);
  async function greet(){if(!client)return;setBusy(true);try{setMessage((await client.call('app.greet',{name})).message)}catch(error){setMessage(describeError(error))}finally{setBusy(false)}}
  async function runTask(){
    if(!client)return;
    setTaskBusy(true);
    let subscription:ReturnType<typeof client.subscribe>|undefined;
    try{
      const operationId=`starter-${Date.now()}-${Math.random().toString(36).slice(2)}`;
      const started=await client.call('task.start',{kind:'app.count',operationId,input:{steps:20}});
      setTaskId(started.taskId);
      setTaskStatus('已排队');
      subscription=client.subscribe(started.taskId,'task.progress',event=>{
        const payload=event.payload as {state?:string;progress?:number};
        if(typeof payload.progress==='number')setTaskStatus(`${payload.state??'运行中'} · ${payload.progress}%`);
      });
      await subscription.ready;
      for(let attempt=0;attempt<100;++attempt){
        const status=await client.call('task.query',{taskId:started.taskId});
        setTaskStatus(`${status.state} · ${status.progress}%${status.message?` · ${status.message}`:''}`);
        if(['succeeded','failed','cancelled','partial'].indexOf(status.state)>=0)return;
        await new Promise<void>(resolve=>setTimeout(resolve,100));
      }
      throw new Error('任务查询超时。');
    }catch(error){setTaskStatus(describeError(error));}
    finally{if(subscription)await subscription.unsubscribe().catch(()=>undefined);setTaskBusy(false);}
  }
  async function cancelTask(){
    if(!client||!taskId)return;
    try{await client.call('task.cancel',{taskId});setTaskStatus('正在取消…');}
    catch(error){setTaskStatus(describeError(error));}
  }
  async function copyText(){
    if(!client)return;
    try{
      await client.call('desktop.clipboard.writeText',{text:'SOUI SDK 中文剪贴板验证'});
      setClipboardStatus('已复制中文文本到系统剪贴板');
    }catch(error){setClipboardStatus(describeError(error));}
  }
  async function updateWindowTitle(){
    if(!client)return;
    try{await client.call('desktop.window.setTitle',{title:windowTitle});setWindowStatus('窗口标题已更新');}
    catch(error){setWindowStatus(describeError(error));}
  }
  async function toggleWindowMaximize(){
    if(!client)return;
    try{
      const result=await client.call('desktop.window.toggleMaximize',{});
      setWindowStatus(result.maximized?'窗口已最大化':'窗口已还原');
    }catch(error){setWindowStatus(describeError(error));}
  }
  async function minimizeWindow(){
    if(!client)return;
    try{await client.call('desktop.window.minimize',{});setWindowStatus('窗口已最小化，可从任务栏恢复');}
    catch(error){setWindowStatus(describeError(error));}
  }
  return <main><div className="eyebrow">SOUI SDK · DESKTOP STARTER</div><h1>SouiStarter</h1>
    <p className="intro">从这里开始构建你的桌面应用。</p><section><h2>{client?'原生服务已接入':'连接原生服务'}</h2>
      <p>页面通过 SDK 调用 C++，输入你的名字试一试。</p>
      <label htmlFor="name">名字</label><div className="row"><input id="name" value={name} maxLength={32} onChange={e=>setName(e.target.value)}/><button onClick={greet} disabled={busy||!client}>{busy?'处理中…':'调用 C++'}</button></div>
      <div className="result" role="status">{message}</div></section>
    <section><h2>公共 SDK 长任务</h2><p>页面调用 SDK 任务接口，由 C++ 后台执行并报告进度。</p>
      <div className="row"><button onClick={()=>void runTask()} disabled={!client||taskBusy}>开始计数</button>
        <button onClick={()=>void cancelTask()} disabled={!client||!taskBusy||!taskId}>取消任务</button></div>
      <div className="result" role="status">{taskStatus}</div></section>
    <section><h2>公共窗口能力</h2><p>这些操作由 SDK 验证当前会话和宿主窗口后执行。</p>
      <label htmlFor="window-title">窗口标题</label>
      <div className="row"><input id="window-title" value={windowTitle} maxLength={128} onChange={e=>setWindowTitle(e.target.value)}/>
        <button onClick={()=>void updateWindowTitle()} disabled={!client}>更新标题</button></div>
      <div className="row"><button onClick={()=>void toggleWindowMaximize()} disabled={!client}>最大化或还原</button>
        <button onClick={()=>void minimizeWindow()} disabled={!client}>最小化</button></div>
      <div className="result" role="status">{windowStatus}</div></section>
    <section><h2>公共桌面能力</h2><p>点击后由 SDK 写入系统剪贴板；原生侧验证窗口、权限和用户操作。</p>
      <button onClick={()=>void copyText()} disabled={!client}>复制中文测试文本</button>
      <div className="result" role="status">{clipboardStatus}</div></section>
    <footer>React 页面 · C++ 服务 · 离线桌面应用</footer></main>;
}
createRoot(document.getElementById('app')!).render(<App/>);
