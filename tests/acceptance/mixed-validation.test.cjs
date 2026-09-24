'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const {mixedSettings, validateMixedRecords} = require('./mixed-validation.cjs');

const config = mixedSettings(1000, 20, 20000, 1000);
const outcome = {pid:123,exitCode:0,signal:null,timedOut:false};
const sample = (phase, cycle) => ({event:'sample',phase,cycle,privateBytes:20000000,
  workingSetBytes:10000000,handles:100,threads:3,gdiObjects:2,userObjects:3});

function transcript() {
  let elapsed = 0;
  const records = [];
  const add = record => records.push({...record,elapsedMs:elapsed += 100});
  const step = (action,window,cycle,delay=100) => {
    add({event:'step.begin',action,window,cycle});
    elapsed += delay-100;
    add({event:'step.complete',action,window,cycle});
  };
  add({event:'run.begin',...config,pid:123});
  add(sample('process',0));
  add({event:'cycle.begin',cycle:1});
  step('mount',1,1);
  step('runtime.reject-busy',0,1);
  step('reload',1,1);
  step('minimize',1,1);
  step('restore',1,1);
  step('mount',2,1);
  step('mount',3,1);
  step('close',2,1);
  step('reload',1,1);
  step('reload',3,1);
  step('mount',2,1);
  step('close',3,1);
  step('close',2,1);
  step('close',1,1);
  step('idle',0,1,1000);
  add(sample('cycle',1));
  add({event:'cycle.complete',cycle:1});
  add(sample('closed',2));
  step('runtime.shutdown',0,2);
  add(sample('runtime-closed',2));
  step('runtime.reject-restart',0,2);
  step('runtime.shutdown-idempotent',0,2);
  add({event:'run.complete'});
  return records;
}

test('mixed transcript requires one process, every public lifecycle step and full duration', () => {
  const result=validateMixedRecords(transcript(),config,outcome);
  assert.equal(result.measuredCycles,1);
  assert.equal(result.completedSteps,18);
  assert.ok(result.elapsedMs >= config.durationMs);
  assert.equal(result.longRunGate,'not-evaluated');
  assert.equal(result.releaseReady,false);
});

test('mixed verification rejects missing work, shortened idle, premature finish and process failure', () => {
  const missing=transcript().filter(record => !(record.action==='restore'));
  assert.throws(() => validateMixedRecords(missing,config,outcome),/mixed lifecycle steps/);
  const shortIdle=transcript();
  const idleBegin=shortIdle.find(record => record.event==='step.begin' && record.action==='idle');
  const idleEnd=shortIdle.find(record => record.event==='step.complete' && record.action==='idle');
  idleEnd.elapsedMs=idleBegin.elapsedMs+1;
  assert.throws(() => validateMixedRecords(shortIdle,config,outcome),/idle interval/);
  const early=transcript();
  const tooLong={...config,durationMs:early.at(-1).elapsedMs+1};
  early[0].durationMs=tooLong.durationMs;
  assert.throws(() => validateMixedRecords(early,tooLong,outcome),/requested duration/);
  for(const change of [{exitCode:1},{timedOut:true},{signal:'SIGTERM'},{pid:124}])
    assert.throws(() => validateMixedRecords(transcript(),config,{...outcome,...change}),/process failed|identity/);
});

test('mixed parameters reject absent idle and timing beyond the supported range', () => {
  assert.throws(() => mixedSettings(0,20,20000,1000));
  assert.throws(() => mixedSettings(1000,20,20000,20000));
  assert.throws(() => mixedSettings(43200001,20,20000,1000));
  const broken=transcript();
  delete broken.find(record => record.phase==='cycle').handles;
  assert.throws(() => validateMixedRecords(broken,config,outcome),/resource measurement/);
});
