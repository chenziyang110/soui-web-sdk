'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const attemptName = 'guest-layout-explore-v1-attempt1';
const attempt = path.join(root, attemptName);
const frozen = path.join(root, 'layout-explore-v1-attempt1');
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const prior = path.join(repo,
  'verification/production-validation/win7-sp1-x86-v02-display-v2-interrupted-2026-09-28/raw/frozen-input');
const relative = 'verification/production-validation/win7-sp1-x86-v02-layout-wheel-2026-09-28';
const target = path.join(repo, relative);
const stage = path.join(root, 'layout-wheel-archive-stage-attempt1');
const commandRoot = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
assert.equal(fs.existsSync(target), false, 'Refuse overwrite layout archive');
assert.equal(fs.existsSync(stage), false, 'Refuse overwrite partial layout archive');
function ensureIdle() {
  const script = "$ErrorActionPreference='Stop';$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$'}|Select-Object ProcessId,Name);[ordered]@{busy=$busy}|ConvertTo-Json -Compress -Depth 3";
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
    {encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const parsed = JSON.parse(result.stdout.replace(/^\uFEFF/, ''));
  const busy = Array.isArray(parsed.busy) ? parsed.busy : parsed.busy ? [parsed.busy] : [];
  if (busy.length) throw Error(`Other desktop workload active: ${JSON.stringify(busy)}`);
}
ensureIdle();
const review = readJson(path.join(attempt, 'layout-review-v1.json'));
const visual = readJson(path.join(attempt, 'visual-review.json'));
const timeline = readJson(path.join(attempt, 'timeline-result.json'));
const input = readJson(path.join(frozen, 'input.json'));
const priorInput = readJson(path.join(prior, 'input.json'));
const freezeBytes = fs.readFileSync(path.join(frozen, 'freeze.json'));
assert.equal(review.schema, 'soui.win7-v02-current-layout-explore-review/v1');
assert.equal(review.sourceSha256, input.sourceSha256);
assert.equal(review.buildId, input.buildId);
assert.equal(review.freezeSha256, sha(freezeBytes));
assert.equal(review.applicationNativeExitCode, 0);
assert.equal(review.nativeWheelInputsSent, 8);
assert.equal(review.completePayloadsAndLegalPrePost, true);
assert.equal(review.naturalClose, true);
assert.equal(review.visualScrollEffectVerified, false);
assert.equal(review.win7CurrentV02Verified, false);
assert.equal(input.serialFrameLimit, 64);
assert.equal(review.serialFrames, 50);
assert.equal(visual.evidenceType, 'assistant-visual-review-of-frozen-qemu-screenshots');
assert.equal(visual.nativeWheelInputsSent, 8);
assert.equal(visual.visualScrollEffectVerified, false);
assert.equal(visual.win7CurrentV02Verified, false);
for (const frame of visual.frames)
  assert.equal(sha(fs.readFileSync(path.join(attempt, frame.file))), frame.sha256);
assert.equal(timeline.attempt, attemptName);
assert.equal(timeline.qemuPid, review.qemuPid);
assert.equal(timeline.stopReason, 'serial-ended');
assert.equal(timeline.frames.length, 29);
for (const frame of timeline.frames) {
  assert.equal(sha(fs.readFileSync(path.join(attempt, `${frame.label}.ppm`))), frame.rawSha256);
  assert.equal(sha(fs.readFileSync(path.join(attempt, `${frame.label}.png`))), frame.pngSha256);
}
const commands = ['prepare-layout-explore-v1-attempt1', 'freeze-layout-explore-v1-attempt1',
  'verify-layout-explore-v1-attempt1', `${attemptName}-driver`,
  `${attemptName}-timeline`, `${attemptName}-review`];
for (const command of commands) {
  const result = readJson(path.join(commandRoot, command, 'result.json'));
  assert.equal(result.exitCode, 0, command);
  assert.equal(result.timedOut, false, command);
  assert.equal(result.error, null, command);
}
const rejectedArchive = 'archive-layout-explore-v1-attempt1';
const rejectedResult = readJson(path.join(commandRoot, rejectedArchive, 'result.json'));
assert.equal(rejectedResult.exitCode, 1);
assert.equal(rejectedResult.timedOut, false);
assert.match(fs.readFileSync(path.join(commandRoot, rejectedArchive, 'stderr.log'), 'utf8'),
  /ENOENT.*SouiStarter\.zip/s);
const newTransfer = path.join(frozen, 'volume/soui-input');
const oldTransfer = path.join(prior, 'volume/soui-input');
const oldFiles = new Map(priorInput.transferFiles.map(file => [file.path, file]));
const changed = new Set(['export-serial.ps1', 'run-display-explore.ps1']);
const unarchivedPayloads = [];
for (const file of input.transferFiles) {
  const old = oldFiles.get(file.path);
  assert(old, `Missing prior transfer ${file.path}`);
  assert.equal(sha(fs.readFileSync(path.join(newTransfer, file.path))), file.sha256);
  if (changed.has(file.path)) assert.notEqual(file.sha256, old.sha256);
  else {
    assert.equal(file.sha256, old.sha256, `Unexpected transfer change ${file.path}`);
    const archived = path.join(oldTransfer, file.path);
    if (fs.existsSync(archived))
      assert.equal(sha(fs.readFileSync(archived)), file.sha256);
    else {
      assert(['SouiStarter.zip', 'SouiWorkbench.zip'].includes(file.path),
        `Unexpected missing prior byte source ${file.path}`);
      assert.equal(file.bytes, old.bytes);
      unarchivedPayloads.push({path: file.path, bytes: file.bytes, sha256: file.sha256});
    }
  }
}
assert.equal(unarchivedPayloads.length, 2);
assert.equal(sha(fs.readFileSync(path.join(newTransfer, 'transfer-files.tsv'))), input.transferManifestSha256);

const pairs = [];
function visit(directory, relativePath) {
  for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
    const source = path.join(directory, entry.name);
    const destination = path.join(relativePath, entry.name);
    if (entry.isDirectory()) visit(source, destination);
    else if (entry.isFile()) pairs.push([source, destination]);
  }
}
visit(attempt, 'guest-attempt');
for (const command of commands)
  for (const file of ['result.json', 'stdout.log', 'stderr.log'])
    pairs.push([path.join(commandRoot, command, file), path.join('command-logs', command, file)]);
for (const file of ['result.json', 'stdout.log', 'stderr.log'])
  pairs.push([path.join(commandRoot, rejectedArchive, file),
    path.join('command-logs', rejectedArchive, file)]);
for (const file of ['input.json', 'freeze.json'])
  pairs.push([path.join(frozen, file), path.join('frozen-delta', file)]);
for (const file of [...changed, 'transfer-files.tsv'])
  pairs.push([path.join(newTransfer, file), path.join('frozen-delta/volume/soui-input', file)]);
for (const file of ['Run-LayoutExploreV1.ps1', 'Launch-LayoutExploreV1.ps1',
  'Monitor-DisplayExplore.cjs', 'Capture-LayoutTimeline.cjs', 'Review-LayoutExploreV1.cjs',
  'Prepare-LayoutExplore.cjs', 'Freeze-LayoutExplore.cjs', 'Verify-LayoutExplore.cjs',
  'Archive-LayoutExploreV1.cjs', 'Verify-LayoutArchive.cjs',
  'Verify-LayoutArchivePortable.cjs'])
  pairs.push([path.join(root, file), path.join('controls', file)]);
fs.mkdirSync(stage, {recursive: false});
const files = [];
for (const [source, destinationRelative] of pairs) {
  if (files.length % 10 === 0) ensureIdle();
  const destination = path.join(stage, 'raw', destinationRelative);
  fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  const original = fs.readFileSync(source);
  const copy = fs.readFileSync(destination);
  assert.equal(sha(copy), sha(original));
  files.push({source, path: path.join('raw', destinationRelative).replaceAll('\\', '/'),
    bytes: original.length, sha256: sha(original), sourceMtimeUtc: fs.statSync(source).mtime.toISOString()});
}
files.sort((a, b) => a.path.localeCompare(b.path));
const manifest = {schema: 'soui.win7-v02-layout-wheel-raw/v1', fileCount: files.length,
  files, priorFrozenArchive: path.relative(repo, prior).replaceAll('\\', '/'),
  unarchivedPayloads, completeFrozenInputReconstructible: false,
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(freezeBytes), serialSha256: review.serialSha256,
  nativeWheelInputsSent: 8, visualScrollEffectVerified: false,
  imeCompositionVerified: false, taskParallelVerified: false,
  win7CurrentV02Verified: false, releaseReady: false};
fs.writeFileSync(path.join(stage, 'raw-manifest.json'), JSON.stringify(manifest, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(stage, 'review.json'), JSON.stringify({
  schema: 'soui.win7-v02-layout-wheel-review/v1', sourceSha256: input.sourceSha256,
  buildId: input.buildId, freezeSha256: sha(freezeBytes),
  serialSha256: review.serialSha256, serialFrames: review.serialFrames,
  guestIdentity: review.guestIdentity, gateSamples: review.gateSamples,
  applicationNativeExitCode: 0, naturalClose: true,
  completePayloadsAndLegalPrePost: true, nativeWheelInputsSent: 8,
  visualScrollEffectVerified: false, imeCompositionVerified: false,
  taskParallelVerified: false, win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(stage, 'README.md'), [
  '# 当前 V02 Win7 SP1 x86 滚轮输入探索', '',
  `本目录保留 ${files.length} 份原始记录，绑定当前 718 文件 Demo 源码 SHA-256 ${input.sourceSha256}、正式 x86 Release 构建 ${input.buildId}、13 文件只读输入 SHA-256 ${sha(freezeBytes)}。未改的探针和载荷清单可从先前的 ${path.relative(repo, prior).replaceAll('\\', '/')} 按字节复核；两份应用 ZIP 原件仍在外部冻结输入，仓库仅保留其大小、散列及客体传输/载荷核对结果，不能仅用仓库重建完整冻结输入。本轮单独保存改动过的客体脚本、串口导出和传输清单。`,
  `Win7 6.1.7601 SP1 x86 快照桌面临时为 1024×768，普通可见 Workbench 以 \`--react --page=data --width=950 --height=550\` 启动，实际窗口完整处于桌面内。原生探针绑定 PID ${review.applicationPid}、窗口 ${review.applicationHwnd} 和进程创建身份，在自然前台连续发送八次真实系统滚轮，八份探针退出与结果均由严格校验器核对。`,
  `客体应用原生退出 0，正常关闭；50 帧完整串口、完整载荷与法律声明前后、冻结输入和基础盘均通过。宿主 ${review.gateSamples} 次采样未见外部桌面负载，驱动与我方 QEMU 原始退出 0；29 张时间线截图的 PPM/PNG 原件与 SHA-256 保留。`,
  '截图中可见中文输入、动效及公共任务区域；但第一张已渲染截图晚于滚轮序列，缺少滚动前基线，不能严谨证明视觉滚动效果。本轮未输入中文、未启动公共任务或缩放窗口；不能当作 V02 真实并发通过。`win7CurrentV02Verified=false`、`releaseReady=false`。', '',
  '首次归档校验因为先前仓库归档未保存 `SouiStarter.zip`/`SouiWorkbench.zip` 字节而原始退出 1，失败 stderr 保留在 `raw/command-logs/archive-layout-explore-v1-attempt1/`。修正后从外部冻结原件实际复核两个 ZIP 的大小与散列；仓库可移植复核只验证这两个 ZIP 的已提交元数据，不声称能从仓库重建它们。', '',
].join('\n'), {flag: 'wx'});
ensureIdle();
fs.renameSync(stage, target);
console.log(JSON.stringify({target, rawFiles: files.length, serialFrames: review.serialFrames,
  nativeWheelInputsSent: 8, visualScrollEffectVerified: false, releaseReady: false}));
