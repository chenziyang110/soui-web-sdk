'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {launchOwned} = require('../../tools/internal/owned-process.cjs');
const runner = process.env.SOUI_OWNED_PROCESS_RUNNER ||
  path.resolve(__dirname, '../../out/benchmark-protocol/Release/OwnedProcessRunner.exe');
const options = {skip: process.platform !== 'win32', timeout: 15000};
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

function fixture() {
  const base = path.resolve(__dirname, '../../out/owned-runner-client-tests');
  fs.mkdirSync(base, {recursive: true});
  const cwd = fs.mkdtempSync(path.join(base, 'attempt-'));
  return {runner, cwd, report: path.join(cwd, 'owned.ndjson'), timeoutMs: 5000};
}
function launch(t, args, extra = {}) {
  const config = {...fixture(), ...extra};
  const run = launchOwned(process.execPath, args, config);
  t.after(async () => { run.stop(); await run.finished; });
  return {run, config};
}

test('owned client reports the subject identity and binds tool and executable bytes', options, async t => {
  const {run, config} = launch(t, ['-e', "require('node:fs').writeFileSync('subject.pid',String(process.pid))"]);
  const ready = await run.ready;
  const result = await run.finished;
  assert.notEqual(ready.pid, run.child.pid);
  assert.equal(result.pid, ready.pid);
  assert.equal(Number(fs.readFileSync(path.join(config.cwd, 'subject.pid'), 'utf8')), ready.pid);
  assert.equal(result.creationFileTime, ready.creationFileTime);
  assert.equal(result.passed, true);
  assert.equal(result.closureConfirmed, true);
  assert.equal(result.exitCode, 0);
  assert.equal(result.supervisorExitCode, 0);
  assert.equal(result.reportSha256, sha(config.report));
  assert.equal(result.runnerSha256, sha(runner));
  assert.equal(result.executableSha256, sha(process.execPath));
  const context = JSON.parse(fs.readFileSync(config.report + '.context.json', 'utf8'));
  assert.equal(context.runnerSha256, result.runnerSha256);
  assert.equal(context.executableSha256, result.executableSha256);
});

test('owned client retains nonzero subject exit and confirmed cleanup separately', options, async t => {
  const {run} = launch(t, ['-e', 'process.exit(11)']);
  await run.ready;
  const result = await run.finished;
  assert.equal(result.exitCode, 11);
  assert.equal(result.supervisorExitCode, 11);
  assert.equal(result.passed, false);
  assert.equal(result.closureConfirmed, true);
});

test('owned client requests stop through its pipe and receives a failed terminal with closure', options, async t => {
  const {run} = launch(t, ['-e', 'setTimeout(()=>{},30000)']);
  await run.ready;
  assert.equal(run.stop(), true);
  assert.equal(run.stop(), false);
  const result = await run.finished;
  assert.equal(result.terminal.reason, 'supervisor-stop');
  assert.equal(result.stopRequested, true);
  assert.equal(result.exitCode, 124);
  assert.equal(result.supervisorExitCode, 124);
  assert.equal(result.passed, false);
  assert.equal(result.closureConfirmed, true);
  assert.equal(result.fallbackUsed, false);
});

test('owned client refuses missing tool and invalid bounds before launching a subject', options, () => {
  const config = fixture();
  assert.throws(() => launchOwned(process.execPath, [], {...config, timeoutMs: 0}), /Invalid timeoutMs/);
  assert.equal(fs.existsSync(config.report + '.context.json'), false);
  assert.throws(() => launchOwned(process.execPath, [], {...config, runner: path.join(config.cwd, 'missing.exe')}), /ENOENT/);
  assert.equal(fs.existsSync(config.report + '.context.json'), false);
});

test('test-only assignment denial cannot produce a ready subject or a client pass', options, async t => {
  const fixtureRunner = path.join(path.dirname(runner), 'OwnedProcessAssignmentFixture.exe');
  const {run} = launch(t, ['-e', 'process.exit(0)'], {runner: fixtureRunner});
  await assert.rejects(run.ready, /assignment-failed/);
  const result = await run.finished;
  assert.equal(result.pid, null);
  assert.equal(result.supervisorExitCode, 125);
  assert.equal(result.terminal.closureConfirmed, true);
  assert.equal(result.closureConfirmed, false);
  assert.equal(result.passed, false);
});
