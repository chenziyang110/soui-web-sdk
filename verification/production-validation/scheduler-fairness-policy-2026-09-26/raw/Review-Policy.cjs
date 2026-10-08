'use strict';
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const root = __dirname, repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const id = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
const rows = [], selected = ['Test.cjs', 'Capture-Command.cjs', 'Integrate.cjs', 'WebThreadImpl-before.cpp', 'Review-Policy.cjs'];
for (const arch of ['x86', 'x64']) for (const phase of ['red', 'green']) {
  const snapshot = 'snapshots/' + arch + '-' + phase;
  selected.push(...id.walk(path.join(root, snapshot)).map(n => snapshot + '/' + n));
  const outputs = {};
  for (const step of ['configure', 'build', 'execute']) {
    const relative = 'command-logs/' + arch + '-' + phase + '-' + step, base = path.join(root, relative);
    const r = id.readJson(path.join(base, 'result.json'));
    assert.equal(r.exitCode, phase === 'red' && step === 'execute' ? 1 : 0);
    assert.equal(r.timedOut, false); assert.equal(r.error, null);
    for (const stream of ['stdout', 'stderr']) {
      const b = fs.readFileSync(path.join(base, stream + '.log'));
      assert.equal(b.length, r[stream].bytes); assert.equal(id.hash(b), r[stream].sha256);
    }
    outputs[step] = r;
    selected.push(...id.walk(base).map(n => relative + '/' + n));
  }
  const stdout = fs.readFileSync(path.join(root, 'command-logs', arch + '-' + phase + '-execute/stdout.log'), 'utf8');
  const stderr = fs.readFileSync(path.join(root, 'command-logs', arch + '-' + phase + '-execute/stderr.log'), 'utf8');
  if (phase === 'green') {
    assert.match(stdout, /Scheduler policy checks: 12; failures: 0/); assert.equal(stderr, '');
    for (const file of id.walk(path.join(root, snapshot))) assert.equal(id.hash(fs.readFileSync(path.join(root, snapshot, file))), id.hash(fs.readFileSync(path.join(repo, file))));
  } else if (arch === 'x86') {
    assert.match(stdout, /Scheduler policy checks: 12; failures: 5/);
    assert.match(stderr, /Continuous animation starved a ready loading task/);
  } else assert.match(stderr, /Overdue loading task starved behind fresh normal work/);
  const buildRelative = 'builds/' + arch + '-' + phase, buildRoot = path.join(root, buildRelative);
  const binaryPath = buildRelative + '/tests/native/Release/soui_scheduler_fairness_tests.exe';
  const b = fs.readFileSync(path.join(root, binaryPath));
  const buildFiles = id.walk(buildRoot).filter(n => /(^CMakeCache\.txt$|CMakeConfigureLog\.yaml$|soui_scheduler_fairness_tests\.vcxproj$|CL\.command\.1\.tlog$|link\.command\.1\.tlog$)/i.test(n));
  selected.push(...buildFiles.map(n => buildRelative + '/' + n));
  rows.push({ architecture: arch, phase, snapshot, binary: { path: binaryPath, bytes: b.length, sha256: id.hash(b) }, outputs,
    checks: phase === 'green' || arch === 'x86' ? 12 : null,
    failures: phase === 'green' ? 0 : arch === 'x86' ? 5 : 1,
    failFast: arch === 'x64' && phase === 'red', stdout, stderr });
}
const review = { reviewedAt: new Date().toISOString(), baseCommit: '0872075ec45d07b1428719725a8ffbcdd5d962de', rows,
  greenMatchesCurrentPolicySources: true, realKernelRebuildValidated: false, animatedPageValidated: false, productionPass: false };
fs.writeFileSync(path.join(root, 'policy-review.json'), id.encoded(review), { flag: 'wx' }); selected.push('policy-review.json');
const target = path.join(repo, 'verification/production-validation/scheduler-fairness-policy-2026-09-26');
assert(!fs.existsSync(target)); const files = [];
for (const file of [...new Set(selected)].sort()) {
  const original = path.join(root, file), b = fs.readFileSync(original), relative = 'raw/' + file, dest = path.join(target, relative);
  fs.mkdirSync(path.dirname(dest), { recursive: true }); fs.writeFileSync(dest, b, { flag: 'wx' });
  files.push({ path: relative, bytes: b.length, sha256: id.hash(b), original });
}
fs.writeFileSync(path.join(target, 'raw-manifest.json'), id.encoded({ schema: 'soui.raw-evidence/v1', files }), { flag: 'wx' });
console.log(id.encoded({ files: files.length, rows: rows.map(({ outputs, ...rest }) => rest), productionPass: false }));
