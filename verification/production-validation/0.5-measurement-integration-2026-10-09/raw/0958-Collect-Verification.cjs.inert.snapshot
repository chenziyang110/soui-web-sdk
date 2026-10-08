'use strict';
const fs=require('node:fs'), path=require('node:path'), crypto=require('node:crypto'), cp=require('node:child_process');
const root=path.resolve(__dirname,'..'), output=path.join(root,'verification');
const evidence=require('./internal/build-evidence.cjs');
const identity=require('./internal/delivery-identity.cjs');
const releaseVerification=require('./internal/release-verification.cjs');
const sdkPackage=require('./sdk/soui-sdk.cjs');
const read=name=>JSON.parse(fs.readFileSync(path.join(root,name),'utf8').replace(/^\uFEFF/,''));
const digest=name=>crypto.createHash('sha256').update(fs.readFileSync(path.resolve(root,name))).digest('hex');
const save=(name,value)=>fs.writeFileSync(path.join(output,name),JSON.stringify(value,null,2)+'\n');
fs.mkdirSync(output,{recursive:true});
const version=fs.readFileSync(path.join(root,'VERSION'),'utf8').trim();
const scope=process.argv[2] || 'full';
if(!['full','--foundation'].includes(scope))throw new Error('Usage: Collect-Verification.cjs [--foundation]');
// A failed collection leaves an explicit non-acceptance, never yesterday's green result.
save('acceptance.json',{schema:'soui.source-delivery-acceptance/v2',version,scope,
 sourceDeliveryVerified:false,releaseReady:false,status:'collecting',startedAt:new Date().toISOString()});
function requirePassed(condition,message){if(!condition)throw new Error(message);}
function run(name,executable,args) {
 const result=cp.spawnSync(executable,args,{cwd:root,encoding:'utf8',windowsHide:true});
 fs.writeFileSync(path.join(output,name+'.log'),(result.stdout||'')+(result.stderr||''));
 requirePassed(result.status===0, name+' failed: inspect verification/'+name+'.log');
 return {passed:true,exitCode:result.status,log:name+'.log'};
}
const unit={
 ownedSupervision:run('owned-process-tests',process.execPath,['--test','tests/benchmark/owned-process-runner.test.cjs','tests/benchmark/owned-process-client.test.cjs','tests/benchmark/owned-process-client-boundaries.test.cjs','tests/acceptance/owned-supervision-boundaries.test.cjs','tests/benchmark/run-one-cleanup.test.cjs']),
 windowsSdkSelection:run('windows-sdk-selection-tests',process.execPath,['--test','tests/sdk/cmake-sdk-selection.test.cjs']),
 measurementProtocol:run('measurement-protocol-tests',process.execPath,['tools/internal/measurement-validation.cjs','protocol']),
 nativeMeasurementX64:run('native-measurement-x64',process.execPath,['tools/internal/measurement-validation.cjs','native','x64']),
 nativeMeasurementX86:run('native-measurement-x86',process.execPath,['tools/internal/measurement-validation.cjs','native','x86']),
 package:run('sdk-package-tests',process.execPath,['tests/sdk/sdk-package-smoke.cjs']),
 nativePayload:run('sdk-native-payload-tests',process.execPath,['--test','tests/sdk/sdk-native-payload.test.cjs']),
 interruption:run('sdk-installation-interruption-tests',process.execPath,['--test','tests/sdk/installation-interruption.test.cjs','tests/sdk/profile-installation-integration.test.cjs']),
 delivery:run('delivery-identity-tests',process.execPath,['--test','tests/sdk/delivery-identity.test.cjs']),
 modernDelivery:run('modern-evidence-tests',process.execPath,['--test','tests/sdk/modern-evidence.test.cjs']),
 release:run('release-verification-tests',process.execPath,['--test','tests/sdk/release-verification.test.cjs']),
 web:run('web-sdk-tests',process.execPath,['--test','tests/web/sdk-client.test.mjs','tests/web/capabilities.test.mjs','tests/web/action-contracts.test.mjs','tests/web/gui.test.mjs','tests/web/web-features.test.mjs','tests/web/native-resolvers.test.mjs','tests/web/session-renewal.test.mjs','tests/web/agent-session-renewal.test.mjs','tests/web/webview2-preload.test.mjs']),
 nativeActionsX64:run('native-actions-x64',path.join(root,'out/x64/tests/native/Release/soui_action_contract_tests.exe'),[]),
 nativeActionsX86:run('native-actions-x86',path.join(root,'out/x86/tests/native/Release/soui_action_contract_tests.exe'),[]),
 nativeX64:run('native-contract-x64',path.join(root,'out/x64/tests/native/Release/soui_sdk_contract_tests.exe'),[]),
 nativeX86:run('native-contract-x86',path.join(root,'out/x86/tests/native/Release/soui_sdk_contract_tests.exe'),[])
};
if(scope==='--foundation'){
 const matrix=[];
 const sizes=[];
 for(const arch of ['x86','x64']){
  const {build,smoke}=evidence.verifySmoke(root,arch,'Release');
  save('native-'+arch+'-Release.json',smoke);
  matrix.push({architecture:arch,configuration:'Release',buildId:build.buildId,sourceSha256:build.source.sha256,
   scenarios:smoke.runs.map(({target,scenario,passed})=>({target,scenario,passed})),evidence:'native-'+arch+'-Release.json'});
  for(const target of evidence.targets(root)){
   const name=target+'-'+version+'-windows-'+arch+'.zip';
   const report=read('dist/'+name+'.json');
   requirePassed(report.buildId===build.buildId && report.sourceSha256===build.source.sha256 &&
    report.payload.sha256===build.payloads[target].sha256 && report.archive.sha256===digest('dist/'+name) &&
    report.archive.bytes===fs.statSync(path.join(root,'dist',name)).size,'Application archive evidence drift');
   save('size-'+target+'-'+arch+'.json',report);
   sizes.push({target,architecture:arch,bytes:report.payload.bytes,compressedBytes:report.archive.bytes,
    evidence:'size-'+target+'-'+arch+'.json'});
  }
 }
 save('acceptance.json',{schema:'soui.source-delivery-acceptance/v2',version,scope:'foundation',status:'passed',
  sourceDeliveryVerified:false,releaseReady:false,finishedAt:new Date().toISOString(),unitTests:unit,
  nativeApplicationMatrix:matrix,sizes,
  limitations:['Foundation checks only, not a complete 0.3 release acceptance.',
   'Clean clone/archive builds are recorded separately; full dependency rebuild and four-configuration matrix are still required.',
   'Electron performance comparison, long-run lifecycle tests and 0.3 desktop capability work remain open.',
   'No remote CI; production, signing and binary redistribution gates remain open.']});
 console.log(JSON.stringify({version,foundationVerified:true,sourceDeliveryVerified:false,releaseReady:false}));
 process.exit(0);
}
const matrix=[];
for(const arch of ['x86','x64'])for(const config of ['Debug','Release']){
 const {build,smoke}=evidence.verifySmoke(root,arch,config);
 requirePassed(build.nativeBuild==='passed' && smoke.passed,arch+' '+config+' was not verified');
 save('native-'+arch+'-'+config+'.json',smoke);
 matrix.push({architecture:arch,configuration:config,buildPassed:true,templateAndDemoPassed:true,evidence:'native-'+arch+'-'+config+'.json'});
}
const dependencies=[];
const modernMatrix=[];
for(const arch of ['x86','x64'])for(const config of ['Debug','Release']){
 const {smoke}=evidence.verifyModernSmoke(root,arch,config);
 const file='modern-'+arch+'-'+config+'.json';
 save(file,smoke);
 modernMatrix.push({architecture:arch,configuration:config,passed:true,evidence:file});
 run('native-suite-'+arch+'-'+config,'ctest',['--test-dir','out/'+arch,'-C',config,'--output-on-failure']);
}
for(const arch of ['x86','x64']){
 const build=read('out/dependencies/'+arch+'/build-All.json');
 const smoke=read('out/dependencies/'+arch+'/rebuilt-smoke.json');
 requirePassed(build.status==='built' && build.artifacts.length===6 && smoke.passed,arch+' dependency build/smoke incomplete');
 for(const artifact of build.artifacts){
  requirePassed(digest(artifact.path)===artifact.sha256.toLowerCase(),'Rebuilt dependency drift');
  artifact.path=path.relative(root,artifact.path).replace(/\\/g,'/');
 }
 save('dependencies-'+arch+'.json',{build,smoke});
 dependencies.push({architecture:arch,sourceBuildPassed:true,rebuiltRuntimePassed:true,evidence:'dependencies-'+arch+'.json'});
}
const generated=read('verification/generated-project.json');
const fresh=read('verification/fresh-project.json');
const relocated=read('verification/relocation.json');
const relocationSmoke=read('verification/relocated-native-smoke.json');
const currentPackage=sdkPackage.readPackage(path.join(root,'dist/sdk',version));
releaseVerification.validateReports([generated,fresh,relocated,relocationSmoke],{version,
 sourceSha256:identity.source(root).sha256,sdkManifestSha256:identity.hash(identity.encoded(currentPackage))});
const sdkOnly=evidence.verifyBuild(root,'x64','Release',true);
requirePassed(sdkOnly.sdkOnly && sdkOnly.nativeBuild==='passed','SDK independent build missing');
const consumer=run('cmake-consumer',releaseVerification.verifyConsumer(root,generated.consumer),[]);
const sources=[];
function walk(relative){
 for(const entry of fs.readdirSync(path.join(root,relative),{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))){
  if(['node_modules','dist','out','.vs'].includes(entry.name))continue;
  const name=relative+'/'+entry.name;
  if(entry.isDirectory())walk(name);else if(entry.isFile())sources.push({path:name,sha256:digest(name)});
 }
}
for(const dir of ['sdk','templates','demos','cmake','tools','tests'])walk(dir);
sources.push({path:'CMakeLists.txt',sha256:digest('CMakeLists.txt')});
save('first-party-source-hashes.json',sources);
const audits={};
for(const scope of ['web-sdk','template-web','demo-web','modern-web']) audits[scope]=read('verification/'+scope+'-audit.json').metadata.vulnerabilities;
const acceptance={schema:'soui.source-delivery-acceptance/v1',version,finishedAt:new Date().toISOString(),
 sourceDeliveryVerified:true,releaseReady:false,toolchain:read('verification/toolchain.json'),
 nativeApplicationMatrix:matrix,modernApplicationMatrix:modernMatrix,unitTests:unit,sdkOnly:{passed:true,architecture:'x64',configuration:'Release'},externalCmakeConsumer:consumer,
 generatedApplication:{passed:true,upgradeTested:true,evidence:'generated-project.json',freshProjectEvidence:'fresh-project.json'},
 relocatedDistribution:{passed:true,evidence:'relocation.json',nativeEvidence:'relocated-native-smoke.json'},
 dependencies,dependencyAudit:audits,firstPartySourceHashes:'first-party-source-hashes.json',
 limitations:['Preview only; production acceptance remains incomplete.',
 'Long-run memory, shutdown stability, target OS matrix and Electron performance parity are not closed.',
 'Complete binary redistribution review remains open.',
 'Remote CI is intentionally not configured; signing, installer and product-specific update/rollback were not executed.']};
save('acceptance.json',acceptance);
console.log(JSON.stringify({version,sourceDeliveryVerified:true,releaseReady:false,nativeConfigurations:matrix.length,rebuiltArchitectures:dependencies.length}));
