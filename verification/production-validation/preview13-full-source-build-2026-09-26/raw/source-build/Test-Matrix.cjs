'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,source=path.join(root,'source'),id=require(path.join(source,'tools/internal/delivery-identity.cjs')),evidence=require(path.join(source,'tools/internal/build-evidence.cjs'));
const expected='a1007f2188102dfa4de4e79f73a4e0b2c2b8b99cf3e892a3d1e1e3794fd51e68',env={...process.env};for(const key of Object.keys(env))if(key.startsWith('SOUI_'))delete env[key];
async function run(name,program,args){const child=cp.spawn(process.execPath,[path.join(root,'Capture-Command.cjs'),name,'120000',program,...args],{env,windowsHide:true,stdio:'inherit'});const result=await new Promise(resolve=>{child.on('error',e=>resolve({error:String(e)}));child.on('close',(code,signal)=>resolve({code,signal}));});assert.equal(result.code,0,name+' '+JSON.stringify(result));const original=id.readJson(path.join(root,'command-logs',name,'result.json'));assert.equal(original.exitCode,0);assert.equal(original.timedOut,false);assert.equal(original.error,null);return original;}
async function main(){const compiled=id.readJson(path.join(root,'result.json'));assert.equal(compiled.status,'compiled-four-configurations');assert.equal(compiled.sourceSha256,expected);assert.equal(id.source(source).sha256,expected);
 await run('exported-delivery-identity',process.execPath,['--test',path.join(source,'tests/sdk/delivery-identity.test.cjs')]);
 const tests=[];
 for(const row of compiled.matrix){const arch=row.architecture,config=row.configuration,name='ctest-'+arch+'-'+config.toLowerCase();const before=evidence.verifyBuild(source,arch,config);assert.equal(before.buildId,row.buildId);await run(name,'ctest.exe',['--test-dir',path.join(source,'out',arch),'-C',config,'-j','1','--output-on-failure']);
  const log=fs.readFileSync(path.join(root,'command-logs',name,'stdout.log'),'utf8');assert(/100% tests passed, 0 tests failed out of 11/.test(log),name);
  evidence.verifyBuild(source,arch,config);const saved=path.join(root,'builds','build-'+arch+'-'+config.toLowerCase());for(const [target,payload]of Object.entries(before.payloads))id.verifyPayload(path.join(saved,target),target,payload);
  tests.push({architecture:arch,configuration:config,buildId:row.buildId,tests:11,exitCode:0,renderedGuiExecuted:false});
 }
 require(path.join(source,'tools/internal/source-archive.cjs')).verify(source);assert.equal(id.source(source).sha256,expected);
 fs.writeFileSync(path.join(root,'test-result.json'),id.encoded({sourceSha256:expected,version:compiled.version,exportedDeliveryIdentityTests:8,tests,nativeTests:44,sourceAndPayloadUnchanged:true,renderedGuiExecuted:false,desktopRoundtripExecuted:false,fullDependencyRebuild:false,sourceDeliveryVerified:false,benchmarkValid:false,releaseReady:false}),{flag:'wx'});
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
