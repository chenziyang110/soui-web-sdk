'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const here=__dirname,inputFile=here+'/CandidateStageInputs.approved.json';
const approvedSha='63ee99977127c2b07db14bd6f7a2f2c9e008369278668b7764c68aea9bb28297';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>{const b=fs.readFileSync(p);return{path:p.replaceAll('\\','/'),bytes:b.length,sha256:hash(b)}};
const save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
async function main(){
 const architecture=process.argv[2];assert.equal(process.argv.length,3);assert(['x64','x86'].includes(architecture));
 assert.equal(hash(fs.readFileSync(inputFile)),approvedSha);
 const input=JSON.parse(fs.readFileSync(inputFile,'utf8'));
 assert.equal(process.version,'v24.14.0');assert.equal(pin(fs.realpathSync(process.execPath)).sha256,input.tools.node.sha256);
 for(const name of ['NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE'])assert(!process.env[name],'Clean original Node environment required');
 const subjectRunner='F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe';
 assert.equal(pin(subjectRunner).sha256,'1a4cb4bea83a70ac06efb9c50e26a5c3345aab589f9c2e1306b244f68b09713f');
 const output=here+'/stage-'+architecture+'-'+crypto.randomUUID();fs.mkdirSync(output);
 const script=input.sourceRoot+'/tests/acceptance/message-port-stage-candidate.cjs';
 const args=[script,'ROOT_SHARED_ORDINARY_CANDIDATE_STAGE_V1',inputFile,approvedSha,architecture];
 save(output+'/Inputs.actual.json',{schema:'soui.message-port-candidate-stage-invocation/v1',architecture,
  executable:input.tools.node,args,cwd:input.sourceRoot,script:pin(script),wrapper:pin(__filename),runner:pin(subjectRunner),
  timeoutMs:120000,drainMs:2000,newCompilationExecuted:false,browserBehaviorExecuted:false,releaseReady:false});
 const stdout=fs.openSync(output+'/stdout.raw','wx'),stderr=fs.openSync(output+'/stderr.raw','wx');
 let result,ready;
 try{
  const child=require(input.sourceRoot+'/tools/internal/owned-process.cjs').launchOwned(input.tools.node.path,args,
   {runner:subjectRunner,cwd:input.sourceRoot,report:output+'/owned.ndjson',timeoutMs:120000,drainMs:2000,stdout,stderr});
  ready=await child.ready;save(output+'/ready.json',ready);result=await child.finished;
 }finally{fs.closeSync(stdout);fs.closeSync(stderr);}
 save(output+'/Result.actual.json',{architecture,result,newCompilationExecuted:false,browserBehaviorExecuted:false,releaseReady:false});
 assert(result.passed&&result.exitCode===0&&result.supervisorExitCode===0&&result.closureConfirmed&&!result.terminal.terminatedOwnedJob&&result.terminal.activeBeforeCleanup===0&&result.terminal.activeProcesses===0,'Public stage did not close naturally');
 const stream=require(input.sourceRoot+'/tools/internal/standard-build.cjs').verifyStream(fs.readFileSync(output+'/owned.ndjson'),JSON.parse(fs.readFileSync(output+'/owned.ndjson.context.json','utf8')),ready,result);
 const answer=JSON.parse(fs.readFileSync(output+'/stdout.raw','utf8').trim());
 assert.equal(answer.architecture,architecture);assert.deepEqual(answer.payloadDifferences,['node.dll']);
 assert.equal(answer.resourceExecuted,false);assert.equal(answer.qualificationPassed,false);
 const build=require(input.sourceRoot+'/tests/acceptance/runner.cjs').verify(answer.actualCandidateSuite);
 const summary={schema:'soui.message-port-candidate-stage-actual/v1',architecture,output,answer,
  version:build.version,sdkBuildId:build.buildId,consumerBuildId:build.consumerBuildId,sourceSha256:build.sourceSha256,
  newCompilationExecuted:false,webRebuilt:false,actualSdkInstallExecuted:true,rawOwnedVerified:true,
  naturalStageClosure:stream.terminal,rootResult:pin(output+'/Result.actual.json'),stdout:pin(output+'/stdout.raw'),stderr:pin(output+'/stderr.raw'),
  browserBehaviorExecuted:false,resourcePassed:false,qualificationPassed:false,releaseReady:false};
 save(output+'/Summary.actual.json',summary);console.log(JSON.stringify(summary));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1});
