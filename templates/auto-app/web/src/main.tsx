import React, {useEffect, useRef, useState} from 'react';
import {createRoot} from 'react-dom/client';
import {connectSouiNative, createSouiGui, SouiBridgeError, type SouiCoreMethodMap} from '@soui-sdk';
import {callAction, authorizedActionTools, type ActionMethods, type GreetInput} from './generated/actions';
import './style.css';

type Methods = SouiCoreMethodMap & ActionMethods;
function App() {
  const [message, setMessage] = useState('正在连接原生服务…');
  const [name, setName] = useState('开发者');
  const [ready, setReady] = useState(false);
  const [busy, setBusy] = useState(false);
  const action = useRef<ReturnType<typeof createSouiGui> | null>(null);
  useEffect(() => {
    let active = true;
    const gui = createSouiGui();
    action.current = gui;
    void connectSouiNative<Methods>().then(async client => {
      if (!active) return;
      if (!authorizedActionTools(await client.discover()).some(tool => tool.name === 'app.greet'))
        throw new Error('当前宿主未安装问候动作。');
      const button = document.getElementById('greet');
      if (!button) throw new Error('界面尚未准备好。');
      gui.register({id: 'greet', role: 'button', name: '发送问候', element: button,
        state: () => ({enabled: active}),
        actions: {activate: async value => {
          if (active) setBusy(true);
          try {
            const result = await callAction(client, 'app.greet', value as GreetInput);
            if (active) setMessage(result.message);
            return result;
          } finally { if (active) setBusy(false); }
        }}});
      setReady(true);
      await gui.waitFor({id: 'greet', timeoutMs: 5000});
      const response = await gui.perform({id: 'greet', action: 'activate', input: {name: '开发者'}});
      if ((response as {message?: string}).message !== '你好，开发者。') throw new Error('原生问候响应不符合契约。');
      let rejected = false;
      try { await client.call('app.greet', {name: ''}); }
      catch (error) { rejected = error instanceof SouiBridgeError && error.code === 'E_INVALID_PARAMS'; }
      if (!rejected) throw new Error('原生动作未拒绝非法输入。');
      if (!active) return;
      await client.call('bridge.probe.report', {ready: true});
      if (active) setReady(true);
    }).catch(error => { if (active) setMessage(String(error)); });
    return () => { active = false; gui.dispose(); action.current = null; };
  }, []);
  return <main><p className="eyebrow">SOUI · DESKTOP</p><h1>从这里，开始构建。</h1>
    <p className="intro">一个轻巧的起点，将界面与原生能力连接起来。</p>
    <section aria-labelledby="welcome"><h2 id="welcome">打个招呼</h2>
      <label htmlFor="name">你的名字</label><div className="form">
        <input id="name" value={name} maxLength={32} onChange={event => setName(event.target.value)}/>
        <button id="greet" disabled={!ready || busy || !name.trim()} onClick={() => {
          void action.current?.perform({id: 'greet', action: 'activate', input: {name}}).catch(error => setMessage(String(error)));
        }}>{busy ? '正在发送…' : '发送问候'}</button></div>
      <p role="status" className="message">{message}</p></section>
    <footer>本地运行 · 原生连接 · 随你扩展</footer></main>;
}
createRoot(document.getElementById('app')!).render(<App/>);
