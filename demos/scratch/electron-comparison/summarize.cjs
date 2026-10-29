const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');

// Collection validity is separate from performance qualification. Keep a real
// completed stall/zero-frame run, even when its boundary percentiles are unknown.
async function main() {
  const directory = path.resolve(process.argv[2] ?? '.');
  const session = JSON.parse(await fs.readFile(path.join(directory,'session.json'),'utf8'));
  if (session.pass !== true || !session.completedUtc || session.runs.length !== 3 ||
      session.failures.length || session.blockedRequests.length) throw new Error('Three completed clean runs are required.');
  const baselineBytes = await fs.readFile(path.join(directory,'webview2-user-baseline.json'));
  if (crypto.createHash('sha256').update(baselineBytes).digest('hex') !== session.baselineSha256)
    throw new Error('Baseline evidence changed.');
  const baseline = JSON.parse(baselineBytes);
  const reports = [];
  for (let index = 1; index <= 3; ++index) {
    const report = JSON.parse(await fs.readFile(path.join(directory,`electron-run-${index}.json`),'utf8'));
    if (report.measurementVersion !== 2 || !report.validForWindowRateComparison ||
        !Number.isFinite(report.windowRafFps) || report.status !== 'completed' || report.stopReason !== 'duration-complete' ||
        report.durationMs !== baseline.durationMs ||
        report.sceneId !== baseline.sceneId || report.level !== baseline.level ||
        report.metadata.workloadSha256 !== baseline.metadata.workloadSha256 ||
        report.metadata.runtimeStepMs !== baseline.metadata.runtimeStepMs ||
        JSON.stringify(report.viewport) !== JSON.stringify(baseline.viewport) ||
        report.targets !== baseline.targets || report.threads !== baseline.threads)
      throw new Error(`Run ${index} is not comparable.`);
    reports.push(report);
  }
  const median = values => [...values].sort((a,b) => a-b)[Math.floor(values.length/2)];
  const round = value => Math.round(value*1000)/1000;
  const keys = ['windowRafFps','rafFps','frameP50Ms','frameP95Ms','frameP99Ms','maxFrameMs','longFrames','heapBytes','heapPeakBytes'];
  const metrics = Object.fromEntries(keys.map(key => {
    const values = reports.map(report => report[key]);
    const complete = values.every(Number.isFinite), previous = Number.isFinite(baseline[key]) ? baseline[key] : null;
    const middle = complete ? median(values) : null;
    return [key,{baseline:previous,electronRuns:values.map(value=>Number.isFinite(value)?value:null),electronMedian:middle,
      electronMin:complete?Math.min(...values):null,electronMax:complete?Math.max(...values):null,
      difference:middle!==null&&previous!==null?round(middle-previous):null,
      percentDifference:middle!==null&&previous?round((middle/previous-1)*100):null,
      ...(!complete||previous===null?{qualification:'unavailable; do not substitute observed cadence or discard stalled runs'}:{})}];
  }));
  const comparison = {schema:'soui.scratch-electron-comparison/v2',createdUtc:new Date().toISOString(),performancePass:null,
    primaryRate:'windowRafFps',historicalCadenceOnly:'rafFps',
    windowRateComparable:baseline.measurementVersion===2&&baseline.validForWindowRateComparison===true&&Number.isFinite(baseline.windowRafFps),
    percentilesQualified:baseline.measurementVersion===2&&baseline.validForComparison===true&&reports.every(report=>report.validForComparison),
    boundaryWindows:reports.map(report=>({rafCallbacks:report.rafCallbacks,leadingGapMs:report.leadingGapMs,
      trailingGapMs:report.trailingGapMs,boundaryBudgetMs:report.boundaryBudgetMs,censored:report.censored})),
    baselineSamples:1,electronSamples:3,baselineRuntime:baseline.metadata,
    electronEnvironment:session.environment,workloadSha256:baseline.metadata.workloadSha256,
    viewport:baseline.viewport,metrics,
    limitations:[
      'Historical single WebView2 sample versus three consecutive Electron samples; baseline warmup, interactions, visibility coverage, GPU and machine load were not recorded.',
      'Electron uses Chromium '+session.environment.chromium+'; WebView2 runtime '+baseline.metadata.runtimeVersion+' is a different browser build, despite matching major version.',
      'Per-metric medians are computed across runs; they are not pooled frame percentiles or necessarily one representative run.',
      'windowRafFps divides observed callbacks by the full actual wall window, including stalls and zero-frame windows; it is not physical display FPS.',
      'Legacy rafFps is observed-interval cadence and excludes unclosed boundaries. A historical baseline without windowRafFps cannot qualify the new rate comparison and is not rewritten.',
      'Censored boundaries leave percentile metrics unavailable; these runs stay in the results rather than being discarded/retried. JS heap excludes native and GPU memory.',
      'Comparison preload supplies only startup responses; native SDK Bridge overhead is not measured.'
    ]};
  await fs.writeFile(path.join(directory,'comparison.json'),JSON.stringify(comparison,null,2));
  const names = {windowRafFps:'完整时窗 rAF 回调率 (次/秒)',rafFps:'旧口径闭合间隔节奏（仅参考）',frameP50Ms:'P50 帧间隔 (ms)',frameP95Ms:'P95 帧间隔 (ms)',
    frameP99Ms:'P99 帧间隔 (ms)',maxFrameMs:'最大帧间隔 (ms)',longFrames:'长帧 >50 ms (次)',
    heapBytes:'结束 JS 堆 (MiB)',heapPeakBytes:'采样观察 JS 堆峰值 (MiB)'};
  const format = (key,value) => Number.isFinite(value)?String(round(key.startsWith('heap') ? value/1048576 : value)):'未取得';
  const rows = keys.map(key => {
    const item = metrics[key];
    return `| ${names[key]} | ${format(key,item.baseline)} | ${format(key,item.electronMedian)} | ${item.percentDifference===null?'未能比较':`${item.percentDifference > 0 ? '+' : ''}${item.percentDifference}%`} |`;
  });
  const markdown = `# Scratch Electron 对照采集\n\n本地资源身份以 session.json 为准。负载、尺寸、DPR 及时间窗按基线配置；每轮先预热 10 秒。历史 WebView2 报告保留原字节，不改写旧口径。\n\n| 指标 | SOUI / WebView2 报告 | Electron 三轮中位数 | 相对变化 |\n|---|---:|---:|---:|\n${rows.join('\n')}\n\nElectron 各轮完整时窗回调率：${metrics.windowRafFps.electronRuns.join('、')}。首尾未闭合停顿和零帧轮次均保留。分位数资格：${comparison.percentilesQualified?'具备声明边界覆盖':'未取得'}。\n\n新帧率口径可比：${comparison.windowRateComparable?'是':'否，历史基线缺少相同完整时窗口径'}。performancePass 为 null，本汇总不宣称性能通过。旧 rafFps 只是闭合间隔节奏，不能代替完整时窗回调率。\n\n限制：历史样本的预热、交互、GPU、系统负载和首尾缺帧边界未知；双方浏览器构建及资源可能不同，不能据此归因为原生层开销。rAF 不等于物理呈现，JS 堆不代表完整进程内存。原始来源、散列、GPU 和窗口事件见 session.json。\n`;
  await fs.writeFile(path.join(directory,'comparison.md'),markdown);
  console.log(JSON.stringify({collectionPass:true,performancePass:null,output:directory,metrics}));
}
main().catch(error => {console.error(error);process.exitCode=1;});
