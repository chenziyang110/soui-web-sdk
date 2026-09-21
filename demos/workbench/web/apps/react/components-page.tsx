import { useEffect, useMemo, useState } from 'react';
import { Button } from 'primereact/button';
import { Card } from 'primereact/card';
import { Tag } from 'primereact/tag';
import { ProgressBar } from 'primereact/progressbar';
import { InputText } from 'primereact/inputtext';
import { Dropdown } from 'primereact/dropdown';
import { Checkbox } from 'primereact/checkbox';
import { Slider } from 'primereact/slider';
import { DataTable } from 'primereact/datatable';
import { Column } from 'primereact/column';
import type { RendererStats } from './main';
import { componentCatalog } from './component-catalog';
import type { SouiNativeClientImpl } from '../../packages/sdk/soui_native_client';

type ComponentRow = { name: string; state: string; latency: string; owner: string };
const offlineComponentRows: ComponentRow[] = [
  { name: '媒体队列', state: 'offline', latency: '—', owner: 'C++ media worker' },
  { name: '原生进度', state: 'offline', latency: '—', owner: 'soui.bridge/1' },
  { name: 'CAD 视口', state: 'offline', latency: '—', owner: 'C++ scene worker' },
  { name: 'ZIP 任务', state: 'offline', latency: '—', owner: 'zlib worker' },
];

type TimedResult<T> = { ok: boolean; value?: T; elapsedMs: number };
async function measureNative<T>(operation: () => Promise<T>): Promise<TimedResult<T>> {
  const started = performance.now();
  try {
    return { ok: true, value: await operation(), elapsedMs: Math.max(0, Math.round(performance.now() - started)) };
  } catch {
    return { ok: false, elapsedMs: Math.max(0, Math.round(performance.now() - started)) };
  }
}

export function ComponentsPage({ renderer, client, onNotice }: { renderer?: RendererStats; client: SouiNativeClientImpl | null; onNotice: (message: string) => void }) {
  const [value, setValue] = useState('Aurora 产品片头.mp4');
  const [profile, setProfile] = useState('质量优先');
  const [enabled, setEnabled] = useState(true);
  const [quality, setQuality] = useState(82);
  const [query, setQuery] = useState('');
  const [group, setGroup] = useState('全部');
  const [nativeProbe, setNativeProbe] = useState<'idle' | 'running' | 'ready' | 'failed'>('idle');
  const [nativeProbeText, setNativeProbeText] = useState('尚未读取 native 状态');
  const [componentRows, setComponentRows] = useState<ComponentRow[]>(offlineComponentRows);
  const profiles = ['质量优先', '平衡', '速度优先'];
  const groups = useMemo(() => ['全部', ...Array.from(new Set(componentCatalog.map((item) => item.group)))], []);
  const filteredCatalog = useMemo(() => componentCatalog.filter((item) => {
    const haystack = `${item.name} ${item.title}`.toLowerCase();
    return (group === '全部' || item.group === group) && haystack.indexOf(query.toLowerCase()) >= 0;
  }), [group, query]);
  useEffect(() => {
    let live = true;
    if (!client || !enabled) {
      setComponentRows(offlineComponentRows);
      return () => { live = false; };
    }
    void Promise.all([
      measureNative(() => client.call('desktop.media.getCapabilities', {})),
      measureNative(() => client.call('desktop.workload.getCapabilities', {})),
      measureNative(() => client.call('desktop.rendering.getStats', {})),
    ]).then(([media, workload, rendering]) => {
      if (!live) return;
      const mediaReady = Boolean(media.value?.ffmpeg || media.value?.nativeDecoder || media.value?.nativeStillImage);
      const workerReady = Boolean(workload.value?.cadScene || workload.value?.zipDeflate || workload.value?.video4kNativeFrame);
      setComponentRows([
        { name: '媒体队列', state: mediaReady ? 'ready' : media.ok ? 'partial' : 'offline', latency: media.ok ? `${media.elapsedMs}ms` : '—', owner: 'C++ media worker' },
        { name: '原生进度', state: rendering.ok ? 'ready' : 'offline', latency: rendering.ok ? `${rendering.elapsedMs}ms` : '—', owner: 'soui.bridge/1' },
        { name: 'CAD 视口', state: workload.value?.cadScene ? 'ready' : workload.ok ? 'partial' : 'offline', latency: workload.ok ? `${workload.elapsedMs}ms` : '—', owner: 'C++ scene worker' },
        { name: 'ZIP 任务', state: workload.value?.zipDeflate ? 'ready' : workerReady ? 'partial' : 'offline', latency: workload.ok ? `${workload.elapsedMs}ms` : '—', owner: 'zlib worker' },
      ]);
    });
    return () => { live = false; };
  }, [client, enabled]);
  const probeNative = async () => {
    if (!enabled) {
      setNativeProbe('idle');
      setNativeProbeText('native worker 已暂停，启用后才允许调用 C++。');
      onNotice('native worker 当前已暂停。');
      return;
    }
    if (!client || nativeProbe === 'running') {
      onNotice(client ? 'native 状态读取正在进行。' : '当前页面未连接 SOUI + Miniblink 宿主。');
      return;
    }
    setNativeProbe('running');
    setNativeProbeText('正在通过标准 Bridge 读取 renderer 状态…');
    try {
      const stats = await client.call('desktop.rendering.getStats', {});
      setNativeProbe('ready');
      setNativeProbeText(`${stats.gpuStatus} · ${stats.activePacingHz}Hz · ${stats.drawIntervalMs}ms · ${stats.architecture}`);
      onNotice(`native renderer 已返回：${stats.gpuStatus} / ${stats.activePacingHz}Hz`);
    } catch (error) {
      setNativeProbe('failed');
      setNativeProbeText(error instanceof Error ? error.message : 'native 状态读取失败');
      onNotice('native 状态读取失败，请查看宿主连接和权限。');
    }
  };
  return <main className="rx-page rx-components subpixel-antialiased">
    <header className="rx-component-head"><div><small>COMPONENT GALLERY / PRIME REACT</small><h1>一套组件，覆盖桌面产品的常见工作面。</h1><p>输入、选择、进度、表格和原生状态统一使用 React + TypeScript + PrimeReact，最终离线落到 Miniblink。</p></div><Tag value={`${componentCatalog.length} component families`} severity="info" /></header>
    <section className="rx-component-showcase">
      <article className="rx-component-panel rx-component-controls"><header><small>FORM SURFACE</small><Tag value="typed" severity="success" /></header><label>当前文件<InputText value={value} onChange={(event) => setValue(event.target.value)} /></label><label>输出策略<Dropdown value={profile} options={profiles} onChange={(event) => setProfile(event.value)} /></label><div className="rx-component-inline"><Checkbox inputId="native-enabled" checked={enabled} onChange={(event) => { setEnabled(Boolean(event.checked)); if (!event.checked) { setNativeProbe('idle'); setNativeProbeText('native worker 已暂停，启用后才允许调用 C++。'); } }} /><label htmlFor="native-enabled">允许 native worker</label></div><label>质量 <strong>{quality}%</strong><Slider value={quality} onChange={(event) => setQuality(Number(event.value))} /></label><Button label={nativeProbe === 'running' ? '读取 native 中…' : '读取 native 状态'} icon="pi pi-send" onClick={() => void probeNative()} disabled={!enabled || nativeProbe === 'running'} className="rx-prime-button" /><small className={`rx-native-probe rx-native-probe-${nativeProbe}`}>{nativeProbeText}</small></article>
      <article className="rx-component-panel rx-component-table"><header><div><small>LIVE DATA TABLE</small><h2>任务与控件状态</h2></div><span>{renderer?.rendererBackend ?? 'software-safe'}</span></header><DataTable value={componentRows} size="small" stripedRows responsiveLayout="scroll"><Column field="name" header="模块" /><Column field="state" header="状态" body={(row: ComponentRow) => <Tag value={row.state} severity={row.state === 'running' ? 'warning' : row.state === 'ready' ? 'success' : 'info'} />} /><Column field="latency" header="反馈" /><Column field="owner" header="实现" /></DataTable></article>
      <aside className="rx-component-panel rx-component-native"><small>NATIVE CONTRACT</small><h2>桌面能力在线</h2><div className="rx-native-signal"><i />{client && enabled ? 'C++ Bridge connected' : enabled ? 'Bridge unavailable' : 'native worker paused'}</div><dl><div><dt>renderer</dt><dd>{renderer?.rendererBackend ?? 'software-safe'}</dd></div><div><dt>architecture</dt><dd>{renderer?.architecture ?? 'x86/x64'}</dd></div><div><dt>draw budget</dt><dd>{renderer?.drawIntervalMs ?? 17}ms</dd></div><div><dt>device scale</dt><dd>{Number(renderer?.deviceScaleFactor ?? 1).toFixed(2)}x</dd></div></dl><ProgressBar value={quality} showValue={false} /><p>UI 状态、任务进度和窗口能力都通过同一套权限化契约进入 React。</p></aside>
    </section>
    <section className="rx-component-catalog"><header><div><small>ALL COMPONENT FAMILIES / LOCAL CATALOG</small><h2>82 个组件族，按需加载真实示例。</h2></div><span>{filteredCatalog.length} / {componentCatalog.length}</span></header><div className="rx-component-catalog-toolbar"><InputText value={query} onChange={(event) => setQuery(event.target.value)} placeholder="搜索 Button / 表格 / 对话框" aria-label="搜索组件" />{groups.map((item) => <button key={item} className={group === item ? 'active' : ''} onClick={() => setGroup(item)}>{item}</button>)}</div><div className="rx-component-grid">{filteredCatalog.map((item) => <Card key={item.name} className="rx-prime-card" onClick={() => onNotice(`${item.name} · ${item.title} · lazy contract ready`)}><strong>{item.title}</strong><small>{item.name}</small><Tag value={item.group} severity="info" /><span>offline · typed contract · lazy example</span></Card>)}</div></section>
  </main>;
}
