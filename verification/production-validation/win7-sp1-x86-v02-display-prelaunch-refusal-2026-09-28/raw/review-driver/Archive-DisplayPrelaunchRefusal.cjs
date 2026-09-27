'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const command = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/guest-display-explore-attempt2-driver';
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/win7-sp1-x86-v02-display-prelaunch-refusal-2026-09-28';
assert.equal(fs.existsSync(target), false, 'Refuse to replace archived refusal');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
const review = JSON.parse(read(path.join(root, 'display-prelaunch-refusal-review.json')));
assert.equal(review.driverExitCode, 1);
assert.equal(review.ownQemuStarted, false);
assert.equal(review.guestCommandSent, false);
assert.equal(review.releaseReady, false);
const pairs = [];
for (const name of ['result.json', 'stdout.log', 'stderr.log'])
  pairs.push([path.join(command, name), path.join('command', name)]);
for (const name of ['display-prelaunch-refusal-review.json',
  'Review-DisplayPrelaunchRefusal.cjs', 'Archive-DisplayPrelaunchRefusal.cjs'])
  pairs.push([path.join(root, name), path.join('review-driver', name)]);
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
  schema: 'soui.win7-v02-display-prelaunch-refusal-raw/v1', fileCount: files.length, files,
  ownQemuStarted: false, guestCommandSent: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'README.md'), [
  '# 当前 V02 Win7 显示探索第二次：启动前门禁拒绝', '',
  `本目录保存 ${files.length} 份原始记录及逐字节清单。`,
  '第二次外层驱动原始退出 1，报错“Other desktop or QEMU load is running”。',
  '我方 QEMU 未启动，客体目录未创建，命令未发送、串口不存在；这不判定显示探针或应用。',
  '事后本机观察到独立 QEMU 活动，但本次驱动未保存门禁时的 PID，不把事后 PID 冒充当时门禁证据。',
  '原始 attempt 名已用，下次使用新的 guest-display-explore-attempt3。生产准入仍为 false。', '',
].join('\n'), {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, ownQemuStarted: false, releaseReady: false}));
