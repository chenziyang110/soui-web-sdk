'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const name = 'guest-layout-explore-v2-attempt1';
const attempt = path.join(root, name);
const frozen = path.join(root, 'layout-explore-v2-attempt1');
const priorFrozen = path.join(root, 'layout-explore-v1-attempt1');
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const relative = 'verification/production-validation/win7-sp1-x86-v02-layout-wheel-before-after-2026-09-28';
const target = path.join(repo, relative);
const stage = path.join(root, 'layout-wheel-v2-archive-stage-attempt1');
const commandRoot = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
function ensureIdle() {
  const script = "$ErrorActionPreference='Stop';$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$'}|Select-Object ProcessId,Name);[ordered]@{busy=$busy}|ConvertTo-Json -Compress -Depth 3";
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
    {encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const value = JSON.parse(result.stdout.replace(/^\uFEFF/, ''));
  const busy = Array.isArray(value.busy) ? value.busy : value.busy ? [value.busy] : [];
  if (busy.length) throw Error(`Other desktop workload active: ${JSON.stringify(busy)}`);
}
ensureIdle();
assert.equal(fs.existsSync(target), false, 'Refuse to replace committed archive');
assert.equal(fs.existsSync(stage), false, 'Refuse to replace archive stage');
assert.equal(sha(read(path.join(priorFrozen, 'freeze.json'))),
  'c0c5b6c54198c25397756987f39bd396d934436e33e159054c93abcb60477a20');
const input = json(path.join(frozen, 'input.json'));
const freezeBytes = read(path.join(frozen, 'freeze.json'));
const review = json(path.join(attempt, 'layout-review-v2.json'));
const visual = json(path.join(attempt, 'visual-review-v2.json'));
const timeline = json(path.join(attempt, 'timeline-result.json'));
assert.equal(sha(freezeBytes), '3380cde0efbddb9bc18a0eb148dd074086e821ef795487df6da928719fb344ae');
assert.equal(input.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(input.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(review.freezeSha256, sha(freezeBytes));
assert.equal(review.applicationNativeExitCode, 0);
assert.equal(review.serialFrames, 61);
assert.equal(review.nativeWheelInputsSent, 12);
assert.equal(review.naturalClose, true);
assert.equal(review.completePayloadsAndLegalPrePost, true);
assert.equal(review.win7CurrentV02Verified, false);
assert.equal(visual.visualPositionChanged, true);
assert.equal(visual.crossClockLatencyOrExactInputFrameAttributionClaimed, false);
assert.equal(visual.win7CurrentV02Verified, false);
assert.equal(timeline.attempt, name);
assert.equal(timeline.qemuPid, review.qemuPid);
assert.equal(timeline.stopReason, 'serial-ended');
assert.equal(timeline.frames.length, 31);
for (const frame of timeline.frames) {
  assert.equal(sha(read(path.join(attempt, `${frame.label}.ppm`))), frame.rawSha256);
  assert.equal(sha(read(path.join(attempt, `${frame.label}.png`))), frame.pngSha256);
}
for (const key of ['preWheel', 'nearWheelStart', 'afterWheel']) {
  const item = visual[key];
  assert.equal(sha(read(path.join(attempt, item.file))), item.sha256);
  assert(timeline.frames.some(frame => `${frame.label}.png` === item.file && frame.pngSha256 === item.sha256));
}
const commands = ['prepare-layout-explore-v2-attempt1', 'freeze-layout-explore-v2-attempt1',
  'verify-layout-explore-v2-attempt1', `${name}-driver`, `${name}-send`,
  `${name}-timeline`, `${name}-quit`, `${name}-review`];
for (const command of commands) {
  const result = json(path.join(commandRoot, command, 'result.json'));
  assert.equal(result.exitCode, 0, command);
  assert.equal(result.timedOut, false, command);
  assert.equal(result.error, null, command);
}
const transfer = path.join(frozen, 'volume/soui-input');
const priorTransfer = path.join(priorFrozen, 'volume/soui-input');
const old = new Map(json(path.join(priorFrozen, 'input.json')).transferFiles.map(file => [file.path, file]));
const changed = new Set(['run-display-explore.ps1']);
const unarchivedPayloads = [];
for (const file of input.transferFiles) {
  assert.equal(sha(read(path.join(transfer, file.path))), file.sha256);
  assert.equal(read(path.join(transfer, file.path)).length, file.bytes);
  const previous = old.get(file.path);
  assert(previous, `Missing previous input ${file.path}`);
  if (changed.has(file.path)) assert.notEqual(file.sha256, previous.sha256);
  else assert.equal(file.sha256, previous.sha256);
  if (file.path === 'SouiStarter.zip' || file.path === 'SouiWorkbench.zip')
    unarchivedPayloads.push({path: file.path, bytes: file.bytes, sha256: file.sha256});
}
assert.equal(unarchivedPayloads.length, 2);
assert.equal(sha(read(path.join(transfer, 'transfer-files.tsv'))), input.transferManifestSha256);

const pairs = [];
function visit(base, destination) {
  for (const entry of fs.readdirSync(base, {withFileTypes: true})) {
    const source = path.join(base, entry.name);
    const relativePath = path.join(destination, entry.name);
    if (entry.isDirectory()) visit(source, relativePath);
    else {assert.equal(entry.isFile(), true); pairs.push([source, relativePath]);}
  }
}
visit(attempt, 'guest-attempt');
for (const command of commands)
  for (const file of ['result.json', 'stdout.log', 'stderr.log'])
    pairs.push([path.join(commandRoot, command, file), path.join('command-logs', command, file)]);
for (const file of ['input.json', 'freeze.json'])
  pairs.push([path.join(frozen, file), path.join('frozen-delta', file)]);
for (const file of ['run-display-explore.ps1', 'export-serial.ps1', 'transfer-files.tsv'])
  pairs.push([path.join(transfer, file), path.join('frozen-delta/volume/soui-input', file)]);
for (const file of ['Prepare-LayoutExploreV2.cjs', 'Freeze-LayoutExploreV2.cjs',
  'Verify-LayoutExploreV2.cjs', 'Run-LayoutExploreV2.ps1', 'Launch-LayoutExploreV2.ps1',
  'Monitor-DisplayExplore.cjs', 'Capture-LayoutTimelineV2.cjs',
  'Review-LayoutExploreV2.cjs', 'Archive-LayoutExploreV2.cjs'])
  pairs.push([path.join(root, file), path.join('controls', file)]);
for (const file of ['run-layout-explore.ps1', 'export-serial.ps1'])
  pairs.push([path.join(root, 'layout-explore-v2-source', file), path.join('source', file)]);
fs.mkdirSync(stage);
const files = [];
for (const [source, destinationRelative] of pairs) {
  if (files.length % 10 === 0) ensureIdle();
  const destination = path.join(stage, 'raw', destinationRelative);
  fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  const bytes = read(source);
  assert.equal(sha(read(destination)), sha(bytes));
  files.push({source, path: path.join('raw', destinationRelative).replaceAll('\\', '/'),
    bytes: bytes.length, sha256: sha(bytes), sourceMtimeUtc: fs.statSync(source).mtime.toISOString()});
}
files.sort((a, b) => a.path.localeCompare(b.path));
const manifest = {schema: 'soui.win7-v02-layout-wheel-raw/v2', fileCount: files.length,
  files, priorFrozenArchive: 'verification/production-validation/win7-sp1-x86-v02-layout-wheel-2026-09-28',
  unarchivedPayloads, completeFrozenInputReconstructible: false,
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(freezeBytes), serialSha256: review.serialSha256,
  nativeWheelInputsSent: 12, visualPositionChanged: true,
  crossClockLatencyClaimed: false, imeCompositionVerified: false,
  resizeVerified: false, taskParallelVerified: false,
  win7CurrentV02Verified: false, releaseReady: false};
fs.writeFileSync(path.join(stage, 'raw-manifest.json'), JSON.stringify(manifest, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(stage, 'review.json'), JSON.stringify({
  schema: 'soui.win7-v02-layout-wheel-review/v2', sourceSha256: input.sourceSha256,
  buildId: input.buildId, freezeSha256: sha(freezeBytes),
  serialSha256: review.serialSha256, serialFrames: review.serialFrames,
  guestIdentity: review.guestIdentity, gateSamples: review.gateSamples,
  applicationNativeExitCode: 0, naturalClose: true,
  completePayloadsAndLegalPrePost: true, nativeWheelInputsSent: 12,
  visualPositionChanged: true, crossClockLatencyClaimed: false,
  imeCompositionVerified: false, resizeVerified: false,
  taskParallelVerified: false, win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(stage, 'README.md'), [
  '# 当前 V02 Win7 SP1 x86 滚轮前后截图', '',
  `本目录保留 ${files.length} 份原始记录。13 文件只读输入 SHA-256 ${sha(freezeBytes)} 绑定当前 718 文件 Demo 源码 ${input.sourceSha256} 与正式 x86 Release 构建 ${input.buildId}。Win7 6.1.7601 SP1 x86 客体在临时 1024×768 桌面显示普通 React Workbench 数据页。`,
  `PID ${review.applicationPid}、HWND ${review.applicationHwnd} 与进程创建身份固定；自然前台 12 次真实系统滚轮、61 帧完整串口、原生退出 0、自然关窗、载荷及法律声明前后、宿主 ${review.gateSamples} 次无外部负载采样均通过。`,
  '人工查看原始截图 timeline-016/017/018：列表顶部变为下方的中文输入状态、动效和公共任务区。截图散列、原始 PPM/PNG 和时间线保留；客体与宿主时钟有偏差，不据此计算输入到画面的精确延迟或逐帧归因。',
  '本次只关闭可见页面滚动探索子项。中文 IME 组合、窗口缩放和公共任务并行未执行；`win7CurrentV02Verified=false`、`releaseReady=false`。',
  '两份应用 ZIP 原字节仍在外部冻结输入，本仓库仅保存大小、散列与客体前后载荷核对；不能从本目录独立重建完整冻结输入。', ''
].join('\n'), {flag: 'wx'});
ensureIdle();
fs.renameSync(stage, target);
console.log(JSON.stringify({target, rawFiles: files.length, serialFrames: review.serialFrames,
  nativeWheelInputsSent: 12, visualPositionChanged: true, releaseReady: false}));
