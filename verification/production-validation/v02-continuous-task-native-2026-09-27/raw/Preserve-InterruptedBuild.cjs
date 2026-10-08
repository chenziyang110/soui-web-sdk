'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview14-scheduler';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const attempt='green-x64-attempt1';
const read=n=>id.readJson(path.join(__dirname,n));
const before=read(`${attempt}-before-input.json`),source=id.source(repo);
assert.equal(source.sha256,before.source.sha256);
for(const name of ['configure','build']){const r=read(`command-logs/${attempt}-${name}/result.json`);assert.equal(r.exitCode,0);assert(!r.timedOut&&!r.error);}
const driver=read(`command-logs/${attempt}-driver/result.json`);assert.equal(driver.exitCode,1);assert(!driver.timedOut&&!driver.error);
assert(!fs.existsSync(path.join(__dirname,'command-logs',`${attempt}-interaction`)));
assert(!fs.existsSync(path.join(__dirname,'command-logs',`${attempt}-renewal`)));
const gate=read(`${attempt}-test-gate.json`);assert(gate.processes.some(p=>p.ProcessId===74456&&p.Name==='qemu-system-x86_64.exe'));
const frozen=path.join(__dirname,`${attempt}-compiled`);fs.mkdirSync(frozen);
const names=['soui_workbench_interaction_tests','soui_demo_session_renewal_tests'];
const files=names.map(n=>`out/x64/tests/native/Release/${n}.exe`);
for(const n of names){const from=path.join(repo,'out/x64/tests/native/Release',n+'.exe'),data=fs.readFileSync(from);
 assert.equal(data.readUInt16LE(data.readUInt32LE(0x3c)+4),0x8664);
 fs.writeFileSync(path.join(frozen,n+'.exe'),data,{flag:'wx'});
}
const commands=id.walk(path.join(repo,'out/x64/tests/native')).filter(n=>names.some(target=>n.startsWith(`${target}.dir/Release/`))&&/(?:CL|link)\.command\.1\.tlog$/.test(n));
assert(commands.length>=4);
for(const n of commands){const from=path.join(repo,'out/x64/tests/native',n),to=path.join(frozen,'commands',n),data=fs.readFileSync(from),text=data[0]===255&&data[1]===254?data.toString('utf16le'):data.toString('utf8');
 assert(!/[\\/]vcpkg[\\/]/i.test(text));fs.mkdirSync(path.dirname(to),{recursive:true});fs.writeFileSync(to,data,{flag:'wx'});
}
const result={status:'compiled-test-start-blocked-by-external-qemu',at:new Date().toISOString(),sourceSha256:source.sha256,sourceFiles:source.files.length,
 configurationExitCode:0,compilationExitCode:0,driverExitCode:1,testExecuted:false,guiExecuted:false,releaseReady:false,
 testGate:gate,originalBinaries:id.snapshot(repo,files),frozenFiles:id.snapshot(frozen,id.walk(frozen)),compilerLinkCommandFiles:commands.length,
 note:'Other QEMU appeared near the end of compilation. No isolated resource/timing conclusion, no native test pass. Attempt name is consumed; keep original records.'};
fs.writeFileSync(path.join(__dirname,`${attempt}-interrupted-review.json`),id.encoded(result),{flag:'wx'});
console.log(JSON.stringify({status:result.status,sourceSha256:source.sha256,binaries:files.length,commandFiles:commands.length,testExecuted:false}));
