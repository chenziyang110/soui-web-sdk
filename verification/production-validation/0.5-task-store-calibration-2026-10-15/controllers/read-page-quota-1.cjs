'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const sdk='C:/work/sm05-release-050';
const [runArgument,outputArgument]=process.argv.slice(2);assert(runArgument&&outputArgument);
const run=path.resolve(runArgument),output=path.resolve(outputArgument);
assert(!fs.existsSync(output));
const files=require(sdk+'/tests/acceptance/resource-file-bindings.cjs');
const resource=require(sdk+'/tests/acceptance/resource-validation.cjs');
const quota=require(sdk+'/tests/acceptance/task-store-request-quota.cjs');
const evidence=require(sdk+'/tools/internal/dependency-evidence.cjs');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const operationPath=run+'/operation-result.json',bindingPath=run+'/file-bindings.json',eventsPath=run+'/events.ndjson';
const operation=read(operationPath),input=read(bindingPath);
const pins=[__filename,operationPath,bindingPath,eventsPath].map(evidence.pin);
assert.equal(operation.passed,true);
files.verifyCompleteBinding(input,run,operation);
const analyzed=resource.analyzeFile(operationPath,bindingPath);
assert.equal(analyzed.inputsMatched,true);
assert.equal(analyzed.inputBindingScope,'complete-acceptance-inputs');
assert.equal(analyzed.softwareProcessModelGate,'matched-bound-workload');
assert(['passed','failed'].includes(analyzed.resourceRuleObservation));
const privateBinding=analyzed.privateStorageBinding;
assert(privateBinding&&privateBinding.inputsMatched===true);
const binding={schema:'soui.acceptance-task-store-quota-binding/1',...privateBinding,
  expectedPolicy:null,expectedRun:{mode:operation.config.mode,cycles:String(operation.config.cycles),warmup:String(operation.config.warmup)}};
const result=quota.analyzeTaskStoreQuota(fs.readFileSync(eventsPath),binding);
const points=result.points.filter(point=>point.state==='available').map(point=>point.raw);
const max=name=>points.reduce((peak,row)=>BigInt(row[name])>peak?BigInt(row[name]):peak,0n).toString();
files.verifyCompleteBinding(input,run,operation);pins.forEach(evidence.checkPin);
fs.writeFileSync(output,JSON.stringify({pins,scope:'page-reload-task-store-request-byte-baseline',
  resourceRuleObservation:analyzed.resourceRuleObservation,resourceRuleFailure:analyzed.observationFailure,
  quota:result,peaks:{chargedRequestedBytes:max('chargedPeakRequestedBytes'),successfulRequestedBytes:max('successfulPeakRequestedBytes'),
    deniedRequests:max('deniedRequests')},productionDefaultQualified:false,fullCacheBudgetQualified:false,releaseReady:false},null,2),{flag:'wx'});
assert.equal(result.bindingGate,'matched');assert.equal(result.dataGate,'matched',result.failure);
assert.equal(max('deniedRequests'),'0');
console.log(JSON.stringify({output,dataGate:result.dataGate,counts:result.counts,peak:max('chargedPeakRequestedBytes'),productionDefaultQualified:false}));
