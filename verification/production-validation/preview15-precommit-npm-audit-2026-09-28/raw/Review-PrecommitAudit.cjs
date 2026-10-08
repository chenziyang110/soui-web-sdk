'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
const packages = [
  ['sdk-web', 'sdk/web/package-lock.json'],
  ['starter-web', 'templates/desktop-app/web/package-lock.json'],
  ['workbench-web', 'demos/workbench/web/package-lock.json'],
];
const scopes = [
  ['production', 'audit-precommit-20260928-attempt1'],
  ['all', 'audit-precommit-20260928-full-attempt1'],
];
const results = [];
for (const [scope, directory] of scopes) {
  for (const [name, lockPath] of packages) {
    const auditDir = path.join(root, directory);
    const stdout = read(path.join(auditDir, `${name}.stdout.json`));
    const stderr = read(path.join(auditDir, `${name}.stderr.log`));
    const exitCode = Number(read(path.join(auditDir, `${name}.exitcode.txt`)).toString('utf8').trim());
    const audit = JSON.parse(stdout.toString('utf8').replace(/^\uFEFF/, ''));
    const lock = read(path.join(repo, lockPath));
    assert.equal(exitCode, 0, `${scope}/${name} audit exit`);
    assert.equal(stderr.length, 0, `${scope}/${name} stderr`);
    assert.equal(audit.metadata?.vulnerabilities?.total, 0, `${scope}/${name} vulnerabilities`);
    results.push({scope, name, lockPath, lockSha256: sha(lock),
      auditStdoutSha256: sha(stdout), auditStderrSha256: sha(stderr),
      auditExitCode: exitCode, vulnerabilities: audit.metadata.vulnerabilities,
      dependencyCounts: audit.metadata.dependencies});
  }
}
const versions = JSON.parse(read(path.join(root, 'audit-tool-versions.json')).toString('utf8').replace(/^\uFEFF/, ''));
assert.match(versions.node, /^v[0-9]+/);
assert.match(versions.npm, /^[0-9]+/);
const review = {
  schema: 'soui.preview15-precommit-npm-audit/v1',
  version: '0.3.0-preview.15',
  toolVersions: versions,
  results,
  productionAuditPass: true,
  fullAuditPass: true,
  scope: 'The three current JavaScript lockfiles at review time; npm advisories returned by the registry.',
  limits: 'Does not assess native binaries, linked components, redistribution terms, or future advisories.',
  licenseReviewVerified: false,
  releaseReady: false,
};
fs.writeFileSync(path.join(root, 'precommit-audit-review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({results: results.length, productionAuditPass: true, fullAuditPass: true,
  licenseReviewVerified: false, releaseReady: false}));
