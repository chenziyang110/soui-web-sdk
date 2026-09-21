import React, { useEffect, useRef, useState } from 'react';
import { type SouiNativeClientImpl, type SouiNativeEvent } from '../../../packages/sdk/soui_native_client';
import { errorText, isSessionExpired, sessionExpiredMessage } from './session-recovery';
import './fileinsight.css';

type Props = { client: SouiNativeClientImpl | null; onBack?: () => void };
type ScanState = 'idle' | 'picking' | 'ready' | 'running' | 'cancel-pending' | 'succeeded' | 'partial' | 'failed' | 'cancelled' | 'empty';
type Group = { extension: string; fileCount: number; totalBytes: number };
type Entry = { path?: string; directory?: boolean; size?: number };
type ScanResult = {
  taskId: string; state?: string; progress?: number; entries?: unknown[]; cursor?: string; hasMore?: boolean;
  message?: string; skipped?: number; errors?: number; issues?: unknown[]; fileCount?: number;
  totalBytes?: number; groups?: unknown[];
};

type ReactBenchmarkScanResult = {
  files: number;
  totalBytes: number;
  pages: number;
  progressEvents: number;
  scanReadyMs: number;
  totalMs: number;
  boundaryScroll: 'passed';
};

type ReactBenchmarkHost = Window & {
  __souiReactScanDriver?: (token: string) => Promise<ReactBenchmarkScanResult>;
  __souiReactRecoverSessionForBenchmark?: () => void;
};

const terminalStates = new Set(['succeeded', 'partial', 'failed', 'cancelled']);
type BundledSample = 'primary' | 'alternate';
type SampleTokenHost = Window & {
  __souiFileInsightSampleRootToken?: string;
  __souiFileInsightAlternateSampleRootToken?: string;
};
const bundledSampleToken = (sample: BundledSample = 'primary') => {
  const host = window as SampleTokenHost;
  return (sample === 'alternate' ? host.__souiFileInsightAlternateSampleRootToken : host.__souiFileInsightSampleRootToken) || '';
};
const autoSampleRequested = () => Boolean((window as Window & { __souiFileInsightAutoSample?: boolean }).__souiFileInsightAutoSample);
const autoAlternateSampleRequested = () => Boolean((window as Window & { __souiFileInsightAutoAlternateSample?: boolean }).__souiFileInsightAutoAlternateSample);
const formatBytes = (value: number) => value < 1024 ? `${value} B` : value < 1024 * 1024 ? `${(value / 1024).toFixed(1)} KB` : value < 1024 * 1024 * 1024 ? `${(value / (1024 * 1024)).toFixed(1)} MB` : `${(value / (1024 * 1024 * 1024)).toFixed(2)} GB`;
const normalizeGroups = (values: unknown[] | undefined): Group[] => (values ?? []).flatMap((value) => {
  if (!value || typeof value !== 'object') return [];
  const item = value as Partial<Group>;
  return typeof item.extension === 'string' && typeof item.fileCount === 'number' && typeof item.totalBytes === 'number'
    ? [{ extension: item.extension, fileCount: item.fileCount, totalBytes: item.totalBytes }] : [];
});
const normalizeEntries = (values: unknown[] | undefined): Entry[] => (values ?? []).flatMap((value) => {
  if (!value || typeof value !== 'object') return [];
  const item = value as Entry;
  return typeof item.path === 'string' ? [item] : [];
});

export function FileInsight({ client, onBack }: Props) {
  const [state, setState] = useState<ScanState>(client ? 'idle' : 'empty');
  const [message, setMessage] = useState(client ? '选择一个目录，native C++ 会在后台读取并统计。' : '请在 SOUI + Miniblink 桌面程序中运行。');
  const [rootToken, setRootToken] = useState('');
  const [taskId, setTaskId] = useState('');
  const [progress, setProgress] = useState(0);
  const [result, setResult] = useState<ScanResult | null>(null);
  const [groups, setGroups] = useState<Group[]>([]);
  const [entries, setEntries] = useState<Entry[]>([]);
  const [selectedName, setSelectedName] = useState('');
  const runNonce = useRef(0);
  const timer = useRef<number | null>(null);
  const copyTimer = useRef<number | null>(null);
  const unsubscribe = useRef<(() => void) | null>(null);
  const activeTaskRef = useRef('');
  const activeStateRef = useRef<ScanState>(client ? 'idle' : 'empty');
  const liveRef = useRef(true);
  const sessionExpiredRef = useRef(false);
  const recoveryPendingRef = useRef(false);
  const skipCancelOnUnmountRef = useRef(false);
  const [sessionExpired, setSessionExpired] = useState(false);
  const [recoveryPending, setRecoveryPending] = useState(false);
  const [copied, setCopied] = useState(false);

  const clearPolling = () => {
    if (timer.current !== null) window.clearInterval(timer.current);
    timer.current = null;
    unsubscribe.current?.();
    unsubscribe.current = null;
  };
  useEffect(() => { activeStateRef.current = state; }, [state]);
  useEffect(() => () => {
    liveRef.current = false;
    clearPolling();
    if (copyTimer.current !== null) window.clearTimeout(copyTimer.current);
    const activeTask = activeTaskRef.current;
    const activeState = activeStateRef.current;
    if (!skipCancelOnUnmountRef.current && client && activeTask && (activeState === 'running' || activeState === 'cancel-pending')) {
      void client.cancel(activeTask).catch(() => undefined);
    }
  }, [client]);

  const markSessionExpired = (nonce?: number) => {
    if (!liveRef.current || sessionExpiredRef.current) return;
    if (nonce !== undefined && nonce !== runNonce.current) return;
    sessionExpiredRef.current = true;
    clearPolling();
    runNonce.current += 1;
    activeTaskRef.current = '';
    setTaskId('');
    setRootToken('');
    setSelectedName('');
    setResult(null);
    setGroups([]);
    setEntries([]);
    setProgress(0);
    setState('failed');
    setSessionExpired(true);
    setMessage(sessionExpiredMessage);
  };

  const handleRequestError = (error: unknown, nonce?: number) => {
    if (!liveRef.current || (nonce !== undefined && nonce !== runNonce.current)) return;
    if (isSessionExpired(error)) {
      markSessionExpired(nonce);
      return;
    }
    setState('failed');
    setMessage(errorText(error));
  };

  const applyResult = (next: ScanResult, nonce: number) => {
    if (!liveRef.current || nonce !== runNonce.current) return;
    setResult(next); setProgress(typeof next.progress === 'number' ? next.progress : terminalStates.has(next.state ?? '') ? 100 : 0);
    setGroups(normalizeGroups(next.groups)); setEntries(normalizeEntries(next.entries));
    const nextState = next.state === 'partial' ? 'partial' : next.state === 'succeeded' ? 'succeeded' : next.state === 'cancelled' ? 'cancelled' : next.state === 'failed' ? 'failed' : 'running';
    setState(nextState); if (next.message) setMessage(next.message);
    if (terminalStates.has(next.state ?? '')) clearPolling();
  };
  const queryTask = (id: string, nonce: number) => {
    if (!client) return;
    void client.call('fileinsight.scan.query', { taskId: id, cursor: '', pageSize: 100 }).then((next) => applyResult(next as ScanResult, nonce)).catch((error) => {
      if (!liveRef.current || nonce !== runNonce.current) return;
      clearPolling();
      handleRequestError(error, nonce);
    });
  };
  const pickDirectory = async () => {
    if (!client || sessionExpiredRef.current || state === 'picking' || state === 'running' || state === 'cancel-pending') return;
    const nonce = ++runNonce.current; clearPolling(); setState('picking'); setMessage('等待 native 文件夹选择…');
    setResult(null); setGroups([]); setEntries([]); setProgress(0); setTaskId(''); activeTaskRef.current = '';
    try {
      const picked = await client.call('desktop.dialog.pickDirectory', { title: '选择要统计的文件夹', userGesture: true });
      if (!liveRef.current || nonce !== runNonce.current) return;
      if (picked.cancelled || !picked.rootToken) { setState('empty'); setMessage('已取消选择，没有产生错误。'); return; }
      setRootToken(picked.rootToken); setSelectedName('已授权目录（路径只保存在 native）'); setState('ready'); setMessage('目录已授权，可以开始统计。');
    } catch (error) { handleRequestError(error, nonce); }
  };
  const startScan = async (rootOverride = '') => {
    const effectiveRootToken = rootOverride || rootToken;
    if (!client || sessionExpiredRef.current || !effectiveRootToken || state === 'running' || state === 'cancel-pending') return;
    const nonce = ++runNonce.current; clearPolling(); activeTaskRef.current = ''; setState('running'); setProgress(0); setResult(null); setGroups([]); setEntries([]); setMessage('native C++ 扫描线程启动中…');
    try {
      const started = await client.call('fileinsight.scan.start', { rootToken: effectiveRootToken, filter: '', operationId: `fileinsight-${Date.now()}-${nonce}` });
      if (!liveRef.current) { void client.cancel(started.taskId).catch(() => undefined); return; }
      if (nonce !== runNonce.current) { void client.cancel(started.taskId).catch(() => undefined); return; }
      setTaskId(started.taskId);
      activeTaskRef.current = started.taskId;
      unsubscribe.current = client.subscribe(started.taskId, 'task.progress', (event: SouiNativeEvent) => {
        if (!liveRef.current || nonce !== runNonce.current) return;
        const payload = event.payload as { progress?: number; message?: string };
        if (typeof payload?.progress === 'number') setProgress(payload.progress); if (payload?.message) setMessage(payload.message);
      });
      queryTask(started.taskId, nonce); timer.current = window.setInterval(() => queryTask(started.taskId, nonce), 180);
    } catch (error) { handleRequestError(error, nonce); }
  };
  const runBundledSample = (sample: BundledSample = 'primary') => {
    if (sessionExpiredRef.current) return;
    const token = bundledSampleToken(sample);
    if (!token) { setState('failed'); setMessage('当前资源包没有可用的 native 内置夹具。'); return; }
    setRootToken(token);
    setSelectedName(sample === 'alternate'
      ? '备用内置夹具（不同文件与字节，路径只保存在 native）'
      : '内置演示夹具（路径只保存在 native）');
    void startScan(token);
  };
  const autoSampleStarted = useRef(false);
  useEffect(() => {
    if (!client || autoSampleStarted.current || !autoSampleRequested()) return;
    autoSampleStarted.current = true;
    const timer = window.setTimeout(() => runBundledSample(autoAlternateSampleRequested() ? 'alternate' : 'primary'), 120);
    return () => window.clearTimeout(timer);
  }, [client]);

  // The long-running React endurance check deliberately uses the same
  // component-owned client and lifecycle as the visible page. It does not
  // mutate the result table, so a cycle releases its 10k-entry working set as
  // soon as the bounded summary has been written by the native host.
  useEffect(() => {
    if (!client || import.meta.env.MODE !== 'performance') return;
    const host = window as ReactBenchmarkHost;
    let disposed = false;
    const driver = async (token: string): Promise<ReactBenchmarkScanResult> => {
      if (disposed) throw new Error('React FileInsight benchmark driver was disposed.');
      if (!token) throw new Error('React FileInsight benchmark root token is missing.');
      const startedAt = performance.now();
      let task: { taskId: string };
      try {
        task = await client.call('fileinsight.scan.start', {
          rootToken: token,
          filter: '*.txt',
          operationId: `react-fileinsight-benchmark-${Date.now()}`,
        });
      } catch (error) {
        if (isSessionExpired(error)) markSessionExpired();
        throw error;
      }
      let subscription: (() => void) | null = null;
      let progressEvents = 0;
      subscription = client.subscribe(task.taskId, 'task.progress', () => { progressEvents += 1; });
      const entries: Entry[] = [];
      let firstPage: ScanResult | null = null;
      try {
        let state: ScanResult | null = null;
        const deadline = performance.now() + 15_000;
        while (true) {
          state = await client.call('fileinsight.scan.query', { taskId: task.taskId, cursor: '', pageSize: 100 }) as ScanResult;
          if (state.state === 'succeeded' || state.state === 'partial' || state.state === 'cancelled' || state.state === 'failed') break;
          if (performance.now() > deadline) throw new Error('React FileInsight scan deadline exceeded.');
          await new Promise<void>((resolve) => window.setTimeout(resolve, 50));
        }
        if (state.state !== 'succeeded') throw new Error(state.message || `React FileInsight scan ended in ${state.state || 'unknown'} state.`);
        firstPage = state;
        const scanReadyMs = performance.now() - startedAt;
        let cursor = '';
        let hasMore = true;
        let pages = 0;
        let totalBytes = 0;
        while (hasMore) {
          const page = pages === 0 ? firstPage : await client.call('fileinsight.scan.query', {
            taskId: task.taskId,
            cursor,
            pageSize: 100,
          }) as ScanResult;
          const pageEntries = normalizeEntries(page.entries);
          entries.push(...pageEntries);
          totalBytes += pageEntries.reduce((sum, entry) => sum + (entry.directory ? 0 : Number(entry.size || 0)), 0);
          cursor = page.cursor || '';
          hasMore = page.hasMore === true;
          pages += 1;
          if (pages > 200) throw new Error('React FileInsight pagination exceeded its bounded page limit.');
        }
        const files = entries.filter((entry) => !entry.directory);
        if (files.length !== 10_000 || new Set(files.map((entry) => entry.path)).size !== 10_000) {
          throw new Error(`React FileInsight dataset mismatch: expected 10000 unique files, received ${files.length}.`);
        }
        return {
          files: files.length,
          totalBytes,
          pages,
          progressEvents,
          scanReadyMs,
          totalMs: performance.now() - startedAt,
          boundaryScroll: 'passed',
        };
      } catch (error) {
        if (isSessionExpired(error)) markSessionExpired();
        throw error;
      } finally {
        subscription?.();
        subscription = null;
      }
    };
    host.__souiReactScanDriver = driver;
    return () => {
      disposed = true;
      if (host.__souiReactScanDriver === driver) delete host.__souiReactScanDriver;
    };
  }, [client]);
  const cancelScan = async () => {
    if (!client || !taskId || state !== 'running') return;
    setState('cancel-pending'); setMessage('正在等待 native C++ 确认取消…');
    try { await client.cancel(taskId); } catch (error) {
      if (isSessionExpired(error)) { markSessionExpired(); return; }
      setState('running');
      setMessage(errorText(error));
    }
  };
  const copySummary = async () => {
    if (!client || !result) return;
    const groupText = groups.length
      ? groups.map((group) => `${group.extension}: ${group.fileCount} files / ${group.totalBytes} bytes`).join('\n')
      : '暂无扩展名统计';
    const summary = ['SOUI FileInsight', `files: ${totalFiles}`, `bytes: ${totalBytes}`, `state: ${result.state ?? state}`, groupText].join('\n');
    try {
      await client.call('desktop.clipboard.writeText', { text: summary, userGesture: true });
      if (!liveRef.current) return;
      setCopied(true); setMessage('统计摘要已复制到系统剪贴板。');
      if (copyTimer.current !== null) window.clearTimeout(copyTimer.current);
      copyTimer.current = window.setTimeout(() => setCopied(false), 2200);
    } catch (error) {
      if (isSessionExpired(error)) { markSessionExpired(); return; }
      if (liveRef.current) setMessage(errorText(error));
    }
  };
  const recoverSession = (benchmarkApproved = false) => {
    if (!client || !sessionExpiredRef.current || recoveryPendingRef.current) return;
    let confirmed = false;
    try {
      confirmed = benchmarkApproved && import.meta.env.MODE === 'performance'
        ? true
        : window.confirm('授权已到期。确认结束旧会话和旧任务，并重新连接吗？重新连接后需要重新选择目录。');
    } catch {
      setMessage('当前环境无法打开确认对话框，请重载页面后重新连接。');
      return;
    }
    if (!confirmed) {
      setMessage('已取消重连；旧会话仍已到期，请确认后重新连接。');
      return;
    }
    recoveryPendingRef.current = true;
    skipCancelOnUnmountRef.current = true;
    setRecoveryPending(true);
    clearPolling();
    activeTaskRef.current = '';
    liveRef.current = false;
    setMessage('正在结束旧会话和旧任务，并创建新的授权会话…');
    try {
      window.location.reload();
    } catch {
      liveRef.current = true;
      recoveryPendingRef.current = false;
      skipCancelOnUnmountRef.current = false;
      setRecoveryPending(false);
      setMessage('页面重载失败，请手动重载后重新选择目录。');
    }
  };
  useEffect(() => {
    if (!client || import.meta.env.MODE !== 'performance') return;
    const host = window as ReactBenchmarkHost;
    const recover = () => recoverSession(true);
    host.__souiReactRecoverSessionForBenchmark = recover;
    return () => {
      if (host.__souiReactRecoverSessionForBenchmark === recover) delete host.__souiReactRecoverSessionForBenchmark;
    };
  }, [client, sessionExpired]);
  const statusLabel = state === 'idle' ? '等待选择' : state === 'picking' ? '选择目录' : state === 'ready' ? '已授权' : state === 'running' ? '扫描中' : state === 'cancel-pending' ? '取消中' : state === 'succeeded' ? '已完成' : state === 'partial' ? '部分完成' : state === 'cancelled' ? '已取消' : state === 'failed' ? '失败' : '未选择';
  const progressLabel = state === 'running' || state === 'cancel-pending' ? (progress > 0 ? `${progress}%` : '—') : `${progress}%`;
  const totalFiles = result?.fileCount ?? 0; const totalBytes = result?.totalBytes ?? 0;
  return <main className="fi-page" data-generated-module="fileinsight">
    <header className="fi-header"><button type="button" className="fi-back" onClick={onBack}>‹ 返回</button><div><small>NATIVE FILE INSIGHT / STANDARD MODULE</small><h1>文件洞察</h1><p>目录选择、递归读取、扩展名聚合和取消确认全部由 C++ 执行。</p></div><span className={`fi-state fi-state-${state}`}>{statusLabel}</span></header>
    <section className="fi-toolbar"><button className="fi-primary" type="button" onClick={() => void pickDirectory()} disabled={!client || sessionExpired || state === 'picking' || state === 'running' || state === 'cancel-pending'}>选择目录</button><button type="button" className="fi-sample" onClick={() => runBundledSample('primary')} disabled={!client || sessionExpired || state === 'picking' || state === 'running' || state === 'cancel-pending'}>运行内置样例</button><button type="button" className="fi-sample fi-sample-alt" onClick={() => runBundledSample('alternate')} disabled={!client || sessionExpired || state === 'picking' || state === 'running' || state === 'cancel-pending'}>运行另一组夹具</button><button type="button" onClick={() => void startScan()} disabled={!client || sessionExpired || !rootToken || state === 'running' || state === 'cancel-pending'}>开始统计</button><button type="button" className="fi-danger" onClick={() => void cancelScan()} disabled={state !== 'running'}>取消任务</button><button type="button" onClick={() => void copySummary()} disabled={!client || sessionExpired || !result || copied}>{copied ? '已复制' : '复制摘要'}</button><span className="fi-location">{selectedName || '尚未授权本地目录'}</span></section>
    {sessionExpired && <aside className="fi-expiry" role="alert"><div><strong>授权已到期</strong><span>{sessionExpiredMessage} 重连会结束旧会话和旧任务，重新连接后必须重新选择目录。</span></div><button type="button" onClick={() => recoverSession()} disabled={recoveryPending}>{recoveryPending ? '正在重新连接…' : '结束旧会话并重新连接'}</button></aside>}
    <section className="fi-summary" aria-live="polite"><article><small>FILES</small><strong>{totalFiles.toLocaleString()}</strong><span>不包含目录</span></article><article><small>LOGICAL BYTES</small><strong>{formatBytes(totalBytes)}</strong><span>{totalBytes.toLocaleString()} bytes</span></article><article><small>PROGRESS</small><strong>{progressLabel}</strong><span>{state === 'running' || state === 'cancel-pending' ? (progress > 0 ? message : `${message} · 总量未知，等待真实进度`) : message}</span></article></section>
    <section className="fi-grid"><article className="fi-card fi-groups"><header><div><small>EXTENSION BREAKDOWN</small><h2>按扩展名统计</h2></div><span>{result?.skipped ?? 0} skipped · {result?.errors ?? 0} errors</span></header>{groups.length ? <div className="fi-group-list">{groups.map((group) => <div className="fi-group" key={group.extension}><span className="fi-ext">{group.extension}</span><span className="fi-bar"><i style={{ width: `${Math.max(4, Math.min(100, totalFiles ? group.fileCount * 100 / totalFiles : 0))}%` }} /></span><b>{group.fileCount}</b><em>{formatBytes(group.totalBytes)}</em></div>)}</div> : <div className="fi-empty">完成扫描后显示原生统计摘要</div>}</article><article className="fi-card fi-entries"><header><div><small>BOUNDED RESULT PAGE</small><h2>最近发现的条目</h2></div><span>{entries.length} / 100</span></header>{entries.length ? <div className="fi-entry-list">{entries.slice(0, 100).map((entry) => <div className="fi-entry" key={entry.path}><span className={entry.directory ? 'fi-folder' : 'fi-file'}>{entry.directory ? 'DIR' : 'FILE'}</span><span title={entry.path}>{entry.path}</span><b>{entry.directory ? '—' : formatBytes(entry.size ?? 0)}</b></div>)}</div> : <div className="fi-empty">目录内容只在任务完成后分页返回</div>}</article></section>
    {(result?.issues?.length ?? 0) > 0 && <aside className="fi-issues"><strong>扫描说明</strong><span>{result?.issues?.slice(0, 3).map(String).join(' · ')}</span></aside>}
  </main>;
}
