'use strict';
const fs=require('node:fs');
const path=require('node:path');
const identity=require('../../tools/internal/delivery-identity.cjs');
const builds=require('../../tools/internal/build-evidence.cjs');
const sdkRoot=path.resolve(__dirname,'../..');
const sha=/^[a-f0-9]{64}$/;
const uuid=/^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/;
const snapshotRoles=['consumerSource','webSdk','webOutput','installedSdk'];
const need=(value,message)=>{if(!value)throw new Error(message);};
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const samePath=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.resolve(a)===path.resolve(b);

function pinFile(file,maxBytes=16*1024*1024) {
  file=identity.safePath(file);
  const stat=fs.statSync(file);
  need(stat.isFile()&&stat.size<=maxBytes,'Resource input is not a bounded regular file');
  const raw=fs.readFileSync(file);
  need(raw.length===stat.size,'Resource input length changed');
  return {pin:{path:file,bytes:raw.length,sha256:identity.hash(raw)},raw};
}
function readJson(file,maxBytes) {
  const {pin,raw}=pinFile(file,maxBytes);
  return {pin,record:JSON.parse(raw.toString('utf8').replace(/^\uFEFF/,''))};
}
function checkPin(pin,expectedPath,maxBytes=16*1024*1024) {
  need(pin&&samePath(pin.path,expectedPath)&&Number.isSafeInteger(pin.bytes)&&pin.bytes>=0&&sha.test(pin.sha256),
    'Resource input pin or fixed path differs');
  const current=pinFile(expectedPath,maxBytes);
  need(current.pin.bytes===pin.bytes&&current.pin.sha256===pin.sha256,'Resource input pin changed');
  return current.raw;
}
function validateSnapshotRecord(record) {
  need(record&&Array.isArray(record.files)&&record.files.length>0&&sha.test(record.sha256)&&
    Number.isSafeInteger(record.bytes)&&record.bytes>=0&&identity.hash(identity.encoded(record.files))===record.sha256,
    'Incomplete original snapshot record');
  identity.validateFiles(record.files);
  need(record.files.reduce((sum,file)=>sum+file.bytes,0)===record.bytes,'Snapshot total differs');
}
function exactSnapshot(root,expected,mode='exact-tree') {
  need(['exact-tree','acceptance-payload'].includes(mode),'Unknown exact snapshot mode');
  validateSnapshotRecord(expected);
  need(expected.files.every(file=>file.normalization===undefined),'Exact byte inputs cannot override normalization');
  root=identity.safePath(root);
  need(fs.statSync(root).isDirectory(),'Input root is not a directory');
  const names=identity.walk(root,'',name=>mode==='acceptance-payload'&&name==='resources/source-navigation.log');
  const current=identity.snapshot(root,names);
  need(same(current,expected),'Original input bytes or complete membership changed');
  return {...current,root,inventoryMode:mode,inventoryRoots:['']};
}
function validateRecordSet(records) {
  const {sdkBuild,build,pending,webBuild,publicWeb}=records;
  need(sdkBuild?.schema==='soui.source-build/v2'&&sdkBuild.nativeBuild==='passed'&&sdkBuild.sdkOnly===true&&
    build?.schema==='soui.acceptance-build/v3'&&webBuild?.schema==='soui.acceptance-web/v1'&&
    publicWeb?.schema==='soui.web-build/v1','Current build record schemas differ');
  need(['x86','x64'].includes(build.architecture)&&['Debug','Release'].includes(build.configuration)&&
    typeof build.version==='string'&&build.version.length>0&&uuid.test(build.buildId)&&uuid.test(build.consumerBuildId),
    'Build identity is incomplete');
  validateSnapshotRecord(sdkBuild.source);
  for(const record of [build,pending,sdkBuild]) {
    need(record.architecture===build.architecture&&record.configuration===build.configuration&&
      record.version===build.version&&record.buildId===build.buildId,'SDK and consumer build identities differ');
  }
  need(build.releaseReady===false&&sdkBuild.releaseReady===false&&build.sourceSha256===sdkBuild.source.sha256&&
    pending.sourceSha256===build.sourceSha256&&publicWeb.sourceSha256===build.sourceSha256,
    'Current source identity differs');
  for(const field of ['consumerSource','webSdk','kernel'])need(same(pending[field],build[field]),'Pending consumer input differs');
  need(same(webBuild,build.web)&&webBuild.consumerSourceSha256===build.consumerSource.sha256&&
    webBuild.webSdkSha256===build.webSdk.sha256,'Web build identities differ');
  need(webBuild.typecheck?.passed===true&&typeof webBuild.typecheck.typescript==='string'&&
    webBuild.typecheck.typescript.length>0&&sha.test(webBuild.typecheck.configSha256)&&sha.test(webBuild.typecheck.logSha256),
    'Web typecheck record is incomplete');
  need(['prebuilt','explicit-candidate'].includes(build.kernel?.kind)&&Number.isSafeInteger(build.kernel.bytes)&&
    build.kernel.bytes>0&&sha.test(build.kernel.sha256),'Kernel byte input identity differs');
  for(const record of [build.consumerSource,build.webSdk,webBuild.output,build.installedSdk,build.payload,publicWeb.artifacts])
    validateSnapshotRecord(record);
  validateNativeSelection(build,pending,sdkBuild);
  return {buildId:build.buildId,consumerBuildId:build.consumerBuildId,sourceSha256:build.sourceSha256,
    architecture:build.architecture,configuration:build.configuration,version:build.version};
}
function locations(suite,runRoot) {
  suite=identity.safePath(suite);runRoot=identity.safePath(runRoot);
  need(path.dirname(suite)===path.join(sdkRoot,'out','acceptance')&&path.dirname(runRoot)===suite,
    'Resource paths must belong to the current SDK acceptance suite');
  return {suite,runRoot};
}
function validateNativeSelection(build,pending,sdkBuild,suite) {
  if(build.nativeToolchain===undefined) {
    need(pending.nativeToolchain===undefined&&sdkBuild.nativeToolchain===undefined,
      'An explicit current LLVM selection cannot fall back to MSVC');
    return null;
  }
  const selection=build.nativeToolchain,requested={...selection};delete requested.consumer;
  need(['x86','x64'].includes(build.architecture)&&['Debug','Release'].includes(build.configuration)&&
    selection?.profile==='LLVM22MSVC142'&&same(requested,pending.nativeToolchain)&&
    same(Object.keys(selection).sort(),['consumer','profile','requestFile','sdkAccepted','validationTools'])&&
    sdkBuild.nativeToolchain?.profile==='LLVM22MSVC142','Explicit SDK/consumer LLVM selections differ');
  const validPin=value=>value&&typeof value.path==='string'&&path.isAbsolute(value.path)&&
    Number.isSafeInteger(value.bytes)&&value.bytes>0&&sha.test(value.sha256);
  const stock=selection.validationTools;
  need(stock&&typeof stock.root==='string'&&path.isAbsolute(stock.root)&&
    same(Object.keys(stock).sort(),['bridgeIdentity','inputIdentity','preparation','requestFile','root','sourceFingerprint'])&&
    [stock.inputIdentity,stock.bridgeIdentity,stock.sourceFingerprint].every(value=>sha.test(value))&&
    [selection.requestFile,selection.sdkAccepted,selection.consumer,stock.preparation,stock.requestFile,
      sdkBuild.nativeToolchain.requestFile].every(validPin),'Complete independently owned LLVM pin roles required');
  const arch=build.architecture,config=build.configuration,stockBuild=path.join(stock.root,'out',
    arch==='x64'?'benchmark-protocol':'benchmark-protocol-x86');
  need(samePath(selection.requestFile.path,path.join(sdkRoot,`out/sdk-${arch}/llvm-profile-request-${config}.json`))&&
    samePath(selection.sdkAccepted.path,path.join(sdkRoot,`out/sdk-${arch}/standard-llvm-${config}.json`))&&
    samePath(stock.preparation.path,path.join(stockBuild,'measurement-tools.json'))&&
    samePath(stock.requestFile.path,path.join(stockBuild,'llvm-profile-request-Release.json'))&&
    samePath(selection.requestFile.path,sdkBuild.nativeToolchain.requestFile.path)&&
    selection.requestFile.bytes===sdkBuild.nativeToolchain.requestFile.bytes&&
    selection.requestFile.sha256===sdkBuild.nativeToolchain.requestFile.sha256&&
    (suite===undefined||samePath(selection.consumer.path,path.join(suite,'consumer-llvm-build.json'))),
    'LLVM request or accepted record belongs to a different owner/configuration');
  return selection;
}
function mergeNativePins(rows) {
  need(Array.isArray(rows)&&rows.length>0,'Complete LLVM input pins required');
  const pins=new Map();
  for(const row of rows) {
    need(row&&typeof row.path==='string'&&path.isAbsolute(row.path)&&Number.isSafeInteger(row.bytes)&&
      row.bytes>=0&&sha.test(row.sha256),'Exact LLVM input pin required');
    const value={path:identity.safePath(row.path),bytes:row.bytes,sha256:row.sha256},key=path.resolve(row.path);
    if(pins.has(key))need(same(pins.get(key),value),'Conflicting frozen LLVM input pin');else pins.set(key,value);
  }
  return [...pins.values()].sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);
}
function nativeInputs(suite,build,sdk) {
  const selection=validateNativeSelection(build,readJson(path.join(suite,'pending.json')).record,sdk,suite);
  if(selection===null)return null;
  // All executable readers are fixed local modules. Selection roots locate data only.
  const installed=require('../../tools/internal/llvm-installed-acceptance.cjs');
  const verified=installed.verify(sdkRoot,suite,selection),current=installed.current(sdkRoot,build.architecture,build.configuration,selection);
  need(same(verified.build,sdk)&&same(current.build,sdk),'Current accepted LLVM SDK changed');
  const consumer=readJson(selection.consumer.path,64*1024*1024),record=consumer.record;
  checkPin(selection.consumer,consumer.pin.path,64*1024*1024);
  const inputs=readJson(record.inputs.path,64*1024*1024),observation=readJson(record.observation.path,64*1024*1024);
  const sdkInputs=readJson(current.accepted.inputs.path,64*1024*1024);
  const rows=[consumer.pin,inputs.pin,record.inputs,observation.pin,record.observation,record.observerInputs,current.selection.requestFile,
    current.selection.sdkAccepted,current.prepared.reportPin,current.prepared.requestPin,sdkInputs.pin,current.accepted.inputs,
    current.accepted.observation,current.accepted.observed,current.accepted.buildReportSnapshot,
    ...Object.values(current.accepted.buildCapture),...Object.values(current.accepted.observerCapture),
    ...inputs.record.dependencies.pins,...inputs.record.controls,inputs.record.request,
    ...['configure','build','observer'].flatMap(name=>Object.values(record[name].rows)),
    ...observation.record.compileRecords.flatMap(row=>[row.source,row.object,...row.headers]),
    ...observation.record.resourceRecords.flatMap(row=>[row.source,row.output]),
    ...observation.record.binaryRecords.flatMap(row=>[row.output,...row.inputObjects,...row.inputLibraries,...row.inputResources]),
    ...observation.record.evidencePins,...observation.record.dependencyPins,...observation.record.frozenDependencyPins,
    ...observation.record.installedEvidencePins];
  const pins=mergeNativePins(rows);
  for(const row of pins)checkPin(row,row.path,512*1024*1024);
  const runner=pinFile(installed.runtimeRunner(sdkRoot,build.architecture,build.configuration,selection),512*1024*1024).pin;
  // verify above authenticates exact inventory membership and original stage facts.
  // Recompute after every invocation; these are input bindings, never scope proof.
  return {schema:'soui.resource-llvm-file-inputs/v1',selection,runner,pins,
    inventories:inputs.record.dependencies.inventories};
}
function currentRecords(suite) {
  const acceptance=readJson(path.join(suite,'build.json'));
  const build=require('./runner.cjs').verify(suite);
  need(same(build,acceptance.record),'Acceptance build changed during verification');
  const sdk=build.nativeToolchain===undefined?builds.verifyBuild(sdkRoot,build.architecture,build.configuration,true):
    require('../../tools/internal/llvm-installed-acceptance.cjs').verify(sdkRoot,suite,build.nativeToolchain).build;
  const sdkRecord=readJson(path.join(sdkRoot,`out/sdk-${build.architecture}/build-${build.configuration}.json`));
  need(same(sdk,sdkRecord.record),'SDK build changed during verification');
  const pending=readJson(path.join(suite,'pending.json'));
  const web=readJson(path.join(suite,'web-build.json'));
  const publicWeb=readJson(path.join(sdkRoot,'out/web-sdk.json'));
  const records={sdkBuild:sdk,build,pending:pending.record,webBuild:web.record,publicWeb:publicWeb.record};
  const ids=validateRecordSet(records);
  const source=identity.sourceSnapshot(sdkRoot);
  need(source.sha256===sdk.source.sha256&&source.bytes===sdk.source.bytes&&same(source.files,sdk.source.files),
    'Complete current SDK source differs from the accepted source record');
  const publicArtifacts=identity.snapshot(sdkRoot,identity.walk(sdkRoot,'sdk/web/dist'));
  need(same(publicArtifacts,publicWeb.record.artifacts),'Public Web SDK artifact bytes differ');
  for(const file of publicArtifacts.files) {
    need(file.path.startsWith('sdk/web/dist/'),'Unexpected public Web SDK artifact path');
    const copied=pinFile(identity.safePath(path.join(suite,'web-sdk'),file.path.slice('sdk/web/'.length)));
    need(copied.pin.bytes===file.bytes&&copied.pin.sha256===file.sha256,'Copied public Web SDK artifact differs');
  }
  return {records,ids,source,nativeToolchain:nativeInputs(suite,build,sdk),pins:{sdkBuild:sdkRecord.pin,build:acceptance.pin,pending:pending.pin,
    webBuild:web.pin,publicWeb:publicWeb.pin}};
}
function requestFor(runRoot,build) {
  const request=readJson(path.join(runRoot,'request.json'));
  const record=request.record,reader=require('./resource-validation.cjs');
  if(record.longRunPolicySha256!==undefined){
    const long=require('./long-run-policy.cjs');
    need(uuid.test(record.runId),'Long request requires a fresh run UUID');
    long.validateFrozenRequest(record,build,runRoot);
    const policy=pinFile(path.join(runRoot,'long-run-policy.json'));
    need(policy.raw.equals(long.policyBytes(record.mode)),'Actual frozen long policy bytes differ');
    return {request,policy};
  }
  const selected=reader.resourcePolicySelection(record);
  need(record.schema==='soui.acceptance-request/v1'&&uuid.test(record.runId)&&record.mode==='reload'&&
    record.cycles===100&&record.warmup===10&&record.settleMs===5000&&record.timeoutMs===20000&&
    record.consumerBuildId===build.consumerBuildId&&record.sourceSha256===build.sourceSha256&&
    record.payloadSha256===build.payload.sha256&&record.resourcePolicySha256===selected.policySha256&&
    same(record.resourceScope,{kernel:'miniblink',renderer:'software',processModel:'single-process'})&&
    path.basename(runRoot)===`reload-${record.runId}`,'Frozen current resource request differs');
  const policy=pinFile(path.join(runRoot,'resource-policy.json'));
  need(policy.raw.equals(selected.policyRaw),'Actual resource policy bytes differ');
  return {request,policy};
}
function snapshotInputs(suite,build) {
  return {
    consumerSource:exactSnapshot(path.join(suite,'consumer-source'),build.consumerSource),
    webSdk:exactSnapshot(path.join(suite,'web-sdk'),build.webSdk),
    webOutput:exactSnapshot(path.join(suite,'web-output'),build.web.output),
    installedSdk:exactSnapshot(path.join(suite,'install'),build.installedSdk)
  };
}
function verifyCompleteBinding(binding,runRoot,result) {
  need(binding?.schema==='soui.resource-file-bindings/v2'&&binding.additionalInputs?.schema==='soui.acceptance-resource-inputs/v1',
    'Complete resource bindings are missing');
  const extra=binding.additionalInputs;
  const where=locations(extra.suiteRoot,runRoot);
  need(samePath(extra.sdkRoot,sdkRoot)&&samePath(extra.runRoot,where.runRoot)&&
    same(extra.nativeScope,{status:'not-observed'}),'Native software scope cannot be asserted by a file binding');
  const current=currentRecords(where.suite),build=current.records.build;
  need(current.nativeToolchain?same(extra.nativeToolchain,current.nativeToolchain):extra.nativeToolchain===undefined,
    'Complete current LLVM selection/requests/records/inventories changed or missing');
  if(current.nativeToolchain)need(same(binding.runner,current.nativeToolchain.runner),
    'Resource Runner is not the authenticated selected LLVM stock');
  need(same(extra.identity,current.ids)&&same(binding.source,current.source),'Complete current build/source identity differs');
  const expectedPaths={sdkBuild:path.join(sdkRoot,`out/sdk-${build.architecture}/build-${build.configuration}.json`),
    build:path.join(where.suite,'build.json'),pending:path.join(where.suite,'pending.json'),
    webBuild:path.join(where.suite,'web-build.json'),publicWeb:path.join(sdkRoot,'out/web-sdk.json')};
  need(same(Object.keys(extra.records||{}),Object.keys(expectedPaths)),'Build record roles differ');
  for(const [role,file] of Object.entries(expectedPaths))checkPin(extra.records[role],file);
  const snapshots=snapshotInputs(where.suite,build);
  need(same(Object.keys(extra.snapshots||{}),snapshotRoles)&&same(extra.snapshots,snapshots),
    'Complete separate input snapshots differ');
  const payload=exactSnapshot(path.join(where.suite,'runtime'),build.payload,'acceptance-payload');
  need(same(binding.payload,payload)&&samePath(binding.executablePath,path.join(payload.root,'SouiAcceptance.exe')),
    'Runtime payload or fixed EXE differs');
  const exe=checkPin(extra.executable,binding.executablePath,512*1024*1024);
  const expectedExe=build.payload.files.find(file=>file.path==='SouiAcceptance.exe');
  need(expectedExe&&expectedExe.bytes===exe.length&&expectedExe.sha256===identity.hash(exe),'EXE is not the original consumer payload');
  const kernel=checkPin(extra.kernelInput,path.join(where.suite,'kernel-input','node.dll'),512*1024*1024);
  const runtimeKernel=build.payload.files.find(file=>file.path==='node.dll');
  need(kernel.length===build.kernel.bytes&&identity.hash(kernel)===build.kernel.sha256&&runtimeKernel&&
    runtimeKernel.bytes===build.kernel.bytes&&runtimeKernel.sha256===build.kernel.sha256,'Kernel input and deployed kernel differ');
  const config=checkPin(extra.typecheckConfig,path.join(where.suite,'web-typecheck.tsconfig.json'));
  const log=checkPin(extra.typecheckLog,path.join(where.suite,'web-typecheck.log'));
  need(identity.hash(config)===build.web.typecheck.configSha256&&identity.hash(log)===build.web.typecheck.logSha256,
    'Original Web typecheck inputs differ');
  const captured=requestFor(where.runRoot,build);
  checkPin(extra.request,path.join(where.runRoot,'request.json'));
  checkPin(binding.policy,captured.policy.pin.path);
  need(binding.policy.sha256===captured.policy.pin.sha256,'Frozen policy pin differs');
  need(binding.runner&&path.isAbsolute(binding.runner.path),'Actual Owned runner pin missing');
  checkPin(binding.runner,binding.runner.path,512*1024*1024);
  if(result)need(result.schema==='soui.acceptance-run/v1'&&result.runId===captured.request.record.runId&&
    result.consumerBuildId===build.consumerBuildId&&result.sourceSha256===build.sourceSha256&&
    result.payloadSha256===build.payload.sha256&&result.releaseReady===false,'Operation result belongs to different inputs');
  return {inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',nativeScopeObserved:false,
    loadedPayloadModule:{path:path.join(where.suite,'runtime','node.dll'),bytes:runtimeKernel.bytes,sha256:runtimeKernel.sha256},
    kernelInput:{...extra.kernelInput},
    sourceSha256:build.sourceSha256,payloadSha256:build.payload.sha256,
    nativeToolchainProfile:current.nativeToolchain?'LLVM22MSVC142':'MSVC143',...current.ids};
}
function freeze(suite,runRoot) {
  const where=locations(suite,runRoot),current=currentRecords(where.suite),build=current.records.build;
  const {request,policy}=requestFor(where.runRoot,build);
  for(const name of ['events.ndjson','owned-process.ndjson','owned-process.ndjson.context.json','stdout.log','stderr.log',
    'operation-result.json','result.json','file-bindings.json'])
    need(!fs.existsSync(identity.safePath(where.runRoot,name)),'Resource freeze requires fresh prelaunch markers');
  const payload=exactSnapshot(path.join(where.suite,'runtime'),build.payload,'acceptance-payload');
  const runner=current.nativeToolchain?current.nativeToolchain.runner.path:
    process.env.SOUI_OWNED_PROCESS_RUNNER||path.join(sdkRoot,'out/benchmark-protocol/Release/OwnedProcessRunner.exe');
  const binding={schema:'soui.resource-file-bindings/v2',source:current.source,payload,policy:policy.pin,
    runner:pinFile(runner,512*1024*1024).pin,executablePath:path.join(payload.root,'SouiAcceptance.exe'),
    additionalInputs:{schema:'soui.acceptance-resource-inputs/v1',sdkRoot,suiteRoot:where.suite,runRoot:where.runRoot,
      identity:current.ids,records:current.pins,snapshots:snapshotInputs(where.suite,build),
      ...(current.nativeToolchain?{nativeToolchain:current.nativeToolchain}:{}),
      request:request.pin,kernelInput:pinFile(path.join(where.suite,'kernel-input','node.dll'),512*1024*1024).pin,
      executable:pinFile(path.join(payload.root,'SouiAcceptance.exe'),512*1024*1024).pin,
      typecheckConfig:pinFile(path.join(where.suite,'web-typecheck.tsconfig.json')).pin,
      typecheckLog:pinFile(path.join(where.suite,'web-typecheck.log')).pin,nativeScope:{status:'not-observed'}}};
  verifyCompleteBinding(binding,where.runRoot);
  const file=identity.safePath(where.runRoot,'file-bindings.json');
  fs.writeFileSync(file,identity.encoded(binding),{flag:'wx'});
  return {bindingFile:pinFile(file).pin,inputsMatched:true,resourceGate:'not-evaluated',passed:false,
    nativeScopeGate:'not-observed',releaseReady:false};
}
function recheck(bindingFile) {
  bindingFile=identity.safePath(bindingFile);
  need(path.basename(bindingFile)==='file-bindings.json','Fixed resource binding filename required');
  const {record}=readJson(bindingFile),checked=verifyCompleteBinding(record,path.dirname(bindingFile));
  return {...checked,resourceGate:'not-evaluated',passed:false,nativeScopeGate:'not-observed',releaseReady:false,
    closureObserved:false};
}
module.exports={freeze,recheck,verifyCompleteBinding,exactSnapshot,validateRecordSet,validateNativeSelection,mergeNativePins,requestFor};
