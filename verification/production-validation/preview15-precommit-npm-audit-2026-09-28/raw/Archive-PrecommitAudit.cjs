'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/preview15-precommit-npm-audit-2026-09-28';
assert.equal(fs.existsSync(target), false, 'Refuse to overwrite audit archive');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const review = JSON.parse(fs.readFileSync(path.join(root, 'precommit-audit-review.json'), 'utf8'));
assert.equal(review.productionAuditPass, true);
assert.equal(review.fullAuditPass, true);
assert.equal(review.results.length, 6);
assert.equal(review.releaseReady, false);
const pairs = [
  [path.join(root, 'audit-tool-versions.json'), 'audit-tool-versions.json'],
  [path.join(root, 'precommit-audit-review.json'), 'precommit-audit-review.json'],
  [path.join(root, 'Review-PrecommitAudit.cjs'), 'Review-PrecommitAudit.cjs'],
  [path.join(root, 'Archive-PrecommitAudit.cjs'), 'Archive-PrecommitAudit.cjs'],
];
for (const [, directory] of [
  ['production', 'audit-precommit-20260928-attempt1'],
  ['all', 'audit-precommit-20260928-full-attempt1'],
]) {
  for (const name of ['sdk-web', 'starter-web', 'workbench-web']) {
    for (const extension of ['stdout.json', 'stderr.log', 'exitcode.txt'])
      pairs.push([path.join(root, directory, `${name}.${extension}`), path.join(directory, `${name}.${extension}`)]);
  }
}
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
  schema: 'soui.preview15-precommit-npm-audit-raw/v1',
  fileCount: files.length, files,
  productionAuditPass: true, fullAuditPass: true, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'README.md'), [
  '# preview.15 提交前 JavaScript 依赖公告核查', '',
  `本目录保存 ${files.length} 份原始记录及逐字节清单。三个当前锁文件分别运行生产依赖和完整依赖的 npm audit；六条原始退出码均为 0，stderr 为空，报告的漏洞总数均为 0。`,
  '核查输入为当前工作树中的 SDK Web、Starter Web 与 Workbench Web 锁文件；review.json 固定各锁文件和输出的 SHA-256。',
  '结果只反映执行时 npm 公告服务的答复，不覆盖原生 DLL、安全边界、许可与未来公告；生产准入仍为 false。', '',
].join('\n'), {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, audits: review.results.length, releaseReady: false}));
