'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {spawn, spawnSync} = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const {performance} = require('node:perf_hooks');

const runner = process.env.SOUI_OWNED_PROCESS_RUNNER ||
  path.resolve(__dirname, '../../out/benchmark-protocol/Release/OwnedProcessRunner.exe');
const supported = process.platform === 'win32';

function assertOwnedClosure(records, expectedNodes) {
  const terminal = records.at(-1);
  const nodePids = new Set(records.filter(row => row.event === 'owned-member' &&
    typeof row.image === 'string' && path.basename(row.image).toLowerCase() === 'node.exe').map(row => row.pid));
  nodePids.add(records[0].pid); // Its held creation identity is recorded before resume.
  assert.equal(nodePids.size, expectedNodes);
  // Windows may also create owned console hosts. Keep their full count and
  // require the entire Job to close; no helper is excluded from cleanup.
  assert.ok(terminal.totalProcesses >= expectedNodes);
  assert.equal(terminal.activeProcesses, 0);
  assert.equal(terminal.closureConfirmed, true);
  assert.equal(typeof terminal.limitTerminatedProcesses, 'number');
  assert.equal(Object.hasOwn(terminal, 'terminatedProcesses'), false);
}

function fixture(t) {
  assert.ok(fs.existsSync(runner), 'Build benchmarks/protocol or set SOUI_OWNED_PROCESS_RUNNER');
  const output = path.resolve(__dirname, '../../out/owned-runner-tests');
  fs.mkdirSync(output, {recursive: true});
  // Preserve raw reports for both successful and failed native runs.
  const directory = fs.mkdtempSync(path.join(output, 'attempt-进程-'));
  return directory;
}

function run(directory, args, {timeout = 5000, drain = 500, report = 'run.ndjson'} = {}) {
  const reportPath = path.join(directory, report);
  const result = spawnSync(runner, ['--timeout-ms', String(timeout), '--drain-ms', String(drain),
    '--report', reportPath, '--cwd', directory, '--', process.execPath, ...args],
    {windowsHide: true, encoding: 'utf8', timeout: timeout + drain + 8000});
  assert.ifError(result.error);
  const records = fs.existsSync(reportPath) ? fs.readFileSync(reportPath, 'utf8').trim().split('\n')
    .filter(Boolean).map(row => JSON.parse(row)) : [];
  return {result, records, terminal: records.at(-1)};
}

test('owned runner preserves Unicode, spaces, quotes, empty args and trailing slashes',
  {skip: !supported}, t => {
    const directory = fixture(t);
    const script = path.join(directory, '参数 程序.cjs');
    fs.writeFileSync(script, `require('node:fs').writeFileSync('args.json',JSON.stringify(process.argv.slice(2)));`);
    const args = ['', 'value with spaces', '中文', 'say "hello"', 'C:\\path with spaces\\', 'a\\\\"b'];
    const {result, records, terminal} = run(directory, [script, ...args]);
    assert.equal(result.status, 0, result.stderr);
    assert.deepEqual(JSON.parse(fs.readFileSync(path.join(directory, 'args.json'), 'utf8')), args);
    assert.equal(records[0].schema, 'soui.owned-process-run/v1');
    assert.equal(records[0].jobAssignedBeforeResume, true);
    assert.equal(records[0].breakawayAllowed, false);
    assert.match(records[0].creationFileTime, /^\d+$/);
    assert.equal(terminal.reason, 'completed');
    assert.equal(terminal.rootExitCode, 0);
    assert.equal(terminal.activeProcesses, 0);
    assertOwnedClosure(records, 1);
    assert.equal(terminal.closureConfirmed, true);
    assert.equal(terminal.terminatedOwnedJob, false);
    assert.equal(terminal.passed, true);
  });

test('native nonzero exit is retained as failure even when the Job closes normally',
  {skip: !supported}, t => {
    const directory = fixture(t);
    const {result, terminal} = run(directory, ['-e', 'process.exit(9)']);
    assert.equal(result.status, 9, result.stderr);
    assert.equal(terminal.reason, 'root-failed');
    assert.equal(terminal.rootExitCode, 9);
    assert.equal(terminal.closureConfirmed, true);
    assert.equal(terminal.passed, false);
  });

test('nonzero root keeps failure while owned descendants drain naturally',
  {skip: !supported}, t => {
    const directory = fixture(t);
    const script = path.join(directory, 'failed-root-drain.cjs');
    fs.writeFileSync(script, `const child=require('node:child_process').spawn(process.execPath,
      ['-e','setTimeout(()=>{},150)'],{detached:true,windowsHide:true,stdio:'ignore'});child.unref();process.exit(9);`);
    const {result, records, terminal} = run(directory, [script], {drain: 2000});
    assert.equal(result.status, 9, result.stderr);
    assertOwnedClosure(records, 2);
    assert.equal(terminal.reason, 'root-failed');
    assert.equal(terminal.rootExitCode, 9);
    assert.equal(terminal.passed, false);
    assert.equal(terminal.activeBeforeCleanup, 0);
    assert.equal(terminal.terminatedOwnedJob, false);
  });

test('nonzero root still forces owned descendants after the explicit drain',
  {skip: !supported}, t => {
    const directory = fixture(t);
    const script = path.join(directory, 'failed-root-live-tail.cjs');
    fs.writeFileSync(script, `const child=require('node:child_process').spawn(process.execPath,
      ['-e','setTimeout(()=>{},30000)'],{detached:true,windowsHide:true,stdio:'ignore'});
      require('node:fs').writeFileSync('failed-child.pid',String(child.pid));child.unref();process.exit(7);`);
    const {result, records, terminal} = run(directory, [script], {drain: 100});
    assert.equal(result.status, 7, result.stderr);
    assertOwnedClosure(records, 2);
    assert.equal(terminal.reason, 'root-failed');
    assert.equal(terminal.rootExitCode, 7);
    assert.equal(terminal.passed, false);
    assert.ok(terminal.activeBeforeCleanup >= 1);
    assert.equal(terminal.terminatedOwnedJob, true);
    const rootExit = records.find(row => row.event === 'root-exit');
    assert.ok(terminal.elapsedMs - rootExit.elapsedMs >= 100, 'nonzero root bypassed explicit drain');
    assert.throws(() => process.kill(Number(fs.readFileSync(path.join(directory, 'failed-child.pid'), 'utf8')), 0),
      {code: 'ESRCH'});
  });

test('only child output and null input are inherited, preserving native diagnostics',
  {skip: !supported}, t => {
    const directory = fixture(t);
    const script = path.join(directory, 'stdio.cjs');
    fs.writeFileSync(script, `process.stdout.write('原生输出\\n');process.stderr.write('native-error\\n');
      process.stdin.resume();process.stdin.on('data',()=>process.exit(8));`);
    const {result, terminal} = run(directory, [script]);
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.stdout, '原生输出\n');
    assert.equal(result.stderr, 'native-error\n');
    assert.equal(terminal.passed, true);
  });

test('dedicated supervisor control cleans the held Job and cannot convert a stop into success',
  {skip: !supported, timeout: 15000}, async t => {
    const directory = fixture(t);
    const script = path.join(directory, 'controlled.cjs');
    fs.writeFileSync(script, `require('node:fs').writeFileSync('ready.pid',String(process.pid));
      process.stdin.resume();process.stdin.on('data',()=>require('node:fs').writeFileSync('leaked-control','bad'));
      setTimeout(()=>{},30000);`);
    const report = path.join(directory, 'controlled.ndjson');
    const child = spawn(runner, ['--timeout-ms', '10000', '--stop-on-stdin', '--report', report,
      '--cwd', directory, '--', process.execPath, script],
      {windowsHide: true, stdio: ['pipe', 'ignore', 'pipe']});
    t.after(() => { if (child.exitCode === null) child.kill(); });
    let stderr = '';
    child.stderr.setEncoding('utf8').on('data', bytes => { stderr += bytes; });
    const finished = new Promise((resolve, reject) => {
      child.once('error', reject);
      child.once('close', (code, signal) => resolve({code, signal}));
    });
    const started = performance.now();
    while (!fs.existsSync(path.join(directory, 'ready.pid'))) {
      assert.ok(performance.now() - started < 5000, 'The subject did not reach ready');
      await new Promise(resolve => setTimeout(resolve, 20));
    }
    child.stdin.end('STOP\n');
    const result = await finished;
    assert.equal(result.code, 124, stderr);
    assert.equal(result.signal, null);
    const records = fs.readFileSync(report, 'utf8').trim().split('\n').map(line => JSON.parse(line));
    assertOwnedClosure(records, 1);
    assert.equal(records.at(-1).reason, 'supervisor-stop');
    assert.equal(records.at(-1).passed, false);
    assert.equal(records.at(-1).terminatedOwnedJob, true);
    assert.equal(fs.existsSync(path.join(directory, 'leaked-control')), false);
    assert.throws(() => process.kill(Number(fs.readFileSync(path.join(directory, 'ready.pid'), 'utf8')), 0),
      {code: 'ESRCH'});
  });

test('timeout closes all three owned generations and leaves an unrelated process alive',
  {skip: !supported}, t => {
    const directory = fixture(t);
    const unrelated = spawn(process.execPath, ['-e', 'setTimeout(()=>{},30000)'],
      {windowsHide: true, stdio: 'ignore'});
    unrelated.unref();
    t.after(() => unrelated.kill());
    const leaf = path.join(directory, 'leaf.cjs');
    const middle = path.join(directory, 'middle.cjs');
    const root = path.join(directory, 'root.cjs');
    fs.writeFileSync(leaf, `require('node:fs').writeFileSync('leaf.pid',String(process.pid));setTimeout(()=>{},30000);`);
    fs.writeFileSync(middle, `require('node:child_process').spawn(process.execPath,[${JSON.stringify(leaf)}],{windowsHide:true,stdio:'ignore'});setTimeout(()=>{},30000);`);
    fs.writeFileSync(root, `require('node:child_process').spawn(process.execPath,[${JSON.stringify(middle)}],{windowsHide:true,stdio:'ignore'});setTimeout(()=>{},30000);`);
    const {result, records, terminal} = run(directory, [root], {timeout: 1500});
    assert.equal(result.status, 124, result.stderr);
    assert.equal(terminal.reason, 'timeout');
    assert.equal(terminal.passed, false);
    assert.equal(terminal.terminatedOwnedJob, true);
    assert.ok(terminal.activeBeforeCleanup >= 3);
    assertOwnedClosure(records, 3);
    assert.equal(terminal.rootExited, true);
    assert.equal(terminal.rootExitCode, 124);
    assert.equal(terminal.activeProcesses, 0);
    assert.equal(terminal.closureConfirmed, true);
    const leafPid = Number(fs.readFileSync(path.join(directory, 'leaf.pid'), 'utf8'));
    assert.throws(() => process.kill(leafPid, 0), {code: 'ESRCH'});
    assert.doesNotThrow(() => process.kill(unrelated.pid, 0));
  });

test('a root that exits zero cannot hide a still-running detached descendant',
  {skip: !supported}, t => {
    const directory = fixture(t);
    const script = path.join(directory, 'orphan.cjs');
    fs.writeFileSync(script, `const child=require('node:child_process').spawn(process.execPath,
      ['-e','setTimeout(()=>{},30000)'],{detached:true,windowsHide:true,stdio:'ignore'});
      require('node:fs').writeFileSync('child.pid',String(child.pid));child.unref();`);
    const {result, records, terminal} = run(directory, [script], {drain: 100});
    assert.equal(result.status, 124, result.stderr);
    assert.equal(terminal.rootExitCode, 0);
    assert.equal(terminal.reason, 'owned-descendants-remain');
    assert.equal(terminal.passed, false);
    assertOwnedClosure(records, 2);
    assert.equal(terminal.activeProcesses, 0);
    assert.equal(terminal.closureConfirmed, true);
    assert.throws(() => process.kill(Number(fs.readFileSync(path.join(directory, 'child.pid'), 'utf8')), 0),
      {code: 'ESRCH'});
  });

test('owned descendants may finish naturally within the explicit drain budget',
  {skip: !supported}, t => {
    const directory = fixture(t);
    const script = path.join(directory, 'drain.cjs');
    fs.writeFileSync(script, `const child=require('node:child_process').spawn(process.execPath,
      ['-e','setTimeout(()=>{},150)'],{detached:true,windowsHide:true,stdio:'ignore'});child.unref();`);
    const {result, records, terminal} = run(directory, [script], {drain: 2000});
    assert.equal(result.status, 0, result.stderr);
    assertOwnedClosure(records, 2);
    assert.equal(terminal.passed, true);
    assert.equal(terminal.terminatedOwnedJob, false);
  });

test('an existing report or invalid limits cannot launch a child or overwrite evidence',
  {skip: !supported}, t => {
    const directory = fixture(t);
    fs.writeFileSync(path.join(directory, 'run.ndjson'), '{"event":"original-evidence"}\n');
    const blocked = run(directory, ['-e', "require('node:fs').writeFileSync('should-not-exist','bad')"]);
    assert.equal(blocked.result.status, 125);
    assert.equal(fs.readFileSync(path.join(directory, 'run.ndjson'), 'utf8'), '{"event":"original-evidence"}\n');
    assert.equal(fs.existsSync(path.join(directory, 'should-not-exist')), false);
    for (const limit of ['-1', '0', '172800001', '12x']) {
      const result = spawnSync(runner, ['--timeout-ms', limit, '--report', path.join(directory, 'invalid.ndjson'),
        '--cwd', directory, '--', process.execPath, '-e', "require('node:fs').writeFileSync('should-not-exist','bad')"],
        {windowsHide: true, encoding: 'utf8', timeout: 5000});
      assert.equal(result.status, 125, result.stderr);
      assert.equal(fs.existsSync(path.join(directory, 'invalid.ndjson')), false);
      assert.equal(fs.existsSync(path.join(directory, 'should-not-exist')), false);
    }
  });

test('test-only assignment denial exercises real held suspended-process cleanup, not Win7 policy',
  {skip: !supported}, t => {
    const directory = fixture(t);
    const assignmentFixture = path.join(path.dirname(runner), 'OwnedProcessAssignmentFixture.exe');
    assert.ok(fs.existsSync(assignmentFixture), 'Build the test-only OwnedProcessAssignmentFixture target');
    fs.writeFileSync(path.join(directory, 'assignment-test-context.json'), JSON.stringify({
      schema: 'soui.owned-process-assignment-denial-test/v1',
      scope: 'test-only compiled AssignProcessToJobObject ERROR_ACCESS_DENIED replacement',
      realApis: ['CreateProcessW', 'GetProcessTimes', 'TerminateProcess', 'WaitForSingleObject', 'GetExitCodeProcess'],
      fixtureSha256: require('node:crypto').createHash('sha256').update(fs.readFileSync(assignmentFixture)).digest('hex'),
      win7NestedJobVerified: false,
      pcaVerified: false,
    }, null, 2) + '\n', {flag: 'wx'});
    const reportPath = path.join(directory, 'assignment.ndjson');
    const result = spawnSync(assignmentFixture, ['--timeout-ms', '5000', '--report', reportPath,
      '--cwd', directory, '--', process.execPath, '-e', "require('node:fs').writeFileSync('not-resumed','bad')"],
      {windowsHide: true, encoding: 'utf8', timeout: 15000});
    assert.ifError(result.error);
    assert.equal(result.status, 125, result.stderr);
    assert.match(result.stderr, /TEST_ONLY_ASSIGNMENT_DENIAL/);
    const terminal = JSON.parse(fs.readFileSync(reportPath, 'utf8').trim());
    assert.equal(terminal.reason, 'assignment-failed');
    assert.equal(terminal.assignmentError, 5); // Synthetic Win32 ERROR_ACCESS_DENIED.
    assert.equal(terminal.jobAssignedBeforeResume, false);
    assert.equal(terminal.terminateSucceeded, true);
    assert.equal(terminal.terminateError, 0);
    assert.equal(terminal.waitResult, 0);
    assert.equal(terminal.waitError, 0);
    assert.equal(terminal.rootExitCode, 125);
    assert.equal(terminal.exitReadError, 0);
    assert.equal(terminal.closureConfirmed, true);
    assert.equal(terminal.passed, false);
    assert.match(terminal.creationFileTime, /^\d+$/);
    assert.equal(terminal.identityError, 0);
    assert.ok(BigInt(terminal.creationFileTime) > 0n);
    assert.equal(fs.existsSync(path.join(directory, 'not-resumed')), false);
    assert.throws(() => process.kill(terminal.pid, 0), {code: 'ESRCH'});
  });
