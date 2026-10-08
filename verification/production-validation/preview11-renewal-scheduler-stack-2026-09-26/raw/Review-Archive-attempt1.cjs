'use strict';
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const root = __dirname, repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const id = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
const freezeSha256 = '8615b71960b35aa074553efd3ba6c4a7c91af594e88427cc1e59b153ec993749';
const freeze = id.readJson(path.join(root, 'freeze.json'));
assert.equal(id.hash(fs.readFileSync(path.join(root, 'freeze.json'))), freezeSha256);
for (const f of freeze.controls) {
  const bytes = fs.readFileSync(path.join(root, f.path));
  assert.equal(bytes.length, f.bytes); assert.equal(id.hash(bytes), f.sha256);
}
const original = freeze.runtimes.x86;
assert.deepEqual(id.snapshot(original.runtime, id.walk(original.runtime)).files, original.payload.files);
const runName = 'x86-2026-09-26T14-23-43-468Z', run = path.join(root, 'runs', runName);
const result = id.readJson(path.join(run, 'result.json'));
assert.equal(result.exitCode, 1); assert.equal(result.timedOut, false);
assert.equal(result.externalLoad, false); assert.equal(result.spawnError, null); assert.equal(result.identityError, null);
const debug = id.readJson(path.join(run, 'debugger-result.json'));
assert.equal(debug.exitCode, 0); assert.equal(debug.timedOut, false); assert.equal(debug.error, null);
assert.deepEqual(result.debuggerResult, debug);
const before = id.readJson(path.join(run, 'payload-before.json'));
assert.deepEqual(before, original.payload.files);
const current = id.snapshot(path.join(run, 'runtime'), id.walk(path.join(run, 'runtime')).filter(n => n !== 'source-navigation.log')).files;
assert.deepEqual(current, before);
assert.deepEqual(id.readJson(path.join(run, 'payload-after.json')).filter(f => f.path !== 'source-navigation.log'), before);
const events = fs.readFileSync(path.join(run, 'native.ndjson'), 'utf8').trim().split(/\r?\n/).map(JSON.parse);
assert.equal(events.length, 19); assert(events.every((e, i) => e.seq === i + 1));
const failed = events.filter(e => e.event === 'run.failed'); assert.equal(failed.length, 1);
assert.equal(failed[0].message, 'Initial page/Host readiness exceeded 60 seconds');
const final = events.at(-1); assert.equal(final.event, 'run.finished'); assert.equal(final.exitCode, 1);
for (const field of ['readyCount', 'bootstrapCount', 'longInvocations', 'resultInvocations']) assert.equal(final[field], 0);
for (const gate of ['gate-before.json', 'gate-prelaunch.json', 'gate-after.json'].map(n => id.readJson(path.join(run, n))).concat(id.readJson(path.join(run, 'gates-during.json')))) {
  assert.equal(gate.exitCode, 0); assert.equal(gate.error, null);
}
const log = fs.readFileSync(path.join(run, 'debugger.log'), 'utf8');
assert.match(log, /node\s+C \(private pdb symbols\)/);
assert.match(log, /SOUI_SCHEDULER_HIT/); assert.match(log, /SOUI_SNAPSHOT_END/);
const snapshot = log.slice(log.lastIndexOf('SOUI_SCHEDULER_HIT'));
assert.match(snapshot, /m_isMainThread\s+: 1/);
const clock = Number(snapshot.match(/m_currentFrameCreateTime\s+: ([\d.]+)/)[1]);
const heap = [...snapshot.matchAll(/^    \[(\d+)\] +: .*?(?=^    \[\d+\] +:|^SOUI_SNAPSHOT_END)/gms)].map(m => ({
  index: Number(m[1]),
  functionName: m[0].match(/m_functionName[^\n]*"([^"]+)"/)[1],
  nextFireTime: Number(m[0].match(/m_nextFireTime\s+: ([\d.]+)/)[1]),
  priority: Number(m[0].match(/m_priority\s+: (\d+)/)[1])
}));
assert.equal(heap.length, 6);
const parser = heap.find(x => x.functionName === 'blink::HTMLParserScheduler::scheduleForResume');
const commit = heap.find(x => x.functionName === 'content::WebPageImpl::setNeedsCommitAndNotLayout');
assert(parser && commit); assert.equal(parser.priority, 100); assert.equal(commit.priority, 200);
assert(clock - parser.nextFireTime > 10); assert(commit.nextFireTime < clock);
for (const name of ['dll-debug-directory.txt', 'pdb-summary.txt']) {
  const text = fs.readFileSync(path.join(root, name), 'utf8');
  assert.match(text, /D47D4204-C2AE-4E48-A148-D7B5140B01F9/); assert.match(text, /Age: 4/);
}
const review = {
  reviewedAt: new Date().toISOString(), freezeSha256, runName,
  originalAnimationUnchanged: true, nativeExitCode: 1, nativeRecords: events.length,
  failure: failed[0], final, debuggerExitCode: 0,
  pdbGuid: freeze.pdb.guid, pdbAge: freeze.pdb.age, privateSymbolsLoaded: true,
  previousFrameTime: clock, heap, parserOverdueSecondsAtPreviousFrame: clock - parser.nextFireTime,
  inputUnchanged: true, externalLoad: false,
  conclusion: 'Matching-symbol main-thread snapshot confirms overdue loading work with ready higher-priority commit work; original animated startup still fails. Real rebuilt-kernel correction remains unverified.',
  performanceEvidence: false, productionPass: false
};
fs.writeFileSync(path.join(root, 'review.json'), id.encoded(review), { flag: 'wx' });
const target = path.join(repo, 'verification/production-validation/preview11-renewal-scheduler-stack-2026-09-26');
assert(!fs.existsSync(target)); const files = [];
for (const file of id.walk(root, '', (_, leaf) => ['runtimes', 'runtime'].includes(leaf))) {
  const originalPath = path.join(root, file), bytes = fs.readFileSync(originalPath), relative = 'raw/' + file;
  const dest = path.join(target, relative); fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.writeFileSync(dest, bytes, { flag: 'wx' });
  files.push({ path: relative, bytes: bytes.length, sha256: id.hash(bytes), original: originalPath });
}
files.sort((a, b) => a.path.localeCompare(b.path));
fs.writeFileSync(path.join(target, 'raw-manifest.json'), id.encoded({ schema: 'soui.raw-evidence/v1', files }), { flag: 'wx' });
console.log(id.encoded({ files: files.length, review }));
