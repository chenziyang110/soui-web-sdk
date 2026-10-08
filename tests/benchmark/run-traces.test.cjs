'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
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
  assert.equal(report.exactFileTimePair, false);
  assert.equal(report.exactFileTimeBound, false);
});

const exactCreation = '134000000000000001';
const ownedSubject = {pid:812, creationFileTime:exactCreation};
function exactFixture() {
  const value = fixture();
  value.input[0].schema = 'soui.external-input-trace/v2';
  value.input[0].processCreationTime100ns = exactCreation;
  value.input[0].expectedCreationTime100ns = exactCreation;
  value.input[0].startedAtUtcFiletime100ns = '134000000110000001';
  value.process[0].schema = 'soui.process-tree-samples/v2';
  value.process[0].rootCreationTime100ns = exactCreation;
  value.process[0].expectedCreationTime100ns = exactCreation;
  value.process[0].startedAtUtcFiletime100ns = '134000000100000000';
  for (const sample of value.process.slice(1, -1))
    for (const member of sample.processes) member.creationTime100ns = exactCreation;
  return value;
}

test('legacy rounded creation values remain readable without exact binding', () => {
  const value = fixture();
  value.input[0].processCreationTime100ns = Number('134000000000000001');
  value.process[0].rootCreationTime100ns = Number('134000000000000002');
  assert.equal(value.input[0].processCreationTime100ns, value.process[0].rootCreationTime100ns,
    'RED counterexample: different raw creation ticks collapse as JSON numbers');
  const report = checkRunTraces(value.input, value.process, 'S01');
  assert.equal(report.traceIntegrityAccepted, true);
  assert.equal(report.exactFileTimeBound, false);
  assert.throws(() => checkRunTraces(value.input, value.process, 'S01', {ownedSubject}), /exact.*v2|v2.*exact/i);
});

test('new exact pairs require the held subject before binding and preserve one-tick alignment', () => {
  const value = exactFixture();
  const pair = checkRunTraces(value.input, value.process, 'S01');
  assert.equal(pair.exactFileTimePair, true);
  assert.equal(pair.exactFileTimeBound, false);
  assert.equal(pair.inputOffset100ns, '10000001');
  assert.equal(pair.inputOffsetMs, 1000.0001);
  const bound = checkRunTraces(value.input, value.process, 'S01', {ownedSubject});
  assert.equal(bound.exactFileTimeBound, true);
  assert.equal(bound.hostCreationTime100ns, exactCreation);
  value.input[0].processCreationTime100ns = '134000000000000002';
  value.input[0].expectedCreationTime100ns = '134000000000000002';
  assert.throws(() => checkRunTraces(value.input, value.process, 'S01', {ownedSubject}), /different host/);
});

test('un-pinned collector CLI runs cannot claim exact owned binding', () => {
  for (const collector of ['input', 'process']) {
    const value = exactFixture();
    value[collector][0].expectedCreationTime100ns = null;
    assert.equal(checkRunTraces(value.input, value.process, 'S01').exactFileTimeBound, false);
    assert.throws(() => checkRunTraces(value.input, value.process, 'S01', {ownedSubject}), /expected.*subject/i);
  }
  for (const invalid of [undefined, '0', '01', '134000000000000002', Number(exactCreation)]) {
    const value = exactFixture(); value.input[0].expectedCreationTime100ns = invalid;
    assert.throws(() => checkRunTraces(value.input, value.process, 'S01'), /expected.*identity/i);
  }
});

const nativeFixture = process.env.BENCHMARK_IDENTITY_FIXTURE_EXE;
const ownedClient = process.env.BENCHMARK_OWNED_CLIENT || path.resolve(__dirname, '../../tools/internal/owned-process.cjs');
const ownedRunner = process.env.BENCHMARK_OWNED_RUNNER_EXE;
const nativeAvailable = process.platform === 'win32' && nativeFixture && ownedRunner &&
    fs.existsSync(nativeFixture) && fs.existsSync(ownedRunner) && fs.existsSync(ownedClient);
test('actual native serializer emits readable legacy and exact collector schemas', {skip:!nativeAvailable}, async () => {
  const {launchOwned} = require(ownedClient);
  const folder = path.resolve(__dirname, '../../out/native-identity-' + crypto.randomUUID());
  fs.mkdirSync(folder);
  const run = launchOwned(nativeFixture, [], {runner:ownedRunner, cwd:path.resolve(__dirname, '../..'),
    report:path.join(folder, 'owned.ndjson'), timeoutMs:5000, drainMs:2000, stdout:'pipe', stderr:'pipe'});
  const stdout = [], stderr = [];
  run.child.stdout.on('data', value => stdout.push(value));
  run.child.stderr.on('data', value => stderr.push(value));
  await run.ready;
  const result = await run.finished;
  fs.writeFileSync(path.join(folder, 'stdout.bin'), Buffer.concat(stdout));
  fs.writeFileSync(path.join(folder, 'stderr.bin'), Buffer.concat(stderr));
  fs.writeFileSync(path.join(folder, 'finished.json'), JSON.stringify(result, null, 2) + '\n');
  assert.equal(result.passed, true, JSON.stringify(result));
  const rows = Buffer.concat(stdout).toString('utf8').trimEnd().split('\n').map(line => JSON.parse(line));
  assert.equal(rows.length, 5);
  for (const [inputIndex, processIndex, exact] of [[0, 1, false], [2, 3, true]]) {
    const value = exact ? exactFixture() : fixture();
    value.input[0] = rows[inputIndex]; value.process[0] = rows[processIndex];
    const report = checkRunTraces(value.input, value.process, 'S01', exact ? {ownedSubject} : {});
    assert.equal(report.exactFileTimePair, exact);
    assert.equal(report.exactFileTimeBound, exact);
    assert.equal(report.hostCreationTime100ns, exact ? exactCreation : Number(exactCreation));
  }
  assert.equal(rows[4].maximum, '18446744073709551615');
  assert.ok([32, 64].includes(rows[4].hostBits));
});

test('exact records reject mixed versions and cannot be silently downgraded to numbers', () => {
  const mixed = exactFixture();
  mixed.input[0] = fixture().input[0];
  assert.throws(() => checkRunTraces(mixed.input, mixed.process, 'S01'), /mixed|version/i);
  const value = exactFixture();
  value.input[0].processCreationTime100ns = Number(exactCreation);
  assert.throws(() => checkRunTraces(value.input, value.process, 'S01'), /identity|FILETIME/i);
});

test('exact FILETIME strings enforce uint64 and canonical positive decimal boundaries', () => {
  for (const invalid of ['0', '01', '+1', '-1', '1.0', '1e17', ' 1', '1 ', '',
      '18446744073709551616', '99999999999999999999999999999999999999999', 1, null]) {
    for (const where of ['host', 'utc', 'member']) {
      const value = exactFixture();
      if (where === 'host') value.input[0].processCreationTime100ns = invalid;
      if (where === 'utc') value.process[0].startedAtUtcFiletime100ns = invalid;
      if (where === 'member') value.process[1].processes[0].creationTime100ns = invalid;
      assert.throws(() => checkRunTraces(value.input, value.process, 'S01'), /identity|sample|FILETIME/i,
        `${where}: ${JSON.stringify(invalid)}`);
    }
  }
  const maximum = exactFixture();
  maximum.input[0].processCreationTime100ns = '18446744073709551615';
  maximum.input[0].expectedCreationTime100ns = '18446744073709551615';
  maximum.process[0].rootCreationTime100ns = '18446744073709551615';
  maximum.process[0].expectedCreationTime100ns = '18446744073709551615';
  maximum.process.slice(1, -1).forEach(sample => { sample.processes[0].creationTime100ns = '18446744073709551615'; });
  assert.equal(checkRunTraces(maximum.input, maximum.process, 'S01', {
    ownedSubject:{pid:812, creationFileTime:'18446744073709551615'},
  }).exactFileTimeBound, true);
});

test('PID reuse, duplicate members and forged held identities fail exact binding', () => {
  for (const subject of [{pid:813, creationFileTime:exactCreation},
      {pid:812, creationFileTime:'134000000000000002'},
      {pid:812, creationFileTime:Number(exactCreation)}, {pid:0, creationFileTime:exactCreation},
      {pid:4294967296, creationFileTime:exactCreation}, null]) {
    const value = exactFixture();
    assert.throws(() => checkRunTraces(value.input, value.process, 'S01', {ownedSubject:subject}), /subject|identity|FILETIME/i);
  }
  for (const invalidPid of [0, -1, 1.5, '812', 4294967296]) {
    const value = exactFixture(); value.process[1].processes[0].pid = invalidPid;
    assert.throws(() => checkRunTraces(value.input, value.process, 'S01'), /sample|identity/i);
  }
  const reused = exactFixture();
  reused.process[3].processes[0].creationTime100ns = '134000000000000002';
  assert.throws(() => checkRunTraces(reused.input, reused.process, 'S01'), /sample|identity/i);
  const duplicate = exactFixture();
  duplicate.process[1].processes.push({...duplicate.process[1].processes[0]});
  assert.throws(() => checkRunTraces(duplicate.input, duplicate.process, 'S01'), /sample|duplicate/i);
});

test('UTC alignment uses exact differences near uint64 max and rejects a one-tick overrun', () => {
  const value = exactFixture();
  value.process[0].startedAtUtcFiletime100ns = '18446744072709551614';
  value.input[0].startedAtUtcFiletime100ns = '18446744072709551615';
  assert.equal(checkRunTraces(value.input, value.process, 'S01').inputOffset100ns, '1');
  const overrun = exactFixture();
  overrun.process[0].startedAtUtcFiletime100ns = '134000000100000000';
  // 64,000ms allowed coverage minus 61,100ms input duration is exactly 2,900ms.
  overrun.input[0].startedAtUtcFiletime100ns = '134000000129000001';
  assert.throws(() => checkRunTraces(overrun.input, overrun.process, 'S01'), /outside process-tree/);
  overrun.input[0].startedAtUtcFiletime100ns = '134000000099999999';
  assert.throws(() => checkRunTraces(overrun.input, overrun.process, 'S01'), /outside process-tree/);
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
