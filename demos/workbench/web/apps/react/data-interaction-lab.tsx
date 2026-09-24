import React, { useEffect, useMemo, useRef, useState } from 'react';
import { AsyncMemoryAdapter, FIXTURE, type BenchmarkRow } from '../../benchmarks/electron-smoothness/benchmark-contract';
import './data-interaction-lab.css';

const visibleRows = 34;

export function DataInteractionLab({ task }: { task: React.ReactNode }) {
  const adapter = useMemo(() => new AsyncMemoryAdapter(), []);
  const [rows, setRows] = useState<BenchmarkRow[]>([]);
  const [loaded, setLoaded] = useState(0);
  const [scrollTop, setScrollTop] = useState(0);
  const [input, setInput] = useState('');
  const [compositionEvents, setCompositionEvents] = useState(0);
  const [composing, setComposing] = useState(false);
  const [viewport, setViewport] = useState(() => ({ width: window.innerWidth, height: window.innerHeight }));
  const [rafRate, setRafRate] = useState(0);
  const list = useRef<HTMLDivElement>(null);

  useEffect(() => {
    let live = true;
    void adapter.loadAll((count) => { if (live) setLoaded(count); }).then((complete) => {
      if (live) setRows(complete);
    });
    return () => { live = false; };
  }, [adapter]);
  useEffect(() => {
    const onResize = () => setViewport({ width: window.innerWidth, height: window.innerHeight });
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, []);
  useEffect(() => {
    let frame = 0;
    let frames = 0;
    let began = performance.now();
    const sample = (now: number) => {
      frames += 1;
      if (now - began >= 1000) {
        setRafRate(Math.round(frames * 1000 / (now - began)));
        frames = 0;
        began = now;
      }
      frame = window.requestAnimationFrame(sample);
    };
    frame = window.requestAnimationFrame(sample);
    return () => window.cancelAnimationFrame(frame);
  }, []);

  const first = Math.max(0, Math.floor(scrollTop / FIXTURE.rowHeightPx) - 8);
  const visible = rows.slice(first, first + visibleRows);
  return <main className="rx-page v02-lab" data-v02-ready={rows.length === FIXTURE.rowCount}>
    <header className="v02-head"><div><small>V02 / DATA & INTERACTION</small><h1>万条数据，操作仍需跟手。</h1><p>同 Electron 对照页使用一份确定性数据。滚动、中文输入、窗口缩放和后台 SDK 任务可以同时操作。</p></div><div className="v02-head-metrics"><strong>{loaded.toLocaleString()} / {FIXTURE.rowCount.toLocaleString()}</strong><span>loaded rows</span></div></header>
    <section className="v02-grid">
      <article className="v02-card"><header><div><small>VIRTUAL LIST</small><h2>10,000 行文件索引</h2></div><button type="button" onClick={() => { if (list.current) list.current.scrollTop = 0; setScrollTop(0); }}>回到顶部</button></header>
        <p>固定 32 px 行高，视口只绘制附近行；向下滚动后仍可启动下方长任务。</p>
        <div ref={list} className="v02-list" role="region" aria-label="万条文件索引滚动列表" tabIndex={0} data-v02-scroll-top={Math.round(scrollTop)} onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}>
          <div style={{ height: rows.length * FIXTURE.rowHeightPx, position: 'relative' }}>{visible.map((row, index) => <div key={row.id} className="v02-row" style={{ top: (first + index) * FIXTURE.rowHeightPx, height: FIXTURE.rowHeightPx }}><b>{row.name}</b><span>{row.group}</span><span>{row.sizeKb} KB</span><em>{row.status}</em></div>)}</div>
        </div><footer>fixture {FIXTURE.fixtureId} · 当前滚动 {Math.round(scrollTop)} px · DOM 行 {visible.length}</footer>
      </article>
      <div className="v02-side"><article className="v02-card"><small>CHINESE INPUT</small><h2>输入法与焦点</h2><label htmlFor="v02-chinese-input">在此输入中文并观察组合事件</label><input id="v02-chinese-input" value={input} onChange={(event) => setInput(event.currentTarget.value)} onCompositionStart={() => { setComposing(true); setCompositionEvents((value) => value + 1); }} onCompositionUpdate={() => setCompositionEvents((value) => value + 1)} onCompositionEnd={() => { setComposing(false); setCompositionEvents((value) => value + 1); }} placeholder="请输入中文，边输入边滚动" /><div className="v02-state"><span>状态：{composing ? '正在组合' : '等待输入'}</span><span>组合事件：{compositionEvents}</span><span>字符：{input.length}</span></div><p className="v02-echo">{input || '输入内容会显示在这里。'}</p></article>
        <article className="v02-card"><small>LAYOUT & MOTION</small><h2>缩放与动效</h2><div className="v02-motion" aria-hidden="true"><i /></div><div className="v02-state"><span>内容区：{viewport.width} × {viewport.height}</span><span>RAF 诊断：{rafRate} / s</span></div><p>RAF 是 JavaScript 回调频率，不代表屏幕实际呈现帧率；性能验收仍需独立呈现追踪。</p></article>
      </div>
    </section>
    {task}
  </main>;
}
