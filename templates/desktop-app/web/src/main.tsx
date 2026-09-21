import React, {useEffect,useState} from 'react';
import {createRoot} from 'react-dom/client';
import {connectNative} from './native';
import './style.css';
const connection=(()=>{try{return {client:connectNative(),error:''};}catch(error){return {client:null,error:String(error)}}})();
function App(){
  const [name,setName]=useState('Developer');
  const [message,setMessage]=useState(connection.error || '正在连接原生服务…');
  const [busy,setBusy]=useState(false);
  useEffect(()=>{if(connection.client)void connection.client.call('app.greet',{name:'Developer'}).then(async result=>{
    setMessage(result.message);
    await connection.client!.call('bridge.probe.report',{ready:true});
  }).catch(error=>setMessage(String(error)));},[]);
  async function greet(){if(!connection.client)return;setBusy(true);try{setMessage((await connection.client.call('app.greet',{name})).message)}catch(error){setMessage(String(error))}finally{setBusy(false)}}
  return <main><div className="eyebrow">SOUI SDK · DESKTOP STARTER</div><h1>SouiStarter</h1>
    <p className="intro">从这里开始构建你的桌面应用。</p><section><h2>原生服务已接入</h2>
      <p>页面通过 SDK 调用 C++，输入你的名字试一试。</p>
      <label htmlFor="name">名字</label><div className="row"><input id="name" value={name} maxLength={32} onChange={e=>setName(e.target.value)}/><button onClick={greet} disabled={busy||!connection.client}>{busy?'处理中…':'调用 C++'}</button></div>
      <div className="result" role="status">{message}</div></section><footer>React 页面 · C++ 服务 · 离线桌面应用</footer></main>;
}
createRoot(document.getElementById('app')!).render(<App/>);
