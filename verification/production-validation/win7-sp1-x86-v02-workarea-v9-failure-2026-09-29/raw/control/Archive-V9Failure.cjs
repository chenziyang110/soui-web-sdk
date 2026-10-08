'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const preparationCommandRoot = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const attemptName = 'guest-work-area-v9-attempt1';
const target = 'C:/SOUI-SDK-development/work-area-placement/verification/production-validation/win7-sp1-x86-v02-workarea-v9-failure-2026-09-29';
assert.equal(fs.existsSync(target), false, 'Refuse overwrite V9 failure archive');
const attempt = path.join(root, attemptName);
const review = JSON.parse(fs.readFileSync(path.join(attempt, 'failure-review-v9.json'), 'utf8'));
assert.equal(review.schema, 'soui.win7-v02-workarea-v9-failure-review/v1');
assert.equal(review.windowVisibleVerified, false);
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
  'workarea-v9-payload-attempt1-driver',
  'workarea-v9-transfer-attempt1-driver',
  'workarea-v9-freeze-attempt1-driver',
  'workarea-v9-verify-attempt1-driver',
]) collect(path.join(preparationCommandRoot, name), path.join('command-logs', name));
for (const name of [
  'workarea-v9-review-attempt1',
  'workarea-v9-failure-review-attempt1',
]) collect(path.join(root, 'command-logs', name), path.join('command-logs', name));
for (const name of ['freeze.json', 'input.json'])
  pairs.push([path.join(root, 'win7-v9-transfer-attempt1', name), path.join('frozen-input', name)]);
for (const name of ['transfer-files.tsv', 'SouiStarter-payload.tsv', 'SouiWorkbench-payload.tsv'])
  pairs.push([path.join(root, 'win7-v9-transfer-attempt1/volume/soui-input', name), path.join('frozen-input', name)]);
for (const name of ['Run-V9.ps1', 'Launch-V9.ps1', 'Monitor-DisplayExplore.cjs',
  'Verify-V9Transfer.cjs', 'Review-V9.cjs', 'Review-V9Failure.cjs', 'Archive-V9Failure.cjs'])
  pairs.push([path.join(root, name), path.join('control', name)]);
for (const name of ['V9-FROZEN-INPUT-RESULT.md', 'V9-PREPARATION.md'])
  pairs.push([path.join(root, name), path.join('control', name)]);

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
  schema: 'soui.win7-v02-workarea-v9-failure-raw/v1', fileCount: files.length,
  files, guestAttempt: attemptName, workAreaFixVerifiedOnWin7: false,
  win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'README.md'), [
  '# Win7 SP1 x86 V02 V9 窗口放置探索：窗口未检出', '',
  `本目录保存 ${files.length} 份原始记录和逐字节清单，绑定官方 x86 Release buildId ${review.buildId}、源码 SHA-256 ${review.sourceSha256} 与冻结输入 SHA-256 ${review.freezeSha256}。`,
  '宿主启动前无其他桌面/虚拟机负载，运行中门禁未记录外部负载；QEMU 原生退出 0，冻结输入及 Win7 基础盘前后校验一致。客体确认为 Windows 7 SP1 x86、PowerShell 2，显示模式从 800×600 变为 1024×768，载荷前置校验通过。',
  'Workbench 进程启动并记录 `window-constructed`、`host-started`，但九次可见窗口探测均返回 `visible unowned top-level target window missing`。客体在约 90 秒由测试脚本发送关闭请求，Workbench 进程原生退出 0；这不是自然关闭或可见页面通过。客体整轮 `run-exitcode.txt` 为 1，只有 29/116 预期串口文件；严格成功校验原始退出 1。命令只发送一次，截图和所有失败原样本保留。',
  '不能据此确认工作区定位修正解决了 Win7 任务栏遮挡，也不能确认真实滚动、中文输入法组合或公共任务并行。需在新的冻结输入和尝试中查明窗口可见性；本次输入和结果不得覆写。`releaseReady` 继续为 false。', '',
].join('\n'), {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, serialFrames: review.serialFrames,
  workAreaFixVerifiedOnWin7: false, win7CurrentV02Verified: false, releaseReady: false}));
