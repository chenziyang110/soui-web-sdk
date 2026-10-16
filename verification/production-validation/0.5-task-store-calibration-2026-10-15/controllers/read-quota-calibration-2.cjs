'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',arch=process.argv[2],attempt=process.argv[3],limit=process.argv[4];assert(/^[1-9][0-9]*$/.test(limit)&&BigInt(limit)<=0xffffffffn);assert(['x64','x86'].includes(arch)&&/^[a-z0-9-]+$/.test(attempt));
const folder=__dirname+'/quota-calibration-'+arch+'-'+attempt,base=folder+'/baseline';
const pin=require(root+'/tools/internal/dependency-evidence.cjs').pin,standard=require(root+'/tools/internal/standard-build.cjs');
const contextBytes=fs.readFileSync(base+'.ndjson.context.json'),context=JSON.parse(contextBytes.toString('utf8')),
 raw=fs.readFileSync(base+'.ndjson'),ready=JSON.parse(fs.readFileSync(base+'-ready.json','utf8')),finished=JSON.parse(fs.readFileSync(base+'-finished.json','utf8'));
assert.equal(context.executable,'C:/work/sm05-quota-calibration-'+arch+'-'+attempt+'/Release/SouiTaskStoreCalibration.exe');assert.deepEqual(context.args,[limit]);
const native=standard.verifyStream(raw,context,ready,finished);
require(root+'/tools/internal/devkit-command.cjs').validateStage(raw,contextBytes,ready,finished,{executable:context.executable,args:[limit],cwd:context.cwd,timeoutMs:360000,drainMs:2000,
 executableSha256:pin(context.executable).sha256,runnerSha256:pin('F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe').sha256,contextSha256:pin(base+'.ndjson.context.json').sha256});
const summary=require(root+'/tests/calibration/read-task-store-quota.cjs').read(fs.readFileSync(base+'-stdout.bin'),ready,limit);
assert(finished.exitCode===0&&finished.supervisorExitCode===0&&native.terminal.activeProcesses===0&&!native.terminal.terminatedOwnedJob);
const result={...summary,scope:'Diagnostic against the explicitly captured installed SDK, not final release calibration',nativeProcessClosureVerified:true,
 stdout:pin(base+'-stdout.bin'),context:pin(base+'.ndjson.context.json'),raw:pin(base+'.ndjson'),finished:pin(base+'-finished.json'),
 executable:pin(context.executable),linkMap:pin('C:/work/sm05-quota-calibration-'+arch+'-'+attempt+'/SouiTaskStoreCalibration.map')};
fs.writeFileSync(folder+'/READ-RESULT.json',JSON.stringify(result,null,2),{flag:'wx'});console.log(JSON.stringify(result));
