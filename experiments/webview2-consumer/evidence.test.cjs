'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {validateRecords, recordsFrom} = require('./evidence.cjs');
const sdkVersion = '0.3.0-preview.9';
const outcome = {pid:100,exitCode:0,signal:null,timedOut:false,spawnError:null,terminationConfirmed:true};
const request = mode => ({mode,cycles:3,sdkVersion});
function transcript(mode = 'reload') {
  const records = [{event:'started',pid:100,cycles:3,windowVisible:true}];
  for (let cycle = 1; cycle <= 3; cycle++) {
    records.push({event:'ready',cycle,runtimeVersion:'141.0.3537.99',sessionDigest:String(cycle).padStart(64,'0')});
    if (mode === 'recreate' || cycle === 3) records.push({event:'closed',cycle});
  }
  records.push({event:'completed',cycles:3});
  return records.map((record, index) => ({mode,sdkVersion,elapsedMs:index * 100,...record}));
}

test('complete reload and recreate logs prove native operations while leaving broader gates unevaluated', () => {
  for (const mode of ['reload','recreate']) {
    const verified = validateRecords(transcript(mode), request(mode), outcome);
    assert.equal(verified.completedCycles, 3);
    assert.equal(verified.distinctDocumentSessions, 3);
    assert.equal(verified.runtimeVersion, '141.0.3537.99');
    assert.equal(verified.resourceGate, 'not-evaluated');
    assert.equal(verified.processTreeGate, 'not-evaluated');
    assert.equal(verified.releaseReady, false);
  }
});

test('missing or duplicate ready, close, start and completion events cannot pass', () => {
  for (const mode of ['reload','recreate']) {
    for (const event of ['started','ready','closed','completed']) {
      const records = transcript(mode);
      const position = records.findIndex(record => record.event === event);
      const missing = records.filter((_record, index) => index !== position);
      assert.throws(() => validateRecords(missing, request(mode), outcome), /Missing or duplicate/);
      const duplicate = records.slice();
      duplicate.splice(position, 0, {...records[position]});
      assert.throws(() => validateRecords(duplicate, request(mode), outcome), /Missing or duplicate/);
    }
  }
});

test('real exit failure, timeout and unconfirmed termination override complete green native logs', () => {
  for (const failure of [{exitCode:1},{exitCode:-1073741819},{timedOut:true},{signal:'SIGTERM'},
    {pid:null},{spawnError:'Unable to start'},{terminationConfirmed:false}])
    assert.throws(() => validateRecords(transcript(), request('reload'), {...outcome,...failure}), /process failed/);
});

test('a hidden or unobserved GUI window cannot pass the visible-page probe', () => {
  for (const visibility of [false, undefined]) {
    const records = transcript();
    records[0].windowVisible = visibility;
    assert.throws(() => validateRecords(records, request('reload'), outcome), /visible test window/);
  }
});

test('cross-process, stale-source and reordered lifecycle evidence is rejected', () => {
  for (const change of [records => { records[0].pid = 101; },
    records => { records[0].cycles = 2; }, records => { records.at(-1).cycles = 2; },
    records => { records[1].sdkVersion = '0.3.0-preview.8'; },
    records => { records[1].mode = 'recreate'; }, records => { records[1].cycle = 2; },
    records => { records[2].elapsedMs = 0; }, records => { records[1].event = 'failure'; },
    records => { [records[1],records[2]] = [records[2],records[1]]; }]) {
    const records = transcript();
    change(records);
    assert.throws(() => validateRecords(records, request('reload'), outcome));
  }
});

test('every ready event requires a fresh session and actual stable runtime version', () => {
  for (const change of [records => { delete records[1].runtimeVersion; },
    records => { records[1].runtimeVersion = ''; }, records => { records[2].runtimeVersion = '142.0.1.1'; },
    records => { delete records[1].sessionDigest; }, records => { records[1].sessionDigest = 'not-a-digest'; },
    records => { records[2].sessionDigest = records[1].sessionDigest; }]) {
    const records = transcript();
    change(records);
    assert.throws(() => validateRecords(records, request('reload'), outcome), /Runtime version|session identity/);
  }
});

test('an unterminated native log cannot be treated as complete evidence', () => {
  const folder = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-webview2-evidence-'));
  try {
    const file = path.join(folder, 'events.jsonl');
    const text = transcript().map(record => JSON.stringify(record)).join('\n');
    fs.writeFileSync(file, text);
    assert.throws(() => recordsFrom(file), /empty or truncated/);
    fs.writeFileSync(file, text + '\n');
    assert.deepEqual(recordsFrom(file), transcript());
  } finally {
    if (!folder.startsWith(path.resolve(os.tmpdir()) + path.sep)) throw new Error('Unsafe test cleanup target');
    fs.rmSync(folder, {recursive:true,force:true});
  }
});
