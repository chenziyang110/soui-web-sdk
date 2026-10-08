'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const name = 'guest-display-explore-v2-attempt1';
const attempt = path.join(root, name);
const frozen = path.join(root, 'display-explore-v2-attempt1');
const command = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/guest-display-explore-v2-attempt1-driver';
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/win7-sp1-x86-v02-display-v2-interrupted-2026-09-28';
assert.equal(fs.existsSync(target), false, 'Refuse overwrite display v2 interruption archive');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const review = JSON.parse(fs.readFileSync(path.join(attempt, 'interrupted-review.json'), 'utf8'));
assert.equal(review.guestCommandSent, false);
assert.equal(review.serialBytes, 0);
assert.equal(review.releaseReady, false);
const pairs = [];
function visit(directory, relative, excludeSnapshotTemps = false) {
  for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
    if (excludeSnapshotTemps && /^vl\./.test(entry.name)) continue;
    const source = path.join(directory, entry.name);
    const destination = path.join(relative, entry.name);
    if (entry.isDirectory()) visit(source, destination);
    else if (entry.isFile()) pairs.push([source, destination]);
  }
}
visit(attempt, 'guest-attempt', true);
for (const file of ['freeze.json', 'input.json',
  'volume/soui-input/7za.exe', 'volume/soui-input/V02DisplayModeProbe.exe',
  'volume/soui-input/V02InputProbe.exe', 'volume/soui-input/SouiStarter-payload.tsv',
  'volume/soui-input/SouiWorkbench-payload.tsv', 'volume/soui-input/run-display-explore.ps1',
  'volume/soui-input/run-display-explore.cmd', 'volume/soui-input/transfer-files.tsv',
  'volume/soui-input/verify-apps.ps1', 'volume/soui-input/export-serial.ps1'])
  pairs.push([path.join(frozen, file), path.join('frozen-input', file)]);
for (const file of ['Prepare-DisplayExploreV2.cjs', 'Freeze-DisplayExploreV2.cjs',
  'Verify-DisplayExploreV2.cjs', 'Run-DisplayExploreV2.ps1',
  'Launch-DisplayExploreV2.ps1', 'Review-DisplayV2Interrupted.cjs',
  'Archive-DisplayV2Interruption.cjs'])
  pairs.push([path.join(root, file), path.join('controls', file)]);
for (const file of ['result.json', 'stdout.log', 'stderr.log'])
  pairs.push([path.join(command, file), path.join('driver-command', file)]);
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
  schema: 'soui.win7-v02-display-v2-interrupted-raw/v1', fileCount: files.length, files,
  frozenInputFileCount: 13,
  frozenApplicationZipsRemainExternal: true,
  snapshotTemporaryFilesRemainExternal: true,
  guestCommandSent: false, win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'README.md'), [
  '# 当前 V02 Win7 显示探索 v2：只读输入冻结与外部负载中断', '',
  `本目录保存 ${files.length} 份原始记录及逐字节清单。v2 夹具保持原 60 秒应用期限，在 10–40 秒多次采样可见窗口、45–55 秒尝试自然关闭，并使用独立进程捕获探针 stderr。`,
  `13 文件只读输入冻结 SHA-256 为 ${review.freezeSha256}，绑定当前源码和正式 x86 Release 构建；两个原应用 ZIP 仍留在外部冻结目录，由 freeze.json 的完整文件清单固定。`,
  '第一次 v2 客体启动到 Win7 桌面后出现独立 QEMU。宿主门禁只终止我方 QEMU；驱动原始退出 1、我方 QEMU 退出 -1，基础盘与冻结输入不变。未发送客体命令，串口 0 字节，显示探针和应用均未运行。快照临时文件留在外部原目录，不把它们当成应用证据。',
  '此样本不说明 v2 夹具或应用兼容性。下一次空闲必须使用新 guest-display-explore-v2-attempt2 名称，先确认桌面，再发送一次冻结命令；生产准入仍为 false。', '',
].join('\n'), {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, guestCommandSent: false,
  win7CurrentV02Verified: false, releaseReady: false}));
