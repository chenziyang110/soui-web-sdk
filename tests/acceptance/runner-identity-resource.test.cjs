'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const {verifyNativeIdentity, resourceBinding, settings} = require('./runner.cjs');
const reader = require('./resource-validation.cjs');
const fileTime = '134360198253710104';
const native = () => [{event:'run.begin', pid:125, creationFileTime:fileTime}];
const outcome = () => ({pid:125, ownedProcess:{pid:125, creationFileTime:fileTime}});

test('native operation identity binds the actual Owned PID and string FILETIME', () => {
  assert.deepEqual(verifyNativeIdentity(native(), outcome()), {pid:125, creationFileTime:fileTime});
});
test('missing native or Owned creation FILETIME is refused', () => {
  const records=native(); delete records[0].creationFileTime;
  assert.throws(() => verifyNativeIdentity(records,outcome()), /identity/);
  const result=outcome(); delete result.ownedProcess.creationFileTime;
  assert.throws(() => verifyNativeIdentity(native(),result), /identity/);
});
test('a reused PID cannot borrow an earlier creation FILETIME', () => {
  const result=outcome(); result.ownedProcess.creationFileTime='134360198253710105';
  assert.throws(() => verifyNativeIdentity(native(),result), /identity/);
});
test('native, outcome and actual Owned PIDs must all agree', () => {
  for (const modify of [v=>v[0].pid=126, v=>v[1].pid=126, v=>v[1].ownedProcess.pid=126]) {
    const values=[native()[0],outcome()]; modify(values);
    assert.throws(() => verifyNativeIdentity([values[0]],values[1]), /identity/);
  }
});
test('numeric, zero, overflow and nondecimal FILETIMEs are refused', () => {
  for (const value of [134360198253710104,'0','18446744073709551616','123x','01',null]) {
    const records=native(); records[0].creationFileTime=value;
    assert.throws(() => verifyNativeIdentity(records,outcome()), /identity/);
  }
});
test('ordinary invocation adds no resource policy or scope', () => {
  assert.equal(resourceBinding(settings('reload',1,0,500,20000)),null);
  assert.equal(resourceBinding(settings('reload',1,0,500,20000),{resource:false}),null);
});
test('resource policy comes from the loaded reader bytes rather than the caller', () => {
  const binding=resourceBinding(settings('reload',100,10,5000,20000),{resource:true});
  assert.ok(binding.policyRaw.equals(reader.policyBytes()));
  assert.equal(binding.requestFields.resourcePolicySha256,reader.POLICY_SHA256);
  assert.deepEqual(binding.requestFields.resourceScope,{kernel:'miniblink',renderer:'software',processModel:'single-process'});
});
test('resource entry rejects caller hashes, unknown options and nonfrozen scenarios', () => {
  const config=settings('reload',100,10,5000,20000);
  for (const value of [{resource:true,resourcePolicySha256:reader.POLICY_SHA256},{resource:true,resourceScope:{}},
    {resource:'true'},{policy:reader.POLICY_SHA256},[],null]) {
    assert.throws(() => resourceBinding(config,value));
  }
  for (const change of [{mode:'bridge'},{cycles:99},{warmup:0},{settleMs:4999},{timeoutMs:20001}])
    assert.throws(() => resourceBinding({...config,...change},{resource:true}), /frozen/);
});
