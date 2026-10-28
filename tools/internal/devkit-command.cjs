'use strict';
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const {pathToFileURL}=require('node:url');
const {isDeepStrictEqual}=require('node:util');
const owned=require('./owned-process.cjs');
const identity=require('./delivery-identity.cjs');
const standard=require('./standard-build.cjs');
const {parseStrictJson}=require('./owned-tail-evidence.cjs');

class CommandError extends Error {
  constructor(code,fix,message=fix) { super(message); this.code=code; this.fix=fix; }
}
const need=(ok,code,fix)=>{if(!ok)throw new CommandError(code,fix);};
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const encoded=value=>JSON.stringify(value,null,2)+'\n';
const common={'--architecture':'architecture','--configuration':'configuration','--format':'format',
  '--timeout-ms':'timeoutMs','--drain-ms':'drainMs','--runner':'runner','--devkit-root':'devkitRoot'};
const definitions={
  create:{'--name':'name','--output-root':'outputRoot','--sdk-package':'sdkPackage','--profile':'profile'},
  doctor:{'--native-toolchain':'nativeToolchain','--llvm-root':'llvmRoot','--msvc-root':'msvcRoot','--windows-sdk-root':'windowsSdkRoot',
    '--ninja-exe':'ninjaExe','--windows-cmd-exe':'windowsCmdExe','--powershell-exe':'powerShellExe'},
  build:{'--sdk-only':'sdkOnly','--skip-web':'skipWeb','--with-webview2':'withWebView2','--with-auto-host':'withAutoHost','--with-uia':'withUia',
    '--with-gui-bindings':'withGuiBindings','--with-vendor-tests':'withVendorTests','--install':'install','--fresh-configure':'freshConfigure',
    '--compiler-tail':'compilerTail','--tail-image':'tailImage','--tail-sha256':'tailSha256',
    '--native-toolchain':'nativeToolchain','--llvm-root':'llvmRoot','--msvc-root':'msvcRoot','--windows-sdk-root':'windowsSdkRoot',
    '--ninja-exe':'ninjaExe','--windows-cmd-exe':'windowsCmdExe','--powershell-exe':'powerShellExe'},
  test:{'--scope':'scope','--native-toolchain':'nativeToolchain','--llvm-root':'llvmRoot','--msvc-root':'msvcRoot',
    '--windows-sdk-root':'windowsSdkRoot','--ninja-exe':'ninjaExe','--windows-cmd-exe':'windowsCmdExe','--powershell-exe':'powerShellExe'},
  package:{'--kind':'kind','--output':'output','--with-gui-bindings':'withGuiBindings','--profile':'profile',
    '--compression-profile':'compressionProfile','--bundle':'bundle'},
  upgrade:{'--sdk-package':'sdkPackage','--project':'project'},
  dev:{'--web-root':'webRoot','--port':'port','--duration-ms':'durationMs',
    '--native-toolchain':'nativeToolchain','--llvm-root':'llvmRoot','--msvc-root':'msvcRoot','--windows-sdk-root':'windowsSdkRoot',
    '--ninja-exe':'ninjaExe','--windows-cmd-exe':'windowsCmdExe','--powershell-exe':'powerShellExe'}
};
const booleans=new Set(['sdkOnly','skipWeb','withWebView2','withAutoHost','withUia','withGuiBindings','withVendorTests','install','freshConfigure','compilerTail','bundle']);
const pathKeys=new Set(['runner','devkitRoot','outputRoot','sdkPackage','tailImage','output','project','webRoot']);
const defaults={architecture:'x64',configuration:'Release',format:'human',drainMs:2000,
  profile:'compatibility',scope:'unit',kind:'source',port:5173,durationMs:900000};
function parseArgs(args) {
  need(Array.isArray(args)&&args.every(a=>typeof a==='string'&&!/[\x00-\x1f]/.test(a)),
    'E_ARGUMENT','Pass command arguments without control characters.');
  const [command,...rest]=args;
  need(Object.hasOwn(definitions,command),'E_COMMAND','Use create, doctor, dev, build, test, package or upgrade.');
  const fields={...common,...definitions[command]},value={...defaults,command},seen=new Set();
  value.timeoutMs=command==='build'||command==='test'?900000:120000;
  for(let i=0;i<rest.length;i++) {
    const key=fields[rest[i]];
    need(key&&!seen.has(key),'E_ARGUMENT','Use each documented option once; arbitrary commands and extra argv are not accepted.');
    seen.add(key);
    if(booleans.has(key))value[key]=true;
    else {
      const item=rest[++i];
      need(typeof item==='string'&&item.length>0&&!item.startsWith('-'),'E_ARGUMENT','Supply one literal value after each option.');
      if(['timeoutMs','drainMs','port','durationMs'].includes(key)) {
        need(/^(0|[1-9]\d*)$/.test(item),'E_ARGUMENT','Use a canonical decimal budget or port.');value[key]=Number(item);
      } else value[key]=item;
    }
  }
  need(['x86','x64'].includes(value.architecture)&&['Debug','Release'].includes(value.configuration)&&
    ['human','json'].includes(value.format),'E_ARGUMENT','Select x86/x64, Debug/Release and human/json.');
  for(const [key,min,max] of [['timeoutMs',1000,86400000],['drainMs',0,60000],['durationMs',1000,3600000],['port',1024,65535]])
    need(Number.isSafeInteger(value[key])&&value[key]>=min&&value[key]<=max,'E_ARGUMENT','Use bounded, documented numeric values.');
  for(const key of pathKeys)if(value[key]!==undefined)
    need(typeof value[key]==='string'&&/^[A-Za-z]:[\\/]/.test(value[key])&&!/[<>"|?*]/.test(value[key])&&!value[key].slice(2).includes(':'),
      'E_PATH','Supply an absolute local Windows path; UNC, device paths and wildcards are not accepted.');
  if(command==='create')need(/^[A-Za-z][A-Za-z0-9_]{1,63}$/.test(value.name||'')&&
    !/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$/i.test(value.name)&&value.outputRoot&&['compatibility','modern','auto'].includes(value.profile),
    'E_ARGUMENT','Create requires a safe 2–64 character name, new output-root and compatibility/modern/auto profile.');
  if(command==='upgrade')need(value.sdkPackage&&value.project,'E_ARGUMENT','Upgrade requires sdk-package and project.');
  if(command==='package')need(['source','sdk','applications'].includes(value.kind)&&
    (value.kind!=='sdk'||value.output)&&(!value.withGuiBindings||value.kind==='sdk')&&
    (!seen.has('profile')||value.kind==='applications')&&['compatibility','modern','auto'].includes(value.profile)&&
    (!seen.has('compressionProfile')||(value.kind==='applications'&&['zip','7z-balanced','7z-small'].includes(value.compressionProfile)))&&
    (!seen.has('bundle')||value.kind==='applications')&&
    (value.kind!=='applications'||value.output===undefined),'E_ARGUMENT','Select source/sdk/applications; sdk requires output, and applications uses its existing fixed archive names. Compression profiles zip/7z-balanced/7z-small and bundle require applications.');
  if(command==='test')need(['contract','unit','native','desktop'].includes(value.scope),'E_ARGUMENT','Select contract, unit, native or desktop test scope.');
  value.scopeExplicit=seen.has('scope');
  if(command==='dev'&&value.nativeToolchain!==undefined)need(value.nativeToolchain==='LLVM22MSVC142',
    'E_ARGUMENT','Dev selects LLVM only with the explicit LLVM22MSVC142 profile and all six paths.');
  if(['doctor','test','dev'].includes(command)&&value.nativeToolchain!==undefined) {
    const profileFlags=Object.entries(definitions.doctor).flatMap(([flag,key])=>value[key]===undefined?[]:[flag,value[key]]);
    try{standard.parseArgs(profileFlags);}catch{throw new CommandError('E_ARGUMENT','Use the explicit LLVM profile with all six paths; helper overrides are not accepted.');}
  } else if(['doctor','test','dev'].includes(command))need(Object.values(definitions.doctor).every(key=>value[key]===undefined),
    'E_ARGUMENT','LLVM paths require explicit native-toolchain selection.');
  if(command==='build') {
    try{standard.parseArgs(buildOptions(value));}catch{throw new CommandError('E_ARGUMENT','Use the documented standard-build flags and explicit reviewed compiler-tail pin.');}
  }
  return value;
}
function buildOptions(v) {
  const result=['--architecture',v.architecture,'--configuration',v.configuration,'--timeout-ms',String(v.timeoutMs),'--drain-ms',String(v.drainMs)];
  for(const [flag,key] of Object.entries(definitions.build))if(v[key]!==undefined)
    result.push(flag,...(booleans.has(key)?[]:[v[key]]));
  return result;
}
function planCommand(v,root,powershell,ctest) {
  const ps=(script,args=[])=>({executable:powershell,args:['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'tools',script),...args]});
  if(v.command==='create')return ps('New-Project.ps1',['-Name',v.name,'-OutputRoot',v.outputRoot,'-Profile',v.profile,...(v.sdkPackage?['-SdkPackage',v.sdkPackage]:[])]);
  if(v.command==='doctor')return ps('Doctor.ps1',[
    ...(v.nativeToolchain?['-NativeToolchain',v.nativeToolchain]:[]),
    ...(v.nativeToolchain==='LLVM22MSVC142'?['-Architecture',v.architecture,
      '-LlvmRoot',v.llvmRoot,'-MsvcRoot',v.msvcRoot,'-WindowsSdkRoot',v.windowsSdkRoot,
      '-NinjaExe',v.ninjaExe,'-WindowsCmdExe',v.windowsCmdExe,'-PowerShellExe',v.powerShellExe]:[])]);
  if(v.command==='test') {
    const profileArgs=v.nativeToolchain==='LLVM22MSVC142'?['-NativeToolchain',v.nativeToolchain,
      ...Object.entries(standard.llvmPaths).flatMap(([key,flag])=>[flag,v[key]])]:[];
    if(v.scope==='desktop')return {executable:process.execPath,args:[path.join(root,'tools/internal/desktop-smoke.cjs'),
      'run',v.architecture,v.configuration,v.devkitRoot||root,
      ...(v.nativeToolchain==='LLVM22MSVC142'?['--toolchain-request',require('./llvm-build-observer.cjs').profileRequestPath(root,v)]:[])]};
    if(v.scope==='contract')return {executable:process.execPath,args:[path.join(root,'tools/sdk/soui-sdk.cjs'),'verify-project','--project',root]};
    return v.scope==='unit'?ps('Test.ps1',['-Architecture',v.architecture,'-Configuration',v.configuration,'-UnitOnly',...profileArgs]):
      {executable:ctest,args:['--test-dir',path.join(root,'out',v.architecture),'-C',v.configuration,'--output-on-failure']};
  }
  if(v.command==='package'&&v.kind!=='sdk')return ps(v.kind==='source'?'Package.ps1':'Package-Applications.ps1',
    v.kind==='source'?(v.output?['-OutputFile',v.output]:[]):['-Architecture',v.architecture,...(v.profile!=='compatibility'?['-Profile',v.profile]:[]),
      ...(v.compressionProfile?['-CompressionProfile',v.compressionProfile]:[]),...(v.bundle?['-Bundle']:[])]);
  const sdk=path.join(root,'tools/sdk/soui-sdk.cjs');
  if(v.command==='upgrade')return {executable:process.execPath,args:[sdk,'install','--package',v.sdkPackage,'--project',v.project]};
  if(v.command==='package')return {executable:process.execPath,args:[sdk,'pack','--source',root,'--output',v.output,'--version',
    fs.readFileSync(path.join(root,'VERSION'),'utf8').trim(),...(v.withGuiBindings?['--with-gui-bindings']:[])]};
  if(v.command==='build')return {executable:process.execPath,args:[path.join(root,'tools/internal/standard-build.cjs'),
    ...buildOptions(v),'--root',root,'--runner',v.runner,'--powershell',powershell,
    ...(v.nativeToolchain==='LLVM22MSVC142'&&v.devkitRoot?['--devkit-root',v.devkitRoot]:[])]};
  throw new CommandError('E_COMMAND','Internal command plan is unavailable.');
}
function commandDispatch(v,space,root,powershell,ctest){
  const standardBuild=v.command==='build'&&(!space.generated||v.nativeToolchain==='LLVM22MSVC142');
  return {standardBuild,workspaceLeaseRequired:!standardBuild,
    timeoutMs:standardBuild?v.timeoutMs+v.drainMs+45000:v.timeoutMs,
    invocation:v.command!=='build'?null:space.generated&&!standardBuild?
      {executable:powershell,args:standard.buildArguments(root,standard.parseArgs(buildOptions(v)))}:planCommand(v,root,powershell,ctest)};
}
function validateStage(raw,contextBytes,ready,result,expected) {
  need(Buffer.isBuffer(raw)&&raw.length>0&&raw.length<=16777216&&raw.at(-1)===10,'E_SUPERVISION','Retain the raw report; it is missing, excessive or truncated.');
  const rows=raw.toString('utf8').slice(0,-1).split('\n').map(parseStrictJson);
  const starts=rows.filter(x=>x.event==='start'),exits=rows.filter(x=>x.event==='root-exit'),ends=rows.filter(x=>x.event==='terminal');
  need(starts.length===1&&exits.length===1&&ends.length===1&&rows[0]===starts[0]&&rows.at(-1)===ends[0],
    'E_SUPERVISION','Retain the raw report; exactly one ordered start, root-exit and terminal is required.');
  const s=starts[0],t=ends[0],context=parseStrictJson(contextBytes.toString('utf8'));
  need(s.schema==='soui.owned-process-run/v1'&&Number.isSafeInteger(s.pid)&&s.pid>0&&
    typeof s.creationFileTime==='string'&&/^[1-9]\d{0,19}$/.test(s.creationFileTime)&&BigInt(s.creationFileTime)<=18446744073709551615n&&
    s.jobAssignedBeforeResume===true&&s.breakawayAllowed===false&&s.timeoutMs===expected.timeoutMs&&s.drainMs===expected.drainMs,
    'E_SUPERVISION','The subject must have an exact native PID/creation identity and assigned non-breakaway Job.');
  need(ready&&ready.pid===s.pid&&ready.creationFileTime===s.creationFileTime&&result.pid===s.pid&&result.creationFileTime===s.creationFileTime&&
    context.schema==='soui.owned-process-context/v1'&&context.executable===expected.executable&&context.cwd===expected.cwd&&
    isDeepStrictEqual(context.args,expected.args)&&context.timeoutMs===expected.timeoutMs&&context.drainMs===expected.drainMs&&
    context.executableSha256===expected.executableSha256&&context.runnerSha256===expected.runnerSha256&&
    sha(contextBytes)===expected.contextSha256&&sha(raw)===result.reportSha256&&
    result.executableSha256===expected.executableSha256&&result.runnerSha256===expected.runnerSha256&&isDeepStrictEqual(t,result.terminal)&&
    result.exitCode===t.rootExitCode&&exits[0].exitCode===t.rootExitCode,
    'E_SUPERVISION','Actual argv, cwd, hashes, context or native receipt differs from the prelaunch command.');
  need(result.passed===true&&result.exitCode===0&&result.supervisorExitCode===0&&result.signal===null&&result.spawnError===null&&
    result.reportError===null&&!result.stopRequested&&!result.fallbackUsed&&result.closureConfirmed===true&&
    t.reason==='completed'&&t.rootExited===true&&t.rootExitCode===0&&t.passed===true&&t.closureConfirmed===true&&
    t.activeProcesses===0&&t.activeBeforeCleanup===0&&t.limitTerminatedProcesses===0&&t.terminatedOwnedJob===false&&
    Number.isSafeInteger(t.totalProcesses)&&t.totalProcesses>=1,
    'E_STAGE_FAILED','Inspect this attempt’s raw/stdout/stderr. Runtime, tests and preview require natural 0/0 closure; STOP, timeout and compiler tails are not success.');
  return {pid:s.pid,creationFileTime:s.creationFileTime,genericSupervisionPassed:true,closureConfirmed:true};
}
function event(v,stage,status,code,fix,details={}) {
  const row={...details,schema:'soui.devkit-event/v1',command:v.command,stage,status,code,fix,releaseReady:false};
  if(v.format==='json')process.stdout.write(JSON.stringify(row)+'\n');
  else process.stdout.write('SOUI '+stage+': '+status+' ['+code+'] '+fix+(details.url?' '+details.url:'')+'\n');
}
function write(folder,name,value) {const file=identity.safePath(folder,name);fs.writeFileSync(file,encoded(value),{flag:'wx'});return file;}
const junitReader=String.raw`param([string]$XmlPath,[string]$NamesPath)
$ErrorActionPreference='Stop'
$names=@(Get-Content -Raw -LiteralPath $NamesPath | ConvertFrom-Json)
$settings=[Xml.XmlReaderSettings]::new()
$settings.DtdProcessing=[Xml.DtdProcessing]::Prohibit
$settings.XmlResolver=$null
$reader=[Xml.XmlReader]::Create($XmlPath,$settings)
try { $document=[Xml.XmlDocument]::new();$document.XmlResolver=$null;$document.Load($reader) }
finally { $reader.Dispose() }
$cases=@($document.SelectNodes('//testcase'))
if($cases.Count -ne $names.Count -or $cases.Count -eq 0){throw 'Actual JUnit count differs from registry'}
$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach($case in $cases) {
  $name=$case.GetAttribute('name')
  if(-not $seen.Add($name) -or -not ($names -ccontains $name)){throw 'Duplicate or foreign JUnit name'}
  if($case.SelectNodes('failure|error|skipped').Count -ne 0){throw 'Actual JUnit failed, errored or skipped'}
}
[ordered]@{schema='soui.devkit-junit/v1';tests=$cases.Count;failures=0;errors=0;skipped=0}|ConvertTo-Json -Compress
`;
function pins(root,names) {return [...new Set(names)].map(name=>{const file=identity.safePath(root,name),bytes=fs.readFileSync(file);return {path:name,bytes:bytes.length,sha256:sha(bytes)};});}
function workspace(root,v) {
  const projectFile=identity.safePath(root,'project.json');
  if(fs.existsSync(projectFile)) {
    const project=parseStrictJson(fs.readFileSync(projectFile,'utf8'));
    need(project.schema==='soui.application/v1'&&['compatibility','modern','auto'].includes(project.runtimeProfile),
      'E_WORKSPACE','Use a generated project with the existing project.json contract.');
    need(!(v.command==='build'&&v.withVendorTests),'E_WORKSPACE_CAPABILITY','Vendor tests require the complete source devkit; generated applications do not deliver the private dependency tests.');
    require('../sdk/soui-sdk.cjs').readLock(root);
    need(!['create','package'].includes(v.command),'E_WORKSPACE_CAPABILITY','Create and distribution packaging require the complete source devkit root; installed projects contain no templates or Demo.');
    need(!(v.command==='test'&&v.scopeExplicit&&v.scope==='unit'),'E_WORKSPACE_CAPABILITY','Use test --scope contract for installed SDK integrity or native for your real CTest registry. Source-devkit unit suites are not delivered in Core.');
    if(v.command==='test'&&!v.scopeExplicit)v.scope='contract';
    need(!(v.command==='build'&&v.compilerTail),'E_COMPILER_TAIL_SCOPE','The reviewed compiler-tail standard caller applies to the complete source root. Installed-project builds require natural closure in this slice.');
    return {generated:true,profile:project.runtimeProfile};
  }
  need(fs.existsSync(path.join(root,'templates/desktop-app/CMakeLists.txt'))&&fs.existsSync(path.join(root,'benchmarks/protocol/CMakeLists.txt')),
    'E_WORKSPACE','Use the complete source devkit root or a generated project; a bare Core payload is not a full devkit.');
  need(!(v.command==='test'&&v.scope==='contract'),'E_WORKSPACE_CAPABILITY','Contract scope verifies an installed sdk-lock.json; use source unit/native tests here.');
  return {generated:false,profile:'compatibility'};
}
// Selection checks follow the existing reader's authentication of actual stock inputs.
function validatePreviewPreparation(v,prepared,previous) {
  const report=prepared?.setup?.report,request=prepared?.request,stockRoot=prepared?.toolsRoot;
  need(v.command==='dev'&&v.nativeToolchain==='LLVM22MSVC142'&&
    typeof stockRoot==='string'&&path.isAbsolute(stockRoot)&&
    report?.schema==='soui.measurement-tool-preparation/v2'&&report.prepared===true&&report.buildExitCode===0&&
    report.configuration==='Release'&&report.architecture===v.architecture&&
    request?.schema==='soui.build-toolchain-request/v1'&&request.profile===v.nativeToolchain&&request.architecture===v.architecture&&
    report.nativeToolchain?.profile===v.nativeToolchain,
    'E_VALIDATION_TOOLS','Explicit LLVM preview requires current Release/v2 tools and their independently owned request for the selected architecture.');
  const directory=path.join(stockRoot,'out',v.architecture==='x64'?'benchmark-protocol':'benchmark-protocol-x86');
  for(const [row,name] of [[prepared.requestPin,'llvm-profile-request-Release.json'],[prepared.reportPin,'measurement-tools.json']])
    need(row&&typeof row.path==='string'&&path.resolve(row.path)===path.join(directory,name)&&
      Number.isSafeInteger(row.bytes)&&row.bytes>0&&/^[a-f0-9]{64}$/.test(row.sha256),
      'E_VALIDATION_TOOLS','Preview request and preparation must keep their exact stock-owned raw pins.');
  need(isDeepStrictEqual(report.nativeToolchain.requestFile,prepared.requestPin),
    'E_VALIDATION_TOOLS','The prepared report names another toolchain request.');
  const fields={llvmRoot:'SOUI_LLVM_ROOT',msvcRoot:'SOUI_MSVC142_ROOT',windowsSdkRoot:'SOUI_WINDOWS_SDK_ROOT',
    ninjaExe:'SOUI_NINJA_EXECUTABLE',windowsCmdExe:'SOUI_WINDOWS_CMD_EXECUTABLE',powerShellExe:'SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE'};
  for(const [key,name] of Object.entries(fields)) {
    const expected=request.discovery?.CMakeDefinitions?.[name];
    need(typeof v[key]==='string'&&typeof expected==='string'&&
      path.win32.normalize(v[key]).toLowerCase()===path.win32.normalize(expected).toLowerCase(),
      'E_VALIDATION_TOOLS','The explicit preview paths differ from the independently prepared tools.');
  }
  const binding={profile:v.nativeToolchain,architecture:v.architecture,toolsRoot:stockRoot,
    requestFile:{...prepared.requestPin},preparation:{...prepared.reportPin}};
  if(previous!==undefined)need(isDeepStrictEqual(binding,previous),
    'E_INPUT_CHANGED','Preview toolchain request or preparation raw bytes changed during execution.');
  return binding;
}
const callerInputs=['tools/Soui.ps1','tools/internal/devkit-command.cjs','tools/internal/owned-process.cjs',
  'tools/internal/owned-tail-evidence.cjs','tools/internal/delivery-identity.cjs','tools/internal/standard-build.cjs',
  'tools/internal/measurement-validation.cjs','tools/internal/build-evidence.cjs','tools/internal/sdk-version.cjs',
  'tools/internal/compiler-tail-policy.cjs','tools/sdk/soui-sdk.cjs','tools/sdk/install-transaction.cjs',
  'tools/internal/desktop-smoke.cjs'];
async function runCommand(v) {
  need(process.platform==='win32','E_PLATFORM','Run this entry on a Windows developer machine.');
  need(/^[a-f0-9]{32}$/.test(process.env.SOUI_DEVKIT_ENTRY_NONCE||''),'E_ENTRY_REQUIRED','Use tools/Soui.ps1 so the actual DesktopValidation mutex spans every stage.');
  const root=identity.safePath(path.resolve(__dirname,'../..'));
  v={...v};const space=workspace(root,v);
  if(v.command==='upgrade')need(path.resolve(v.project).toLowerCase()!==root.toLowerCase(),
    'E_UPGRADE_SELF','Run upgrade from the complete source devkit with --project pointing to this installed project. Executing tool scripts are not replaced in place.');
  const toolsRoot=identity.safePath(v.devkitRoot||root);
  need(fs.existsSync(path.join(toolsRoot,'benchmarks/protocol/CMakeLists.txt')),'E_VALIDATION_TOOLS',
    'For an installed project, pass --devkit-root pointing to the complete source devkit with reviewed prepared validation tools.');
  const measurement=v.nativeToolchain==='LLVM22MSVC142'?require('./measurement-validation.cjs'):
    require(path.join(toolsRoot,'tools/internal/measurement-validation.cjs'));
  let setup,previewBinding;
  try{
    if(v.nativeToolchain==='LLVM22MSVC142') {
      const prepared=require('./llvm-build-observer.cjs').preparation(toolsRoot,v.architecture);
      if(v.command==='dev')previewBinding=validatePreviewPreparation(v,prepared);
      setup=prepared.setup;
    } else setup=measurement.prepared(v.architecture);
  }catch{throw new CommandError('E_VALIDATION_TOOLS','Prepare Build-ValidationTools in the selected full devkit root, then retry; this command never silently builds or substitutes its runner.');}
  const runner=setup.tools.OwnedProcessRunner;
  if(v.runner)need(path.resolve(v.runner).toLowerCase()===runner.toLowerCase(),'E_RUNNER','Use the current prepared runner for the selected architecture.');
  v={...v,runner};
  const powershell=process.env.SOUI_DEVKIT_POWERSHELL,ctest=process.env.SOUI_DEVKIT_CTEST;
  need(powershell&&['powershell.exe','pwsh.exe'].includes(path.basename(powershell).toLowerCase()),'E_TOOL','Start through the PowerShell entry.');
  const dispatch=commandDispatch(v,space,root,powershell,ctest);
  if(v.command==='test'&&['native','desktop'].includes(v.scope)) {
    if(v.scope==='native')need(ctest,'E_TOOL','Install CMake and make ctest.exe available to the entry.');
    require('./build-evidence.cjs').verifyAcceptedBuild(root,v.architecture,v.configuration);
  }
  const folder=identity.safePath(root,'out/devkit/attempt-'+crypto.randomUUID());fs.mkdirSync(folder,{recursive:true});
  let workspaceLease=null;
  const frozen=pins(root,[...callerInputs,...(v.nativeToolchain==='LLVM22MSVC142'?require('./llvm-build-observer.cjs').codeNames:[]),...({create:['tools/New-Project.ps1'],doctor:['tools/Doctor.ps1','tools/internal/Find-Toolchain.ps1'],
    build:['tools/Build.ps1'],test:v.scope==='unit'||v.scope==='desktop'&&!space.generated?['tools/Test.ps1']:[],package:v.kind==='source'?['tools/Package.ps1']:v.kind==='applications'?['tools/Package-Applications.ps1']:[],upgrade:[],dev:[]}[v.command])]);
  write(folder,'inputs.json',{schema:'soui.devkit-inputs/v1',command:v.command,options:v,callerInputs:frozen,
    selectedPreparation:setup.report,...(previewBinding?{previewToolchain:previewBinding}:{}),
    toolsRoot,workspaceKind:space.generated?'installed-project':'source-devkit',qualificationPassed:false,releaseReady:false});
  let active=null,interrupted=false,graceTimer=null;
  const interrupt=()=>{interrupted=true;if(active) {
    if(v.command==='dev') {if(!fs.existsSync(path.join(folder,'preview-stop.json')))write(folder,'preview-stop.json',{nonce:previewNonce});
      graceTimer=setTimeout(()=>active?.stop(),2000);
    } else active.stop();
  }};
  const previewNonce=crypto.randomBytes(16).toString('hex');
  process.on('SIGINT',interrupt);process.on('SIGTERM',interrupt);
  async function capture(name,invocation,timeoutMs=v.timeoutMs,env={...process.env}) {
    const executable=standard.inspectTool(invocation.executable),runnerPin=standard.inspectTool(runner);
    const report=path.join(folder,name+'.ndjson'),out=fs.openSync(path.join(folder,name+'.stdout.raw'),'wx'),err=fs.openSync(path.join(folder,name+'.stderr.raw'),'wx');
    let finished,ready=null,poll=null,expected;
    try {
      active=owned.launchOwned(invocation.executable,invocation.args,{runner,cwd:root,report,timeoutMs,drainMs:v.drainMs,stdout:out,stderr:err,env});
      expected={...invocation,cwd:root,timeoutMs,drainMs:v.drainMs,executableSha256:executable.sha256,runnerSha256:runnerPin.sha256,
        contextSha256:sha(fs.readFileSync(report+'.context.json'))};
      if(v.command==='dev') {
        let announced=false;poll=setInterval(()=>{const file=path.join(folder,'preview-ready.json');
          if(!announced&&fs.existsSync(file)){announced=true;try{const readyPage=parseStrictJson(fs.readFileSync(file,'utf8'));
            need(readyPage.url==='http://127.0.0.1:'+v.port+'/','E_PREVIEW','Preview URL differs from the fixed loopback port.');
            event(v,'preview','running','I_PREVIEW_READY','Open this URL in a developer browser; native Bridge handlers are unavailable.',readyPage);
          }catch{active?.stop();}}},100);
      }
      try{ready=await active.ready;}catch{ /* Preserve finished/raw before rejection. */ }
      if(interrupted)interrupt();
      finished=await active.finished;write(folder,name+'.finished.json',finished);
      standard.verifyTool(executable);standard.verifyTool(runnerPin);
      return {identity:validateStage(fs.readFileSync(report),fs.readFileSync(report+'.context.json'),ready,finished,expected),
        finished,stdout:fs.readFileSync(path.join(folder,name+'.stdout.raw'),'utf8')};
    } finally {
      if(poll)clearInterval(poll);if(graceTimer)clearTimeout(graceTimer);
      if(active&&!finished){active.stop();finished=await active.finished;write(folder,name+'.finished.json',finished);}
      active=null;fs.closeSync(out);fs.closeSync(err);
    }
  }
  let answer;
  try {
    if(dispatch.workspaceLeaseRequired)workspaceLease=standard.acquireLease(root,v.architecture);
    event(v,'dispatch','running','I_DISPATCH','The command is supervised inside its own Job.',{evidenceDirectory:folder});
    let actual,nativeNames=null;
    if(v.command==='dev') {
      const web=identity.safePath(v.webRoot||path.join(root,space.generated?'web':'templates/desktop-app/web'));
      actual=await capture('preview',{executable:process.execPath,args:[__filename,'__preview-worker',web,String(v.durationMs),String(v.port),folder,previewNonce]},v.durationMs+15000);
      const closed=parseStrictJson(fs.readFileSync(path.join(folder,'preview-closed.json'),'utf8'));
      need(closed.serverClosed===true&&closed.reason==='budget','E_PREVIEW_CLOSE','Preview did not close normally at its stated budget.');
    } else {
      if(v.command==='test'&&v.scope==='desktop'&&!space.generated) {
        // The outer Soui PowerShell entry holds the desktop lease. UnitOnly
        // does not acquire it; full Test.ps1 is deliberately never nested.
        const before=standard.inspectTool(runner);
        try {
          await capture('desktop-unit',{executable:powershell,args:['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',
            path.join(root,'tools/Test.ps1'),'-Architecture',v.architecture,'-Configuration',v.configuration,'-UnitOnly',
            ...(v.nativeToolchain==='LLVM22MSVC142'?['-NativeToolchain',v.nativeToolchain,
              ...Object.entries(standard.llvmPaths).flatMap(([key,flag])=>[flag,v[key]])]:[])]});
        } finally {
          let after=null,failure=null;
          try {
            after=standard.inspectTool(runner);
            need(isDeepStrictEqual(after,before),'E_RUNNER_CHANGED','UnitOnly attempted to replace the prepared runner currently in use. Retain its actual failure and prepare tools separately.');
            if(v.nativeToolchain==='LLVM22MSVC142')require('./llvm-build-observer.cjs').preparation(toolsRoot,v.architecture);
            else measurement.prepared(v.architecture);
          } catch(error) {failure=error;}
          write(folder,'desktop-unit-runner-check.json',{before,after,matched:failure===null,
            ...(failure?{failure:failure.message}:{}),releaseReady:false});
          if(failure)throw failure;
        }
      }
      if(v.command==='test'&&v.scope==='native') {
        const registry=await capture('native-registry',{executable:ctest,args:['--test-dir',path.join(root,'out',v.architecture),'-C',v.configuration,'--show-only=json-v1']});
        const tests=parseStrictJson(registry.stdout.trim());
        need(tests.kind==='ctestInfo'&&Array.isArray(tests.tests)&&tests.tests.length>0,'E_TESTS_MISSING','Add real project CTest cases and rebuild. An empty registry is not a passing test command.');
        nativeNames=tests.tests.map(x=>x.name);
        need(nativeNames.every(x=>typeof x==='string'&&x.length>0)&&new Set(nativeNames).size===nativeNames.length,
          'E_TESTS_MISSING','CTest names must be nonempty and unique.');
        write(folder,'native-names.json',nativeNames);
      }
      const invocation=dispatch.invocation||planCommand(v,root,powershell,ctest);
      if(nativeNames)invocation.args.push('--output-junit',path.join(folder,'native.junit.xml'));
      actual=await capture(v.command,invocation,dispatch.timeoutMs);
      if(nativeNames) {
        const reader=identity.safePath(folder,'verify-junit.ps1');fs.writeFileSync(reader,junitReader,{flag:'wx'});
        const readerHash=sha(fs.readFileSync(reader));
        await capture('native-junit',{executable:powershell,args:['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',reader,
          '-XmlPath',path.join(folder,'native.junit.xml'),'-NamesPath',path.join(folder,'native-names.json')]});
        need(sha(fs.readFileSync(reader))===readerHash,'E_INPUT_CHANGED','The local JUnit reader changed.');
        require('./build-evidence.cjs').verifyAcceptedBuild(root,v.architecture,v.configuration);
      }
      if(v.command==='build') {
        if(!dispatch.standardBuild)require('./build-evidence.cjs').verifyBuild(root,v.architecture,v.configuration,!!v.sdkOnly);
        else {
          const build=parseStrictJson(actual.stdout.trim());
          need(build.schema==='soui.standard-build-result/v1'&&build.accepted===true&&build.releaseReady===false,
            'E_BUILD_FAILED','Inspect the standard-build evidence; compiler-tail acceptance is never generic success.');
          actual.standardBuild=build;
        }
      }
      if(v.command==='test'&&v.scope==='desktop') {
        const smoke=parseStrictJson(actual.stdout.trim());
        need(smoke.schema==='soui.desktop-smoke-controller/v1'&&smoke.passed===true&&smoke.releaseReady===false&&
          smoke.applicationNaturalClosure===true&&smoke.controllerNaturalClosure==='external-caller-evidence-required',
          'E_DESKTOP_SMOKE_FAILED','Inspect each actual application Job and ready/close log, separately from this controller receipt.');
        require('./build-evidence.cjs').verifySmoke(root,v.architecture,v.configuration);
        actual.desktopSmoke=smoke;
      }
      if(v.command==='upgrade') {
        await capture('verify-upgrade',{executable:process.execPath,args:[path.join(root,'tools/sdk/soui-sdk.cjs'),'verify-project','--project',v.project]});
      }
    }
    need(!interrupted,'E_CANCELLED','The user interrupted this command; any retained closure receipt is not a command success.');
    need(isDeepStrictEqual(pins(root,frozen.map(x=>x.path)),frozen),'E_INPUT_CHANGED','Tool scripts changed during execution; retain this attempt and review changes.');
    if(v.nativeToolchain==='LLVM22MSVC142') {
      const prepared=require('./llvm-build-observer.cjs').preparation(toolsRoot,v.architecture);
      if(v.command==='dev')validatePreviewPreparation(v,prepared,previewBinding);
    }
    else measurement.prepared(v.architecture);
    if(space.generated)require('../sdk/soui-sdk.cjs').readLock(root);
    answer={schema:'soui.devkit-result/v1',command:v.command,accepted:true,code:'OK',identity:actual.identity,
      ...(actual.standardBuild?{identityRole:'standard-build-controller',standardBuild:actual.standardBuild}:{}),
      ...(actual.desktopSmoke?{identityRole:'desktop-smoke-controller',desktopSmoke:actual.desktopSmoke}:{}),
      evidenceDirectory:folder,qualificationPassed:false,releaseReady:false};
  } catch(error) {
    answer={schema:'soui.devkit-result/v1',command:v.command,accepted:false,code:error.code||'E_STAGE_FAILED',
      fix:error.fix||'Inspect the retained attempt stdout/stderr and actual owned receipts.',evidenceDirectory:folder,qualificationPassed:false,releaseReady:false};
    if(dispatch.standardBuild)try {
      const build=parseStrictJson(fs.readFileSync(path.join(folder,'build.stdout.raw'),'utf8').trim());
      if(build.schema==='soui.standard-build-result/v1'&&build.releaseReady===false)answer.standardBuild=build;
    } catch { /* A missing/invalid child verdict is not synthesized. */ }
  } finally {process.off('SIGINT',interrupt);process.off('SIGTERM',interrupt);if(graceTimer)clearTimeout(graceTimer);
    if(workspaceLease)try{workspaceLease.release();}catch{answer={...answer,accepted:false,code:'E_LEASE_CHANGED',fix:'The exact workspace lease changed; retain it for inspection.'};}}
  write(folder,'result.json',answer);
  event(v,'complete',answer.accepted?'completed':'failed',answer.code,
    answer.fix||'The finite command completed; this is not a release or target-OS acceptance.',answer);
  return answer;
}
async function previewWorker(args) {
  need(args.length===5&&/^[a-f0-9]{32}$/.test(process.env.SOUI_DEVKIT_ENTRY_NONCE||'')&&/^[a-f0-9]{32}$/.test(args[4]),
    'E_ENTRY_REQUIRED','Preview workers are internal supervised commands.');
  const [webText,durationText,portText,folderText,nonce]=args;
  const values=parseArgs(['dev','--web-root',webText,'--duration-ms',durationText,'--port',portText]);
  const web=identity.safePath(webText),folder=identity.safePath(folderText);
  const root=identity.safePath(path.resolve(__dirname,'../..'));
  need(folder.startsWith(path.join(root,'out/devkit')+path.sep),'E_PATH','Preview control must belong to this attempt.');
  const pkgFile=identity.safePath(web,'package.json'),lockFile=identity.safePath(web,'package-lock.json');
  need(fs.existsSync(pkgFile)&&fs.existsSync(lockFile)&&fs.existsSync(path.join(web,'node_modules/vite/package.json')),
    'E_WEB_DEPENDENCIES','Install the existing package-lock dependencies in this web root before dev.');
  const packageText=fs.readFileSync(pkgFile),lockText=fs.readFileSync(lockFile),pkg=parseStrictJson(packageText.toString('utf8'));
  const vitePackage=identity.safePath(web,'node_modules/vite/package.json'),vitePkg=parseStrictJson(fs.readFileSync(vitePackage,'utf8'));
  need(['6.4.3','6.4.4','6.4.5','8.3.3'].includes(vitePkg.version)&&pkg.devDependencies?.vite===vitePkg.version,
    'E_WEB_DEPENDENCIES','Install the existing frozen package-lock dependencies in this web root; dependencies are never installed or upgraded by dev.');
  const [major,minor]=process.versions.node.split('.').map(Number);
  need((vitePkg.version==='8.3.3'?major>22||(major===22&&minor>=12):major>22||(major===22&&minor>=12)||(major===20&&minor>=19)),
    'E_NODE_VERSION','Use the Node version required by the selected frozen Vite profile.');
  const entry=identity.safePath(web,'node_modules/vite/dist/node/index.js'),entryBefore=sha(fs.readFileSync(entry));
  const {createServer}=await import(pathToFileURL(entry).href);
  need(typeof createServer==='function','E_WEB_DEPENDENCIES','The installed Vite entry does not expose its real createServer API.');
  let server=null,poll=null,timer=null,reason='failed',finish;
  const stop=new Promise(resolve=>{finish=resolve;});
  const end=value=>{if(reason==='failed'){reason=value;finish();}};
  try {
    server=await createServer({root:web,mode:'development',clearScreen:false,
      server:{host:'127.0.0.1',port:values.port,strictPort:true,open:false,
        fs:{strict:true,allow:[web,path.join(root,'sdk/web/src')]}}});
    await server.listen();
    write(folder,'preview-ready.json',{url:'http://127.0.0.1:'+values.port+'/',viteVersion:vitePkg.version,
      resourceUpdates:'actual-vite-watch-and-hmr',nativeBridgeAvailable:false,qualificationPassed:false});
    poll=setInterval(()=>{const file=path.join(folder,'preview-stop.json');if(fs.existsSync(file)){
      try{identity.safePath(file);if(parseStrictJson(fs.readFileSync(file,'utf8')).nonce===nonce)end('requested');else end('invalid-control');}
      catch{end('invalid-control');}}},100);
    timer=setTimeout(()=>end('budget'),values.durationMs);
    await stop;
  } finally {
    if(poll)clearInterval(poll);if(timer)clearTimeout(timer);
    if(server)await server.close();
  }
  need(fs.readFileSync(pkgFile).equals(packageText)&&fs.readFileSync(lockFile).equals(lockText)&&sha(fs.readFileSync(entry))===entryBefore,
    'E_INPUT_CHANGED','The frozen package/lock or Vite entry changed during preview.');
  need(['budget','requested'].includes(reason),'E_PREVIEW_CLOSE','Preview control was invalid.');
  write(folder,'preview-closed.json',{serverClosed:true,reason,qualificationPassed:false,releaseReady:false});
}
module.exports={CommandError,parseArgs,planCommand,commandDispatch,buildOptions,validateStage,workspace,validatePreviewPreparation};
if(require.main===module)(async()=>{
  if(process.argv[2]==='__preview-worker'){await previewWorker(process.argv.slice(3));return;}
  let v;
  try{v=parseArgs(process.argv.slice(2));const result=await runCommand(v);process.exitCode=result.accepted?0:1;}
  catch(error){v??={command:process.argv[2]||'',format:process.argv.includes('json')?'json':'human'};
    event(v,'entry','failed',error.code||'E_SETUP',error.fix||'Prepare reviewed owned validation tools and inspect the setup error.');process.exitCode=1;}
})().catch(error=>{process.stderr.write('SOUI internal preview failed: '+(error.code||'E_PREVIEW')+'\n');process.exitCode=1;});
