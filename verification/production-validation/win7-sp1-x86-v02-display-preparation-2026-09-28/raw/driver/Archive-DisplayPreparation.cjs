'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const target = path.join(repo, 'verification/production-validation/win7-sp1-x86-v02-display-preparation-2026-09-28');
assert.equal(fs.existsSync(target), false, 'Refuse to replace archived evidence');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
const review = json(path.join(root, 'display-preparation-review.json'));
const frozenRoot = path.join(root, 'display-explore-attempt1');
const freeze = json(path.join(frozenRoot, 'freeze.json'));
const input = json(path.join(frozenRoot, 'input.json'));
assert.equal(review.freezeSha256, sha(read(path.join(frozenRoot, 'freeze.json'))));
assert.equal(review.guestDisplayTestExecuted, false);
assert.equal(review.win7CurrentV02Verified, false);
assert.equal(review.releaseReady, false);
assert.equal(freeze.files.length, 13);
assert.equal(input.transferFiles.length, 11);
for (const item of freeze.files) {
  const bytes = read(path.join(frozenRoot, item.path));
  assert.equal(bytes.length, item.bytes, item.path);
  assert.equal(sha(bytes), item.sha256, item.path);
}
const pairs = [];
const add = (source, destination) => pairs.push([source, destination]);
function addTree(sourceRoot, destinationRoot) {
  for (const entry of fs.readdirSync(sourceRoot, {withFileTypes: true})) {
    const source = path.join(sourceRoot, entry.name);
    const destination = path.join(destinationRoot, entry.name);
    if (entry.isDirectory()) addTree(source, destination);
    else if (entry.isFile()) add(source, destination);
    else throw Error(`Unsupported raw item: ${source}`);
  }
}
for (const name of [
  'Prepare-DisplayExplore.cjs', 'Prepare-DisplayDriver.cjs', 'Prepare-DisplayLaunch.cjs',
  'Freeze-DisplayExplore.cjs', 'Verify-DisplayExplore.cjs',
  'Run-DisplayExplore.ps1', 'Launch-DisplayExplore.ps1', 'Monitor-DisplayExplore.cjs',
  'Review-DisplayPreparation.cjs', 'Review-DisplayExplore.cjs',
  'display-preparation-review.json', 'Archive-DisplayPreparation.cjs',
]) add(path.join(root, name), path.join('driver', name));
for (const name of ['CMakeLists.txt', 'V02DisplayModeProbe.cpp'])
  add(path.join(root, 'display-probe-input', name), path.join('display-probe-input', name));
add(path.join(root, 'display-probe-build-attempt1/Release/V02DisplayModeProbe.exe'),
  'display-probe-output/V02DisplayModeProbe.exe');
add(path.join(root, 'display-probe-build-attempt1/V02DisplayModeProbe.vcxproj'),
  'display-probe-output/V02DisplayModeProbe.vcxproj');
for (const name of ['input.json', 'freeze.json'])
  add(path.join(frozenRoot, name), path.join('frozen-input', name));
for (const item of fs.readdirSync(path.join(frozenRoot, 'volume/soui-input'), {withFileTypes: true})) {
  assert.equal(item.isFile(), true);
  if (item.name === 'SouiStarter.zip' || item.name === 'SouiWorkbench.zip') continue;
  add(path.join(frozenRoot, 'volume/soui-input', item.name), path.join('frozen-input/volume/soui-input', item.name));
}
for (const name of ['input.json', 'freeze.json'])
  add(path.join(root, 'visible-explore-attempt1', name), path.join('prior-visible-input', name));
addTree(path.join(root, 'guest-visible-explore-attempt1'), 'prior-visible-refusal');
const logs = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
for (const item of review.commandRecords)
  addTree(path.join(logs, item.name), path.join('commands', item.name));

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
  schema: 'soui.win7-v02-display-preparation-raw/v1', fileCount: files.length, files,
  guestDisplayTestExecuted: false, win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'README.md'), [
  '# Win7 SP1 x86 当前 V02 可见页面显示模式准备',
  '',
  `本目录保存 ${files.length} 份原始记录。\`raw-manifest.json\` 将仓库内副本绑定到外部原件的字节数与 SHA-256。`,
  '两个应用 ZIP 因体积未复制到仓库，保留在外部只读冻结输入；其身份由 input.json 与 freeze.json 逐文件绑定。',
  '',
  `当前 718 文件源码 SHA-256 \`${review.sourceSha256}\`，正式 x86 Release buildId \`${review.buildId}\`。`,
  `显示探针为静态 x86 /MT、子系统 6.00，EXE SHA-256 \`${review.displayProbeSha256}\`。`,
  `新客体输入共 ${review.frozenFiles} 文件，freeze SHA-256 \`${review.freezeSha256}\`。`,
  '先前可见探索在另一台 QEMU 已运行时由二次门禁拒绝，本方未启动客体或发送命令；原样本单独保留。',
  '',
  '**本目录仅证明准备和启动前拒绝。新显示模式未在 Win7 客体运行，V02 页面、真实交互与生产门槛均未通过。**',
  '',
].join('\n'), {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, freezeSha256: review.freezeSha256,
  guestDisplayTestExecuted: false, releaseReady: false}));
