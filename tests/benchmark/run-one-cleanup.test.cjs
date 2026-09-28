'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {test} = require('node:test');
const {launch, stop, verifiedDirectChild} = require('../../benchmarks/protocol/run-one.cjs');

const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const sameExecutable = (left, right) =>
  path.win32.normalize(fs.realpathSync.native(left)).toLowerCase() ===
  path.win32.normalize(fs.realpathSync.native(right)).toLowerCase();
const query = pid => {
  const script = "$ErrorActionPreference='Stop';" +
    `$targetId=${pid};` +
    '$item=Get-CimInstance Win32_Process -Filter "ProcessId=$targetId" -ErrorAction Stop;' +
    'if($null -ne $item){$item|Select-Object ProcessId,ParentProcessId,ExecutablePath|ConvertTo-Json -Compress}';
  const result = spawnSync('powershell.exe', ['-NoProfile', '-Command', script], {
    encoding: 'utf8', windowsHide: true, timeout: 15000,
    env: {...process.env, PSModulePath: path.join(process.env.SystemRoot,
      'System32/WindowsPowerShell/v1.0/Modules')},
  });
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stderr, '');
  return result.stdout.trim() ? JSON.parse(result.stdout.replace(/^\uFEFF/, '')) : null;
};
async function until(check) {
  for (let i = 0; i < 50; ++i) {
    const result = check();
    if (result) return result;
    await delay(100);
  }
  throw new Error('Timed out waiting for the owned process fixture');
}
test('failure cleanup verifies direct child identity and stops its own process tree',
  {skip: process.platform !== 'win32'}, async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-run-one-cleanup-'));
    const grandchildFile = path.join(root, 'grandchild.json');
    const parentCode = `const fs=require('node:fs');const cp=require('node:child_process');` +
      `const child=cp.spawn(process.execPath,['-e','setInterval(()=>{},1000)'],` +
      `{stdio:'ignore',windowsHide:true});` +
      `fs.writeFileSync(process.argv[1],JSON.stringify({pid:child.pid}));` +
      `setInterval(()=>{},1000);`;
    const run = launch('host', process.execPath, ['-e', parentCode, grandchildFile],
      root, root);
    let grandchildPid;
    try {
      await until(() => fs.existsSync(grandchildFile));
      grandchildPid = JSON.parse(fs.readFileSync(grandchildFile, 'utf8')).pid;
      assert(verifiedDirectChild(run), JSON.stringify({
        observed: query(run.child.pid), expectedPid: run.child.pid,
        expectedParent: process.pid, expectedExecutable: run.executable}));
      const owned = query(grandchildPid);
      assert.equal(owned.ParentProcessId, run.child.pid);
      const cleanup = [];
      stop([run], cleanup);
      await run.finished;
      assert.deepEqual(cleanup.map(item => [item.method, item.treeCleanupVerified]),
        [['verified-tree', true]]);
      await until(() => !query(grandchildPid));
    } finally {
      if (run.child.exitCode === null && !run.child.killed) run.child.kill();
      await run.finished;
      if (grandchildPid) {
        const remaining = query(grandchildPid);
        if (remaining && remaining.ParentProcessId === run.child.pid &&
            sameExecutable(remaining.ExecutablePath, process.execPath))
          spawnSync('taskkill.exe', ['/PID', String(grandchildPid), '/F'],
            {windowsHide: true, timeout: 15000});
        await until(() => !query(grandchildPid));
      }
      assert.equal(path.dirname(root), os.tmpdir());
      assert.match(path.basename(root), /^soui-run-one-cleanup-/);
    }
  });

test('failed identity verification falls back to the owned process handle',
  {skip: process.platform !== 'win32'}, async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-run-one-cleanup-'));
    const run = launch('host', process.execPath,
      ['-e', 'setInterval(()=>{},1000)'], root, root);
    try {
      await until(() => verifiedDirectChild(run));
      const launchedAt = run.spawnRequestedAtMs;
      const returnedAt = run.spawnReturnedAtMs;
      run.spawnRequestedAtMs = 0;
      run.spawnReturnedAtMs = 0;
      assert.equal(verifiedDirectChild(run), false,
        'A matching PID, parent and executable cannot replace launch-time identity');
      run.spawnRequestedAtMs = launchedAt;
      run.spawnReturnedAtMs = returnedAt;
      run.executable = path.join(root, 'not-the-child.exe');
      const cleanup = [];
      stop([run], cleanup);
      await run.finished;
      assert.deepEqual(cleanup.map(item => [item.method, item.treeCleanupVerified]),
        [['process-handle', false]]);
    } finally {
      if (run.child.exitCode === null && !run.child.killed) run.child.kill();
      await run.finished;
      assert.equal(path.dirname(root), os.tmpdir());
      assert.match(path.basename(root), /^soui-run-one-cleanup-/);
    }
  });
