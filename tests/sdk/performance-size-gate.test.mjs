import test from 'node:test';
import assert from 'node:assert/strict';
import {compareSizes} from '../../tools/performance-size-gate.mjs';
import {createHash} from 'node:crypto';

function bind(value) {
  const canonical=JSON.stringify(value.files.map(file=>({bytes:file.bytes,path:file.path,sha256:file.sha256})));
  value.archive.inventorySha256=createHash('sha256').update(canonical).digest('hex');
  return value;
}

function snapshot() {
  return bind({schema:'soui.performance-size-snapshot/v1',profile:{backend:'miniblink',architecture:'x64',
    configuration:'Release',toolchain:'MSVC143',featureSet:'same',resourceSet:'same',packaging:'zip-fixed',
    requiredRuntimeDependencies:[]},kernelPaths:['engine.dll'],archive:{bytes:10,sha256:'a'.repeat(64)},
    files:[{path:'engine.dll',bytes:20,sha256:'b'.repeat(64)},{path:'app.js',bytes:5,sha256:'c'.repeat(64)}],
    totals:{archiveBytes:10,runtimeBytes:25,kernelBytes:20}});
}
test('all three independent budgets must pass', () => {
  const before = snapshot(), after = snapshot();
  after.archive.bytes=9;after.totals.archiveBytes=9;
  after.files[0].bytes=21;after.totals.runtimeBytes=26;after.totals.kernelBytes=21;
  const result = compareSizes(before,bind(after));
  assert.equal(result.pass,false);
  assert.deepEqual(result.reasons,['runtimeBytes-grew','kernelBytes-grew']);
});
test('unchanged payload and smaller native code pass', () => {
  const after = snapshot();after.files[0].bytes=19;after.totals.runtimeBytes=24;after.totals.kernelBytes=19;
  assert.equal(compareSizes(snapshot(),bind(after)).pass,true);
});
test('profile, duplicate paths and forged totals fail closed', () => {
  const changed = snapshot();changed.profile.toolchain='different';
  assert.throws(() => compareSizes(snapshot(),changed),/profiles differ/);
  const forged = snapshot();forged.totals.runtimeBytes=1;
  assert.throws(() => compareSizes(snapshot(),forged),/totals/);
  const duplicate = snapshot();duplicate.files.push({...duplicate.files[0],path:'ENGINE.dll'});
  assert.throws(() => compareSizes(snapshot(),duplicate),/Duplicate/);
});
test('new native runtime file cannot hide behind a smaller archive', () => {
  const after=snapshot();after.files[0].bytes=15;after.files.push({path:'extra.dll',bytes:1,sha256:'d'.repeat(64)});
  after.totals.runtimeBytes=21;after.totals.kernelBytes=15;
  assert.equal(compareSizes(snapshot(),bind(after)).pass,false);
});
test('removing resources requires an explicit equivalence review', () => {
  const after=snapshot();after.files.pop();after.totals.runtimeBytes=20;
  assert.deepEqual(compareSizes(snapshot(),bind(after)).reasons,['runtime-files-removed-require-equivalence-review']);
});
