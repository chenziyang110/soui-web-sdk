'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const attemptName = 'guest-display-explore-attempt3';
const attempt = path.join(root, attemptName);
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/win7-sp1-x86-v02-display-failure-2026-09-28';
const commandRoot = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
assert.equal(fs.existsSync(target), false, 'Refuse overwrite display failure archive');
const review = JSON.parse(fs.readFileSync(path.join(attempt, 'display-failure-review-v2.json'), 'utf8'));
assert.equal(review.schema, 'soui.win7-v02-current-display-failure-review/v2');
assert.equal(review.displayMode1024x768Verified, true);
assert.equal(review.win7CurrentV02Verified, false);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const pairs = [];
function visit(directory, relative) {
  for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
    const source = path.join(directory, entry.name);
    const destination = path.join(relative, entry.name);
    if (entry.isDirectory()) visit(source, destination);
    else if (entry.isFile()) pairs.push([source, destination]);
  }
}
visit(attempt, 'guest-attempt');
for (const command of [`${attemptName}-driver`, `${attemptName}-review-attempt1`]) {
  for (const name of ['result.json', 'stdout.log', 'stderr.log'])
    pairs.push([path.join(commandRoot, command, name), path.join('command-logs', command, name)]);
}
for (const name of ['freeze.json', 'input.json'])
  pairs.push([path.join(root, 'display-explore-attempt1', name), path.join('frozen-input', name)]);
for (const name of ['Review-DisplayFailure.cjs', 'Review-DisplayFailure-v1.cjs',
  'Review-DisplayExplore.cjs', 'Inspect-DisplaySerial.cjs', 'Export-DisplaySerial.cjs',
  'Archive-DisplayFailure.cjs'])
  pairs.push([path.join(root, name), path.join('review-driver', name)]);
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
  schema: 'soui.win7-v02-display-failure-raw/v1', fileCount: files.length, files,
  guestAttempt: attemptName, displayMode1024x768Verified: true,
  applicationWindowVerified: false, win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'README.md'), [
  '# 当前 V02 Win7 可见页面探索：显示模式成功，窗口探测失败', '',
  `本目录保存 ${files.length} 份原始记录与逐字节清单，绑定当前 718 文件源码、正式 x86 Release 构建及 13 文件冻结输入。`,
  '客体身份为 Windows 7 SP1 x86；冻结传输、载荷前置校验和临时显示模式变更成功，桌面从 800×600 变为 1024×768。34 次宿主门禁没有检测到外部桌面负载，驱动及我方 QEMU 正常退出，基础盘和冻结输入前后不变。',
  '客体应用启动后的 10 秒窗口探测报告 `visible unowned top-level target window missing`，客体 `run-exitcode.txt` 原始为 1。严格成功校验原始退出 1；没有 `run-complete.txt`、应用原生退出码、自然关闭或页面可见证据。启动后与完成后的桌面截图均保留，后者只证明显示模式，不证明应用页面。',
  '该结果不能断言应用在 Win7 不兼容，也不能将旧隐藏短测或本轮 QEMU 退出 0 当作可见交互通过。需检查窗口启动时序或可见性，再用新冻结输入与新客体尝试验证；不覆写本次失败。生产准入仍为 false。', '',
].join('\n'), {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, serialFrames: review.serialFrames,
  displayMode1024x768Verified: true, win7CurrentV02Verified: false, releaseReady: false}));
