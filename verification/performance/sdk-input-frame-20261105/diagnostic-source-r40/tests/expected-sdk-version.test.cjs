'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {checkSync64,checkExpectedSdkVersion}=require('../reader/sync-input-reader.cjs');
test('missing expectedSdkVersion rejects before any incomplete trace could be read',()=>{
 assert.throws(()=>checkSync64({hostResult:{compiled_sdk_version:'0.7.0-preview.4'}}),/explicit frozen expectedSdkVersion/);
});
test('wrong or unsupported frozen expected version is rejected',()=>{
 assert.throws(()=>checkSync64({hostResult:{compiled_sdk_version:'0.7.0-preview.4'},expectedSdkVersion:'0.6.0'}),/Host compiled SDK version differs/);
 assert.throws(()=>checkExpectedSdkVersion({compiled_sdk_version:'0.7.0-preview.4'},''),/explicit frozen/);
});
test('current and baseline identities remain accepted only with their own explicit exact expectation',()=>{
 assert.equal(checkExpectedSdkVersion({compiled_sdk_version:'0.7.0-preview.4'},'0.7.0-preview.4'),true);
 assert.equal(checkExpectedSdkVersion({compiled_sdk_version:'0.6.0'},'0.6.0'),true);
});
