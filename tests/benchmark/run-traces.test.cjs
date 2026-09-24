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
    {event:'input', action:'select-scene-confirm', elapsedMs:150, qpcBefore:102,
      qpcAfter:103, clientX:47, clientY:654},
    ...Array.from({length:60}, (_, index) => ({event:'input',
      action:index % 2 ? 'expand' : 'switch', elapsedMs:800 + index * 1000,
      qpcBefore:104 + index * 2, qpcAfter:105 + index * 2,
      clientX:index % 2 ? 137 : 53, clientY:698})),
    {event:'input', action:'finish-scene', elapsedMs:61000, qpcBefore:224,
      qpcAfter:225, clientX:1040, clientY:698},
    {event:'run.end', elapsedMs:61100, actions:63, imeCompositionCollected:false},
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

test('rejects a full-length idle trace and missing scene operations', () => {
  const {input, process} = fixture();
  input.splice(3, 60);
  input.at(-1).actions = 3;
  assert.throws(() => checkRunTraces(input, process, 'S01'), /missing switch\/expand/);
  const complete = fixture();
  complete.input[20].action = 'switch';
  assert.throws(() => checkRunTraces(complete.input, complete.process, 'S01'), /missing switch\/expand/);
});

function sceneFixture(scene, work, durationMs = 61100) {
  const {input, process} = fixture();
  const actions = ['select-scene', 'select-scene-confirm', ...work, 'finish-scene'];
  const records = actions.map((action, index) => ({event:'input', action,
    elapsedMs:index === actions.length - 1 ? durationMs - 100 :
      index === 0 ? 0 : index === 1 ? 150 : 800 + (index - 2) * 900,
    qpcBefore:100 + index * 2, qpcAfter:101 + index * 2,
    clientX:action === 'window-resize' ? 1100 : 53,
    clientY:action === 'window-resize' ? 720 : 698}));
  input.splice(1, input.length - 2, ...records);
  input[0].scene = scene;
  input.at(-1).actions = actions.length;
  input.at(-1).elapsedMs = durationMs;
  if (scene === 'S05') {
    process.splice(1, process.length - 2,
      ...Array.from({length:125}, (_, index) => ({event:'sample', elapsedMs:index * 1000,
        rootPid:812, complete:true, privateBytes:1024, workingSetBytes:2048,
        handles:40, threads:4, cpuPercent:index ? 1.5 : null,
        processes:[{pid:812, creationTime100ns:process[0].rootCreationTime100ns}]})));
    process.at(-1).elapsedMs = 126000;
    process.at(-1).samples = 125;
  }
  return {input, process};
}

test('accepts full scroll, resize and motion traces, but rejects missing defining actions', () => {
  const scrollActions = Array.from({length:60}, (_, index) => [
    index % 8 < 4 ? 'wheel-down' : 'wheel-up',
    ...(index % 12 === 11 ? ['reset-scroll'] : []),
  ]).flat();
  const scroll = sceneFixture('S02', scrollActions);
  assert.equal(checkRunTraces(scroll.input, scroll.process, 'S02').traceIntegrityAccepted, true);
  scroll.input.at(-3).action = 'wheel-down';
  assert.throws(() => checkRunTraces(scroll.input, scroll.process, 'S02'), /missing scroll\/reset/);

  const resize = sceneFixture('S04', ['arm-window', ...Array.from({length:60}, () => 'window-resize'), 'window-resize']);
  resize.input.slice(4, -3).forEach((record, index) => {
    record.clientX = index % 2 ? 1100 : 1060;
    record.clientY = index % 2 ? 720 : 700;
  });
  assert.equal(checkRunTraces(resize.input, resize.process, 'S04').traceIntegrityAccepted, true);
  resize.input.at(-3).clientX = 1060;
  assert.throws(() => checkRunTraces(resize.input, resize.process, 'S04'), /did not restore/);

  const motion = sceneFixture('S05', ['start-motion'], 123100);
  assert.equal(checkRunTraces(motion.input, motion.process, 'S05').traceIntegrityAccepted, true);
  motion.input[3].action = 'idle';
  assert.throws(() => checkRunTraces(motion.input, motion.process, 'S05'), /missing motion start/);
});
