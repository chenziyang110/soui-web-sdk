const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');

// Summarize only completed evidence. Percentiles are medians of per-run values,
// not pooled frame percentiles; the historical baseline has one sample.
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
    if (!report.validForComparison || report.status !== 'completed' || report.stopReason !== 'duration-complete' ||
        report.durationMs !== baseline.durationMs || report.sampleCoverage !== 'complete' ||
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
  const keys = ['rafFps','frameP50Ms','frameP95Ms','frameP99Ms','maxFrameMs','longFrames','heapBytes','heapPeakBytes'];
  const metrics = Object.fromEntries(keys.map(key => {
    const values = reports.map(report => report[key]);
    if (values.some(value => !Number.isFinite(value)) || !Number.isFinite(baseline[key]))
      throw new Error(`Metric ${key} is unavailable.`);
    const middle = median(values);
    return [key,{baseline:baseline[key],electronRuns:values,electronMedian:middle,
      electronMin:Math.min(...values),electronMax:Math.max(...values),
      difference:round(middle-baseline[key]),percentDifference:baseline[key] ? round((middle/baseline[key]-1)*100) : null}];
  }));
  const comparison = {schema:'soui.scratch-electron-comparison/v1',createdUtc:new Date().toISOString(),
    baselineSamples:1,electronSamples:3,baselineRuntime:baseline.metadata,
    electronEnvironment:session.environment,workloadSha256:baseline.metadata.workloadSha256,
    viewport:baseline.viewport,metrics,
    limitations:[
      'Historical single WebView2 sample versus three consecutive Electron samples; baseline warmup, interactions, visibility coverage, GPU and machine load were not recorded.',
      'Electron uses Chromium '+session.environment.chromium+'; WebView2 runtime '+baseline.metadata.runtimeVersion+' is a different browser build, despite matching major version.',
      'Per-metric medians are computed across runs; they are not pooled frame percentiles or necessarily one representative run.',
      'rAF is callback cadence, not physical display FPS or input-to-photon latency. JS heap excludes native and GPU memory.',
      'Comparison preload supplies only startup responses; native SDK Bridge overhead is not measured.'
    ]};
  await fs.writeFile(path.join(directory,'comparison.json'),JSON.stringify(comparison,null,2));
  const names = {rafFps:'rAF FPS',frameP50Ms:'P50 帧间隔 (ms)',frameP95Ms:'P95 帧间隔 (ms)',
    frameP99Ms:'P99 帧间隔 (ms)',maxFrameMs:'最大帧间隔 (ms)',longFrames:'长帧 >50 ms (次)',
    heapBytes:'结束 JS 堆 (MiB)',heapPeakBytes:'采样观察 JS 堆峰值 (MiB)'};
  const format = (key,value) => String(round(key.startsWith('heap') ? value/1048576 : value));
  const rows = keys.map(key => {
    const item = metrics[key];
    return `| ${names[key]} | ${format(key,item.baseline)} | ${format(key,item.electronMedian)} | ${item.percentDifference > 0 ? '+' : ''}${item.percentDifference}% |`;
  });
  const markdown = `# Scratch Electron 对照实测\n\n相同 r4 本地资源、Scratch 15.3.0、240 角色/240 脚本、种子 20261029、60 Hz 步进、1424×861 / DPR 1、每轮 30 秒。\n\n用户 WebView2 历史报告 1 轮；Electron ${session.environment.electron} / Chromium ${session.environment.chromium} 可见窗口 3 轮，每轮预热 10 秒。项目只在开始载入一次，三轮连续执行。\n\n| 指标 | SOUI / WebView2 用户报告 | Electron 三轮中位数 | 相对变化 |\n|---|---:|---:|---:|\n${rows.join('\n')}\n\nElectron 各轮 FPS：${metrics.rafFps.electronRuns.join('、')}。P95：${metrics.frameP95Ms.electronRuns.join('、')} ms。\n\n限制：历史样本的预热时间、交互、GPU 和系统负载未记录；双方浏览器构建不同。这里比较的是该场景的页面回调节奏，不是物理输入延迟；差值不能直接归因为 SOUI 原生层开销。JS 堆不代表完整进程内存。完整来源、散列、GPU、窗口事件及错误记录见 session.json；原始数据见 electron-run-1/2/3.json 和 webview2-user-baseline.json。\n`;
  await fs.writeFile(path.join(directory,'comparison.md'),markdown);
  console.log(JSON.stringify({pass:true,output:directory,metrics}));
}
main().catch(error => {console.error(error);process.exitCode=1;});
