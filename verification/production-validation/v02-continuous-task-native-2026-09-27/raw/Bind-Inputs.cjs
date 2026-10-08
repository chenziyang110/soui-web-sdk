'use strict';
const fs=require('node:fs'),path=require('node:path');
const repo='C:/SOUI-SDK-development/preview14-scheduler';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const [phase,attempt,arch]=process.argv.slice(2);
if(!['before','after'].includes(phase)||!/^[a-z0-9-]+$/.test(attempt||'')||!['x86','x64'].includes(arch))throw Error('Invalid arguments');
const source=id.source(repo);
const snapshot={at:new Date().toISOString(),architecture:arch,source};
const before=path.join(__dirname,`${attempt}-before-input.json`);
if(phase==='after'){
  if(source.sha256!==id.readJson(before).source.sha256)throw Error('Source changed');
  snapshot.binaries=id.snapshot(repo,[`out/${arch}/tests/native/Release/soui_workbench_interaction_tests.exe`,`out/${arch}/tests/native/Release/soui_demo_session_renewal_tests.exe`]);
  for(const name of ['configure','build','interaction','renewal']){
    const result=id.readJson(path.join(__dirname,'command-logs',`${attempt}-${name}`,'result.json'));
    if(result.exitCode!==0||result.timedOut||result.error)throw Error(`Command failed: ${name}`);
  }
}
fs.writeFileSync(path.join(__dirname,`${attempt}-${phase}-input.json`),id.encoded(snapshot),{flag:'wx'});
console.log(JSON.stringify({phase,attempt,architecture:arch,sourceSha256:source.sha256,files:source.files.length,binaries:snapshot.binaries?.files.length,guiExecuted:false,releaseReady:false}));
