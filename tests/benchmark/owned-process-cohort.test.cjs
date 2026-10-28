'use strict';

// Opt-in integration fixtures for a separately built new runner. No stock EXE fallback.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const runner = process.env.SOUI_OWNED_COHORT_RUNNER;
const options = {skip: process.platform !== 'win32' || !runner, timeout: 15000};
const frozenRunner = 'df0afe34ed4eda497bfd644b01fe5e0f1e817876f744cf7f7c6641c42a68fdb5';

function fixture() {
  assert(path.isAbsolute(runner));
  const digest = crypto.createHash('sha256').update(fs.readFileSync(runner)).digest('hex');
  assert.notEqual(digest, frozenRunner, 'Select the separately built cohort runner; never replace the frozen EXE');
  const root = path.resolve(__dirname, '../../out/owned-cohort-tests');
  fs.mkdirSync(root, {recursive: true});
  return fs.mkdtempSync(path.join(root, 'attempt-'));
}
function run(directory, extra, script, timeout = 5000) {
  const report = path.join(directory, 'owned.ndjson');
  const result = spawnSync(runner, ['--timeout-ms', String(timeout), '--drain-ms', '2000',
    '--report', report, '--cwd', directory, ...extra, '--', process.execPath, '-e', script],
  {windowsHide: true, encoding: 'utf8', timeout: timeout + 7000});
  assert.ifError(result.error);
  const records = fs.existsSync(report) ? fs.readFileSync(report, 'utf8').trim().split('\n').filter(Boolean).map(JSON.parse) : [];
  return {result, records, report};
}
function canonical(value) { assert.equal(typeof value, 'string'); assert.match(value, /^(?:0|[1-9][0-9]*)$/); return BigInt(value); }
function cohort(row, root) {
  assert.equal(row.schema, 'soui.owned-job-cohort/v1');
  assert.equal(row.authority, 'observation-only');
  assert.equal(row.subjectPid, root.pid);
  assert.equal(row.subjectCreationFileTime, root.creationFileTime);
  assert.equal(row.outsideJobResidual, null); assert.equal(row.borrowedResourceResidual, null);
  assert.equal(Object.hasOwn(row, 'childResidual'), false);
  canonical(row.sampleId);
  assert(canonical(row.queryEndTickMs) >= canonical(row.queryBeginTickMs));
  assert(canonical(row.queryEndElapsedMs) >= canonical(row.queryBeginElapsedMs));
  assert(row.members.length <= 256);
  if (!row.complete) { assert.equal(row.status, 'unknown'); return; }
  assert.equal(row.status, 'complete'); assert.equal(row.reason, null);
  assert.equal(row.accountingBefore.observed, true); assert.equal(row.accountingAfter.observed, true);
  for (const key of ['activeProcesses', 'totalProcesses', 'limitTerminatedProcesses'])
    assert.equal(row.accountingBefore[key], row.accountingAfter[key]);
  assert.equal(row.assignedProcesses, row.returnedProcesses);
  assert.equal(row.members.length, row.assignedProcesses);
  assert.equal(row.members.length, row.accountingAfter.activeProcesses);
  const ids = new Set();
  for (const member of row.members) {
    assert(member.pid > 0); assert(!ids.has(member.pid)); ids.add(member.pid);
    assert(canonical(member.creationFileTime) > 0n);
    assert.equal(member.inJob, true); assert.equal(member.live, true);
    assert.equal(member.recheckInJob, true); assert.equal(member.recheckLive, true);
  }
}

test('default runner mode adds no cohort and retains original natural Job closure', options, () => {
  const directory = fixture();
  const {result, records} = run(directory, [], 'setTimeout(()=>{},160)');
  assert.equal(result.status, 0, result.stderr);
  assert.equal(records.some(row => row.event === 'owned-cohort'), false);
  assert.equal(records[0].jobAssignedBeforeResume, true); assert.equal(records[0].breakawayAllowed, false);
  assert.equal(records.at(-1).reason, 'completed'); assert.equal(records.at(-1).terminatedOwnedJob, false);
  assert.equal(records.at(-1).activeProcesses, 0);
});

test('opt-in cohorts keep real subject and child identities and the original query clock', options, () => {
  const directory = fixture();
  const script = "const fs=require('node:fs');const c=require('node:child_process').spawn(process.execPath,['-e','setTimeout(()=>{},650)'],{windowsHide:true,stdio:'ignore'});fs.writeFileSync('fixture-identities.json',JSON.stringify({root:process.pid,child:c.pid}));setTimeout(()=>{},1000);";
  const {result, records} = run(directory, ['--cohort-interval-ms', '20'], script);
  assert.equal(result.status, 0, result.stderr);
  const start = records[0], ids = JSON.parse(fs.readFileSync(path.join(directory, 'fixture-identities.json')));
  assert.equal(start.pid, ids.root);
  const observations = records.filter(row => row.event === 'owned-cohort'); assert(observations.length >= 2);
  let previous = 0n;
  for (const row of observations) { cohort(row, start); assert(canonical(row.sampleId) > previous); previous = canonical(row.sampleId); }
  assert(observations.some(row => row.complete && row.members.some(member => member.pid === ids.root) &&
    row.members.some(member => member.pid === ids.child)), 'A real live child must be retained; no conhost/other member is filtered');
  const terminal = records.at(-1); assert.equal(terminal.reason, 'completed');
  assert.equal(terminal.activeProcesses, 0); assert.equal(terminal.terminatedOwnedJob, false);
});

test('cohort option rejects invalid or duplicate intervals before target creation', options, () => {
  for (const extra of [['--cohort-interval-ms','0'],['--cohort-interval-ms','19'],
    ['--cohort-interval-ms','1001'],['--cohort-interval-ms','-1'],['--cohort-interval-ms','20x'],
    ['--cohort-interval-ms','20','--cohort-interval-ms','20']]) {
    const directory = fixture(), {result, records, report} = run(directory, extra,
      "require('node:fs').writeFileSync('must-not-start','bad')");
    assert.equal(result.status, 125); assert.equal(records.length, 0);
    assert.equal(fs.existsSync(report), false); assert.equal(fs.existsSync(path.join(directory,'must-not-start')), false);
  }
});

test('opt-in observation does not turn original timeout cleanup into a pass', options, () => {
  const directory = fixture(), {result, records} = run(directory, ['--cohort-interval-ms','20'],
    'setTimeout(()=>{},10000)', 300);
  assert.equal(result.status, 124, result.stderr);
  assert(records.some(row => row.event === 'owned-cohort'));
  const terminal = records.at(-1); assert.equal(terminal.reason, 'timeout');
  assert.equal(terminal.passed, false); assert.equal(terminal.terminatedOwnedJob, true);
  assert.equal(terminal.activeProcesses, 0); assert.equal(terminal.closureConfirmed, true);
});
