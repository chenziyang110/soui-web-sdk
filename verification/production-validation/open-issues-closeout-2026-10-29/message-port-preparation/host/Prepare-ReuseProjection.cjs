'use strict';
// Metadata-only projection of actual old producers. Does not stage or launch a consumer.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const here=__dirname,sourceRoot='F:/work/soui-issue96',currentRoot='C:/sc07';
const evidenceRoot=sourceRoot+'/verification/production-validation/remaining-issues-2026-10-16';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const read=p=>fs.readFileSync(p),json=p=>JSON.parse(read(p).toString('utf8').replace(/^\uFEFF/,''));
const pin=p=>{const s=fs.lstatSync(p);assert(s.isFile()&&!s.isSymbolicLink());const b=read(p);return{path:p.replaceAll('\\','/'),bytes:b.length,sha256:hash(b)}};
const check=p=>{const a=pin(p.path);assert.equal(a.bytes,p.bytes);assert.equal(a.sha256,p.sha256);return a};
const write=(name,value)=>{const p=path.join(here,name);fs.writeFileSync(p,JSON.stringify(value,null,2)+'\n',{flag:'wx'});return pin(p)};
function exportsOf(dll){
 const pe=dll.readUInt32LE(0x3c);assert.equal(dll.readUInt32LE(pe),0x4550);
 const opt=pe+24,magic=dll.readUInt16LE(opt);assert(magic===0x10b||magic===0x20b);
 const start=opt+dll.readUInt16LE(pe+20),n=dll.readUInt16LE(pe+6),sections=[];
 for(let i=0;i<n;i++){const at=start+40*i;sections.push({rva:dll.readUInt32LE(at+12),size:Math.max(dll.readUInt32LE(at+8),dll.readUInt32LE(at+16)),raw:dll.readUInt32LE(at+20)});}
 const offset=rva=>{const s=sections.find(s=>rva>=s.rva&&rva<s.rva+s.size);assert(s,'Unmapped PE RVA');return s.raw+rva-s.rva};
 const rva=dll.readUInt32LE(opt+(magic===0x20b?112:96)),at=offset(rva),names=offset(dll.readUInt32LE(at+32)),result=[];
 for(let i=0;i<dll.readUInt32LE(at+24);i++){const begin=offset(dll.readUInt32LE(names+4*i));const end=dll.indexOf(0,begin);assert(end>=begin);result.push(dll.toString('ascii',begin,end));}
 return {machine:dll.readUInt16LE(pe+4),names:result.sort()};
}
const archivedIndex=json(evidenceRoot+'/INDEX.json'),archivedSummary=json(evidenceRoot+'/SUMMARY.json');
assert.equal(archivedSummary.version,'0.5.1-preview.1');
const standard=require(sourceRoot+'/tools/internal/standard-build.cjs');
const runner=require(sourceRoot+'/tests/acceptance/runner.cjs');
const originalSchemas=[],architectures=[],staticCandidateChecks=[],rawPins=[];
const projectionStages=[];
for(const architecture of ['x64','x86']){
 const recorded=archivedSummary.builds.find(row=>row.architecture===architecture&&row.fixture==='message-port');assert(recorded);
 const prefix='raw/'+architecture+'/message-port/capture/';
 const selected={};
 for(const name of ['INPUTS.json','RESULT.json','prepare.ndjson','prepare.ndjson.context.json','prepare-ready.json','prepare-finished.json','prepare-stdout.bin','prepare-stderr.bin']){
  const relative=prefix+name,expected=archivedIndex.files.find(row=>row.path===relative);assert(expected);
  const actual=check({...expected,path:evidenceRoot+'/'+relative});selected[name]=actual;rawPins.push(actual);
 }
 const result=json(selected['RESULT.json'].path),inputs=json(selected['INPUTS.json'].path);
 assert.equal(inputs.root,sourceRoot);assert.equal(inputs.architecture,architecture);
 assert(inputs.args.includes('-PrepareOnly')&&inputs.args.includes('message-port'));
 const verified=standard.verifyStream(read(selected['prepare.ndjson'].path),json(selected['prepare.ndjson.context.json'].path),json(selected['prepare-ready.json'].path),result.result);
 assert(result.natural&&result.result.exitCode===0&&result.result.supervisorExitCode===0&&result.result.passed&&verified.terminal.closureConfirmed&&!verified.terminal.terminatedOwnedJob&&verified.terminal.activeProcesses===0);
 const baseline=runner.verify(recorded.suite);
 for(const key of ['version','architecture','buildId','consumerBuildId','sourceSha256'])assert.equal(baseline[key],recorded[key]);
 assert.equal(baseline.payload.sha256,recorded.payloadSha256);
 const suiteBuild=pin(recorded.suite+'/build.json'),sdkBuild=pin(sourceRoot+'/out/sdk-'+architecture+'/build-Release.json');
 const sdk=json(sdkBuild.path);assert.equal(sdk.buildId,baseline.buildId);assert.equal(sdk.source.sha256,baseline.sourceSha256);
 const producerSummary=json(currentRoot+'/verification/production-validation/0.6.0-source2-kernel-2026-10-29/'+architecture+'/summary.json');
 const candidate=pin(currentRoot+'/third_party/prebuilt/'+architecture+'/miniblink/node.dll');
 assert.equal(candidate.bytes,producerSummary.dll.bytes);assert.equal(candidate.sha256,producerSummary.dll.sha256);
 const baseKernel=pin(recorded.suite+'/kernel-input/node.dll');assert.equal(baseKernel.sha256,baseline.kernel.sha256);
 const baseExports=exportsOf(read(baseKernel.path)),candidateExports=exportsOf(read(candidate.path));
 assert.equal(baseExports.machine,candidateExports.machine);
 const expected=baseExports.names.filter(name=>/^(wke|js)/.test(name));
 const missing=expected.filter(name=>!candidateExports.names.includes(name));assert.equal(missing.length,0,'Candidate lacks old public exports');
 const compatibility={architecture,baselineKernel:baseKernel,candidateKernel:candidate,machine:baseExports.machine,
  oldPublicExports:expected.length,missingOldPublicExports:missing,exportPresenceOnly:true,
  fullAbiCompatibilityQualified:false,browserBehaviorExecuted:false,candidateVersion:'0.6.0 Source2 kernel',
  candidateProducerSummary:pin(currentRoot+'/verification/production-validation/0.6.0-source2-kernel-2026-10-29/'+architecture+'/summary.json')};
 staticCandidateChecks.push(compatibility);
 const sourceReads={sourceFileCount:sdk.source.files.length,normalizedSourceBytes:sdk.source.bytes,
  requiredSourceRoots:['sdk','cmake','tools','third_party/include','third_party/prebuilt','third_party/sources/zlib','third_party/licenses','templates','demos','tests','benchmarks'],
  rootFiles:['VERSION','CMakeLists.txt','LICENSE.md','THIRD_PARTY_NOTICES.md'],sdkBuild,
  additionalBuildReads:['out/sdk-'+architecture+'/Release/soui_sdk.lib','out/sdk-'+architecture+'/CMakeCache.txt'],
  originalSuite:recorded.suite,wholeOriginalSuiteMustRemainByteExact:true};
 architectures.push({architecture,baselineSuite:recorded.suite,baselineBuild:suiteBuild,sdkBuild,
  baselineConsumerBuildId:baseline.consumerBuildId,consumerExe:pin(recorded.suite+'/consumer/bin/Release/SouiAcceptance.exe'),
  candidateKernel:candidate,baselineKernel:baseKernel,sourceReads});
 projectionStages.push({job:{name:'prepare-'+architecture},accepted:true,result:result.result,
  mappingMeaning:'Review projection of the archived original PrepareOnly Job; not a new invocation or altered old result.',
  originalInputs:selected['INPUTS.json'],originalResult:selected['RESULT.json'],originalReport:selected['prepare.ndjson'],
  originalContext:selected['prepare.ndjson.context.json'],historicalActualPreparation:true,newCompilationExecuted:false});
 originalSchemas.push({architecture,inputKeys:Object.keys(inputs),resultKeys:Object.keys(result),currentConsumerBuildId:baseline.consumerBuildId});
}
const producerProjection=write('OriginalPrepareJobs.review-projection.json',{
 schema:'soui.original-prepare-review-projection/v1',completed:true,failure:null,
 historicalActualPreparation:true,newCompilationExecuted:false,newPreparationExecuted:false,reviewCreatedNow:true,
 createdUtc:new Date().toISOString(),sourceRoot,version:'0.5.1-preview.1',stages:projectionStages});
const review=write('ProducerReuse.metadata-review.actual.json',{
 schema:'soui.message-port-producer-reuse-metadata-review/v1',createdUtc:new Date().toISOString(),
 executable:pin(fs.realpathSync(process.execPath)),argv:process.argv,cwd:process.cwd(),script:pin(__filename),
 archivedSummary:pin(evidenceRoot+'/SUMMARY.json'),archivedIndex:pin(evidenceRoot+'/INDEX.json'),rawPins,
 historicalActualPreparation:true,newCompilationExecuted:false,newPreparationExecuted:false,
 reviewCreatedNow:true,metadataValidationPassed:true,rootIndependentReviewPending:true,
 producerProjection,originalSchemas,staticCandidateChecks,
 resultScope:'Missing persistent public fixture behavior on historical 0.5.1-preview.1 installed SDK with old0.5/current0.6 DLL arms; not 0.6.0 SDK production qualification.',
 attemptedOriginalPrivateCandidateRoots:['F:/work/sm05/out/bh2-86cdf392','F:/work/SOUI_WEB/out/bh2-86cdf392','F:/work/soui-issue96/out/bh2-86cdf392'],
 originalPrivateCandidateNotFoundInThoseRoots:true,
 browserBehaviorExecuted:false,candidateStagingExecuted:false,completeKernelToolchainQualified:false,qualificationPassed:false,releaseReady:false});
for(const row of architectures){row.preparationSummary=producerProjection;row.producerReview=review;}
const node=pin(fs.realpathSync(process.execPath)),cmake=pin('C:/Program Files/CMake/bin/cmake.exe');
const input=write('CandidateStageInputs.draft.json',{
 schema:'soui.ordinary-shared-candidate-stage.actual-inputs/v1',sourceRoot,
 preparationCompleted:true,producerClosureReviewed:false,rootIndependentReviewPending:true,
 historicalActualPreparation:true,newCompilationExecuted:false,reviewCreatedNow:true,
 tools:{node,cmake},pins:[pin(sourceRoot+'/tests/acceptance/message-port-stage-candidate.cjs'),
  pin(sourceRoot+'/tests/acceptance/runner.cjs'),pin(sourceRoot+'/tests/acceptance/message-port-ab-controller.cjs'),
  pin(sourceRoot+'/tests/acceptance/message-port-report-reader.cjs'),...rawPins],architectures,
 completeKernelToolchainQualified:false,qualificationPassed:false,releaseReady:false});
console.log(JSON.stringify({producerProjection,review,candidateStageDraft:input,
 readyForRootIndependentReview:true,candidateStagingExecuted:false,browserBehaviorExecuted:false,releaseReady:false}));
