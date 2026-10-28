'use strict';
// File/JSON/archive analysis only. This program never starts a fixture, SDK,
// product, launcher or virtual machine. Raw capture bytes are never normalized.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');

const ORDINARY = [
  'scheduler-new-bad-alloc', 'event-null', 'native-launch-zero', 'registry-reserve-bad-alloc',
  'startup-tls-failure', 'ready-acquire-visibility', 'native-exit-delayed', 'normal-worker-close',
  'concurrent-registry-close', 'main-thread-compatibility', 'main-scheduler-task-budget',
  'main-scheduler-clock-budget', 'worker-scheduler-no-count-cap', 'worker-scheduler-clock-budget',
  'main-scheduler-shutdown-drain', 'main-scheduler-pre-shutdown-drain'
];
const MATRIX = [
  ['red-x86', 'main-scheduler-task-budget', 3],
  ['red-x86', 'main-scheduler-clock-budget', 3],
  ['first1-x86', 'main-scheduler-pre-shutdown-drain', 3],
  ...['green-x86', 'green-x64'].flatMap(bucket => ORDINARY.map(name => [bucket, name, 0]))
];
const CAPTURE = {
  bytes: 130475,
  sha256: '1e1fea0b040acce235ccd4b3d1d09fbfe0c74656731e622efc37ed6802c2a8e9',
  declaredPayloadBytes: 130368,
  totalChildElapsedMs: 11359
};
const EXTERNAL = {
  'before.json': {bytes: 279, sha256: 'e4bb3170d726d94952eeb69c3f94341b67b83c7bc7c58f0b26554f0b4ebc69bd'},
  'child-outcomes.json': {bytes: 17144, sha256: '84ff1a9532b889737704d0e2e0b42ac1ac793351c945378e5022b4ff98016d55'},
  'result.png': {bytes: 77460, sha256: '3c32d9759959b0150474b50a27c3c5ee52a9c520b413da72fbbe1b595bb458db'}
};
const ISO = {bytes: 8192000, sha256: 'e19065c3be66d416a891cac6601b61119b802eff9626db7e5300590fe5c371d0'};
const sha = buffer => crypto.createHash('sha256').update(buffer).digest('hex');
const identity = (buffer, artifactLabel, storage) => ({artifactLabel, storage, bytes: buffer.length, sha256: sha(buffer)});
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
function pin(buffer, expected, label) {
  assert.equal(buffer.length, expected.bytes, `${label}: byte count`);
  assert.equal(sha(buffer), expected.sha256, `${label}: SHA-256`);
}
function integer(value, label) { assert.ok(Number.isSafeInteger(value) && value >= 0, label); }
function uniquePhase(item, phase) {
  const notes = item.records.filter(record => record.record === 'native-observation' && record.phase === phase);
  assert.equal(notes.length, 1, `${item.bucket}/${item.name}: exactly one ${phase}`);
  integer(notes[0].value, phase);
  return notes[0].value;
}
function analyzeCapture(raw, requirePinnedCapture = true) {
  if (requirePinnedCapture) pin(raw, CAPTURE, 'raw serial');
  const prefix = Buffer.from('\r\n{"record":"scheduler-serial-begin"}\r\n');
  const suffix = Buffer.from('\r\n{"record":"scheduler-serial-end","bytes":130368,"complete":true}\r\n');
  assert.ok(raw.subarray(0, prefix.length).equals(prefix), 'exact serial prefix');
  assert.ok(raw.subarray(raw.length - suffix.length).equals(suffix), 'exact complete serial suffix');
  const payload = raw.subarray(prefix.length, raw.length - suffix.length);
  assert.equal(payload.length, CAPTURE.declaredPayloadBytes, 'declared payload byte boundary');
  assert.ok(payload.subarray(payload.length - 2).equals(Buffer.from('\r\n')), 'complete payload terminal CRLF');
  const text = payload.toString('latin1');
  assert.ok(!text.includes('\0'), 'capture contains no NUL');
  // Splitting for JSON interpretation leaves the stored capture and payload
  // identities intact, including any repeated CR bytes in other captures.
  const lines = text.split(/\r*\n/);
  assert.equal(lines.pop(), '', 'no trailing partial payload record');
  const records = lines.map((line, index) => {
    assert.ok(line.length > 0, `empty payload line ${index + 1}`);
    const record = JSON.parse(line);
    assert.ok(record && typeof record === 'object' && !Array.isArray(record), 'object record');
    return record;
  });
  assert.equal(records.filter(record => record.record === 'scheduler-launcher-begin').length, 1);
  assert.equal(records.filter(record => record.record === 'scheduler-launcher-end').length, 1);
  assert.ok(!records.some(record => String(record.record || '').startsWith('scheduler-serial-')), 'one outer serial envelope');
  assert.deepEqual(records[0], {record:'scheduler-launcher-begin', schema:'scheduler-guest-regression/3',
    plannedOutcomes:35, childBoundMs:15000, performanceMeasurement:false});
  const cases = [];
  let active = null;
  for (const record of records.slice(1, -1)) {
    if (record.record === 'scheduler-case-begin') {
      assert.equal(active, null, 'no overlapping cases');
      const expected = MATRIX[cases.length];
      assert.ok(expected, 'no extra case');
      assert.deepEqual([record.bucket, record.case, record.expectedExit], expected, 'fixed unique ordered case matrix');
      assert.equal(record.boundMs, 15000);
      active = {bucket:record.bucket, name:record.case, expectedExit:record.expectedExit, records:[], summaries:[]};
    } else if (record.record === 'scheduler-case-end') {
      assert.ok(active, 'case end requires a begin');
      assert.deepEqual([record.bucket, record.case, record.expectedExit], [active.bucket, active.name, active.expectedExit]);
      for (const field of ['created','assignedBeforeResume','resumed','exitObserved','jobDrained','jobHandleClosed','passed'])
        assert.equal(record[field], true, `${record.bucket}/${record.case}: ${field}`);
      for (const field of ['timeout','forcedCleanup','cleanupSucceeded']) assert.equal(record[field], false, field);
      assert.equal(record.waitResult, 0, 'natural root process signal');
      assert.equal(record.error, 0);
      assert.equal(record.actualExit, active.expectedExit, 'expected observed process exit');
      integer(record.elapsedMs, 'case elapsed');
      assert.ok(record.elapsedMs <= 15000, 'case deadline');
      assert.equal(active.summaries.length, 1, 'exactly one actual child contract summary');
      const summary = active.summaries[0];
      assert.equal(summary.case, active.name);
      assert.equal(summary.submode, 'wait');
      assert.equal(summary.variant, 'after'); // Existing fixture spelling; bucket binds source variant.
      assert.equal(summary.contractPassed, active.expectedExit === 0);
      assert.equal(summary.reason, active.expectedExit === 0 ? 'actual-source-contract-satisfied' : 'actual-source-contract-failed');
      assert.equal(summary.expectedFailFast, false);
      assert.equal(summary.qualificationPassed, false);
      assert.equal(summary.constructorOomClosed, false);
      assert.equal(summary.registryCount, 0);
      assert.equal(summary.registrySize, 0);
      integer(summary.pid, 'child pid');
      assert.ok(summary.pid > 0);
      for (const child of active.records) {
        if ('case' in child) assert.equal(child.case, active.name, 'child case identity');
        if ('pid' in child) assert.equal(child.pid, summary.pid, 'child PID identity');
      }
      active.summary = summary;
      active.outcome = record;
      cases.push(active);
      active = null;
    } else {
      assert.ok(active, 'child record requires active case');
      const summary = record.schema === 'worker-lifetime-source-run/1';
      assert.ok(summary || ['native-observation','task-receipt','ready-handoff','delayed-native-exit'].includes(record.record), 'known child record type');
      active.records.push(record);
      if (summary) active.summaries.push(record);
    }
  }
  assert.equal(active, null, 'no unfinished case');
  assert.equal(cases.length, 35);
  assert.equal(new Set(cases.map(item => `${item.bucket}/${item.name}`)).size, 35);
  assert.deepEqual(records.at(-1), {record:'scheduler-launcher-end', outcomes:35, passed:35, failed:0, logHealthy:true, allExpected:true});
  const get = (bucket, name) => {
    const found = cases.filter(item => item.bucket === bucket && item.name === name);
    assert.equal(found.length, 1);
    return found[0];
  };
  const originalCount = get('red-x86', 'main-scheduler-task-budget');
  const originalClock = get('red-x86', 'main-scheduler-clock-budget');
  const first1 = get('first1-x86', 'main-scheduler-pre-shutdown-drain');
  assert.equal(uniquePhase(originalCount, 'first-timer-ready-tasks'), 96, 'original loop consumes count backlog');
  assert.equal(uniquePhase(originalCount, 'native-probe-ready-tasks'), 0, 'no native probe turn before original count backlog finishes');
  const originalClockTasks = uniquePhase(originalClock, 'first-timer-ready-tasks');
  assert.equal(originalClockTasks, 50, 'actual captured original clock slice');
  assert.equal(uniquePhase(originalClock, 'native-probe-ready-tasks'), originalClockTasks);
  assert.equal(uniquePhase(first1, 'pre-shutdown-drained-ready-tasks'), 32, 'initial patch truncates pre-shutdown drain');
  const greenNotes = [];
  for (const bucket of ['green-x86','green-x64']) {
    const count = get(bucket, 'main-scheduler-task-budget');
    const clock = get(bucket, 'main-scheduler-clock-budget');
    assert.equal(uniquePhase(count, 'first-timer-ready-tasks'), 32);
    assert.equal(uniquePhase(count, 'native-probe-ready-tasks'), 32);
    const clockTasks = uniquePhase(clock, 'first-timer-ready-tasks');
    assert.ok(clockTasks >= 2 && clockTasks <= 3, 'strict 2 ms simulated clock budget');
    assert.equal(uniquePhase(clock, 'native-probe-ready-tasks'), clockTasks);
    const workerCount = uniquePhase(get(bucket, 'worker-scheduler-no-count-cap'), 'worker-first-yield-ready-tasks');
    const workerClock = uniquePhase(get(bucket, 'worker-scheduler-clock-budget'), 'worker-first-yield-ready-tasks');
    const shutdown = uniquePhase(get(bucket, 'main-scheduler-shutdown-drain'), 'shutdown-drained-ready-tasks');
    const preShutdown = uniquePhase(get(bucket, 'main-scheduler-pre-shutdown-drain'), 'pre-shutdown-drained-ready-tasks');
    assert.equal(workerCount, 0, 'no intermediate worker wait under fixed clock');
    assert.equal(workerClock, 50, 'original worker clock budget preserved');
    assert.equal(shutdown, 96);
    assert.equal(preShutdown, 96);
    greenNotes.push({bucket, firstCountSliceTasks:32, nativeCountProbeAtTasks:32,
      firstClockSliceTasks:clockTasks, nativeClockProbeAtTasks:clockTasks, workerFirstCountYieldTasks:workerCount,
      workerFirstClockYieldTasks:workerClock, finalShutdownDrainTasks:shutdown, preShutdownDrainTasks:preShutdown});
  }
  const elapsed = cases.reduce((total, item) => total + item.outcome.elapsedMs, 0);
  assert.equal(elapsed, CAPTURE.totalChildElapsedMs);
  return {rawIdentity:identity(raw, 'raw/scheduler-guest-serial.bin', 'repository'),
    envelope:{prefixBytes:prefix.length, payloadStartByte:prefix.length,
      payloadEndByteExclusive:raw.length-suffix.length, suffixBytes:suffix.length,
      declaredPayloadBytes:payload.length, payloadSha256:sha(payload), complete:true,
      rawCrCount:(raw.toString('latin1').match(/\r/g)||[]).length,
      rawLfCount:(raw.toString('latin1').match(/\n/g)||[]).length,
      rawCrCrLfCount:(raw.toString('latin1').match(/\r\r\n/g)||[]).length, rawBytesPreserved:true},
    records:records.length, cases, totalChildElapsedMs:elapsed,
    causalEvidence:{originalRed:{countFirstSliceTasks:96, countNativeProbeAtTasks:0,
      clockFirstSliceTasks:originalClockTasks, clockNativeProbeAtTasks:originalClockTasks},
      first1Red:{preShutdownDrainTasks:32}, finalGreen:greenNotes}};
}
function verifyExternal(options, analysis, builds) {
  const required = ['external-run','bundle','iso'];
  for (const key of required) assert.ok(options[key], `--${key} required when writing a report`);
  const external = [];
  for (const [name, expected] of Object.entries(EXTERNAL)) {
    const bytes = fs.readFileSync(path.join(options['external-run'], name));
    pin(bytes, expected, name);
    external.push(identity(bytes, `guest-regression-run-1/${name}`, 'external-held'));
  }
  const before = readJson(path.join(options['external-run'], 'before.json'));
  assert.equal(before.serialBytesBefore, 1958841, 'serial append extraction offset');
  assert.deepEqual(readJson(path.join(options['external-run'], 'child-outcomes.json')),
    analysis.cases.map(item => item.outcome), 'independent child outcome extraction agrees with raw serial');
  const manifestBytes = fs.readFileSync(path.join(options.bundle, 'artifact-manifest.json'));
  pin(manifestBytes, builds.sealedBundleManifest, 'sealed bundle manifest');
  const manifest = JSON.parse(manifestBytes.toString('utf8').replace(/^\uFEFF/, ''));
  assert.equal(manifest.schema, 'scheduler-guest-bundle/3');
  assert.equal(manifest.outcomes, 35);
  for (const file of manifest.files) pin(fs.readFileSync(path.join(options.bundle, file.path)), file, file.path);
  const isoBytes = fs.readFileSync(options.iso);
  pin(isoBytes, ISO, 'sealed ISO');
  const isoEntries = [];
  const entries = [builds.sealedBundleManifest, ...builds.artifacts];
  for (const expected of entries) {
    assert.ok(expected.artifactLabel.startsWith('guest-regression-3/'));
    const name = expected.artifactLabel.slice('guest-regression-3/'.length);
    const extraction = spawnSync(options.sevenzip || '7z', ['x','-so',options.iso,name],
      {encoding:null, maxBuffer:4*1024*1024, windowsHide:true});
    if (extraction.error) throw extraction.error;
    assert.equal(extraction.status, 0, `read-only ISO entry extraction: ${name}`);
    pin(extraction.stdout, expected, `ISO entry ${name}`);
    isoEntries.push({...identity(extraction.stdout, `guest-regression-3.iso/${name}`, 'external-held'), matchesBuildArtifact:expected.artifactLabel});
  }
  return {external, serialAppendStartByte:before.serialBytesBefore,
    iso:identity(isoBytes, 'guest-regression-3.iso', 'external-held'), isoEntries,
    isoEntryVerificationMethod:'7z x -so: archive bytes read only; no extracted program execution',
    bundleManifest:identity(manifestBytes, 'guest-regression-3/artifact-manifest.json', 'external-held')};
}
function main() {
  const args = process.argv.slice(2), options = {};
  const allowed = new Set(['check-only','self-test','raw','builds','out','external-run','bundle','iso','sevenzip']);
  for (let i=0; i<args.length; ++i) {
    const key = args[i];
    assert.ok(key.startsWith('--'), 'named arguments only');
    assert.ok(allowed.has(key.slice(2)), `unknown option ${key}`);
    assert.ok(!(key.slice(2) in options), `duplicate option ${key}`);
    if (['--check-only','--self-test'].includes(key)) options[key.slice(2)] = true;
    else { assert.ok(i+1<args.length, `value missing for ${key}`); options[key.slice(2)] = args[++i]; }
  }
  const rawPath = options.raw || path.join(__dirname, 'raw/scheduler-guest-serial.bin');
  const buildsPath = options.builds || path.join(__dirname, 'fixture-builds.json');
  const raw = fs.readFileSync(rawPath), buildsBytes = fs.readFileSync(buildsPath);
  const builds = JSON.parse(buildsBytes.toString('utf8').replace(/^\uFEFF/, ''));
  assert.equal(builds.evidenceScope, 'source-and-build-only');
  assert.equal(builds.artifacts.length, 5);
  assert.equal(builds.fixturePayloadEquality.verified, true);
  assert.equal(builds.fixturePayloadEquality.payloadCount, 22);
  const analysis = analyzeCapture(raw);
  if (options['self-test']) {
    const mutations = [
      ['original causal count', '"phase":"first-timer-ready-tasks","value":96', '"phase":"first-timer-ready-tasks","value":95'],
      ['case matrix/order', '"bucket":"green-x64","case":"scheduler-new-bad-alloc"', '"bucket":"green-x86","case":"scheduler-new-bad-alloc"'],
      ['job drain', '"jobDrained":true', '"jobDrained":null'],
      ['expected exit', '"actualExit":3', '"actualExit":0'],
      ['child contract reason', 'actual-source-contract-failed', 'actual-source-contract-broken']
    ];
    for (const [name, from, to] of mutations) {
      const text = raw.toString('latin1');
      assert.ok(text.includes(from), name);
      const changed = Buffer.from(text.replace(from,to), 'latin1');
      assert.equal(changed.length, raw.length, `${name}: preserve byte boundaries`);
      assert.throws(() => analyzeCapture(changed,false), undefined, name);
    }
    assert.throws(() => analyzeCapture(raw.subarray(0,raw.length-1),false), undefined, 'truncated envelope');
  }
  if (options['check-only'] || !options.out) {
    process.stdout.write(JSON.stringify({validated:true, outcomes:analysis.cases.length,
      totalChildElapsedMs:analysis.totalChildElapsedMs, causalEvidence:analysis.causalEvidence,
      semanticMutationChecks:options['self-test'] ? 6 : 0})+'\n');
    return;
  }
  const external = verifyExternal(options,analysis,builds);
  const report = {schema:'soui-scheduler-guest-results/1', scope:'isolated scheduler fixture correctness',
    runtimeExecuted:true, guestChildCasesValidated:35, expectedOriginalFailures:2, expectedFirst1LifecycleFailure:1,
    finalGreenPassingCases:32, totalChildElapsedMs:analysis.totalChildElapsedMs,
    elapsedMeaning:'Sum of launcher per-child elapsed values; not an input-latency or performance measurement',
    analysisTool:identity(fs.readFileSync(__filename), 'analyze-scheduler-run.cjs', 'repository'),
    buildEvidence:identity(buildsBytes,'fixture-builds.json','repository'), rawSerial:analysis.rawIdentity,
    serialEnvelope:analysis.envelope, parsedPayloadRecordCount:analysis.records,
    analyzerSelfTest:{executed:Boolean(options['self-test']),mutationRejectionsVerified:options['self-test'] ? 6 : 0},
    externalEvidence:external,
    strictChecks:{oneSerialEnvelope:true, oneLauncherBeginAndEnd:true, fixedUniqueOrderedCaseMatrix:true,
      oneActualChildSummaryPerCase:true, expectedObservedExits:true, assignedToJobBeforeResume:true,
      naturalChildProcessSignals:true, allJobsDrainedAndHandlesClosed:true, noTimeouts:true,
      noForcedCleanup:true, childSummaryIdentityAndContractMatched:true, causalNotesMatched:true,
      externalOutcomeExtractionMatchesRaw:true, sealedIsoSelectedEntriesMatchBuildPins:true},
    causalEvidence:analysis.causalEvidence,
    cases:analysis.cases.map(item=>({bucket:item.bucket,case:item.name,pid:item.summary.pid,
      executableArtifactLabel:`guest-regression-3/${item.bucket}/soui_worker_lifetime_regressions.exe`,
      expectedExit:item.expectedExit,actualExit:item.outcome.actualExit,contractPassed:item.summary.contractPassed,
      childReason:item.summary.reason,elapsedMs:item.outcome.elapsedMs,jobDrained:item.outcome.jobDrained,
      timeout:item.outcome.timeout,forcedCleanup:item.outcome.forcedCleanup,
      nativeNotes:item.records.filter(record=>record.record==='native-observation'
        && /first-timer-ready-tasks|native-probe-ready-tasks|worker-first-yield-ready-tasks|shutdown-drained-ready-tasks/.test(record.phase))
        .map(record=>({phase:record.phase,value:record.value}))})),
    limitations:{intentionalTerminateCasesNotRun:['native-wait-fails','worker-self-delete'],
      fullKernelBuildValidatedByThisReport:false,productInputLatencyValidated:false,
      synchronousMouseCallbackLatencyValidated:false,performanceMeasurement:false,
      launcherProcessExitObserved:false,screenshotKeptExternal:true,
      uiObservation:'External screenshot shows 35/35 expected and COM1 export success; raw records are the correctness evidence',
      fixtureContractScope:'Passing fixture contracts include retained-task exact-once completion, delayed-task eligibility and continued admission after pre-shutdown drain; no broader engine qualification'}};
  fs.writeFileSync(options.out,JSON.stringify(report,null,2)+'\n');
  process.stdout.write(JSON.stringify({output:path.basename(options.out),bytes:fs.statSync(options.out).size,
    sha256:sha(fs.readFileSync(options.out)),outcomes:35,totalChildElapsedMs:analysis.totalChildElapsedMs})+'\n');
}
if (require.main===module) {
  try { main(); } catch (error) { process.stderr.write(`Scheduler evidence validation failed: ${error.message}\n`); process.exitCode=1; }
}
module.exports={analyzeCapture};
