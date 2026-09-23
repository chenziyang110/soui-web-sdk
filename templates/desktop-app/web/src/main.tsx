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
  return <main><div className="eyebrow">SOUI SDK · DESKTOP STARTER</div><h1>SouiStarter</h1>
    <p className="intro">从这里开始构建你的桌面应用。</p><section><h2>{client?'原生服务已接入':'连接原生服务'}</h2>
      <p>页面通过 SDK 调用 C++，输入你的名字试一试。</p>
      <label htmlFor="name">名字</label><div className="row"><input id="name" value={name} maxLength={32} onChange={e=>setName(e.target.value)}/><button onClick={greet} disabled={busy||!client}>{busy?'处理中…':'调用 C++'}</button></div>
      <div className="result" role="status">{message}</div></section><footer>React 页面 · C++ 服务 · 离线桌面应用</footer></main>;
}
createRoot(document.getElementById('app')!).render(<App/>);
