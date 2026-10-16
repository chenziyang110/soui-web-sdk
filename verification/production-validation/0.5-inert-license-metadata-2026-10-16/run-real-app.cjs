'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',source='C:/work/sm05-generated-build-probe7/project/out/x64/bin/Release/CompatUpgradeProbe',target=__dirname+'/real-app';
const id=require(root+'/tools/internal/delivery-identity.cjs'),standard=require(root+'/tools/internal/standard-build.cjs'),e=require(root+'/tools/internal/dependency-evidence.cjs');
assert(!fs.existsSync(target));const original=id.payload(source,'CompatUpgradeProbe');
fs.cpSync(source,target,{recursive:true,errorOnExist:true,force:false,filter:()=>true});
for(const change of JSON.parse(fs.readFileSync(__dirname+'/RENAMES.json')).changes){
 const from=id.safePath(target,'resources/legal/'+change.from),to=id.safePath(target,'resources/legal/'+change.to);
 assert.equal(id.hash(fs.readFileSync(from)),change.sha256);assert(!fs.existsSync(to));fs.renameSync(from,to);
}
fs.copyFileSync(__dirname+'/proposed/third_party/licenses/npm/index.json',target+'/resources/legal/third_party/licenses/npm/index.json');
const modified=id.payload(target,'CompatUpgradeProbe'),pins=[__filename,target+'/CompatUpgradeProbe.exe',target+'/node.dll'].map(e.pin);
for(const name of ['CompatUpgradeProbe.exe','node.dll'])assert.equal(id.hash(fs.readFileSync(source+'/'+name)),id.hash(fs.readFileSync(target+'/'+name)));
const out=__dirname+'/real-app-smoke';fs.mkdirSync(out);fs.writeFileSync(out+'/INPUTS.json',JSON.stringify({original,modified,pins,scope:'Packaging-only comparison using unchanged historical preview.1 executable and kernel',currentVersionQualified:false},null,2));
(async()=>{
 const c=await standard.capture(out,'smoke',target+'/CompatUpgradeProbe.exe',['--smoke-test'],{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:target,timeoutMs:45000,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
 const log=fs.readFileSync(target+'/smoke.log','utf8');fs.writeFileSync(out+'/app.log',log);
 id.verifyPayload(source,'CompatUpgradeProbe',original);id.verifyPayload(target,'CompatUpgradeProbe',modified);pins.forEach(e.checkPin);
 const ready=/application-ready/.test(log)&&/host-close-complete/.test(log);
 fs.writeFileSync(out+'/RESULT.json',JSON.stringify({natural,ready,log,result:c.result,terminal:v.terminal,currentVersionQualified:false},null,2),{flag:'wx'});
 console.log(JSON.stringify({natural,ready,log}));assert(natural&&ready,'Original app plus corrected legal packaging failed');
})().catch(error=>{console.error(error);process.exitCode=1;});
