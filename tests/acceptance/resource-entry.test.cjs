'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {settings,parseRunCommand,resourceBinding,resourceCaptureResult,matchResourceBindingPin,completeResourceCaptureResult,mixedSettings,sessionSettings}=require('./runner.cjs');
const reader=require('./resource-validation.cjs');

test('public resource capture selects the frozen protocol and loaded policy',()=>{
  const invocation=parseRunCommand('run-resource',[]);
  assert.deepEqual(invocation,{config:settings('reload',100,10,5000,20000),options:{resource:true}});
  const binding=resourceBinding(invocation.config,invocation.options);
  assert.ok(binding.policyRaw.equals(reader.policyBytes()));
  assert.equal(binding.requestFields.resourcePolicySha256,reader.POLICY_SHA256);
});

test('resource command rejects every caller protocol or binding override',()=>{
  for(const args of [['reload'],['100','10','5000','20000'],['--resource-bindings=fake.json'],
    ['--resourceScope=software'],['--timeout-ms=120000'],null,{}])
    assert.throws(()=>parseRunCommand('run-resource',args));
});

test('ordinary and duration routes preserve their existing configurations',()=>{
  assert.deepEqual(parseRunCommand('run',['bridge','3','0','500','20000']),
    {config:settings('bridge',3,0,500,20000)});
  assert.deepEqual(parseRunCommand('run-mixed',['60000','500','20000','10000']),
    {config:mixedSettings(60000,500,20000,10000)});
  assert.deepEqual(parseRunCommand('run-session',['3120000','500','20000','60000']),
    {config:sessionSettings(3120000,500,20000,60000)});
  for(const [command,args] of [['run',[]],['run-mixed',[]],['run-session',[]],['unknown',[]]])
    assert.throws(()=>parseRunCommand(command,args));
});

// These DTOs verify classification only; they are not native/Owned evidence.
test('a passed lifecycle DTO cannot become a passed resource result',()=>{
  const operation={schema:'soui.acceptance-run/v1',runId:'synthetic-classification-only',
    config:settings('reload',100,10,5000,20000),passed:true,releaseReady:false};
  const capture=resourceCaptureResult(operation,null,null);
  assert.equal(capture.operationPassed,true);
  assert.equal(capture.resourceGate,'not-evaluated');
  assert.equal(capture.passed,false);
  assert.equal(capture.releaseReady,false);
  assert.equal(capture.m0Complete,false);
  assert.equal(capture.bindings,null);
  assert.equal(capture.analysis,null);
  assert.equal(operation.passed,true);
});

test('failed operation evidence remains failed and retains its reason',()=>{
  const capture=resourceCaptureResult({schema:'soui.acceptance-run/v1',passed:false,
    failure:'Owned process closure was not verified'},null,null);
  assert.equal(capture.operationPassed,false);
  assert.equal(capture.operationFailure,'Owned process closure was not verified');
  assert.equal(capture.passed,false);
  assert.equal(capture.resourceGate,'not-evaluated');
  for(const operation of [null,{}, {schema:'soui.acceptance-run/v1',passed:'true'}])
    assert.throws(()=>resourceCaptureResult(operation,null,null));
});

const path=require('node:path');
const output=path.resolve('classification-only-run');
const binding={path:path.join(output,'file-bindings.json'),bytes:123,sha256:'a'.repeat(64)};
const inputs={inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',closureObserved:false,
  resourceGate:'not-evaluated',passed:false,nativeScopeGate:'not-observed',releaseReady:false};
const operation={schema:'soui.acceptance-run/v1',runId:'classification-only',
  config:settings('reload',100,10,5000,20000),passed:true,releaseReady:false};

// These pins/records are pure metadata fixtures, never file/native/Owned proof.
test('freeze receipt retains an immutable exact run binding pin',()=>{
  const retained=matchResourceBindingPin(binding,{...binding},output);
  assert.deepEqual(retained,binding);assert.ok(Object.isFrozen(retained));
  assert.notEqual(retained,binding);
});
test('a synchronized current JSON pin cannot replace the retained prelaunch pin',()=>{
  for(const changed of [{...binding,sha256:'b'.repeat(64)},{...binding,bytes:124},
    {...binding,path:path.join(output,'other.json')},{...binding,bytes:-1},
    {...binding,bytes:16*1024*1024+1},{...binding,sha256:1},null])
    assert.throws(()=>matchResourceBindingPin(binding,changed,output));
});
test('complete matched input and passed counter DTO still has no native resource qualification',()=>{
  const analysis={resourceGate:'not-evaluated',passed:false,releaseReady:false,nativeScopeGate:'not-observed',
    resourceRuleObservation:'passed',inputsMatched:true,failure:'native-software-scope-not-observed'};
  const result=completeResourceCaptureResult(operation,null,null,{bindingFile:binding,finalVerified:true,inputs,
    analysis:{record:analysis,file:binding},failure:null});
  assert.equal(result.inputBindingGate,'matched');assert.equal(result.resourceRuleObservation,'passed');
  assert.equal(result.resourceGate,'not-evaluated');assert.equal(result.nativeScopeGate,'not-observed');
  assert.equal(result.passed,false);assert.equal(result.releaseReady,false);assert.equal(result.m0Complete,false);
  assert.deepEqual(result.bindings,binding);
});
test('failed or forced operation retains its original reason and cannot gain input closure',()=>{
  const failed={...operation,passed:false,failure:'Owned process closure was not verified'};
  const result=completeResourceCaptureResult(failed,null,null,{bindingFile:binding,finalVerified:false,inputs:null,analysis:null,failure:null});
  assert.equal(result.operationPassed,false);assert.equal(result.failure,failed.failure);
  assert.equal(result.inputBindingGate,'not-evaluated');assert.equal(result.resourceGate,'not-evaluated');
});
test('input-only recheck and analysis refuse green or closure authority claims',()=>{
  for(const changed of [{...inputs,passed:true},{...inputs,resourceGate:'passed'},
    {...inputs,closureObserved:true},{...inputs,nativeScopeGate:'observed'}])
    assert.throws(()=>completeResourceCaptureResult(operation,null,null,{bindingFile:binding,finalVerified:true,inputs:changed,analysis:null,failure:null}));
  for(const changed of [{resourceGate:'passed',passed:true,releaseReady:false,nativeScopeGate:'not-observed'},
    {resourceGate:'not-evaluated',passed:false,releaseReady:false,nativeScopeGate:'observed'}])
    assert.throws(()=>completeResourceCaptureResult(operation,null,null,{bindingFile:binding,finalVerified:true,inputs,
      analysis:{record:changed,file:binding},failure:null}));
});
test('reader missing-file observation remains retained without becoming resource success',()=>{
  const analysis={resourceGate:'not-evaluated',passed:false,releaseReady:false,failure:'missing-or-invalid-file-observation'};
  const result=completeResourceCaptureResult(operation,null,null,{bindingFile:binding,finalVerified:true,inputs,
    analysis:{record:analysis,file:binding},failure:null});
  assert.equal(result.failure,analysis.failure);assert.equal(result.passed,false);
});
test('Source freeze precedes stdio and retained pin brackets fixed recheck and reader',()=>{
  const fs=require('node:fs'),source=fs.readFileSync(path.join(__dirname,'runner.cjs'),'utf8');
  const freeze=source.indexOf('resourceFiles.freeze(suite,output)');
  assert.ok(freeze>source.indexOf("write(path.join(output,'request.json')"));
  assert.ok(freeze<source.indexOf("fs.openSync(path.join(output,'stdout.log')"));
  const post=source.slice(source.indexOf('if (!resource) return summary;'));
  assert.ok(post.indexOf('if(result.passed)')<post.indexOf('resourceFiles.recheck(frozenBinding.path)'));
  assert.ok(post.indexOf('matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output)')<
    post.indexOf('resourceFiles.recheck(frozenBinding.path)'));
  assert.ok(post.includes("require('./resource-validation.cjs').analyzeFile(operationPath,frozenBinding.path)"));
  assert.equal((post.match(/resourceFiles\.recheck\(frozenBinding\.path\)/g)||[]).length,2);
  const finalPin=post.lastIndexOf('matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output)');
  const receipt=post.indexOf('bindingEvidence.analysis={record:analysis,file:filePin(analysisPath)};');
  const final=post.indexOf('bindingEvidence.finalVerified=true;');
  assert.ok(finalPin>post.lastIndexOf('resourceFiles.recheck(frozenBinding.path)'));
  assert.ok(receipt>finalPin);assert.ok(final>receipt);
  assert.ok(post.includes('catch(error) { bindingEvidence.finalVerified=false;bindingEvidence.failure=error.message; }'));
});

test('any incomplete final phase keeps complete input evidence required and an early snapshot diagnostic only',()=>{
  const analysis={resourceGate:'not-evaluated',passed:false,releaseReady:false,nativeScopeGate:'not-observed',
    resourceRuleObservation:'passed',inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',
    failure:'native-software-scope-not-observed'};
  for(const phase of ['first-recheck','post-reader-pin','second-recheck','final-pin','analysis-receipt']) {
    const early=phase==='first-recheck'?null:inputs,failure=`classification-only ${phase} failure`;
    const result=completeResourceCaptureResult(operation,null,null,{bindingFile:binding,finalVerified:false,
      inputs:early,analysis:early?{record:analysis,file:binding}:null,failure});
    assert.equal(result.inputBindingGate,'not-evaluated',phase);
    assert.equal(result.inputRecheck,null,phase);assert.equal(result.inputRecheckDiagnostic,early,phase);
    assert.deepEqual(result.requiredEvidence,['complete-current-source-and-consumer-input-bindings',
      'actual-native-software-scope','independent-resource-file-analysis'],phase);
    assert.equal(result.failure,failure,phase);
    assert.equal(result.resourceGate,'not-evaluated',phase);assert.equal(result.nativeScopeGate,'not-observed',phase);
    assert.equal(result.passed,false,phase);assert.equal(result.releaseReady,false,phase);
    assert.equal(result.m0Complete,false,phase);
  }
});

test('only a completed final phase with a passed operation and no retained failure may report matched inputs',()=>{
  const analysis={resourceGate:'not-evaluated',passed:false,releaseReady:false,nativeScopeGate:'not-observed',
    resourceRuleObservation:'passed',inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',
    failure:'native-software-scope-not-observed'};
  const evidence={bindingFile:binding,finalVerified:true,inputs,analysis:{record:analysis,file:binding},failure:null};
  for(const [current,observed] of [[operation,{...evidence,finalVerified:false}],
    [operation,{...evidence,failure:'classification-only retained failure'}],
    [{...operation,passed:false,failure:'classification-only operation failed'},evidence]]) {
    const result=completeResourceCaptureResult(current,null,null,observed);
    assert.equal(result.inputBindingGate,'not-evaluated');assert.equal(result.inputRecheck,null);
    assert.equal(result.inputRecheckDiagnostic,inputs);
    assert.ok(result.requiredEvidence.includes('complete-current-source-and-consumer-input-bindings'));
    assert.equal(result.passed,false);assert.equal(result.resourceGate,'not-evaluated');
  }
  const result=completeResourceCaptureResult(operation,null,null,evidence);
  assert.equal(result.inputBindingGate,'matched');assert.equal(result.inputRecheck,inputs);
  assert.deepEqual(result.requiredEvidence,['actual-native-software-scope']);
  assert.equal(result.resourceRuleObservation,'passed');assert.equal(result.nativeScopeGate,'not-observed');
  assert.equal(result.passed,false);assert.equal(result.releaseReady,false);assert.equal(result.m0Complete,false);
});

test('a matched compositor-only analysis retains broader software/process qualification as required evidence',()=>{
  const analysis={resourceGate:'not-evaluated',passed:false,releaseReady:false,nativeScopeGate:'matched-compositor-only',
    nativeSoftwareGate:'matched',softwareProcessModelGate:'not-evaluated',inputsMatched:true,
    inputBindingScope:'complete-acceptance-inputs',resourceRuleObservation:'passed',
    nativeSoftware:{schema:'soui.bound-dll-compositor-observation/v1',scope:'bound-dll-completed-compositor-calls',
      nativeSoftwareGate:'matched',qualified:false},failure:'broader-software-process-workload-not-observed'};
  const evidence={bindingFile:binding,finalVerified:true,inputs,analysis:{record:analysis,file:binding},failure:null};
  const result=completeResourceCaptureResult(operation,null,null,evidence);
  assert.equal(result.nativeSoftwareGate,'matched');assert.equal(result.nativeScopeGate,'matched-compositor-only');
  assert.equal(result.resourceGate,'not-evaluated');assert.equal(result.passed,false);
  assert.deepEqual(result.requiredEvidence,['broader-software-process-workload-coverage']);
  for(const phase of ['post-reader-pin','second-recheck','final-pin','analysis-receipt']) {
    const failed=completeResourceCaptureResult(operation,null,null,{...evidence,finalVerified:false,failure:phase});
    assert.equal(failed.nativeSoftwareGate,'not-evaluated');assert.equal(failed.nativeSoftware,null);
    assert.ok(failed.requiredEvidence.includes('complete-current-source-and-consumer-input-bindings'));
  }
});

test('compositor DTO cannot claim wider process software scope or replace the exact Native scope label',()=>{
  const base={resourceGate:'not-evaluated',passed:false,releaseReady:false,nativeScopeGate:'matched-compositor-only',
    nativeSoftwareGate:'matched',softwareProcessModelGate:'not-evaluated',
    nativeSoftware:{schema:'soui.bound-dll-compositor-observation/v1',scope:'bound-dll-completed-compositor-calls',
      nativeSoftwareGate:'matched',qualified:false}};
  for(const changed of [{...base,softwareProcessModelGate:'passed'},
    {...base,nativeSoftwareGate:'passed'},
    {...base,nativeSoftware:{...base.nativeSoftware,scope:'whole-process'}},
    {...base,nativeSoftware:{...base.nativeSoftware,qualified:true}}])
    assert.throws(()=>completeResourceCaptureResult(operation,null,null,{bindingFile:binding,finalVerified:true,
      inputs,analysis:{record:changed,file:binding},failure:null}));
});
