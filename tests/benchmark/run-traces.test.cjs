'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {checkRunTraces} = require('../../benchmarks/protocol/check-run-traces.cjs');

function fixture() {
  const creation = 134000000000000000;
  const started = 134000000100000000;
  const input = [
    {event:'run.begin', schema:'soui.external-input-trace/v1', pid:812,
      scene:'S01', mode:'full', contentWidth:1100, contentHeight:720,
      processCreationTime100ns:creation, startedAtUtcFiletime100ns:started + 10000000,
      qpcFrequency:10000000},
    {event:'input', action:'select-scene', elapsedMs:0, qpcBefore:100,
      qpcAfter:101, clientX:47, clientY:654},
    {event:'input', action:'finish-scene', elapsedMs:61000, qpcBefore:200,
      qpcAfter:201, clientX:1040, clientY:698},
    {event:'run.end', elapsedMs:61100, actions:2, imeCompositionCollected:false},
  ];
  const process = [
    {event:'run.begin', schema:'soui.process-tree-samples/v1', rootPid:812,
      rootCreationTime100ns:creation, startedAtUtcFiletime100ns:started,
      intervalMs:1000, durationMs:90000},
    ...Array.from({length:63}, (_, index) => ({event:'sample', elapsedMs:index * 1000,
      rootPid:812, complete:true, privateBytes:1024 + index,
      workingSetBytes:2048 + index, handles:40, threads:4,
      cpuPercent:index ? 1.5 : null,
      processes:[{pid:812, creationTime100ns:creation}]})),
    {event:'run.end', elapsedMs:63000, rootExitCode:0,
      durationExpired:false, samples:63, incompleteSamples:0},
  ];
  return {input, process};
}

test('accepts matching full traces without promoting performance or presentation', () => {
  const {input, process} = fixture();
  const report = checkRunTraces(input, process, 'S01');
  assert.equal(report.traceIntegrityAccepted, true);
  assert.equal(report.inputOffsetMs, 1000);
  assert.equal(report.processSamples, 63);
  assert.equal(report.physicalPresentation, 'not-measured');
  assert.equal(report.releaseReady, false);
});

test('rejects a different process, shortened input, missing process sample, and timeout', () => {
  const {input, process} = fixture();
  input[0].processCreationTime100ns += 100000;
  assert.throws(() => checkRunTraces(input, process, 'S01'), /different host/);
  input[0].processCreationTime100ns -= 100000;
  input.at(-1).elapsedMs = 60000;
  assert.throws(() => checkRunTraces(input, process, 'S01'), /incomplete duration/);
  input.at(-1).elapsedMs = 61100;
  process[1].complete = false;
  assert.throws(() => checkRunTraces(input, process, 'S01'), /incomplete sample/);
  process[1].complete = true;
  process[2].elapsedMs = 9000;
  assert.throws(() => checkRunTraces(input, process, 'S01'), /invalid or incomplete sample/);
  process[2].elapsedMs = 1000;
  process.at(-1).durationExpired = true;
  assert.throws(() => checkRunTraces(input, process, 'S01'), /did not exit/);
});

test('rejects late input and S03 without IME composition', () => {
  const {input, process} = fixture();
  input[0].startedAtUtcFiletime100ns += 100000000;
  assert.throws(() => checkRunTraces(input, process, 'S01'), /outside process-tree/);
  input[0].startedAtUtcFiletime100ns -= 100000000;
  input[0].scene = 'S03';
  assert.throws(() => checkRunTraces(input, process, 'S03'), /IME composition/);
});
