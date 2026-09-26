'use strict';
// One real, already-failed run only. No native process, build, or GUI is launched.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const ROOT = path.resolve(__dirname);
const EXPECTED = 'C:/SOUI-SDK-production-validation/preview11-real-renewal-runs-20260926/x86-smoke-attempt1';
const PREP = 'C:/SOUI-SDK-production-validation/preview11-real-renewal-preparation-20260926';
const DEST = 'C:/SOUI-SDK-development/preview9-tray/verification/production-validation/preview11-real-renewal-smoke-attempt1-2026-09-26';
const hash = b => crypto.createHash('sha256').update(b).digest('hex');
const assert = (test, message) => { if (!test) throw new Error(message); };
const read = relative => fs.readFileSync(path.join(ROOT, relative));
const json = relative => JSON.parse(read(relative));
const write = (relative, value) => fs.writeFileSync(path.join(ROOT, relative), JSON.stringify(value, null, 2) + '\n', { flag: 'wx' });
function files(root, prefix = '') {
  const result = [];
  for (const name of fs.readdirSync(path.join(root, prefix)).sort()) {
    const relative = prefix + name, absolute = path.join(root, relative), stat = fs.lstatSync(absolute);
    assert(!stat.isSymbolicLink(), 'Refuse links: ' + relative);
    if (stat.isDirectory()) result.push(...files(root, relative + '/'));
    else {
      assert(stat.isFile(), 'Refuse non-file: ' + relative);
      const bytes = fs.readFileSync(absolute);
      result.push({ path: relative, bytes: bytes.length, sha256: hash(bytes) });
    }
  }
  return result.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
}
function unchanged(items) {
  for (const item of items) { const bytes = read(item.path);
    assert(bytes.length === item.bytes && hash(bytes) === item.sha256, 'Original evidence changed: ' + item.path); }
}
function verify() {
  const before = files(ROOT), supervisor = json('supervisor.json');
  for (const name of ['verification.stdout.log', 'verification.stderr.log', 'verification.result.json', 'verification.summary.json'])
    assert(!fs.existsSync(path.join(ROOT, name)), 'Refuse repeat verification/overwrite: ' + name);
  const analyzer = path.join(ROOT, 'Verify-Run.cjs');
  assert(hash(fs.readFileSync(analyzer)) === supervisor.analyzerSha256, 'Frozen analyzer differs');
  const args = [analyzer, '--report', path.join(ROOT, 'native.ndjson'), '--supervisor', path.join(ROOT, 'supervisor.json'), '--output', path.join(ROOT, 'verification.summary.json')];
  const startedAt = new Date().toISOString();
  const result = spawnSync(process.execPath, args, { cwd: ROOT, windowsHide: true, timeout: 60000, maxBuffer: 16 * 1024 * 1024 });
  const finishedAt = new Date().toISOString();
  const stdout = result.stdout || Buffer.alloc(0), stderr = result.stderr || Buffer.alloc(0);
  fs.writeFileSync(path.join(ROOT, 'verification.stdout.log'), stdout, { flag: 'wx' });
  fs.writeFileSync(path.join(ROOT, 'verification.stderr.log'), stderr, { flag: 'wx' });
  let evidenceUnchanged = true, evidenceError = null;
  try { unchanged(before); } catch (error) { evidenceUnchanged = false; evidenceError = error.message; }
  const receipt = { schema: 'soui.real-renewal-failed-run-verification/v1', startedAt, finishedAt,
    command: process.execPath, arguments: args, workingDirectory: ROOT, analyzerSha256: hash(fs.readFileSync(analyzer)),
    exitCode: result.status, signal: result.signal, timedOut: result.error?.code === 'ETIMEDOUT',
    processError: result.error ? { code: result.error.code, message: result.error.message } : null,
    stdout: { path: 'verification.stdout.log', bytes: stdout.length, sha256: hash(stdout) },
    stderr: { path: 'verification.stderr.log', bytes: stderr.length, sha256: hash(stderr) },
    summaryProduced: fs.existsSync(path.join(ROOT, 'verification.summary.json')),
    originalEvidenceUnchanged: evidenceUnchanged, evidenceError, originalEvidence: before,
    realRenewalVerified: false, smokePassed: false, releaseReady: false };
  write('verification.result.json', receipt);
  process.stdout.write(JSON.stringify({ exitCode: receipt.exitCode, stderr: stderr.toString('utf8'), originalEvidenceUnchanged: evidenceUnchanged, summaryProduced: receipt.summaryProduced }) + '\n');
  assert(evidenceUnchanged, 'Original evidence changed');
  assert(Number.isInteger(result.status), 'Analyzer did not return a numeric exit code');
  assert(result.status !== 0 && !receipt.summaryProduced, 'Unexpected acceptance of known failed run');
}
function sourceEvidence(relative, ranges) {
  const absolute = path.join(PREP, relative), bytes = fs.readFileSync(absolute), lines = bytes.toString('utf8').split(/\r?\n/);
  return { path: absolute, bytes: bytes.length, sha256: hash(bytes), excerpts: ranges.map(([start, end]) => ({ start, end, text: lines.slice(start - 1, end).join('\n') })) };
}
function archive() {
  assert(!fs.existsSync(DEST), 'Refuse archive overwrite');
  const supervisor = json('supervisor.json'), receipt = json('verification.result.json');
  const rows = read('native.ndjson').toString('utf8').trimEnd().split(/\r?\n/).map(JSON.parse);
  const failure = rows.find(row => row.event === 'run.failed'), finish = rows.find(row => row.event === 'run.finished');
  assert(rows.length === 8 && failure?.seq === 5 && failure.elapsedMs === 7431 && finish?.exitCode === 1, 'Unexpected failure evidence');
  assert(supervisor.exitCode === 1 && supervisor.timedOut === false && supervisor.externalLoadDetected === false &&
    supervisor.remainingDesktopProcesses.length === 0 && supervisor.pid === 66336 && supervisor.durationMs === 30000, 'Supervisor evidence differs');
  assert(hash(read('native.ndjson')) === supervisor.reportSha256, 'Native log differs from supervisor');
  assert(receipt.exitCode === 1 && receipt.originalEvidenceUnchanged === true && receipt.summaryProduced === false, 'Verifier did not reject as expected');
  unchanged(receipt.originalEvidence);
  const diagnosis = { schema: 'soui.real-renewal-failure-diagnosis/v1', diagnosedAt: new Date().toISOString(),
    runDirectory: ROOT, reportSha256: hash(read('native.ndjson')), supervisorSha256: hash(read('supervisor.json')),
    freezeSha256: supervisor.freezeSha256, sourceInputSha256: 'a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550',
    actual: { architecture: 'x86', pid: 66336, requestedDurationMs: 30000, exitCode: 1,
      nativeFinalElapsedMs: finish.elapsedMs, failedAtElapsedMs: failure.elapsedMs, failureMessage: failure.message,
      readyCount: finish.readyCount, longInvocations: finish.longInvocations, resultInvocations: finish.resultInvocations,
      shutdownSessions: finish.shutdownSessions, describedRecords: rows.filter(row => row.event === 'probe.describe').length,
      webRecords: rows.filter(row => row.event === 'web.record').length, timedOut: false, externalLoadDetected: false,
      remainingDesktopProcesses: [], payloadUnchanged: supervisor.payloadUnchanged, inputsUnchanged: supervisor.inputsUnchanged,
      guiLaunchAttempted: true, visiblePageReadinessProven: false, x64ExecutedInThisRun: false },
    certainFindings: [
      'The native fixture failed before a successful probe.describe or probe.ready record and before either task invocation. Its one recorded shutdown callback triggers the fixture session_shutdown failure path.',
      'Fixture session_shutdown rejects every callback received before explicit closing, even while no probe authorization/session identity is bound. It does not record the received session ID or callback phase.',
      'Host start creates a webview_session lifecycle ID after mount and seeds it into the adapter when that slot is empty. Creation of a main-world main-frame context swaps that slot and forwards any nonempty ID to bridge.shutdown_session.',
      'The public json_bridge bootstrap independently creates its authorized session ID; Host records that ID after successful bootstrap. The lifecycle ID can therefore exist before the probe has an authorized document session.',
      'The archived navigation diagnostic contains one allowed local index.html URL line; it contains no timestamp, session ID, context generation, or callback source.',
      'The frozen analyzer was executed once against this actual report/supervisor and rejected it with a numeric exit code 1. Original run evidence and frozen analyzer bytes remained unchanged.'
    ],
    inference: { status: 'consistent-with-source-and-timeline-not-directly-observed',
      explanation: 'An initial main-context callback retiring the pre-seeded Host lifecycle ID can explain the failure before describe/ready. The actual received ID and caller were not logged, so this run does not conclusively identify that callback or establish an SDK renewal defect.' },
    notEstablished: ['A successful bridge bootstrap was not logged and its absence is not established merely by missing probe.describe.',
      'No same-document renewal, task/subscription continuity, 30-second smoke pass, 35-minute run, 8-hour run, or x64 run is proven.',
      'Resource samples are diagnostic only; no isolated resource, performance, production, or Win7 conclusion follows.'],
    proposedNextRevisionOnly: [
      'Preserve this frozen failure. Add token-free shutdown observations containing received session ID, whether the public bootstrap/document identity is bound, describe/ready/closing phase, and separate pre-bind versus bound-session shutdown counts.',
      'Observe and bind the single successful public bootstrap session before returning its response, so session loss between bootstrap and describe/ready remains a failure. Never rebootstrap to recover.',
      'Classify pre-bootstrap lifecycle retirement separately from shutdown of the actual bound document session. Continue forwarding every callback to inner.shutdown_session; do not suppress real cleanup.',
      'Once the authorized document session is bound, every unexpected shutdown/navigation before explicit close must remain fatal, including task/subscription setup before ready. On close require revocation of that exact bound session and full task/subscription cleanup.',
      'Add no-GUI contract coverage for lifecycle retirement before bootstrap, bound-session shutdown before describe and before ready, after-ready navigation, wrong-session/duplicate shutdown, and exactly-once final bound-session revocation before any new real run.'
    ],
    sourceEvidence: [
      sourceEvidence('attempts/x86-attempt4/consumer-source/main.cpp', [[239,247],[365,373],[418,430],[514,527]]),
      sourceEvidence('inputs/source/sdk/src/host/soui_miniblink_host.cpp', [[817,819],[881,887],[1791,1795],[1940,1956],[1963,1973],[2243,2250],[2259,2268],[2360,2367]]),
      sourceEvidence('inputs/source/sdk/src/runtime/host/session/webview_session.cpp', [[15,20],[86,90]]),
      sourceEvidence('inputs/source/sdk/src/json_bridge.cpp', [[191,203],[208,215]])
    ],
    fixtureLifecycleContractGapConfirmed: true, exactRuntimeCallbackSourceProven: false, sdkRenewalBugProven: false,
    smokePassed: false, realRenewalVerified: false, resourceGate: false, benchmarkValid: false, releaseReady: false };
  write('diagnosis.json', diagnosis);
  const originals = files(ROOT);
  fs.mkdirSync(DEST, { recursive: false });
  for (const item of originals) {
    const destination = path.join(DEST, 'raw', item.path);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    fs.copyFileSync(path.join(ROOT, item.path), destination, fs.constants.COPYFILE_EXCL);
    const copied = fs.readFileSync(destination);
    assert(copied.length === item.bytes && hash(copied) === item.sha256, 'Archive differs: ' + item.path);
  }
  unchanged(originals);
  const manifest = { schema: 'soui.production-evidence-raw-archive/v1', archivedAt: new Date().toISOString(), sourceRoot: ROOT,
    files: originals.map(item => ({ ...item, archivedPath: 'raw/' + item.path })),
    fileCount: originals.length, bytes: originals.reduce((sum, item) => sum + item.bytes, 0),
    originalBytesPreserved: true, smokePassed: false, realRenewalVerified: false, releaseReady: false };
  fs.writeFileSync(path.join(DEST, 'raw-manifest.json'), JSON.stringify(manifest, null, 2) + '\n', { flag: 'wx' });
  const readme = `# preview.11 真实续期夹具：x86 smoke attempt 1 失败原样本\n\n本次实际启动了 x86 GUI 夹具，原生 PID 66336，计划时长 30,000 ms；在原生日志 elapsed 7,431 ms 因 \`Unexpected document session shutdown or navigation\` 失败，7,944 ms 记录最终退出码 **1**。\`readyCount=0\`、两类任务调用各 0、\`shutdownSessions=1\`，没有成功的 \`probe.describe\` 或 Web record。不能认定页面已就绪，也不能称为短 smoke 或真实续期通过。**本轮 x64 未执行。**\n\n监督记录为非超时、未发现外部负载、结束时无剩余桌面进程，payload 与输入未变；这些只描述运行环境和收尾，不改变失败结论。Host destroy 与 runtime shutdown 返回成功。两个资源样本只作原始诊断，不用于资源或性能门槛。\n\n## 原始核验\n\n\`raw/\` 保存本次运行目录的全部文件原字节，包括监督器、冻结分析器、输入检查、门禁、载荷身份、NDJSON、空 stdout/stderr 和导航诊断。\`raw-manifest.json\` 逐项记录原路径、大小和 SHA-256。\`Archive-Failure.cjs\` 的 verify 模式仅调用冻结的 JavaScript 分析器，不启动原生程序、构建或 GUI；archive 模式拒绝覆盖并逐文件核对复制字节。\n\n冻结 \`Verify-Run.cjs\` 已对实际 NDJSON 与 supervisor 执行一次，原始 stdout/stderr、完整绝对参数、时间和**数值退出码 1**见 \`raw/verification.result.json\`。分析器拒绝此失败样本，未产生通过 summary；原始证据和分析器散列未变。此核验是失败判定，不是再次运行 smoke。\n\n## 诊断与尚未证实之处\n\n冻结夹具 \`session_shutdown\` 在尚未绑定 probe 授权会话时也把所有非 closing 的通知判为失败。SDK Host 的已冻结源码显示：start 会放入一个 Host 生命周期 ID，主 frame 的初次脚本 context 创建可取走该 ID 并转发到同一 shutdown 回调。这是确定的夹具与生命周期通知语义不匹配路径。\n\n实际导航诊断仅记录一条允许访问本地 index.html 的 URL，没有时间戳、收到的 session ID 或 context 来源；因此“本次正是初始生命周期 ID 退役触发”仍是与源码和时序一致的解释，**不能宣布精确运行原因已证实，更不能先归因 SDK 续期故障**。缺少 describe 也不能单独证明 bootstrap 从未成功。详细源码散列、行号、摘录及边界见 \`raw/diagnosis.json\`。\n\n建议仅在新的夹具版本中增加无 token 的回调阶段/身份观察，区分 bootstrap 前的生命周期退役与实际授权会话关闭；在唯一成功 bootstrap 时立即绑定真实会话，确保 describe/ready 之前的已绑定会话丢失仍失败。ready 后导航禁止、一次 bootstrap、任务/订阅身份连续性和最终清理要求均保持严格。此次未修源码、未重新编译、未重跑桌面。\n\n## 身份及结论\n\n- SDK 输入 SHA-256：\`a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550\`。\n- freeze SHA-256：\`7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d\`。\n- native.ndjson SHA-256：\`${supervisor.reportSha256}\`。\n- 编译归档保留其历史时点；本目录补充后续真实启动失败，不覆写此前记录。\n- \`smokePassed\`、\`realRenewalVerified\`、\`resourceGate\`、\`benchmarkValid\`、\`releaseReady\` 全部为 false；本样本不证明 35 分钟、8 小时、Win7 或生产能力。\n`;
  fs.writeFileSync(path.join(DEST, 'README.md'), readme, { flag: 'wx' });
  process.stdout.write(JSON.stringify({ archivePath: DEST, rawFileCount: originals.length, totalFileCount: originals.length + 2,
    rawBytes: manifest.bytes, rawManifestSha256: hash(fs.readFileSync(path.join(DEST, 'raw-manifest.json'))),
    diagnosisSha256: hash(read('diagnosis.json')), originalBytesPreserved: true }) + '\n');
}
assert(ROOT.toLowerCase() === path.resolve(EXPECTED).toLowerCase(), 'This collector is bound to one external run directory');
assert(process.argv.length === 3 && ['verify', 'archive'].includes(process.argv[2]), 'Usage: node Archive-Failure.cjs verify|archive');
if (process.argv[2] === 'verify') verify(); else archive();
