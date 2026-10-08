'use strict';
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const root = __dirname, repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const id = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
const raw = fs.readFileSync(path.join(root, 'freeze.json')), freeze = JSON.parse(raw);
assert.equal(freeze.schema, 'soui.renewal-animated-kernel-candidate/v1');
for (const c of freeze.controls) { const b = fs.readFileSync(path.join(root, c.path)); assert.equal(b.length, c.bytes); assert.equal(id.hash(b), c.sha256); }
const runtime = freeze.runtimes.x64;
assert.deepEqual(id.snapshot(runtime.runtime, id.walk(runtime.runtime)).files, runtime.payload.files);
const parent = id.readJson('C:/SOUI-SDK-production-validation/preview11-renewal-supervisor-race-20260926/freeze.json');
assert.deepEqual(runtime.payload.files.filter(f => f.sha256 !== parent.runtimes.x64.payload.files.find(p => p.path === f.path).sha256).map(f => f.path), ['node.dll']);
assert.equal(runtime.payload.files.length, parent.runtimes.x64.payload.files.length);
assert.match(fs.readFileSync(path.join(runtime.runtime, 'resources/index.html'), 'utf8'), /animation:turn 1s linear infinite;/);
const runs = fs.readdirSync(path.join(root, 'runs')).filter(n => n.startsWith('x64-')); assert.equal(runs.length, 1);
const run = path.join(root, 'runs', runs[0]), result = id.readJson(path.join(run, 'result.json'));
const events = fs.readFileSync(path.join(run, 'native.ndjson'), 'utf8').trim().split(/\r?\n/).map(JSON.parse);
assert(events.every((e, i) => e.seq === i + 1));
assert.equal(result.exitCode, 0); assert.equal(result.timedOut, false); assert.equal(result.externalLoad, false);
assert.equal(result.spawnError, null); assert.equal(result.identityError, null);
const payload = id.readJson(path.join(run, 'payload-before.json')); assert.deepEqual(payload, runtime.payload.files);
const after = id.readJson(path.join(run, 'payload-after.json')).filter(f => f.path !== 'source-navigation.log'); assert.deepEqual(after, payload);
assert.deepEqual(id.snapshot(path.join(run, 'runtime'), id.walk(path.join(run, 'runtime')).filter(n => n !== 'source-navigation.log')).files, payload);
const launch = id.readJson(path.join(run, 'launch.json')); assert.equal(launch.freezeSha256, id.hash(raw)); assert.equal(launch.animatedKernelCandidate, true);
assert.deepEqual(launch.args.slice(-2), ['--duration-ms', '30000']);
assert(!events.some(e => e.event === 'run.failed')); const final = events.at(-1);
assert.equal(final.event, 'run.finished'); assert.equal(final.exitCode, 0);
for (const field of ['readyCount', 'bootstrapCount', 'longInvocations', 'resultInvocations']) assert.equal(final[field], 1);
assert.equal(final.realRenewalVerified, false);
const completed = events.filter(e => e.event === 'web.record').map(e => JSON.parse(e.dataJson)).filter(e => e.kind === 'complete');
assert.equal(completed.length, 1); const task = completed[0];
assert.equal(task.cancelled, true); assert.equal(task.unsubscribed, true); assert.equal(task.resultValue, 42);
assert.equal(task.startCount, 2); assert.equal(task.subscribeCount, 1);
for (const gate of ['gate-before.json', 'gate-prelaunch.json', 'gate-after.json'].map(n => id.readJson(path.join(run, n))).concat(id.readJson(path.join(run, 'gates-during.json')))) {
  assert.equal(gate.exitCode, 0); assert.equal(gate.error, null);
}
const review = { reviewedAt: new Date().toISOString(), freezeSha256: id.hash(raw), kernelSha256: payload.find(f => f.path === 'node.dll').sha256,
  kernelBuildSourceSha256: freeze.kernelBuildSourceSha256, consumerSdkVersion: freeze.consumerSdkVersion,
  runtimeDifferenceFromParent: ['node.dll'], originalAnimationUnchanged: true, debuggerAttached: false,
  run: runs[0], nativeRecords: events.length, nativeExitCode: result.exitCode, final,
  publicTasks: { started: 2, resultValue: 42, cancelled: true, unsubscribed: true },
  allRuntimeInputsUnchanged: true, externalLoadObserved: false,
  currentSdkPackageValidated: false, realRenewalVerified: false, productionPass: false };
fs.writeFileSync(path.join(root, 'review.json'), id.encoded(review), { flag: 'wx' });
console.log(id.encoded(review));
