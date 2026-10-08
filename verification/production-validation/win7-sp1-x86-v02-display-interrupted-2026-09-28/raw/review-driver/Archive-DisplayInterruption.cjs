'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const attempt = path.join(root, 'guest-display-explore-attempt1');
const command = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/guest-display-explore-attempt1-driver';
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/win7-sp1-x86-v02-display-interrupted-2026-09-28';
assert.equal(fs.existsSync(target), false, 'Refuse to replace archived interruption');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
const review = json(path.join(attempt, 'interrupted-review.json'));
assert.equal(review.guestCommandSent, false);
assert.equal(review.serialBytes, 0);
assert.equal(review.displayModeExecuted, false);
assert.equal(review.applicationStarted, false);
assert.equal(review.win7CurrentV02Verified, false);
assert.equal(review.releaseReady, false);
assert.equal(sha(read(path.join(attempt, 'boot-01.ppm'))), review.bootScreenshotSha256);
const pairs = [];
function add(source, relative) { pairs.push([source, relative]); }
for (const entry of fs.readdirSync(attempt, {withFileTypes: true})) {
  assert.equal(entry.isFile(), true);
  if (/^vl\.[A-Z0-9]+$/.test(entry.name)) continue; // QEMU temporary snapshot overlays stay at original path.
  add(path.join(attempt, entry.name), path.join('guest-attempt', entry.name));
}
for (const entry of fs.readdirSync(command, {withFileTypes: true})) {
  assert.equal(entry.isFile(), true);
  add(path.join(command, entry.name), path.join('driver-command', entry.name));
}
add(path.join(root, 'Review-DisplayInterrupted.cjs'), 'review-driver/Review-DisplayInterrupted.cjs');
add(path.join(root, 'Archive-DisplayInterruption.cjs'), 'review-driver/Archive-DisplayInterruption.cjs');
const files = [];
for (const [source, relative] of pairs) {
  const destination = path.join(target, 'raw', relative);
  fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  const original = read(source), copy = read(destination);
  assert.equal(sha(copy), sha(original));
  files.push({source, path: path.join('raw', relative).replaceAll('\\', '/'),
    bytes: original.length, sha256: sha(original), sourceMtimeUtc: fs.statSync(source).mtime.toISOString()});
}
files.sort((a, b) => a.path.localeCompare(b.path));
fs.writeFileSync(path.join(target, 'raw-manifest.json'), JSON.stringify({
  schema: 'soui.win7-v02-display-interrupted-raw/v1', fileCount: files.length, files,
  guestCommandSent: false, displayModeExecuted: false, win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'README.md'), [
  '# 当前 V02 Win7 显示探索首次客体：外部负载中断', '',
  `本目录保存 ${files.length} 份原始记录，\`raw-manifest.json\` 逐字节绑定外部原件。`,
  '临时 QEMU 快照覆盖文件保留在外部原尝试目录，不作为可复用运行输入。', '',
  `我方 QEMU PID ${review.qemuPid} 已启动，在 Win7 欢迎界面留下 800×600 截图；第 ${review.gateSamples} 次门禁发现外部 QEMU PID ${review.externalQemuPid}。`,
  '启动器只终止我方客体：QEMU 原始退出 -1、外层驱动原始退出 1，基础盘与冻结输入前后不变。',
  '客体命令未发送、串口 0 字节、显示模式探针与 Workbench 均未执行。此结果不判定应用兼容或 V02 交互。',
  '',
  '后续使用全新的 guest-display-explore-attempt2，旧 attempt1 文件不可覆写。生产准入仍为 false。', '',
].join('\n'), {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, externalQemuPid: review.externalQemuPid,
  guestCommandSent: false, releaseReady: false}));
