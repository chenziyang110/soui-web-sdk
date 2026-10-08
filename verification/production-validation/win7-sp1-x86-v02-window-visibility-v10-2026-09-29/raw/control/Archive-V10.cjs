'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const commandRoot = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const attemptName = 'guest-window-visibility-v10-attempt1';
const target = 'C:/SOUI-SDK-development/work-area-placement/verification/production-validation/win7-sp1-x86-v02-window-visibility-v10-2026-09-29';
assert.equal(fs.existsSync(target), false, 'Refuse overwrite V10 diagnostic archive');
const attempt = path.join(root, attemptName);
const review = JSON.parse(fs.readFileSync(path.join(attempt, 'review-v10.json'), 'utf8'));
assert.equal(review.schema, 'soui.win7-v02-window-visibility-v10-review/v1');
assert.equal(review.serialFrames, 46);
assert.equal(review.probeSamples, 20);
assert.equal(review.visibilityObserved, true);
assert.equal(review.workAreaFixVerifiedOnWin7, false);
assert.equal(review.releaseReady, false);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const pairs = [];
function collect(directory, relative) {
  for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
    const source = path.join(directory, entry.name);
    const destination = path.join(relative, entry.name);
    if (entry.isDirectory()) collect(source, destination);
    else if (entry.isFile()) pairs.push([source, destination]);
  }
}
collect(attempt, 'guest-attempt');
for (const name of [
  'workarea-v10-transfer-prepare-attempt1-driver',
  'workarea-v10-transfer-freeze-attempt1-driver',
  'workarea-v10-transfer-verify-attempt1-driver',
]) collect(path.join(root, 'command-logs', name), path.join('command-logs', name));
for (const name of [
  'workarea-v10-review-attempt1-driver',
  'workarea-v10-review-attempt2-driver',
  'workarea-v10-review-attempt3-driver',
]) collect(path.join(commandRoot, name), path.join('command-logs', name));
for (const name of ['freeze.json', 'input.json'])
  pairs.push([path.join(root, 'win7-v10-transfer-attempt1', name), path.join('frozen-input', name)]);
for (const name of ['transfer-files.tsv', 'SouiStarter-payload.tsv', 'SouiWorkbench-payload.tsv',
  'run-display-explore.ps1', 'run-display-explore.cmd', 'verify-apps.ps1', 'export-serial.ps1'])
  pairs.push([path.join(root, 'win7-v10-transfer-attempt1/volume/soui-input', name),
    path.join('frozen-input', name)]);
for (const name of ['Run-V10.ps1', 'Launch-V10.ps1', 'Monitor-DisplayExplore.cjs',
  'Prepare-V10Transfer.cjs', 'Freeze-V10Transfer.cjs', 'Verify-V10Transfer.cjs',
  'Review-V10.cjs', 'Archive-V10.cjs', 'Validate-V10Toolchain.cjs',
  'Capture-V10Parser.ps1', 'v10-powershell-parser.json', 'v10-static-preparation.json',
  'V10-PREPARATION.md'])
  pairs.push([path.join(root, name), path.join('control', name)]);
const sourceScript = path.join(root, 'win7-v10-source/run-visibility-diagnostic.ps1');
pairs.push([sourceScript, path.join('control', 'run-visibility-diagnostic.ps1')]);

const files = [];
for (const [source, relative] of pairs) {
  const destination = path.join(target, 'raw', relative);
  fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  const original = fs.readFileSync(source);
  const copy = fs.readFileSync(destination);
  assert.equal(sha(copy), sha(original));
  files.push({source, path: path.join('raw', relative).replaceAll('\\', '/'),
    bytes: original.length, sha256: sha(original), sourceMtimeUtc: fs.statSync(source).mtime.toISOString()});
}
files.sort((a, b) => a.path.localeCompare(b.path));
fs.writeFileSync(path.join(target, 'raw-manifest.json'), JSON.stringify({
  schema: 'soui.win7-v02-window-visibility-v10-raw/v1', fileCount: files.length,
  files, guestAttempt: attemptName, workAreaFixVerifiedOnWin7: false,
  win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'README.md'), [
  '# Win7 SP1 x86 V02 V10 窗口可见性时序诊断', '',
  `本目录保存 ${files.length} 份原始记录及逐字节清单，绑定官方 x86 Release buildId ${review.buildId}、源码 SHA-256 ${review.sourceSha256}、冻结输入 SHA-256 ${review.freezeSha256}。`,
  '客体是 Windows 7 SP1 x86、PowerShell 2。宿主及运行中门禁未记录外部桌面/虚拟机负载；QEMU 原生退出 0，冻结输入和基础盘前后校验一致。传输载荷在客体前后均逐文件匹配，串口 46 帧逐帧校验通过。显示模式从 800×600 切换到 1024×768。',
  '同一轮 20 次只读窗口探针在 10–40 秒七次未检出窗口；第 45 秒计划采样在客体单调计时 46.933 秒首次检出，上一失败样本为 40.273 秒。随后至 105 秒均可见，窗口边界为 [87,24,937,664]；约 108 秒由测试脚本发出关闭请求，Workbench 原生退出 0，客体整轮退出 0。截图显示 V02 万行数据页。此区间只界定这一次客体的首次可见时间，不能推断原因或物理机时延。',
  '本轮未执行滚动、缩放、中文 IME 或公共任务并行。窗口关闭由夹具请求，不是自然关闭。虽后来可见，该只读诊断不证明工作区定位修正在 Win7 的交互验收；此前 V9 失败原件仍有效，Issue 42 保持开放。`workAreaFixVerifiedOnWin7=false`、`win7CurrentV02Verified=false`、`benchmarkValid=false`、`releaseReady=false`。', '',
].join('\n'), {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, serialFrames: review.serialFrames,
  workAreaFixVerifiedOnWin7: false, win7CurrentV02Verified: false, releaseReady: false}));
