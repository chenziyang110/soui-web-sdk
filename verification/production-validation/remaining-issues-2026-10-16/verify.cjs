'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../..');
const standard=require(path.join(root,'tools/internal/standard-build.cjs'));
const read=name=>JSON.parse(fs.readFileSync(path.join(__dirname,name),'utf8').replace(/^\uFEFF/,''));
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const summary=read('SUMMARY.json'),index=read('INDEX.json');
const seen=new Set();
for(const entry of index.files){
  assert(entry.path.startsWith('raw/')&&!entry.path.split('/').includes('..')&&!seen.has(entry.path));seen.add(entry.path);
  const bytes=fs.readFileSync(path.join(__dirname,entry.path));
  assert.equal(bytes.length,entry.bytes,entry.path);assert.equal(hash(bytes),entry.sha256,entry.path);
}
assert.equal(summary.version,fs.readFileSync(path.join(root,'VERSION'),'utf8').trim());
assert.equal(summary.licenseSha256,hash(fs.readFileSync(path.join(root,'LICENSE.md'))));
assert.equal(summary.productionQualificationComplete,false);assert.equal(summary.releaseReady,false);
assert.equal(summary.desktopScenariosExecuted,false);
assert.equal(summary.eightHourSessionExecuted,false);assert.equal(summary.eightHourMixedExecuted,false);
assert.equal(summary.builds.length,4);
for(const build of summary.builds){
  const prefix='raw/'+build.architecture+'/'+build.fixture;
  const captured=read(prefix+'/capture/RESULT.json');
  const verified=standard.verifyStream(fs.readFileSync(path.join(__dirname,prefix+'/capture/prepare.ndjson')),
    read(prefix+'/capture/prepare.ndjson.context.json'),read(prefix+'/capture/prepare-ready.json'),captured.result);
  assert(captured.natural&&captured.result.exitCode===0&&captured.result.supervisorExitCode===0);
  assert(verified.terminal.closureConfirmed&&!verified.terminal.terminatedOwnedJob&&verified.terminal.activeProcesses===0);
  const original=read(prefix+'/suite/build.json');
  for(const key of ['architecture','configuration','version','sourceSha256','buildId','consumerBuildId'])assert.equal(build[key],original[key]);
  assert.equal(build.sourceSha256,summary.sourceSha256);assert.equal(build.payloadSha256,original.payload.sha256);
  const license=original.installedSdk.files.find(f=>f.path==='share/SouiSdk/LICENSE.md');
  assert.equal(license.sha256,summary.licenseSha256);assert.equal(build.installedLicenseSha256,license.sha256);
}
const upgrade=read('raw/package/RESULT.json');
assert.equal(upgrade.licenseSha256,summary.licenseSha256);
assert.equal(upgrade.packageManifestSha256,hash(fs.readFileSync(path.join(__dirname,'raw/package/sdk-manifest.json'))));
assert.equal(upgrade.previousManifestSha256,hash(fs.readFileSync(path.join(__dirname,'raw/package/previous-sdk-manifest.json'))));
assert.deepEqual(upgrade.operations.map(o=>o.version),summary.upgradeRollback.sequence);
for(const operation of upgrade.operations){assert(operation.businessUnchanged);assert.equal(operation.licensePresent,operation.version===summary.version);}
const gui=read('raw/gui-tests/result.json');assert(gui.passed&&gui.commands.every(c=>c.exitCode===0));
assert.equal(gui.sourceSha256,summary.gui.sourceSha256);
for(const command of gui.commands)for(const stream of ['stdout','stderr']){
  const bytes=fs.readFileSync(path.join(__dirname,'raw/gui-tests',command[stream]));
  assert.equal(hash(bytes),command.logs[stream].sha256);assert.equal(bytes.length,command.logs[stream].bytes);
}
assert.match(fs.readFileSync(path.join(__dirname,'raw/commands/regression.log'),'utf8'),/pass 46[\s\S]*fail 0/);
console.log(JSON.stringify({files:index.files.length,preparedBuilds:summary.builds.length,version:summary.version,releaseReady:false}));
