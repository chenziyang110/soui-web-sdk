'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const attemptName = 'guest-display-explore-v2-attempt2';
const attempt = path.join(root, attemptName);
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const relative = 'verification/production-validation/win7-sp1-x86-v02-visible-page-2026-09-28';
const target = path.join(repo, relative);
const staging = path.join(root, 'visible-page-archive-stage-attempt1');
const commandRoot = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const frozen = path.join(root, 'display-explore-v2-attempt1');
const priorFrozen = path.join(repo,
  'verification/production-validation/win7-sp1-x86-v02-display-v2-interrupted-2026-09-28/raw/frozen-input');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
assert.equal(fs.existsSync(target), false, 'Refuse overwrite visible page archive');
assert.equal(fs.existsSync(staging), false, 'Refuse overwrite partial visible page archive');
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
const review = readJson(path.join(attempt, 'display-review-v2.json'));
const visual = readJson(path.join(attempt, 'visual-review.json'));
const timeline = readJson(path.join(attempt, 'timeline-result.json'));
const input = readJson(path.join(frozen, 'input.json'));
const freeze = fs.readFileSync(path.join(frozen, 'freeze.json'));
assert.equal(review.schema, 'soui.win7-v02-current-display-explore-review/v2');
assert.equal(review.applicationNativeExitCode, 0);
assert.equal(review.naturalClose, true);
assert.equal(review.completePayloadsAndLegalPrePost, true);
assert.equal(review.win7CurrentV02Verified, false);
assert.equal(review.freezeSha256, sha(freeze));
assert.equal(review.sourceSha256, input.sourceSha256);
assert.equal(review.buildId, input.buildId);
assert.equal(sha(fs.readFileSync(path.join(priorFrozen, 'freeze.json'))), sha(freeze));
assert.equal(sha(fs.readFileSync(path.join(priorFrozen, 'input.json'))),
  sha(fs.readFileSync(path.join(frozen, 'input.json'))));
assert.equal(visual.evidenceType, 'assistant-visual-review-of-frozen-qemu-screenshot');
assert.equal(visual.frame, 'timeline-018.png');
assert.equal(visual.frameSha256, sha(fs.readFileSync(path.join(attempt, visual.frame))));
assert.equal(visual.dataPageVisibleVerified, true);
assert.equal(visual.win7CurrentV02Verified, false);
assert.equal(timeline.attempt, attemptName);
assert.equal(timeline.qemuPid, review.qemuPid);
assert.equal(timeline.stopReason, 'serial-ended');
assert.equal(timeline.frames.length, 24);
for (const frame of timeline.frames) {
  assert.equal(sha(fs.readFileSync(path.join(attempt, `${frame.label}.ppm`))), frame.rawSha256);
  assert.equal(sha(fs.readFileSync(path.join(attempt, `${frame.label}.png`))), frame.pngSha256);
}
const commands = [`${attemptName}-driver`, `${attemptName}-review-attempt1`];
for (const command of commands) {
  const result = readJson(path.join(commandRoot, command, 'result.json'));
  assert.equal(result.exitCode, 0, command);
  assert.equal(result.timedOut, false, command);
}

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
for (const command of commands) {
  for (const name of ['result.json', 'stdout.log', 'stderr.log'])
    pairs.push([path.join(commandRoot, command, name), path.join('command-logs', command, name)]);
}
for (const name of ['freeze.json', 'input.json'])
  pairs.push([path.join(frozen, name), path.join('frozen-identity', name)]);
for (const name of ['Run-DisplayExploreV2.ps1', 'Launch-DisplayExploreV2.ps1',
  'Monitor-DisplayExplore.cjs', 'Verify-DisplayExploreV2.cjs',
  'Capture-DisplayTimeline.cjs', 'Review-DisplayExploreV2.cjs',
  'Verify-DisplayV2VisibleArchive.cjs',
  'Archive-DisplayV2Visible.cjs'])
  pairs.push([path.join(root, name), path.join('controls', name)]);

fs.mkdirSync(staging, {recursive: false});
const files = [];
for (const [source, destinationRelative] of pairs) {
  if (files.length % 10 === 0) ensureIdle();
  const destination = path.join(staging, 'raw', destinationRelative);
  fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  const original = fs.readFileSync(source);
  const copy = fs.readFileSync(destination);
  assert.equal(sha(copy), sha(original));
  files.push({source, path: path.join('raw', destinationRelative).replaceAll('\\', '/'),
    bytes: original.length, sha256: sha(original), sourceMtimeUtc: fs.statSync(source).mtime.toISOString()});
}
files.sort((a, b) => a.path.localeCompare(b.path));
const manifest = {schema: 'soui.win7-v02-visible-page-raw/v1', fileCount: files.length,
  files, sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(freeze), serialSha256: review.serialSha256,
  visualFrameSha256: visual.frameSha256, displayMode1024x768Verified: true,
  dataPageVisibleVerified: true, v02RealInputVerified: false,
  win7CurrentV02Verified: false, releaseReady: false};
fs.writeFileSync(path.join(staging, 'raw-manifest.json'), JSON.stringify(manifest, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(staging, 'review.json'), JSON.stringify({
  schema: 'soui.win7-v02-visible-page-review/v1', sourceSha256: input.sourceSha256,
  buildId: input.buildId, freezeSha256: sha(freeze),
  serialSha256: review.serialSha256, serialFrames: review.serialFrames,
  guestIdentity: review.guestIdentity, gateSamples: review.gateSamples,
  applicationNativeExitCode: 0, naturalClose: true,
  completePayloadsAndLegalPrePost: true, visualFrame: visual.frame,
  visualFrameSha256: visual.frameSha256, dataPageVisibleVerified: true,
  v02RealInputVerified: false, win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(staging, 'README.md'), [
  '# 当前 V02 Win7 SP1 x86 可见页面验证', '',
  `本目录保存 ${files.length} 份原始记录，绑定当前 718 文件源码 SHA-256 ${input.sourceSha256}、正式 x86 Release 构建 ${input.buildId}、13 文件冻结输入 SHA-256 ${sha(freeze)}。完整冻结输入已逐字节保存在前一轮中断归档，本轮另保留输入身份及原外部目录。`,
  `Win7 SP1 x86 客体临时切换到 1024×768 后，Workbench 以普通可见模式及 \`--react --page=data\` 启动。第 10–30 秒五次原生窗口探测均未找到目标，第 35 秒探测到 PID 1236 的可见顶层窗口；\`timeline-018.png\` 原始截图经助理目视确认显示 V02 / DATA & INTERACTION、10000 / 10000 载入指标、虚拟列表及中文文字。截图 SHA-256 为 ${visual.frameSha256}。截图只证明该时刻的可见渲染，不证明真实输入或全部数据行已绘制。`,
  `客体应用原生退出 0，45 秒窗口关闭请求成功，过程未超原 60 秒期限；串口 ${review.serialFrames} 帧完整校验，完整载荷与各 5 份法律声明前后匹配。宿主 ${review.gateSamples} 次采样未见外部桌面负载，驱动与我方 QEMU 原始退出 0，基础盘及冻结输入前后不变。24 张间隔截图的 PPM/PNG 原件和逐帧 SHA-256 均保留。`,
  '上一轮第 10 秒即终止的失败样本保持原状；这轮显示窗口约在第 35 秒出现，说明之前的单次过早探测不能用来判定 Workbench 不兼容。真实滚动、中文 IME 组合、缩放与公共任务并行尚未执行；800×600 原始桌面条件也未验收。\`win7CurrentV02Verified=false\`、\`releaseReady=false\`。', '',
].join('\n'), {flag: 'wx'});
ensureIdle();
fs.renameSync(staging, target);
console.log(JSON.stringify({target, rawFiles: files.length, serialFrames: review.serialFrames,
  dataPageVisibleVerified: true, v02RealInputVerified: false, releaseReady: false}));
