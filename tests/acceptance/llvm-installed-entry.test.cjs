'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs');
const {validateObservationFacts,configureArguments}=require('../../tools/internal/llvm-installed-acceptance.cjs');
const root=path.resolve(__dirname,'../..'),suite=path.join(root,'out/acceptance/00000000-0000-0000-0000-000000000001');
const raw=(file,hash='a')=>({path:file,bytes:123,sha256:hash.repeat(64)}),clone=value=>JSON.parse(JSON.stringify(value));
test('actual CMake manifest mode parser distinguishes configured tools from required execution',
  {skip:process.platform!=='win32'},()=>{
    const result=require('node:child_process').spawnSync(process.env.SOUI_GUI_TEST_POWERSHELL||'powershell.exe',
      ['-NoProfile','-NonInteractive','-File',path.join(root,'tests/sdk/llvm-cmake-manifest-mode.Tests.ps1')],
      {encoding:'utf8',timeout:30000,windowsHide:true});
    assert.ifError(result.error);assert.equal(result.status,0,result.stderr);assert.equal(JSON.parse(result.stdout.trim()).testsPassed,22);
  });
for(const entryPoint of ['Test-Acceptance.ps1','Verify-Release.ps1'])
test(entryPoint+' forwards six separate named paths through the real PowerShell binder',
  {skip:process.platform!=='win32'},()=>{
    const {spawnSync}=require('node:child_process'),os=require('node:os');
    const shell=process.env.SOUI_GUI_TEST_POWERSHELL||'powershell.exe';
    const directory=fs.mkdtempSync(path.join(os.tmpdir(),'soui-llvm-argv-'));
    try{
      const names=['LlvmRoot','MsvcRoot','WindowsSdkRoot','NinjaExe','WindowsCmdExe','PowerShellExe'];
      const expected=Object.fromEntries(names.map((name,index)=>[name,`C:\\tool paths\\测试 ${index}\\${name}`]));
      fs.writeFileSync(path.join(directory,'values.json'),JSON.stringify(expected));
      fs.writeFileSync(path.join(directory,'probe.ps1'),
        '[CmdletBinding()]param('+names.map(name=>'[string]$'+name).join(',')+')\n'+
        '[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false)\n$PSBoundParameters|ConvertTo-Json -Compress');
      fs.writeFileSync(path.join(directory,'driver.ps1'),String.raw`
param([string]$SourcePath,[string]$ValuesPath,[string]$ProbePath,[string]$ShellPath)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false)
$tokens=$null;$errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($SourcePath,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Acceptance entry must parse'}
$loops=@($ast.FindAll({param($node)
  $node -is [System.Management.Automation.Language.ForEachStatementAst] -and
  $node.Body.Extent.Text -match '\$stock(?:Args)?\s*\+='
},$true))
if($loops.Count -ne 1){throw 'Expected one real LLVM stock argument forwarding loop'}
$llvmPaths=[ordered]@{}
($values=Get-Content -LiteralPath $ValuesPath -Raw -Encoding UTF8|ConvertFrom-Json).PSObject.Properties|
  ForEach-Object{$llvmPaths[$_.Name]=$_.Value}
if($loops[0].Body.Extent.Text -notmatch '\$(stock(?:Args)?)\s*\+='){throw 'Forwarding variable missing'}
$stockVariable=$Matches[1]
Set-Variable -Name $stockVariable -Value @('-NoProfile','-NonInteractive','-File',$ProbePath)
. ([scriptblock]::Create($loops[0].Extent.Text))
$forwarded=Get-Variable -Name $stockVariable -ValueOnly
& $ShellPath @forwarded
exit $LASTEXITCODE
`);
      const result=spawnSync(shell,['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',
        path.join(directory,'driver.ps1'),'-SourcePath',path.join(root,'tools',entryPoint),
        '-ValuesPath',path.join(directory,'values.json'),'-ProbePath',path.join(directory,'probe.ps1'),'-ShellPath',shell],
      {encoding:'utf8',timeout:30000,windowsHide:true});
      assert.ifError(result.error);
      assert.equal(result.status,0,result.stderr);
      assert.deepEqual(JSON.parse(result.stdout.trim()),expected);
    }finally{fs.rmSync(directory,{recursive:true,force:true});}
  });
test('actual installed archive comparison handles Int64 sizes and rejects changed member multisets',
  {skip:process.platform!=='win32'},()=>{
    const {spawnSync}=require('node:child_process');
    const result=spawnSync(process.env.SOUI_GUI_TEST_POWERSHELL||'powershell.exe',
      ['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'tests/sdk/llvm-installed-archive.Tests.ps1')],
      {encoding:'utf8',timeout:30000,windowsHide:true});
    assert.ifError(result.error);assert.equal(result.status,0,result.stderr);
    assert.equal(JSON.parse(result.stdout.trim()).testsPassed,7);
  });
// Every value below is a logical DTO fixture, never file/compiler/Owned proof.
function fixture(architecture='x64'){
  const machine=architecture==='x64'?34404:332,buildDirectory=path.join(suite,'consumer');
  const capture={raw:raw(path.join(suite,'build.ndjson')),context:raw(path.join(suite,'build.context.json')),
    ready:raw(path.join(suite,'ready.json')),finished:raw(path.join(suite,'finished.json'))};
  const archive=raw(path.join(suite,'install/lib/Release/soui_sdk.lib'));
  const cfg={root,architecture,configuration:'Release',buildDirectory,inputIdentity:'sdk-owner-full-id',bridgeIdentity:'sdk-owner-bridge-id',
    requestFile:raw(path.join(root,'out',`sdk-${architecture}`,'llvm-profile-request-Release.json')),
    buildReport:raw(path.join(root,'out',`sdk-${architecture}`,'build-Release.json')),buildCapture:capture,dependencyPins:[],
    installedConsumer:{suiteDirectory:suite,pending:raw(path.join(suite,'pending.json')),
      requestFile:raw(path.join(buildDirectory,'llvm-profile-request-Release.json')),sdkAccepted:raw(path.join(root,'accepted.json'))}};
  const subject={start:{pid:42,creationFileTime:'123456789'}},build={buildId:'fixture-sdk-id',source:{sha256:'fixture-source'}};
  const object=raw(path.join(buildDirectory,'main.cpp.obj'));
  const row={schema:'soui.installed-acceptance-llvm-build-observation/v1',profile:'LLVM22MSVC142',root,architecture,configuration:'Release',sdkOnly:false,
    inputIdentity:cfg.inputIdentity,bridgeIdentity:cfg.bridgeIdentity,compilerVerified:true,observedCompilerId:'Clang',observedFrontend:'MSVC',
    observedVersion:'22.1.3',observedPointerBytes:architecture==='x64'?8:4,completeToolchainProvenance:false,kernelQualified:false,
    abiQualified:false,win7Qualified:false,qualificationPassed:false,releaseReady:false,
    buildCapture:{...capture,...subject.start,wholeJobClosed:true,forced:false},requestFile:cfg.requestFile,buildReport:cfg.buildReport,
    installedConsumer:{sdkBuildId:build.buildId,sdkSourceSha256:build.source.sha256,sdkAccepted:cfg.installedConsumer.sdkAccepted,
      pending:cfg.installedConsumer.pending,consumerRequest:cfg.installedConsumer.requestFile,installedSdkArchive:archive},
    compilerFacts:[{language:'CXX'}],compileRecords:[{source:raw(path.join(suite,'consumer-source/main.cpp')),object,machine,reusedFrom:null}],
    currentCompileCount:1,currentBinaryCount:1,ninjaCompileEdges:[{}],ninjaDependencyRecords:[{}],frozenDependencyPins:[],
    binaryRecords:[{role:'LINKER',output:raw(path.join(buildDirectory,'bin/Release/SouiAcceptance.exe')),machine,reusedFrom:null,
      inputLibraries:[archive],inputObjects:[object]}]};
  return {row,cfg,subject,build,archive};
}
const accepts=value=>validateObservationFacts(value.row,value.cfg,value.subject,value.build,value.archive);

for(const kind of ['acceptance','release'])
test(kind+' installed reader authenticates the immutable configure log and original observer output',t=>{
  // Execute the actual file-reading adapter. Unrelated SDK/compiler facts are
  // substituted here; the snapshot bytes, binding and path checks are real.
  const folder=fs.mkdtempSync(path.join(require('node:os').tmpdir(),'soui-consumer-config-log-'));
  t.after(()=>{const actual=fs.realpathSync(folder);assert(actual.startsWith(fs.realpathSync(require('node:os').tmpdir())+path.sep));fs.rmSync(actual,{recursive:true,force:true});});
  const buildDirectory=path.join(folder,'consumer'),evidence=path.join(folder,'consumer-llvm-observer');
  fs.mkdirSync(path.join(buildDirectory,'CMakeFiles'),{recursive:true});fs.mkdirSync(evidence,{recursive:true});
  const live=path.join(buildDirectory,'CMakeFiles/CMakeConfigureLog.yaml'),snapshot=path.join(evidence,'cmake-configure-log.input.yaml');
  const observationPath=path.join(evidence,'llvm-observation.json');
  const pin=file=>{const bytes=fs.readFileSync(file);return {path:file,bytes:bytes.length,sha256:require('node:crypto').createHash('sha256').update(bytes).digest('hex')};};
  fs.writeFileSync(live,'Actual fixture configure bytes\n');fs.copyFileSync(live,snapshot);
  const binding={schema:'soui.cmake-configure-log-capture/v1',live:pin(live),snapshot:pin(snapshot)};
  const context={schema:'soui.owned-process-context/v1',args:['-ObservationPath',observationPath]};
  const cfg={buildDirectory,[kind==='release'?'releaseConsumer':'installedConsumer']:{suiteDirectory:folder}};
  const row={configureLogCapture:binding,compileRecords:[],resourceRecords:[],binaryRecords:[],evidencePins:[binding.snapshot],dependencyPins:[],frozenDependencyPins:[],installedEvidencePins:[]};
  const source=fs.readFileSync(path.join(root,'tools/internal/llvm-installed-acceptance.cjs'),'utf8');
  const body=source.slice(source.indexOf('function validateObservation('),source.indexOf('function controls('));
  const check=require('node:vm').runInNewContext(body+'\nvalidateObservation;',{
    owner:root,path,Buffer,validateObservationFacts(){},paths:()=>({installedArchive:snapshot}),pin,
    match:p=>assert.deepEqual(pin(p.path),p),
    require:name=>{assert.equal(name,'./configure-log-evidence.cjs');return require('../../tools/internal/configure-log-evidence.cjs');}
  });
  const run=(value=row,args=context.args)=>check(value,cfg,{}, {build:{}},observationPath,Buffer.from(JSON.stringify({...context,args})));
  run();fs.appendFileSync(live,'Later configure\n');run();
  for(const mutate of [v=>v.configureLogCapture.schema='substituted',v=>v.configureLogCapture.live.path+='-other',
    v=>v.configureLogCapture.snapshot.path+='-other',v=>v.configureLogCapture.snapshot.sha256='b'.repeat(64),
    v=>v.evidencePins=[],v=>v.evidencePins.push({...binding.snapshot})]){
    const altered=clone(row);mutate(altered);assert.throws(()=>run(altered),/Configure log capture/);
  }
  for(const args of [[],['-ObservationPath',observationPath+'-other'],['-ObservationPath',observationPath,'-ObservationPath',observationPath]])
    assert.throws(()=>run(row,args),/Configure log capture/);
  fs.writeFileSync(snapshot,'X'.repeat(binding.snapshot.bytes));assert.throws(()=>run(),/Configure log capture file bytes differs|Configure log capture file bytes differ/);
});

function dualFixture(){
 const f=fixture();f.cfg.schema='soui.standard-llvm-observer-inputs/v2';
 f.cfg.debugCapture={input:raw(path.join(suite,'host64-inputs.json')),raw:raw(path.join(suite,'host64-debug.ndjson'))};
 f.cfg.fastToolEvidence={innerBuild:{pid:43,creationFileTime:'123456790'},outerObserver:f.subject.start,experimentalLifecycleObserved:true};
 f.row.schema='soui.installed-acceptance-llvm-build-observation/v2';
 f.row.outerObserverCapture=f.row.buildCapture;f.row.originalPollingCapture=f.cfg.buildCapture;
 f.row.buildCapture={role:'inner-build',...f.cfg.fastToolEvidence.innerBuild,raw:f.cfg.debugCapture.raw,input:f.cfg.debugCapture.input,wholeJobClosed:true,forced:false,naturalRootExitCode:0};
 f.row.debugCapture=f.cfg.debugCapture;f.row.fastToolEvidence=f.cfg.fastToolEvidence;return f;
}
test('consumer dual root DTO retains actual CMake identity and original owned observer identity',()=>assert.equal(accepts(dualFixture()),true));
test('consumer dual root DTO rejects swapped roots, changed raw pins and schema laundering',()=>{
 for(const mutate of [f=>f.row.buildCapture.pid=42,f=>f.row.outerObserverCapture.pid=43,
  f=>f.row.buildCapture.raw={...f.row.buildCapture.raw,sha256:'0'.repeat(64)},f=>f.row.buildCapture.forced=true,
  f=>f.row.buildCapture.role='outer',f=>f.row.schema='soui.installed-acceptance-llvm-build-observation/v1',
  f=>f.row.fastToolEvidence={...f.row.fastToolEvidence,experimentalLifecycleObserved:false},
  f=>f.row.originalPollingCapture={}]){const f=clone(dualFixture());mutate(f);assert.throws(()=>accepts(f));}
});
test('dedicated fresh CXX import DTOs preserve unqualified scope for both target architectures',()=>{
  for(const architecture of ['x86','x64']){const f=fixture(architecture);assert.equal(accepts(f),true);
    for(const key of ['completeToolchainProvenance','kernelQualified','abiQualified','win7Qualified','qualificationPassed','releaseReady'])assert.equal(f.row[key],false);}
});
test('normal graph and wrong owner profile compiler or full identity cannot substitute for installed output',()=>{
  for(const [key,value] of [['schema','soui.standard-llvm-build-observation/v1'],['profile','MSVC143'],['root',path.dirname(root)],
    ['architecture','arm64'],['configuration','Debug'],['sdkOnly',true],['inputIdentity','stock-id'],['bridgeIdentity','stock-bridge'],
    ['observedCompilerId','MSVC'],['observedVersion','22.1.2'],['observedPointerBytes',4]]){
    const f=fixture();f.row[key]=value;assert.throws(()=>accepts(f),key);
  }
});
test('published build subject frozen captures and natural closure cannot be replaced by DTO claims',()=>{
  for(const mutate of [row=>row.buildCapture.pid++,row=>row.buildCapture.creationFileTime='987',row=>row.buildCapture.forced=true,
    row=>row.buildCapture.wholeJobClosed=false,row=>row.buildCapture.raw={...row.buildCapture.raw,sha256:'b'.repeat(64)}]){
    const f=fixture();f.row=clone(f.row);mutate(f.row);assert.throws(()=>accepts(f));
  }
});
test('current SDK import identity and independently owned consumer request are required',()=>{
  for(const mutate of [row=>row.installedConsumer.sdkBuildId='old-sdk',row=>row.installedConsumer.sdkSourceSha256='old-source',
    row=>row.installedConsumer.installedSdkArchive={...row.installedConsumer.installedSdkArchive,sha256:'b'.repeat(64)},
    row=>row.installedConsumer.consumerRequest=row.requestFile,row=>row.installedConsumer.pending={...row.installedConsumer.pending,bytes:124},
    row=>row.binaryRecords[0].inputLibraries=[]]){const f=fixture();f.row=clone(f.row);mutate(f.row);assert.throws(()=>accepts(f));}
});
test('empty extra foreign reused or disconnected compile and link DTOs fail the fresh ordinary graph',()=>{
  for(const mutate of [row=>row.compilerFacts=[],row=>row.compilerFacts.push({language:'C'}),row=>row.compileRecords=[],
    row=>row.currentCompileCount=0,row=>row.currentBinaryCount=0,row=>row.compileRecords[0].reusedFrom={},
    row=>row.compileRecords[0].source.path=path.join(suite,'consumer-source/other.cpp'),row=>row.compileRecords[0].machine=332,
    row=>row.binaryRecords[0].inputObjects=[raw(path.join(suite,'foreign.obj'))],row=>row.binaryRecords[0].reusedFrom={},
    row=>row.binaryRecords[0].role='ARCHIVER',row=>row.binaryRecords[0].output.path=path.join(suite,'other.exe')]){
    const f=fixture();mutate(f.row);assert.throws(()=>accepts(f));
  }
});
test('installed observations cannot elevate toolchain kernel ABI OS or release qualification',()=>{
  for(const key of ['completeToolchainProvenance','kernelQualified','abiQualified','win7Qualified','qualificationPassed','releaseReady']){
    const f=fixture();f.row[key]=true;assert.throws(()=>accepts(f),key);
  }
  const f=fixture();f.row.frozenDependencyPins=[raw(path.join(root,'unfrozen.h'))];assert.throws(()=>accepts(f));
});
test('configure argv preserves all twelve discovery definitions and fixed owner toolchain paths',()=>{
  const names=['SOUI_NATIVE_TOOLCHAIN','SOUI_LLVM_ARCHITECTURE','SOUI_LLVM_ROOT','SOUI_MSVC142_ROOT','SOUI_WINDOWS_SDK_ROOT',
    'SOUI_NINJA_EXECUTABLE','SOUI_WINDOWS_CMD_EXECUTABLE','SOUI_LLVM_INPUT_IDENTITY','SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE',
    'SOUI_TOOL_IDENTITY_BRIDGE','SOUI_TOOL_IDENTITY_HELPER','SOUI_LLVM_BRIDGE_IDENTITY'];
  for(const architecture of ['x86','x64']){
    const definitions=Object.fromEntries(names.map((name,index)=>[name,'fixture-'+index]));
    const request={value:{architecture,discovery:{CMakeDefinitions:definitions},toolchain:{path:'untrusted-path-is-not-selected'}}};
    const args=configureArguments(root,suite,request);
    assert.ok(args.includes('--fresh'));assert.ok(args.includes('Ninja Multi-Config'));
    assert.equal(args.filter(arg=>/^\-DSOUI_[A-Z0-9_]+:STRING=/.test(arg)).length,12);
    assert.ok(args.includes('-DCMAKE_TOOLCHAIN_FILE:FILEPATH='+path.join(root,'cmake/toolchains/SouiLLVM22.cmake').replaceAll('\\','/')));
    assert.ok(!args.some(arg=>arg.includes('untrusted-path-is-not-selected')));
  }
});
test('Source retains resource phase classification and MSVC default while binding explicit LLVM resource entry',()=>{
  const source=fs.readFileSync(path.join(__dirname,'runner.cjs'),'utf8');
  assert.ok(source.includes('evidence.finalVerified===true && evidence.failure===null'));
  assert.ok(source.includes('inputRecheckDiagnostic:inputs || null'));
  assert.ok(!source.includes('LLVM ResourceCapture is unsupported'));
  assert.ok(source.includes('const frozen=resourceFiles?resourceFiles.freeze(suite,output):null'));
  assert.ok(source.includes('nativeToolchain===undefined?builds.verifyBuild(root,architecture,configuration,true)'));
  const helper=fs.readFileSync(path.join(root,'tools/internal/llvm-installed-acceptance.cjs'),'utf8');
  assert.ok(helper.includes('standard.capture(p.evidence,name,executable,argv'));
  assert.ok(!helper.includes('launchOwned('));assert.ok(!helper.includes('spawnSync('));
  assert.ok(helper.includes('profiles.sameProfile(sdk.project,request)'));
  assert.ok(helper.includes('llvm.validatePreparedProfile(root,v,prepared,requestFile)'));
});
