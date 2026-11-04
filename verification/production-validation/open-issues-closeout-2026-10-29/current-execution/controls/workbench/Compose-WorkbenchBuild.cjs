'use strict';
// Output-only composition of two independently completed finite build cases.
// It executes no compiler, product, GUI or policy case, and never edits an old attempt.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {isDeepStrictEqual}=require('node:util');
const base=__dirname,oldAttempt=path.join(base,'attempt-ca3ff662-7003-4503-a652-00e198ce53b0');
const oldSummarySha='ee2422f682d39fee1fa45ee3ea646a56975f9c3f53abb9cdc0d045ad81676746';
const runnerSha='f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const json=f=>JSON.parse(fs.readFileSync(f,'utf8'));
function pin(f){
 const absolute=path.resolve(f),a=fs.lstatSync(absolute,{bigint:true});assert(a.isFile()&&!a.isSymbolicLink());
 const b=fs.readFileSync(absolute),z=fs.lstatSync(absolute,{bigint:true});
 for(const k of ['dev','ino','size','mtimeNs','ctimeNs','nlink'])assert.equal(z[k],a[k]);assert.equal(BigInt(b.length),a.size);
 return {path:absolute,bytes:b.length,sha256:sha(b),fileId:a.ino.toString(),mtimeNs:a.mtimeNs.toString(),ctimeNs:a.ctimeNs.toString()};
}
function write(directory,name,value){fs.writeFileSync(path.join(directory,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
function walk(d){const out=[];for(const e of fs.readdirSync(d,{withFileTypes:true})){const f=path.join(d,e.name);assert(!e.isSymbolicLink());if(e.isDirectory())out.push(...walk(f));else{assert(e.isFile());out.push(f);}}return out.sort();}
function bytesEqual(actual,expected){assert.equal(actual.bytes,expected.bytes);assert.equal(actual.sha256,expected.sha256);}
const samePath=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.win32.normalize(a).toLowerCase()===path.win32.normalize(b).toLowerCase();
function natural(r){const t=r.terminal;assert(r.passed===true&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&r.stopRequested===false&&r.fallbackUsed===false&&r.closureConfirmed===true);assert(t&&t.reason==='completed'&&t.rootExited===true&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&t.terminatedOwnedJob===false&&t.limitTerminatedProcesses===0&&t.closureConfirmed===true&&t.passed===true);}
function verifyCase(attempt,architecture,parent){
 const folder=path.join(attempt,architecture),verdict=json(path.join(folder,'case-verdict.json'));
 assert.deepEqual(verdict,parent.verdicts.find(v=>v.architecture===architecture));
 assert.equal(verdict.schema,'soui.workbench-consumer-build-only-case/v1');assert.equal(verdict.architecture,architecture);
 assert(verdict.genericSupervisionPassed===true&&verdict.wholeJobNaturallyClosed===true&&verdict.executableExecuted===false&&verdict.oldFailuresReclassified===false&&verdict.productionQualified===false&&verdict.releaseReady===false);
 const result=json(path.join(folder,'native-finished.json')),ready=json(path.join(folder,'native-ready.json')),context=json(path.join(folder,'native.ndjson.context.json')),raw=fs.readFileSync(path.join(folder,'native.ndjson'));
 require('C:/sc07/tools/internal/standard-build.cjs').verifyStream(raw,context,ready,result);natural(result);
 assert.deepEqual(ready,verdict.root);assert.deepEqual(result.terminal,verdict.terminal);assert.equal(result.runnerSha256,runnerSha);assert.equal(result.reportSha256,sha(raw));
 assert.equal(context.timeoutMs,120000);assert.equal(context.drainMs,2000);assert(!context.tailImagePin);
 const receipt=json(path.join(folder,'root-return.json'));assert(receipt.completed===true&&receipt.productExecuted===false&&receipt.executableExecuted===false&&receipt.oldFailureReclassified===false&&receipt.cleanupFailures.length===0);assert.equal(receipt.architecture,architecture);assert.equal(receipt.rootPid,ready.pid);
 const before=json(path.join(folder,'tool-pins-before.json')),after=json(path.join(folder,'tool-pins-after.json'));assert.deepEqual(before,after);for(const expected of before)bytesEqual(pin(expected.path),expected);
 const invocations=json(path.join(folder,'invocations.json'));assert.equal(invocations.environment.MSBUILDDISABLENODEREUSE,'1');assert.equal(invocations.toolchain.MsvcVersion,'14.44.35207');assert.equal(invocations.toolchain.HostArchitecture,'x64');assert.equal(invocations.toolchain.Toolset,'v143');assert.equal(invocations.compilerHelperSuppressionConfigured,false);
 const rows=raw.toString('utf8').trimEnd().split('\n').map(JSON.parse),expectedMembers=[];
 for(const phase of ['configure','build']){
  const phaseFiles=walk(path.join(folder,phase)).filter(f=>path.basename(f)==='returned.json');assert.equal(phaseFiles.length,1);const r=json(phaseFiles[0]);
  assert(r.schema==='soui.msvc-compiler-lifecycle/v1'&&r.phase===phase&&r.architecture===architecture&&r.serverNaturallyExited===true&&r.exitCode===0&&r.nativeExitCode===0&&r.failure===null&&r.readinessSignaled===true&&r.readinessIsOwnershipProof===false&&r.preexistingServiceRejected===true&&r.compilerHelperSuppressionConfigured===false&&r.originalOwnedJobClosureRequired===true&&r.forcedTerminationUsed===false&&r.naturalWaitLimitMs===30000);
  assert.deepEqual(r.arguments,['-timeout:20']);assert.equal(r.readyEvent,'VCTIP_14.44.35226.0_READY_EVENT');
  const compiler=before.find(f=>samePath(f.path,r.compiler.path)),image=before.find(f=>samePath(f.path,r.image.path)),config=before.find(f=>samePath(f.path,r.image.path+'.config'));assert(compiler&&image&&config);assert.equal(compiler.sha256,r.compiler.sha256);bytesEqual(image,r.image);assert.equal(config.sha256,r.image.configSha256);
  const id={pid:r.pid,creationFileTime:r.creationFileTime,image:r.image.path};assert(rows.some(x=>x.event==='owned-member'&&x.pid===id.pid&&x.creationFileTime===id.creationFileTime&&samePath(x.image,id.image)));expectedMembers.push(id);
 }
 assert.deepEqual(expectedMembers,verdict.helperPhases);
 const allTips=rows.filter(r=>r.event==='owned-member'&&typeof r.image==='string'&&path.win32.basename(r.image).toLowerCase()==='vctip.exe');assert(allTips.length>0&&allTips.every(r=>expectedMembers.some(x=>r.pid===x.pid&&r.creationFileTime===x.creationFileTime&&samePath(r.image,x.image))));
 bytesEqual(pin(verdict.compiledExecutable.path),verdict.compiledExecutable);
 if(architecture==='x64'){assert(verdict.policyExecutable);bytesEqual(pin(verdict.policyExecutable.path),verdict.policyExecutable);}
 const inputs=json(path.join(attempt,'inputs-before.json')),inputsAfter=json(path.join(attempt,'inputs-after.json'));assert.deepEqual(inputs.sourcePins,inputsAfter.sourcePins);
 for(const f of inputs.sourcePins)assert.deepEqual(pin(f.path),f);
 return {folder,verdict,receipt,inputs};
}
function normalizedCopies(caseData,kind,expectedRows){
 const isPolicy=kind==='policy',filename=isPolicy?'policy-source-pins.json':'fixture-source-pins.json';
 const root=path.join(caseData.receipt.workspace,isPolicy?'policy-source':'source'),actual=json(path.join(caseData.folder,filename));
 assert.equal(actual.length,expectedRows.length);const seen=new Set();
 const result=actual.map(row=>{
  const relative=path.relative(root,row.path).split(path.sep).join('/');assert(relative&&!relative.startsWith('../')&&!path.isAbsolute(relative)&&!seen.has(relative));seen.add(relative);
  const expected=expectedRows.find(f=>f.relative===relative);assert(expected);bytesEqual(row,expected.copy);bytesEqual(pin(row.path),row);bytesEqual(pin(expected.source.path),expected.source);bytesEqual(pin(expected.copy.path),expected.copy);
  return {relative,bytes:row.bytes,sha256:row.sha256};
 }).sort((a,b)=>a.relative.localeCompare(b.relative,'en'));
 assert.deepEqual([...seen].sort(),expectedRows.map(f=>f.relative).sort());return result;
}
function copyExact(source,destination){
 const before=pin(source);fs.mkdirSync(path.dirname(destination),{recursive:true});fs.copyFileSync(source,destination,fs.constants.COPYFILE_EXCL);const copied=pin(destination),after=pin(source);assert.deepEqual(before,after);bytesEqual(copied,before);return {source:before,copied,sourceAfter:after};
}
async function main(){
 const [gate,newAttemptArg]=process.argv.slice(2);assert.equal(process.argv.length,4);assert.equal(gate,'ROOT_REVIEWED_CURRENT_WORKBENCH_COMPOSITION_V1');assert.equal(process.platform,'win32');assert(path.isAbsolute(newAttemptArg));const newAttempt=path.resolve(newAttemptArg);assert(!samePath(newAttempt,oldAttempt));assert(samePath(path.dirname(newAttempt),base));assert(/^attempt-[a-f0-9-]{36}$/.test(path.basename(newAttempt)));
 const oldSummaryFile=path.join(oldAttempt,'summary.json'),newSummaryFile=path.join(newAttempt,'summary.json');assert.equal(pin(oldSummaryFile).sha256,oldSummarySha);
 const oldSummary=json(oldSummaryFile),newSummary=json(newSummaryFile);assert.equal(oldSummary.schema,'soui.workbench-consumer-build-only/v1');assert.equal(oldSummary.passed,false);assert(oldSummary.failure);assert.deepEqual(oldSummary.verdicts.map(v=>v.architecture),['x64']);assert.equal(oldSummary.executableExecuted,false);
 assert.equal(newSummary.schema,'soui.workbench-consumer-x86-retry/v1');assert.equal(newSummary.passed,true);assert.equal(newSummary.failure,null);assert.equal(newSummary.passedX64Reexecuted,false);assert.deepEqual(newSummary.verdicts.map(v=>v.architecture),['x86']);assert.equal(newSummary.executableExecuted,false);
 const x64=verifyCase(oldAttempt,'x64',oldSummary),x86=verifyCase(newAttempt,'x86',newSummary);
 const frozenFile=path.join(base,'FrozenSource.actual.json'),policyFile=path.join(base,'PolicySource.actual.json'),frozen=json(frozenFile),policy=json(policyFile);assert.equal(frozen.sourceFiles.length,19);assert.equal(policy.sourceFiles.length,3);
 const product64=normalizedCopies(x64,'product',frozen.sourceFiles),product86=normalizedCopies(x86,'product',frozen.sourceFiles),policy64=normalizedCopies(x64,'policy',policy.sourceFiles),policy86=normalizedCopies(x86,'policy',policy.sourceFiles);assert.deepEqual(product64,product86);assert.deepEqual(policy64,policy86);
 const key=p=>path.win32.normalize(p).toLowerCase(),oldMap=new Map(x64.inputs.sourcePins.map(p=>[key(p.path),p])),newMap=new Map(x86.inputs.sourcePins.map(p=>[key(p.path),p]));
 const oldOnly=[...oldMap.keys()].filter(k=>!newMap.has(k)).sort(),newOnly=[...newMap.keys()].filter(k=>!oldMap.has(k)).sort();
 assert.deepEqual(oldOnly,['Invoke-WorkbenchBuild.ps1','Run-WorkbenchBuild.cjs'].map(n=>key(path.join(base,n))).sort());assert.deepEqual(newOnly,['Invoke-WorkbenchBuild-x86Retry.ps1','Run-WorkbenchBuild-x86Retry.cjs'].map(n=>key(path.join(base,n))).sort());
 const common=[];for(const [k,p]of oldMap)if(newMap.has(k)){assert.deepEqual(newMap.get(k),p);common.push(p);}
 const directory=path.join(base,'composed-'+crypto.randomUUID());fs.mkdirSync(directory);const copies=[];
 for(const [architecture,data,attempt]of [['x64',x64,oldAttempt],['x86',x86,newAttempt]]){
  const originals=walk(data.folder),relativeNames=originals.map(f=>path.relative(data.folder,f));
  for(const f of originals)copies.push(copyExact(f,path.join(directory,architecture,path.relative(data.folder,f))));
  assert.deepEqual(walk(data.folder).map(f=>path.relative(data.folder,f)),relativeNames);assert.deepEqual(walk(path.join(directory,architecture)).map(f=>path.relative(path.join(directory,architecture),f)),relativeNames);
  for(const name of ['summary.json','inputs-before.json','inputs-after.json','original-preparation.json'])copies.push(copyExact(path.join(attempt,name),path.join(directory,'source-attempts',architecture,name)));
 }
 for(const name of ['FrozenSource.actual.json','PolicySource.actual.json','Run-WorkbenchBuild.cjs','Invoke-WorkbenchBuild.ps1','Run-WorkbenchBuild-x86Retry.cjs','Invoke-WorkbenchBuild-x86Retry.ps1',path.basename(__filename)])copies.push(copyExact(path.join(base,name),path.join(directory,'composition-inputs',name)));
 write(directory,'Copies.actual.json',{schema:'soui.workbench-build-case-composition-copies/v1',copies,sourceBeforeCopyAfterEqual:true,sourceCaseMembershipUnchanged:true});
 const equality={schema:'soui.workbench-build-case-composition-source-equality/v1',productSources:product64,policySources:policy64,productFileCount:19,policyFileCount:3,commonFrozenInputs:common,allowedRecipePathChanges:{oldOnly,newOnly},allProductAndPolicySourceBytesEqual:true,buildCasesReexecuted:false};write(directory,'SourceEquality.actual.json',equality);
 const sources=[{architecture:'x64',originalAttempt:oldAttempt,parentSummary:pin(oldSummaryFile),originalCase:pin(path.join(x64.folder,'case-verdict.json')),parentAttemptPassed:false,casePassed:true,reusedWithoutExecution:true},{architecture:'x86',originalAttempt:newAttempt,parentSummary:pin(newSummaryFile),originalCase:pin(path.join(x86.folder,'case-verdict.json')),parentAttemptPassed:true,casePassed:true,reusedWithoutExecution:true}];
 const summary={schema:'soui.workbench-consumer-build-composed/v1',passed:true,scope:'explicit composition of independently completed current Workbench x64 and x86 finite compile cases; no two-architecture attempt was relabeled',composedFromIndependentBuildCases:true,simultaneousTwoArchitectureAttemptPassed:false,originalParentAttemptRemainsFailed:true,noBuildExecutedByComposition:true,sources,sourceEquality:pin(path.join(directory,'SourceEquality.actual.json')),exactCopies:pin(path.join(directory,'Copies.actual.json')),verdicts:[x64.verdict,x86.verdict],failure:null,productExecuted:false,executableExecuted:false,oldFailuresReclassified:false,currentFullBuildEntriesExecuted:false,productionQualified:false,productionQualificationComplete:false,releaseReady:false};
 write(directory,'summary.json',summary);console.log(JSON.stringify({directory,summary:pin(path.join(directory,'summary.json')),passed:true,productExecuted:false,buildExecuted:false}));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
