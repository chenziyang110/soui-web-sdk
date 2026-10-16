import React, {useEffect, useRef, useState, type ComponentType, type PropsWithChildren} from 'react';
import {createReferenceController} from './controller';
import {visibleRows} from './model.mjs';
import {runReferenceParity} from './parity-scenario';
import './style.css';

type ButtonProps = PropsWithChildren<{id?:string;disabled?:boolean;onClick?:()=>void;type?:'button'|'submit'}>;
const NativeButton = (props:ButtonProps) => <button type="button" {...props}/>;
export function ReferenceReact({profile,Button=NativeButton}:{profile:string;Button?:ComponentType<ButtonProps>}) {
  const [controller] = useState(()=>createReferenceController(profile));
  const [,redraw] = useState(0), [scrollTop,setScrollTop] = useState(0), [scenario,setScenario] = useState('');
  const [scenarioBusy,setScenarioBusy] = useState(false);
  const scroller=useRef<HTMLDivElement>(null),dialogClose=useRef<HTMLButtonElement>(null);
  useEffect(()=>{
    const remove=controller.observe(()=>redraw(value=>value+1));
    void controller.initialize().catch(()=>setScenario('E_REFERENCE_INITIALIZATION'));
    return ()=>{remove();controller.shutdown();};
  },[controller]);
  const view=controller.snapshot(),state=view.state;
  useEffect(()=>{if(view.dialog)dialogClose.current?.focus();},[view.dialog]);
  const command=(id:string,action='activate',input:Record<string,unknown>={})=>{
    void controller.perform(id,action,input as never).catch(()=>undefined);
  };
  const slice=visibleRows(view.rows,scrollTop,320);
  return <main className={'reference '+state.theme}>
    <header><div><p className="eyebrow">SOUI · REFERENCE</p><h1>万行资料工作台</h1>
      <p>{profile} · 同一固定样本、同一 C++ 业务入口</p></div>
      <nav><Button id="reference-theme" onClick={()=>command('theme')}>切换主题</Button>
        <Button id="reference-focus" onClick={()=>command('focus')}>聚焦名称</Button>
        <Button id="reference-reload" onClick={()=>command('reload')}>保存界面并刷新</Button></nav></header>
    <p id="reference-status" role="status" className="status">{view.message}
      {view.error&&<strong> · {view.error}</strong>}</p>
    <p className="note">刷新结果：{view.restoreStatus}；存储：{view.storageStatus}；旧任务：{view.taskRecovery}。
      中文输入框是业务示例；本页面不证明实际 IME、键盘或呈现性能。</p>
    <section aria-labelledby="task-title"><h2 id="task-title">真实原生摘要任务</h2>
      <p>原生读取固定原始样本；当前表单草稿不会改变样本。任务运行期间仍可过滤、排序和选择表格。</p>
      <div className="controls"><Button id="reference-start" disabled={!view.controls.start.enabled||view.controls.start.busy}
        onClick={()=>command('start')}>开始 5 秒任务</Button>
        <Button id="reference-query" disabled={!view.controls.query.enabled||view.controls.query.busy} onClick={()=>command('query')}>查询真实状态</Button>
        <Button id="reference-cancel" disabled={!view.controls.cancel.enabled||view.controls.cancel.busy}
          onClick={()=>command('cancel')}>请求取消</Button>
        <progress max={100} value={view.task.progress} aria-label="原生任务进度"/>
        <span>{view.task.state} · {view.task.progress}%</span></div>
      <p className="task-detail">{view.task.message} {view.task.observationError} {view.task.subscriptionError}</p>
      {view.task.result&&<pre className="result">{JSON.stringify(view.task.result)}</pre>}
    </section>
    <section id="reference-agent-task" aria-labelledby="agent-task-title"><h2 id="agent-task-title">Agent 子任务观察</h2>
      <p>此区与手工任务使用同一原生业务。只有本次授权命令可以启动、查询或请求取消；页面不会借用 Agent 权限。</p>
      <progress max={100} value={view.agentTask.progress} aria-label="Agent 子任务已查询进度"/>
      <span>{view.agentTask.state} · {view.agentTask.progress}%</span>
      <p>{view.agentTask.cancelRequested?'已确认取消请求，等待实际查询终态。':'接纳回执不等于完成；进度和终态来自实际查询。'} {view.agentTask.error}</p>
    </section>
    <div className="columns"><section aria-labelledby="table-title"><h2 id="table-title">固定 10,000 行</h2>
      <div id="reference-filter" className="controls"><label>过滤名称 <input value={state.query} maxLength={128}
        onChange={event=>command('filter','set',{query:event.target.value})}/></label>
        <label>分类 <select value={state.category} onChange={event=>command('filter','set',{query:state.query,category:event.target.value})}>
          <option value="all">全部</option>{controller.fixture.groups.map(group=><option key={group}>{group}</option>)}</select></label></div>
      <div id="reference-sort" className="controls"><label>排序 <select value={state.sort}
        onChange={event=>command('sort','set',{sort:event.target.value,descending:state.descending})}>
        <option value="id">编号</option><option value="name">名称</option><option value="sizeKb">大小</option></select></label>
        <label><input type="checkbox" checked={state.descending}
          onChange={event=>command('sort','set',{sort:state.sort,descending:event.target.checked})}/>降序</label>
        <span>匹配 {view.rows.length}；已选 {state.selectedIds.length}/100</span></div>
      <div className="table-heading"><span>选择</span><span>名称</span><span>分类</span><span>KB</span></div>
      <div id="reference-selection" className="table-scroll" ref={scroller} role="region" aria-label="虚拟化资料表"
        onScroll={event=>setScrollTop(event.currentTarget.scrollTop)}>
        <div style={{height:view.rows.length*32,position:'relative'}}>
          <div style={{position:'absolute',top:slice.first*32,left:0,right:0}}>
          {slice.rows.map(row=><div className="table-row" key={row.id}>
            <input type="checkbox" aria-label={'选择 '+row.name} checked={state.selectedIds.includes(row.id)}
              onChange={event=>command('selection','set',{id:row.id,selected:event.target.checked})}/>
            <span>{row.name}</span><span>{row.group}</span><span>{row.sizeKb}</span></div>)}</div>
        </div></div>
    </section><section aria-labelledby="chart-title"><h2 id="chart-title">同一分类分布</h2>
      <svg viewBox="0 0 340 210" role="img" aria-label="固定样本各分类行数">
        {view.chart.map((item,index)=><g key={item.group}>
          <text x="0" y={24+index*38}>{item.group}</text>
          <rect x="50" y={8+index*38} width={item.count/10} height="24"/>
          <text x={60+item.count/10} y={24+index*38}>{item.count}</text></g>)}</svg>
      <form id="reference-form" onSubmit={event=>{event.preventDefault();command('form','submit');}}>
        <h2>中文表单</h2><label htmlFor="reference-name">名称（64 UTF-8 字节）</label>
        <input id="reference-name" value={state.draftName} maxLength={64}
          onChange={event=>command('form','set',{name:event.target.value,notes:state.draftNotes})}/>
        <label htmlFor="reference-notes">备注（256 UTF-8 字节）</label>
        <textarea id="reference-notes" value={state.draftNotes} maxLength={256}
          onChange={event=>command('form','set',{name:state.draftName,notes:event.target.value})}/>
        <Button type="submit" disabled={!view.ready||view.formBusy}>由 C++ 确认</Button>
      </form>
      <Button id="reference-dialog" onClick={()=>command('dialog','open')}>查看业务说明</Button>
    </section></div>
    <section><h2>同流程自检</h2><p>通过同一公开 GUI 业务入口实际发起任务；这不是键盘、IME、外部 Agent 或性能资格。</p>
      <Button disabled={!view.ready||scenarioBusy} onClick={()=>{
        setScenarioBusy(true);setScenario('正在执行…');
        void runReferenceParity(controller,()=>{if(scroller.current){scroller.current.scrollTop=160000;setScrollTop(160000);}})
          .then(result=>setScenario(JSON.stringify(result,null,2)),()=>setScenario('E_PARITY_SCENARIO'))
          .finally(()=>setScenarioBusy(false));
      }}>运行相同业务流程</Button><pre id="reference-scenario-output">{scenario}</pre></section>
    {view.dialog&&<div className="dialog-backdrop"><div role="dialog" aria-modal="true" aria-labelledby="dialog-title"
      onKeyDown={event=>{if(event.key==='Escape'){event.preventDefault();command('dialog','close');}
        if(event.key==='Tab'){event.preventDefault();dialogClose.current?.focus();}}}>
      <h2 id="dialog-title">界面与原生的边界</h2><p>只保存有界界面草稿和偏好。旧文档任务引用不会恢复权限、不会自动重放。</p>
      <p>取消请求不等于取消完成；失败与未知状态会保留。此表单不写文件。</p>
      <button ref={dialogClose} onClick={()=>command('dialog','close')}>关闭说明</button></div></div>}
  </main>;
}
