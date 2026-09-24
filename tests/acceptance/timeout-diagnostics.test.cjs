'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {once} = require('node:events');
const {captureTimeout} = require('./timeout-diagnostics.cjs');

test('timeout evidence preserves the native log offset without treating the run as passed', async () => {
  const folder = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-timeout-'));
  try {
    const first = '{"event":"step.begin","action":"mount","cycle":1}\n';
    const second = '{"event":"step.complete","action":"mount","cycle":1}\n';
    const nativeLog = path.join(folder, 'events.ndjson');
    fs.writeFileSync(nativeLog, first + second);
    const result = await captureTimeout({pid:1, output:folder, nativeLog,
      snapshot:{runId:'probe',reason:'step-deadline:mount',observedBytes:Buffer.byteLength(first),
        lastNativeRecord:{event:'step.begin',action:'mount',cycle:1},stepStarted:{action:'mount',cycle:1}}});
    const observation = JSON.parse(fs.readFileSync(path.join(folder, 'timeout-observation.json'), 'utf8'));
    assert.equal(observation.unreadBytes, Buffer.byteLength(second));
    assert.match(observation.tail, /step\.complete/);
    assert.equal(result.debugger.status, 'disabled');
    assert.match(result.observationSha256, /^[0-9a-f]{64}$/);
  } finally {
    if (!folder.startsWith(path.resolve(os.tmpdir()) + path.sep)) throw new Error('Unsafe test cleanup target');
    fs.rmSync(folder, {recursive:true,force:true});
  }
});

test('configured x86 debugger creates a dump and thread log before the target is killed',
  {skip: process.platform !== 'win32' || !process.env.SOUI_TEST_CDB_X86 || !process.env.SOUI_TEST_DUMP_ROOT},
  async () => {
    const executable = path.join(process.env.SystemRoot, 'SysWOW64/WindowsPowerShell/v1.0/powershell.exe');
    const child = spawn(executable, ['-NoProfile','-Command','Start-Sleep 120'], {windowsHide:true,stdio:'ignore'});
    await once(child, 'spawn');
    try {
      const output = path.join(process.env.SOUI_TEST_DUMP_ROOT, `probe-${process.pid}`);
      fs.mkdirSync(output, {recursive:true});
      const nativeLog = path.join(output, 'events.ndjson');
      fs.writeFileSync(nativeLog, '{"event":"step.begin","action":"mount","cycle":1}\n');
      const result = await captureTimeout({pid:child.pid, output, nativeLog,
        snapshot:{runId:`probe-${process.pid}`,reason:'synthetic-timeout',observedBytes:fs.statSync(nativeLog).size},
        debuggerExe:process.env.SOUI_TEST_CDB_X86,dumpRoot:process.env.SOUI_TEST_DUMP_ROOT});
      assert.equal(result.debugger.status, 'captured');
      assert.ok(result.debugger.files.dump.bytes > 1024);
      assert.match(result.debugger.files.dump.sha256, /^[0-9a-f]{64}$/);
      assert.match(result.debugger.files.log.sha256, /^[0-9a-f]{64}$/);
    } finally { child.kill(); }
  });
