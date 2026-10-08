'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const nativeRunner = process.env.SOUI_TAIL_RUNNER;
const fixture = process.env.SOUI_TAIL_FIXTURE;
const mutationRunner = process.env.SOUI_TAIL_MUTATION_RUNNER;
const apiRunner = process.env.SOUI_TAIL_API_RUNNER;
const bootstrapRunner = process.env.SOUI_TAIL_BOOTSTRAP_RUNNER;
const clientPath = process.env.SOUI_TAIL_BOOTSTRAP_CLIENT;
const enabled = process.platform === 'win32' && [nativeRunner, fixture, mutationRunner, apiRunner, bootstrapRunner, clientPath].every(Boolean);
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const rows = file => {
  if (!fs.existsSync(file)) return [];
  const text = fs.readFileSync(file, 'utf8');
  return text.slice(0, text.lastIndexOf('\n') + 1).split('\n').filter(Boolean).map(JSON.parse);
};
const pause = () => new Promise(resolve => setTimeout(resolve, 10));

function directory() {
  const parent = process.env.SOUI_TAIL_EVIDENCE_DIRECTORY || path.resolve(__dirname, '../../out/owned-tail-snapshot-tests');
  fs.mkdirSync(parent, {recursive: true});
  const current = fs.mkdtempSync(path.join(parent, 'attempt-'));
  fs.writeFileSync(path.join(current, 'inputs.json'), JSON.stringify({
    schema: 'soui.compiler-tail-test-inputs/v1', nativeRunner, nativeRunnerSha256: sha(nativeRunner),
    fixture, fixtureSha256: sha(fixture), mutationRunner, mutationRunnerSha256: sha(mutationRunner),
    apiRunner, apiRunnerSha256: sha(apiRunner),
    bootstrapRunner, bootstrapRunnerSha256: sha(bootstrapRunner), clientPath, clientSha256: sha(clientPath),
    testSha256: sha(__filename), noGui: true, actualCompilerTail: false,
  }, null, 2) + '\n', {flag: 'wx'});
  return current;
}
function copyPin(folder, name = 'pinned-helper.exe') {
  const destination = path.join(folder, name);
  fs.copyFileSync(fixture, destination, fs.constants.COPYFILE_EXCL);
  return destination;
}
function start(t, folder, name, executable, args, extraEnv = {}) {
  const {launchOwned} = require(clientPath);
  const owned = launchOwned(executable, args, {
    runner: bootstrapRunner, cwd: folder, report: path.join(folder, name + '-outer.ndjson'),
    timeoutMs: 15000, drainMs: 2500, stdout: 'pipe', stderr: 'pipe', env: {...process.env, ...extraEnv},
  });
  const stdout = [], stderr = [];
  owned.child.stdout.on('data', bytes => stdout.push(bytes));
  owned.child.stderr.on('data', bytes => stderr.push(bytes));
  let finished = false;
  const completion = owned.finished.then(result => {
    finished = true;
    fs.writeFileSync(path.join(folder, name + '-stdout.bin'), Buffer.concat(stdout));
    fs.writeFileSync(path.join(folder, name + '-stderr.bin'), Buffer.concat(stderr));
    fs.writeFileSync(path.join(folder, name + '-finished.json'), JSON.stringify(result, null, 2) + '\n');
    assert.equal(result.closureConfirmed, true, 'Outer held Job cleanup must be confirmed');
    assert.equal(result.reportSha256, sha(result.report));
    return result;
  });
  t.after(async () => { if (!finished) { owned.stop(); await completion; } });
  return {...owned, finished: completion};
}
function subject(t, folder, mode, helper, {runner = nativeRunner, pin = true, extraArgs = [], extraEnv = {}} = {}) {
  const report = path.join(folder, 'inner.ndjson');
  const nonce = crypto.randomBytes(16).toString('hex');
  const args = ['--timeout-ms', '8000', '--drain-ms', '100', '--report', report, '--cwd', folder];
  if (pin) args.push('--pin-tail-image', helper, '--pin-tail-sha256', sha(helper), '--snapshot-nonce', nonce);
  args.push(...extraArgs, '--', fixture, mode, folder);
  if (mode !== 'external') args.push(helper);
  const owned = start(t, folder, 'inner', runner, args, extraEnv);
  return {...owned, report, nonce};
}
async function waitFor(file, predicate = () => true) {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    if (fs.existsSync(file) && predicate(file)) return;
    await pause();
  }
  assert.fail('Fixture condition was not observed: ' + file);
}
function verifySequence(records, nonce) {
  const stream = records.filter(row => row.schema === 'soui.owned-tail-snapshot/v1');
  assert.ok(stream.length > 0);
  stream.forEach((row, index) => { assert.equal(row.nonce, nonce); assert.equal(row.sequence, index + 1); });
  return stream;
}
function verifyGenericFailure(result, records) {
  assert.equal(result.exitCode, 124);
  assert.equal(result.passed, false); // The outer root really returned 124.
  const terminal = records.at(-1);
  assert.equal(terminal.reason, 'owned-descendants-remain');
  assert.equal(terminal.rootExitCode, 0);
  assert.equal(terminal.passed, false);
  assert.equal(terminal.terminatedOwnedJob, true);
  assert.equal(terminal.closureConfirmed, true);
  assert.equal(terminal.activeProcesses, 0);
  return terminal;
}

test('a prelaunch exact disk pin proves one complete tail while generic supervision stays failed', {skip: !enabled}, async t => {
  const folder = directory(); const helper = copyPin(folder);
  const owned = subject(t, folder, 'single', helper);
  const result = await owned.finished; const records = rows(owned.report);
  assert.equal(records[0].event, 'start');
  assert.equal(records[0].schema, 'soui.owned-process-run/v1');
  assert.equal(records[0].snapshotNonce, owned.nonce);
  const stream = verifySequence(records, owned.nonce);
  const pin = stream.find(row => row.event === 'pin');
  assert.equal(pin.phase, 'prelaunch'); assert.equal(pin.sha256, sha(helper));
  for (const field of ['volumeSerialNumber', 'fileId', 'size', 'observedBeforeLaunchUtcFileTime', 'observedBeforeLaunchTickMs']) {
    assert.equal(typeof pin[field], 'string'); assert.match(pin[field], /^(0|[1-9]\d*)$/);
  }
  assert.match(pin.volumePath, /^\\\\\?\\Volume\{/);
  assert.equal(pin.diskBytesOnly, true);
  const terminal = verifyGenericFailure(result, records);
  assert.equal(terminal.tailEvidenceComplete, true);
  const snapshot = stream.find(row => row.sequence === terminal.preCleanupSnapshotSequence);
  const begin = stream.find(row => row.event === 'snapshot.begin' && row.attempt === snapshot.attempt);
  const members = stream.filter(row => row.event === 'snapshot.member' && row.attempt === snapshot.attempt);
  assert.equal(snapshot.event, 'snapshot.end'); assert.equal(snapshot.complete, true); assert.equal(snapshot.eligible, true);
  assert.equal(snapshot.members, 1); assert.equal(snapshot.listed, 1); assert.equal(snapshot.assigned, 1);
  assert.equal(snapshot.uniquePids, 1); assert.equal(begin.accountingBefore.activeProcesses, 1);
  assert.equal(snapshot.accountingAfter.activeProcesses, 1);
  assert.deepEqual(snapshot.pids, snapshot.secondPids);
  assert.equal(begin.accountingBefore.totalProcesses, snapshot.accountingAfter.totalProcesses);
  const actual = JSON.parse(fs.readFileSync(path.join(folder, 'child-0.json'), 'utf8'));
  assert.equal(members[0].pid, actual.pid); assert.equal(members[0].creationFileTime, actual.creationFileTime);
  assert.equal(members[0].index, 0); assert.equal(members[0].alive, true); assert.equal(members[0].inHeldJob, true);
  assert.equal(members[0].pinMatched, true);
  for (const field of ['canonicalPath', 'volumePath', 'volumeSerialNumber', 'fileId', 'size', 'sha256'])
    assert.equal(members[0].diskObject[field], pin[field]);
  const confirmations = stream.filter(row => row.event === 'cleanup.confirm');
  assert.equal(confirmations.length, 2);
  for (const confirmation of confirmations) {
    assert.equal(confirmation.confirmed, true); assert.equal(confirmation.allHeldSignaled, true);
    assert.equal(confirmation.accounting.activeProcesses, 0);
    assert.equal(confirmation.accounting.totalProcesses, begin.accountingBefore.totalProcesses);
  }
});

test('an unknown disk object with identical bytes stays in cleanup and cannot be a pinned tail', {skip: !enabled}, async t => {
  const actualFolder = directory(); const actualPin = copyPin(actualFolder); const actualUnknown = copyPin(actualFolder, 'unknown.exe');
  const report = path.join(actualFolder, 'inner.ndjson'); const nonce = crypto.randomBytes(16).toString('hex');
  const args = ['--timeout-ms', '8000', '--drain-ms', '100', '--report', report, '--cwd', actualFolder,
    '--pin-tail-image', actualPin, '--pin-tail-sha256', sha(actualPin), '--snapshot-nonce', nonce,
    '--', fixture, 'unknown', actualFolder, actualUnknown];
  const result = await start(t, actualFolder, 'inner', nativeRunner, args).finished;
  const records = rows(report); const terminal = verifyGenericFailure(result, records);
  assert.equal(sha(actualPin), sha(actualUnknown));
  const stream = verifySequence(records, nonce);
  const member = stream.find(row => row.event === 'snapshot.member');
  assert.equal(member.pinMatched, false); assert.equal(member.diskObject.sha256, sha(actualPin));
  assert.notEqual(member.diskObject.fileId, stream.find(row => row.event === 'pin').fileId);
  assert.equal(stream.find(row => row.event === 'snapshot.end').stickyFailure, true);
  assert.equal(terminal.tailEvidenceComplete, false); assert.equal(terminal.preCleanupSnapshotSequence, null);
});

for (const mode of ['multi', 'many']) test(`${mode} actual tails are fully listed and cannot qualify as one helper`, {skip: !enabled}, async t => {
  const folder = directory(); const helper = copyPin(folder);
  const owned = subject(t, folder, mode, helper); const result = await owned.finished;
  const records = rows(owned.report); const terminal = verifyGenericFailure(result, records);
  const stream = verifySequence(records, owned.nonce);
  const snapshot = stream.find(row => row.event === 'snapshot.end'); const expected = mode === 'multi' ? 2 : 9;
  assert.equal(snapshot.complete, true); assert.equal(snapshot.members, expected); assert.equal(snapshot.assigned, expected);
  assert.equal(snapshot.listed, expected); assert.equal(snapshot.uniquePids, expected);
  assert.equal(snapshot.eligible, false); assert.equal(terminal.tailEvidenceComplete, false);
  if (mode === 'many') assert.ok(stream.some(row => row.event === 'snapshot.list' && row.growth > 0));
});

test('the held pin denies write and rename until a normal root and Job have closed', {skip: !enabled}, async t => {
  const folder = directory(); const helper = copyPin(folder);
  const owned = subject(t, folder, 'external', helper);
  await waitFor(path.join(folder, 'external-ready'));
  await waitFor(owned.report, file => rows(file).some(row => row.event === 'pin'));
  assert.throws(() => { const file = fs.openSync(helper, 'r+'); fs.closeSync(file); }, /EPERM|EBUSY|EACCES/);
  assert.throws(() => fs.renameSync(helper, helper + '.renamed'), /EPERM|EBUSY|EACCES/);
  fs.writeFileSync(path.join(folder, 'external-finish'), 'finish\n', {flag: 'wx'});
  const result = await owned.finished; const records = rows(owned.report);
  assert.equal(result.passed, true);
  assert.equal(records[0].event, 'start'); assert.equal(records.at(-1).passed, true);
  assert.equal(records.at(-1).tailEvidenceComplete, false);
  const file = fs.openSync(helper, 'r+'); fs.closeSync(file);
  fs.renameSync(helper, helper + '.renamed');
});

test('test-only real extra assignment after the fixed snapshot invalidates cleanup evidence', {skip: !enabled}, async t => {
  const folder = directory(); const helper = copyPin(folder);
  const owned = subject(t, folder, 'single', helper, {runner: mutationRunner,
    extraEnv: {SOUI_TEST_TAIL_MUTATION_EXE: fixture}});
  const result = await owned.finished; const records = rows(owned.report); const terminal = verifyGenericFailure(result, records);
  const stream = verifySequence(records, owned.nonce);
  const decisive = stream.find(row => row.sequence === terminal.preCleanupSnapshotSequence);
  assert.equal(decisive.eligible, true);
  assert.equal(terminal.tailEvidenceComplete, false);
  for (const confirmation of stream.filter(row => row.event === 'cleanup.confirm')) {
    assert.equal(confirmation.confirmed, false);
    assert.equal(confirmation.accounting.totalProcesses, confirmation.fixedTotalProcesses + 1);
    assert.equal(confirmation.accounting.activeProcesses, 0);
    assert.equal(confirmation.allHeldSignaled, true);
  }
  assert.match(fs.readFileSync(path.join(folder, 'inner-stderr.bin'), 'utf8'), /TEST_ONLY_FIXED_TOTAL_MUTATION/);
});

test('a failed owned tail cleanup leaves a separately held unrelated process alive', {skip: !enabled}, async t => {
  const folder = directory(); const externalFolder = path.join(folder, 'unrelated'); fs.mkdirSync(externalFolder);
  const unrelated = start(t, externalFolder, 'unrelated', fixture, ['external', externalFolder]);
  const identity = await unrelated.ready; await waitFor(path.join(externalFolder, 'external-ready'));
  const helper = copyPin(folder); const owned = subject(t, folder, 'single', helper);
  verifyGenericFailure(await owned.finished, rows(owned.report));
  assert.equal(unrelated.child.exitCode, null); assert.doesNotThrow(() => process.kill(identity.pid, 0));
  fs.writeFileSync(path.join(externalFolder, 'external-finish'), 'finish\n', {flag: 'wx'});
  assert.equal((await unrelated.finished).passed, true);
});

test('hash mismatch, incomplete and duplicate opt-in arguments cannot launch a subject', {skip: !enabled}, async t => {
  for (const kind of ['hash-mismatch', 'missing-nonce', 'duplicate-image']) {
    const folder = directory(); const helper = copyPin(folder); const report = path.join(folder, 'inner.ndjson');
    const args = ['--timeout-ms', '8000', '--report', report, '--cwd', folder,
      '--pin-tail-image', helper, '--pin-tail-sha256', kind === 'hash-mismatch' ? '0'.repeat(64) : sha(helper)];
    if (kind !== 'missing-nonce') args.push('--snapshot-nonce', crypto.randomBytes(16).toString('hex'));
    if (kind === 'duplicate-image') args.push('--pin-tail-image', helper);
    args.push('--', fixture, 'external', folder);
    const result = await start(t, folder, 'inner', nativeRunner, args).finished;
    assert.equal(result.exitCode, 125); assert.equal(result.passed, false);
    assert.equal(fs.existsSync(path.join(folder, 'external-ready')), false);
    if (kind === 'hash-mismatch') {
      const records = rows(report); assert.equal(records[0].event, 'pin.error');
      assert.equal(records.at(-1).reason, 'pin-setup-failed'); assert.equal(records.at(-1).passed, false);
    } else assert.equal(fs.existsSync(report), false);
  }
});

test('prelaunch pin rejects a reparse directory, an existing writer and an oversized file', {skip: !enabled}, async t => {
  for (const kind of ['reparse-directory', 'existing-writer', 'oversized-file']) {
    const folder = directory(); let helper = copyPin(folder); let writing = null;
    if (kind === 'reparse-directory') {
      const actual = path.join(folder, 'actual'); fs.mkdirSync(actual);
      fs.copyFileSync(fixture, path.join(actual, 'helper.exe'));
      const junction = path.join(folder, 'junction'); fs.symlinkSync(actual, junction, 'junction');
      helper = path.join(junction, 'helper.exe');
    } else if (kind === 'existing-writer') {
      writing = fs.openSync(helper, 'r+');
    } else {
      const file = fs.openSync(helper, 'r+'); fs.ftruncateSync(file, 64 * 1024 * 1024 + 1); fs.closeSync(file);
    }
    const report = path.join(folder, 'inner.ndjson');
    const args = ['--timeout-ms', '8000', '--report', report, '--cwd', folder, '--pin-tail-image', helper,
      '--pin-tail-sha256', kind === 'oversized-file' ? '0'.repeat(64) : sha(helper),
      '--snapshot-nonce', crypto.randomBytes(16).toString('hex'), '--', fixture, 'external', folder];
    let result;
    try { result = await start(t, folder, 'inner', nativeRunner, args).finished; }
    finally { if (writing !== null) fs.closeSync(writing); }
    assert.equal(result.exitCode, 125); assert.equal(result.passed, false);
    assert.equal(fs.existsSync(path.join(folder, 'external-ready')), false);
    const records = rows(report);
    assert.equal(records[0].event, 'pin.error'); assert.equal(records.at(-1).reason, 'pin-setup-failed');
    assert.match(records[0].error, kind === 'reparse-directory' ? /reparse-directory-rejected/ :
      kind === 'existing-writer' ? /open-pin-file:32/ : /file-size-out-of-range/);
  }
});

test('the default generic path keeps v1 first start and never emits tail evidence', {skip: !enabled}, async t => {
  const folder = directory(); const helper = copyPin(folder);
  const owned = subject(t, folder, 'single', helper, {pin: false});
  const result = await owned.finished; const records = rows(owned.report); const terminal = verifyGenericFailure(result, records);
  assert.equal(records[0].schema, 'soui.owned-process-run/v1'); assert.equal(records[0].event, 'start');
  assert.equal(records.some(row => row.schema === 'soui.owned-tail-snapshot/v1'), false);
  assert.equal(Object.hasOwn(terminal, 'tailEvidenceComplete'), false);
  assert.equal(Object.hasOwn(terminal, 'preCleanupSnapshotSequence'), false);
});

for (const mode of ['success-truncated', 'retry-count', 'duplicate-pid', 'pid-hole', 'image-permission'])
  test(`test-only ${mode} diagnostic response preserves expansion, retry or sticky refusal`, {skip: !enabled}, async t => {
    const folder = directory(); const helper = copyPin(folder);
    const owned = subject(t, folder, 'single', helper, {runner: apiRunner, extraEnv: {SOUI_TEST_TAIL_API_MODE: mode}});
    const result = await owned.finished; const records = rows(owned.report); const terminal = verifyGenericFailure(result, records);
    const stream = verifySequence(records, owned.nonce);
    const snapshots = stream.filter(row => row.event === 'snapshot.end');
    if (mode === 'success-truncated') {
      assert.equal(terminal.tailEvidenceComplete, true);
      assert.ok(stream.some(row => row.event === 'snapshot.list' && row.win32Error === 0 && row.listed < row.assigned));
      assert.ok(stream.some(row => row.event === 'snapshot.list' && row.growth > 0));
    } else if (mode === 'retry-count') {
      assert.equal(terminal.tailEvidenceComplete, true); assert.equal(snapshots.length, 2);
      assert.equal(snapshots[0].complete, false); assert.equal(snapshots[0].stickyFailure, false);
      assert.equal(snapshots[1].complete, true); assert.equal(snapshots[1].attempt, 2);
    } else {
      assert.equal(terminal.tailEvidenceComplete, false); assert.equal(snapshots.length, 1);
      assert.equal(snapshots[0].stickyFailure, true); assert.equal(snapshots[0].complete, false);
      assert.match(snapshots[0].error, mode === 'image-permission' ? /read-tail-image:5/ : /duplicate-or-invalid-pid/);
    }
  });
