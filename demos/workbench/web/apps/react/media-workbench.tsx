import React, { useEffect, useRef, useState } from 'react';
import { Button } from 'primereact/button';
import { ProgressBar } from 'primereact/progressbar';
import type { SouiNativeClientImpl } from '../../packages/sdk/soui_native_client';
import { ProductIcon } from './product-home';
import './media-workbench.css';

export const mediaProfiles = [
  { format: 'MP4', codec: 'H.264', label: 'MP4 视频', detail: 'H.264 + AAC · 通用视频' },
  { format: 'MOV', codec: 'H.264', label: 'MOV 视频', detail: 'H.264 + AAC · 编辑工作流' },
  { format: 'WebM', codec: 'VP9', label: 'WebM 视频', detail: 'VP9 + Opus · 编码较慢' },
  { format: 'MP3', codec: 'MP3', label: 'MP3 音频', detail: '提取或转换音轨 · 有损压缩' },
  { format: 'WAV', codec: 'PCM', label: 'WAV 音频', detail: 'PCM 16-bit · 未压缩音频' },
  { format: 'GIF', codec: 'GIF', label: 'GIF 动图', detail: '前 10 秒 · 480px · 12fps' },
  { format: 'PNG', codec: 'PNG', label: 'PNG 截图', detail: '原始尺寸第一帧 / 图片转换' },
  { format: 'WebP', codec: 'WebP', label: 'WebP 图片', detail: '第一帧静态图 / 图片转换' },
];
export type MediaPreset = { title: string; format: string; quality: number; operation?: string };
type Result = { taskId: string; state: string; progress: number; message: string; output: string };
type Source = { token: string; name: string; size?: number };
const terminal = (state: string) => ['succeeded', 'failed', 'cancelled'].includes(state);
const idle: Result = { taskId: '', state: 'idle', progress: 0, message: '', output: '' };
const errorText = (error: unknown) => error instanceof Error ? error.message : '原生操作失败，请重试。';

export function MediaWorkbench({ client, preset, home }: { client: SouiNativeClientImpl | null; preset: MediaPreset; home: () => void }) {
  const [source, setSource] = useState<Source | null>(null);
  const [format, setFormat] = useState(preset.format);
  const [quality, setQuality] = useState(preset.quality);
  const [available, setAvailable] = useState(false);
  const [nativeStillImage, setNativeStillImage] = useState(false);
  const [capability, setCapability] = useState('正在检查本机处理引擎…');
  const [task, setTask] = useState<Result>(idle);
  const [savedName, setSavedName] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const live = useRef(true);
  const pending = useRef(false);
  const timer = useRef(0);
  const taskId = useRef('');
  const outputId = useRef('');
  const serial = useRef(0);
  const profile = mediaProfiles.find(item => item.format === format) || mediaProfiles[0];
  const operation = preset.operation || '';
  const operationLabel = operation === 'reverse' ? '倒放' : operation === 'rotate90' ? '顺时针旋转 90°' : operation === 'mirror' ? '水平镜像' : operation === 'speed2x' ? '2 倍速' : operation === 'crop16x9' ? '中心裁剪 80%' : operation === 'trim10' ? '截取前 10 秒' : operation === 'fade' ? '片头淡入' : operation === 'grayscale' ? '灰度处理' : operation === 'watermark' ? '原生标记' : operation === 'denoise' ? '音频降噪' : operation === 'normalize' ? '响度归一化' : '';
  const cancelNative = (id: string) => { if (id && client) void client.call('media.convert.cancel', { taskId: id }).catch(() => undefined); };
  useEffect(() => {
    live.current = true;
    if (!client) setCapability('浏览器预览未连接 C++；请在桌面 Demo 中使用。');
    else void client.call('desktop.media.getCapabilities', {}).then(result => {
      if (!live.current) return;
      setAvailable(result.ffmpeg);
      setNativeStillImage(Boolean(result.nativeStillImage));
      setCapability(result.ffmpeg ? '已配置本机 FFmpeg · 实际编码能力以任务结果为准' : result.nativeStillImage ? 'Media Foundation 已在线 · 选择 PNG 可直接生成 native 首帧快照；其他格式请配置本机 FFmpeg。' : '未配置处理引擎。请通过 Run-MediaDemo.ps1 指定本机 FFmpeg。');
    }).catch(reason => { if (live.current) setCapability(errorText(reason)); });
    return () => { live.current = false; window.clearTimeout(timer.current); cancelNative(taskId.current); };
  }, [client]);
  useEffect(() => { setFormat(preset.format); setQuality(preset.quality); }, [preset]);
  const canProcess = available || (nativeStillImage && format === 'PNG');
  const nativeSnapshot = !available && nativeStillImage && format === 'PNG';
  // One in-flight query at a time; terminal/error/unmount stops polling.
  const poll = (id: string, saving: boolean, name = '') => {
    taskId.current = id;
    const next = async () => {
      if (!client || !live.current || taskId.current !== id) return;
      try {
        const result = await client.call('media.convert.query', { taskId: id });
        if (!live.current || taskId.current !== id) return;
        setTask(result);
        if (terminal(result.state)) {
          taskId.current = ''; pending.current = false; setBusy(false);
          if (result.state === 'succeeded') { if (saving) setSavedName(name); else outputId.current = id; }
          if (result.state === 'failed') setError(result.message);
          return;
        }
        timer.current = window.setTimeout(next, 250);
      } catch (reason) {
        if (!live.current) return;
        cancelNative(id); taskId.current = ''; pending.current = false; setBusy(false); setError(errorText(reason));
        setTask(previous => ({ ...previous, state: 'failed', message: '状态读取失败，已请求取消后台任务。' }));
      }
    };
    void next();
  };
  const choose = async () => {
    if (!client || pending.current) return;
    pending.current = true; setBusy(true); setError('');
    try {
      const result = await client.call('desktop.media.pickFile', { userGesture: true });
      if (live.current && !result.cancelled) { setSource({ token: result.fileToken, name: result.name, size: result.size }); setTask(idle); outputId.current = ''; setSavedName(''); }
    } catch (reason) { if (live.current) setError(errorText(reason)); }
    finally { pending.current = false; if (live.current) setBusy(false); }
  };
  const start = async () => {
    if (!client || !source || !canProcess || pending.current) return;
    pending.current = true; setBusy(true); setError(''); setSavedName(''); outputId.current = '';
    setTask({ ...idle, state: 'queued', message: '正在创建原生处理任务…' });
    try {
      const operationId = `media-ui-${Date.now()}-${++serial.current}`;
      const result = operation
        ? await client.call('media.transform.start', { source: source.token, operation, format, codec: profile.codec, quality, operationId })
        : await client.call('media.convert.start', { source: source.token, format, codec: profile.codec, quality, operationId });
      if (!live.current) { cancelNative(result.taskId); return; }
      poll(result.taskId, false);
    } catch (reason) { if (live.current) { pending.current = false; setBusy(false); setError(errorText(reason)); setTask(idle); } }
  };
  const save = async () => {
    if (!client || !outputId.current || pending.current) return;
    pending.current = true; setBusy(true); setError('');
    try {
      const result = await client.call('desktop.media.saveOutput', { taskId: outputId.current, userGesture: true });
      if (!live.current) { if (!result.cancelled) cancelNative(result.taskId); return; }
      if (result.cancelled) { pending.current = false; setBusy(false); return; }
      setTask({ ...idle, state: 'queued', message: '正在后台保存文件…' });
      poll(result.taskId, true, result.name);
    } catch (reason) { if (live.current) { pending.current = false; setBusy(false); setError(errorText(reason)); } }
  };
  const cancel = async () => {
    if (!client || !taskId.current) return;
    try { await client.call('media.convert.cancel', { taskId: taskId.current }); if (live.current) setTask(previous => ({ ...previous, state: 'cancel_requested', message: '正在停止原生任务…' })); }
    catch (reason) { if (live.current) setError(errorText(reason)); }
  };
  return <main className="mw-page">
    <div className="mw-breadcrumb"><button onClick={home}>全部工具</button><span>／</span>{preset.title}</div>
    <header className="mw-heading"><div><span className="mw-eyebrow">本地媒体工作台{operationLabel ? ` · ${operationLabel}` : ''}</span><h1>{preset.title}</h1><p>{operationLabel ? `由 C++ native filter 执行${operationLabel}，页面只负责参数和任务状态。` : '文件留在本机，编码交给原生后台。处理完成后，自行选择保存位置。'}</p></div><span className={`mw-engine ${canProcess ? 'ready' : ''}`}>{available ? 'C++ 引擎已配置' : nativeSnapshot ? 'C++ 原生快照可用' : '未连接处理引擎'}</span></header>
    <div className="mw-layout"><section className="mw-input-panel"><div className="mw-panel-title"><b>01　选择素材</b><span>单文件 · 最大 4 GiB</span></div>
      <button className={`mw-file-picker ${source ? 'has-file' : ''}`} onClick={() => void choose()} disabled={!client || busy}><span className="mw-file-icon"><ProductIcon name={source ? 'video' : 'add'} /></span><strong>{source ? source.name : '添加本地媒体文件'}</strong><span>{source ? source.size == null ? '内置样例 · 使用同一真实处理流程' : `${(source.size / 1048576).toFixed(2)} MiB · 点击更换` : '视频、音频或图片 · 打开系统文件选择器'}</span><small>不上传，不依赖浏览器文件权限</small></button>
      <div className="mw-input-actions"><Button label="选择文件" icon="pi pi-folder-open" onClick={() => void choose()} disabled={!client || busy} /><button disabled={!client || busy} onClick={() => { setSource({ token: 'sample', name: '内置视频样例.mp4' }); setTask(idle); outputId.current = ''; setSavedName(''); setError(''); }}>试用内置样例</button></div>
      <div className="mw-native-note"><ProductIcon name="check" /><div><b>真实原生处理</b><p>受限输出配置 → C++ 工作线程 → 编码进程 → 本地文件。进度来自引擎回报；未知时不估算百分比。</p></div></div>
    </section><section className="mw-settings"><div className="mw-panel-title"><b>02　输出设置</b><span>固定安全配置</span></div><label className="mw-field">输出格式<select value={format} disabled={busy || Boolean(operation)} onChange={event => setFormat(event.target.value)}>{mediaProfiles.map(item => <option key={item.format} value={item.format}>{item.label}</option>)}</select></label><p className="mw-profile-detail">{operationLabel ? `native 操作固定使用 ${profile.label}，输出不会覆盖源文件。` : profile.detail}</p>
      <label className="mw-field">编码质量 <b>{quality}</b><input type="range" min="10" max="95" value={quality} disabled={busy || ['PNG', 'WAV', 'GIF'].includes(format)} onChange={event => setQuality(Number(event.target.value))} /></label><p className="mw-profile-detail">{['PNG', 'WAV', 'GIF'].includes(format) ? '此格式使用固定输出配置，不应用质量滑块。' : '数值越高质量越高；压缩后的大小取决于原始素材，不保证一定更小。'}</p>
      <dl className="mw-limits"><div><dt>执行方式</dt><dd>{nativeSnapshot ? '本机 C++ / Media Foundation + WIC' : '本机 C++ / FFmpeg'}</dd></div><div><dt>安全上限</dt><dd>30 分钟 / 输出 1 GiB</dd></div><div><dt>源文件</dt><dd>保留，不覆盖</dd></div></dl></section></div>
    <section className="mw-task-panel"><div className="mw-task-heading"><div><b>03　处理与保存</b><p>{savedName ? `已保存：${savedName}` : task.message || '选择素材与输出格式，然后开始处理。'}</p></div><div className="mw-task-actions">{busy && taskId.current ? <Button label={task.state === 'cancel_requested' ? '正在取消' : '取消任务'} outlined disabled={task.state === 'cancel_requested'} onClick={() => void cancel()} /> : <Button label={nativeSnapshot ? '生成原生 PNG 快照' : '开始处理'} icon="pi pi-play" disabled={!source || !client || !canProcess || busy} onClick={() => void start()} />}<Button label="另存为" icon="pi pi-download" outlined disabled={!outputId.current || busy} onClick={() => void save()} /></div></div>
      <ProgressBar value={task.progress} mode={busy && task.progress === 0 ? 'indeterminate' : 'determinate'} showValue={false} /><div className="mw-task-meta"><span>{busy ? task.progress ? `引擎进度 ${task.progress}%` : '原生任务执行中，等待可用进度' : task.state === 'succeeded' ? '已完成' : task.state === 'cancelled' ? '已取消' : '等待操作'}</span><span>{operationLabel ? `native ${operationLabel} · C++ worker · 关闭工作台会取消任务` : '关闭工作台会取消未完成任务；未保存结果在会话结束后清理'}</span></div>
      {error && <div className="mw-error" role="alert">{error}</div>}<p className="mw-capability">{capability}</p>
    </section>
  </main>;
}
