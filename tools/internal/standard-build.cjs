'use strict';
const versioning = require('./sdk-version.cjs');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {isDeepStrictEqual} = require('node:util');
const {isUtf8} = require('node:buffer');
const identity = require('./delivery-identity.cjs');
const buildEvidence = require('./build-evidence.cjs');
const {launchOwned} = require('./owned-process.cjs');
const {parseStrictJson,validateTailImagePin} = require('./owned-tail-evidence.cjs');
const {acceptCompilerTail} = require('./compiler-tail-policy.cjs');
const encode = value => Buffer.from(JSON.stringify(value,null,2)+'\n');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const need = (condition,message) => {if(!condition)throw new Error(message);};
const switches = {sdkOnly:'-SdkOnly',skipWeb:'-SkipWeb',withWebView2:'-WithWebView2',withAutoHost:'-WithAutoHost',withUia:'-WithUia',withGuiBindings:'-WithGuiBindings',withVendorTests:'-WithVendorTests',install:'-Install',freshConfigure:'-FreshConfigure'};
const cli = {'--root':'root','--architecture':'architecture','--configuration':'configuration','--runner':'runner','--powershell':'powershell',
  '--timeout-ms':'timeoutMs','--drain-ms':'drainMs','--tail-image':'tailImage','--tail-sha256':'tailSha256',
  '--sdk-only':'sdkOnly','--skip-web':'skipWeb','--with-webview2':'withWebView2','--with-auto-host':'withAutoHost','--with-uia':'withUia','--with-gui-bindings':'withGuiBindings','--with-vendor-tests':'withVendorTests',
  '--install':'install','--fresh-configure':'freshConfigure','--compiler-tail':'compilerTail',
  '--native-toolchain':'nativeToolchain','--llvm-root':'llvmRoot','--msvc-root':'msvcRoot','--windows-sdk-root':'windowsSdkRoot',
  '--ninja-exe':'ninjaExe','--windows-cmd-exe':'windowsCmdExe','--powershell-exe':'powerShellExe','--devkit-root':'devkitRoot',
  '--host64-build-observer':'host64BuildObserver','--host64-observer-prepared':'host64ObserverPrepared','--host64-observer-sha256':'host64ObserverSha256'};
const llvmPaths={llvmRoot:'-LlvmRoot',msvcRoot:'-MsvcRoot',windowsSdkRoot:'-WindowsSdkRoot',
  ninjaExe:'-NinjaExe',windowsCmdExe:'-WindowsCmdExe',powerShellExe:'-PowerShellExe'};
const booleans = new Set([...Object.keys(switches),'compilerTail','host64BuildObserver']);
// An opt-in is permission to use the reviewed policy, not to select a new
// helper by name. The policy independently rechecks these measured bytes.
const reviewedTailSha256={x86:'1c1ca5f9c04171c02caca6863e29eca3550333219486d23e65bf01fb02951311',
  x64:'651345513aae26090c813b1ded859a5680c3823ca716a80e1d337bac0ca797ef'};
function options(value) {
  need(value&&typeof value==='object'&&!Array.isArray(value),'Invalid build options');
  for(const key of Object.keys(value))need(Object.values(cli).includes(key),'Unknown build option: '+key);
  const result={architecture:'x64',configuration:'Release',timeoutMs:900000,drainMs:30000,...value};
  need(['x86','x64'].includes(result.architecture)&&['Debug','Release'].includes(result.configuration),'Invalid architecture/configuration');
  for(const key of booleans)need(result[key]===undefined||typeof result[key]==='boolean','Invalid boolean option: '+key);
  for(const [key,min,max] of [['timeoutMs',1,172800000],['drainMs',0,60000]])need(Number.isSafeInteger(result[key])&&result[key]>=min&&result[key]<=max,'Invalid '+key);
  for(const key of ['root','runner','powershell','devkitRoot'])if(result[key]!==undefined)
    need(typeof result[key]==='string'&&/^[A-Za-z]:[\\/]/.test(result[key])&&!/[\x00-\x1f]/.test(result[key]),'Explicit '+key+' must be an absolute Windows path');
  need(!result.withVendorTests||!result.sdkOnly,'Vendor tests require the complete source devkit, without sdk-only');
  const profile=result.nativeToolchain||'MSVC143';
  need(['MSVC143','LLVM22MSVC142'].includes(profile),'Invalid native toolchain');
  if(profile==='LLVM22MSVC142') {
    if(value.drainMs===undefined)result.drainMs=2000;
    need(result.drainMs===2000,'LLVM whole Job requires the fixed 2000ms drain');
  }
  for(const key of Object.keys(llvmPaths)) {
    if(profile==='LLVM22MSVC142')need(typeof result[key]==='string'&&/^[A-Za-z]:[\\/]/.test(result[key])&&
      !/[;"\x00-\x1f]/.test(result[key]),'LLVM requires explicit local '+key);
    else need(result[key]===undefined,'LLVM paths require explicit LLVM22MSVC142');
  }
  need(profile!=='LLVM22MSVC142'||!result.compilerTail,'LLVM does not admit the MSVC compiler-tail exception');
  if(result.host64BuildObserver){
    need(profile==='LLVM22MSVC142'&&result.timeoutMs===900000&&!result.compilerTail,'Host observer requires the explicit fixed LLVM whole-Job mode');
    need(typeof result.host64ObserverPrepared==='string'&&/^[A-Za-z]:[\\/]/.test(result.host64ObserverPrepared)&&
      !/[\x00-\x1f]/.test(result.host64ObserverPrepared)&&/^[a-f0-9]{64}$/.test(result.host64ObserverSha256||''),'Explicit actual prepared observer receipt and SHA required');
  }else need(result.host64ObserverPrepared===undefined&&result.host64ObserverSha256===undefined,'Prepared observer inputs require explicit host64-build-observer');
  if(result.compilerTail) {
    need(!result.withAutoHost,'Compiler-tail policy does not include AutoHost');
    need(!result.withGuiBindings,'Compiler-tail policy does not include WithGuiBindings');
    need(!result.withUia,'Compiler-tail policy does not include UIA');
    validateTailImagePin({image:result.tailImage,sha256:result.tailSha256,nonce:'0'.repeat(32)});
    need(result.tailSha256===reviewedTailSha256[result.architecture],'Tail SHA is not the reviewed target-architecture MSVC object');
  } else need(result.tailImage===undefined&&result.tailSha256===undefined,'Tail pin requires explicit compiler-tail opt-in');
  return result;
}
function parseArgs(args) {
  const value={},seen=new Set();
  for(let i=0;i<args.length;i++) {
    const key=cli[args[i]];need(key&&!seen.has(key),'Unknown or repeated option: '+args[i]);seen.add(key);
    if(booleans.has(key))value[key]=true;
    else {
      const item=args[++i];need(typeof item==='string'&&item.length>0&&!item.startsWith('--'),'Missing option value');
      if(key==='timeoutMs'||key==='drainMs'){need(/^(0|[1-9]\d*)$/.test(item),'Invalid decimal budget');value[key]=Number(item);}else value[key]=item;
    }
  }
  return options(value);
}
function buildArguments(root,value) {
  const v=options(value);
  return ['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.win32.join(root,'tools','Build.ps1'),
    '-Architecture',v.architecture,'-Configuration',v.configuration,...Object.entries(switches).filter(([key])=>v[key]).map(([,flag])=>flag),
    ...(v.nativeToolchain?['-NativeToolchain',v.nativeToolchain]:[]),
    ...(v.nativeToolchain==='LLVM22MSVC142'?Object.entries(llvmPaths).flatMap(([key,flag])=>[flag,v[key]]):[])];
}
function file(filePath) {
  identity.safePath(filePath);const stat=fs.statSync(filePath);need(stat.isFile(),'Expected regular file: '+filePath);
  const bytes=fs.readFileSync(filePath);return {path:filePath,bytes:bytes.length,sha256:sha(bytes)};
}
// External tool managers may expose a junction. Source/evidence, prepared
// runners and native tail pins continue to use file()'s no-link boundary.
function inspectTool(filePath) {
  need(typeof filePath==='string'&&/^[A-Za-z]:[\\/]/.test(filePath)&&!/[\x00-\x1f]/.test(filePath),'Absolute tool path required');
  const requestedPath=path.resolve(filePath),resolvedPath=fs.realpathSync.native(requestedPath);
  identity.safePath(resolvedPath);
  const handle=fs.openSync(resolvedPath,'r');let bytes;
  try {
    const before=fs.fstatSync(handle,{bigint:true});need(before.isFile(),'Expected regular tool file');
    bytes=fs.readFileSync(handle);const after=fs.fstatSync(handle,{bigint:true});
    need(['dev','ino','size','mtimeNs','ctimeNs'].every(key=>before[key]===after[key])&&BigInt(bytes.length)===after.size,
      'Tool bytes/identity changed while reading');
    need(fs.realpathSync.native(requestedPath)===resolvedPath,'Tool resolution target changed while reading');
    const current=fs.statSync(resolvedPath,{bigint:true});
    need(['dev','ino','size','mtimeNs','ctimeNs'].every(key=>current[key]===after[key]),'Tool path identity changed while reading');
  } finally {fs.closeSync(handle);}
  return {path:requestedPath,requestedPath,resolvedPath,bytes:bytes.length,sha256:sha(bytes)};
}
function verifyTool(value) {
  need(value&&isDeepStrictEqual(inspectTool(value.requestedPath),value),'Frozen tool identity/target differs');return true;
}
function currentTool(row) {
  return {...(Object.hasOwn(row,'requestedPath')?inspectTool(row.requestedPath):file(row.path)),role:row.role};
}
function json(bytes,name) {
  need(Buffer.isBuffer(bytes)&&bytes.length>0&&bytes.length<=16777216&&isUtf8(bytes),'Invalid '+name+' bytes');
  return parseStrictJson(bytes.toString('utf8'));
}
function time(value) {
  need(typeof value==='string'&&/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/.test(value),'Invalid build timestamp');
  const n=Date.parse(value);need(Number.isSafeInteger(n)&&new Date(n).toISOString()===value,'Invalid build timestamp');
  return (BigInt(n)+11644473600000n)*10000n;
}
function uint64(value) {need(typeof value==='string'&&/^[1-9]\d{0,19}$/.test(value)&&BigInt(value)<=18446744073709551615n,'Invalid creation FILETIME');return value;}
function artifacts(build) {return {payloads:build.payloads,optionalLibraries:build.optionalLibraries,
  ...(build.sdkOnly?{library:build.library}:{}),...(build.optionalPayloads!==undefined?{optionalPayloads:build.optionalPayloads}:{})};}
function validateInvocation(context,frozen) {
  need(context.schema==='soui.owned-process-context/v1'&&['powershell.exe','pwsh.exe'].includes(path.win32.basename(context.executable).toLowerCase()),'Build PowerShell context required');
  const args=context.args,index=args.indexOf('-File');
  need(index>0&&args[index+1]===path.win32.join(context.cwd,'tools','Build.ps1'),'Only the current standard Build.ps1 may be called');
  const flags=new Set();
  for(let i=0;i<index;i++) {
    const flag=args[i];need(!flags.has(flag),'Repeated PowerShell flag');flags.add(flag);
    if(flag==='-ExecutionPolicy')need(args[++i]==='Bypass','PowerShell execution policy differs');
    else need(['-NoProfile','-NoLogo','-NonInteractive'].includes(flag),'Unknown PowerShell option');
  }
  need(flags.has('-NoProfile')&&flags.has('-ExecutionPolicy'),'Missing standard PowerShell setup');
  const values=new Map();
  for(let i=index+2;i<args.length;i++) {
    const flag=args[i];need(!values.has(flag),'Repeated Build option');
    if(['-Architecture','-Configuration','-NativeToolchain',...Object.values(llvmPaths)].includes(flag))values.set(flag,args[++i]);
    else {need(Object.values(switches).includes(flag),'Unknown Build option');values.set(flag,true);}
  }
  need(values.get('-Architecture')===frozen.architecture&&values.get('-Configuration')===frozen.configuration&&
    values.has('-SdkOnly')===frozen.sdkOnly,'Build argv configuration differs');
  need(!frozen.compilerTail||!values.has('-WithGuiBindings'),'Compiler-tail policy excludes GUI bindings');
  need(!frozen.compilerTail||!values.has('-WithUia'),'Compiler-tail policy excludes UIA');
  need(!frozen.compilerTail||!values.has('-WithAutoHost'),'Compiler-tail policy excludes AutoHost');
  need((values.get('-NativeToolchain')||'MSVC143')===(frozen.nativeToolchain||'MSVC143'),'Build argv profile differs');
  for(const [key,flag] of Object.entries(llvmPaths))need(values.get(flag)===frozen[key],'Build argv LLVM path differs: '+key);
}
function verifyStream(raw,context,ready,result) {
  need(Buffer.isBuffer(raw)&&raw.length>0&&raw.length<=16777216&&isUtf8(raw)&&raw.at(-1)===10,'Truncated/invalid native report');
  const rows=raw.toString('utf8').slice(0,-1).split('\n').map(line=>{need(line.length>0,'Empty native record');return parseStrictJson(line);});
  need(rows.length<=100000&&rows.every(row=>row&&typeof row==='object'&&!Array.isArray(row)),'Invalid/excessive native records');
  const count=(value,positive=false)=>Number.isSafeInteger(value)&&value>=(positive?1:0)&&value<=4294967295;
  for(const row of rows) {
    if(row.schema==='soui.owned-tail-snapshot/v1'){need(context.tailImagePin,'Unexpected unpinned snapshot schema');continue;}
    need(['start','root-exit','terminal','owned-member','member-unavailable','member-no-longer-owned','job-members'].includes(row.event),'Unknown native event');
    if(['owned-member','member-unavailable','member-no-longer-owned'].includes(row.event))need(count(row.pid,true),'Invalid native member PID');
    if(row.event==='owned-member') {
      uint64(row.creationFileTime);
      need((typeof row.image==='string'&&row.image.length>0&&!Object.hasOwn(row,'imageReadError'))||
        (row.image===null&&count(row.imageReadError,true)),'Invalid native member image observation');
    }
    if(row.event==='member-unavailable')need(count(row.win32Error,true)&&(!Object.hasOwn(row,'operation')||
      ['verify-membership','read-identity'].includes(row.operation)),'Invalid unavailable-member observation');
    if(row.event==='job-members')need(count(row.totalProcesses,true)&&count(row.activeProcesses)&&count(row.limitTerminatedProcesses)&&
      row.activeProcesses<=row.totalProcesses,'Invalid native Job counts');
  }
  const start=rows[0],terminal=rows.at(-1),exits=rows.filter(row=>row.event==='root-exit');
  need(rows.filter(row=>row.event==='start').length===1&&rows.filter(row=>row.event==='terminal').length===1&&exits.length===1,'Duplicate or missing native identity/terminal');
  need(start.schema==='soui.owned-process-run/v1'&&start.jobAssignedBeforeResume===true&&start.breakawayAllowed===false&&
    count(start.pid,true)&&start.timeoutMs===context.timeoutMs&&start.drainMs===context.drainMs,'Invalid owned subject');
  uint64(start.creationFileTime);
  need(ready&&ready.pid===start.pid&&ready.creationFileTime===start.creationFileTime&&result.pid===start.pid&&result.creationFileTime===start.creationFileTime,'Published subject differs');
  need(terminal.event==='terminal'&&isDeepStrictEqual(result.terminal,terminal)&&result.exitCode===terminal.rootExitCode&&
    exits[0].exitCode===terminal.rootExitCode&&Number.isSafeInteger(exits[0].elapsedMs)&&exits[0].elapsedMs>=0&&
    Number.isSafeInteger(terminal.elapsedMs)&&terminal.elapsedMs>=exits[0].elapsedMs,'Native terminal/root-exit differs');
  need(sha(raw)===result.reportSha256&&result.runnerSha256===context.runnerSha256&&result.executableSha256===context.executableSha256,'Native raw/tool hash differs');
  need(result.signal===null&&result.spawnError===null&&result.reportError===null&&!result.stopRequested&&!result.fallbackUsed&&
    result.closureConfirmed===true&&terminal.closureConfirmed===true&&terminal.activeProcesses===0&&
    Number.isSafeInteger(terminal.totalProcesses)&&terminal.totalProcesses>=1&&terminal.limitTerminatedProcesses===0,'Incomplete supervision observations');
  return {start,terminal};
}

// Diagnostic observations never grant the compiler-tail exception. In
// particular, the last generic enumeration is not a held identity snapshot at
// cleanup time; its timestamp and unavailable members must remain visible.
function compilerCloseoutDiagnostics(raw,result) {
  const answer={schema:'soui.compiler-closeout-diagnostics/v1',diagnosticOnly:true,
    rawReportMatchesResult:false,membersAtCleanupVerified:false,
    rootExitCode:result?.exitCode??null,supervisorExitCode:result?.supervisorExitCode??null,
    reason:result?.terminal?.reason??null,clientGenericSupervisionPassed:result?.passed===true,
    terminatedOwnedJob:result?.terminal?.terminatedOwnedJob===true,
    lastPostRootJobObservation:null,memberObservations:[],failure:null};
  try {
    need(Buffer.isBuffer(raw)&&raw.length>0&&raw.length<=16777216&&isUtf8(raw)&&raw.at(-1)===10,'Missing/truncated native diagnostic report');
    const lines=raw.toString('utf8').slice(0,-1).split('\n');
    need(lines.length<=100000,'Excessive native diagnostic records');
    const rows=lines.map(parseStrictJson);
    need(rows.every(row=>row&&typeof row==='object'&&!Array.isArray(row)),'Invalid native diagnostic record');
    const terminal=rows.at(-1),rootExits=rows.filter(row=>row.event==='root-exit');
    need(terminal.event==='terminal'&&rows.filter(row=>row.event==='terminal').length===1&&rootExits.length===1&&
      isDeepStrictEqual(terminal,result?.terminal)&&sha(raw)===result?.reportSha256,'Diagnostic raw report differs from the observed result');
    answer.rawReportMatchesResult=true;
    const rootIndex=rows.indexOf(rootExits[0]),last=rows.findLastIndex(row=>row.event==='job-members');
    if(last>rootIndex) {
      const row=rows[last];
      answer.lastPostRootJobObservation={elapsedMs:row.elapsedMs,activeProcesses:row.activeProcesses,
        totalProcesses:row.totalProcesses,limitTerminatedProcesses:row.limitTerminatedProcesses,
        matchesCleanupCount:row.activeProcesses===terminal.activeBeforeCleanup};
      answer.memberObservations=rows.slice(last+1,-1)
        .filter(row=>['owned-member','member-unavailable','member-no-longer-owned'].includes(row.event))
        .map(row=>({...row}));
    }
    if(terminal.activeBeforeCleanup>0&&answer.lastPostRootJobObservation===null)
      answer.failure='No post-root member enumeration is available; residual identities are unknown';
  } catch(error) {answer.failure=error.message;}
  return answer;
}

// The public pure boundary below consumes explicitly supplied observations for
// regression fixtures. runBuild obtains every observation directly from disk.
// Neither this function nor SHA consistency authenticates a same-user rewrite.
function evaluateBuild({frozen,ready,result,contextBytes,rawReport,buildReportBytes,observed,debugReportBytes,debugInputBytes}={}) {
  const answer={schema:'soui.standard-build-result/v1',compilationCompleted:false,genericSupervisionPassed:false,
    closureConfirmed:result?.closureConfirmed===true,compilerTailAcceptance:false,accepted:false,releaseReady:false,
    rootExitCode:result?.exitCode??null,supervisorExitCode:result?.supervisorExitCode??null,
    clientGenericSupervisionPassed:result?.passed===true,
    closeoutDiagnostics:compilerCloseoutDiagnostics(rawReport,result),failure:null};
  try {
    const debugMode=frozen?.schema==='soui.standard-build-inputs/v2';
    if(debugMode)answer.schema='soui.standard-build-result/v2';
    need((debugMode||frozen?.schema==='soui.standard-build-inputs/v1')&&observed,'Missing frozen build inputs/current observations');
    const context=json(contextBytes,'context');need(sha(contextBytes)===frozen.contextSha256&&isDeepStrictEqual(context,frozen.context),'Actual context differs from prelaunch bytes');
    let debugProof=null;
    if(debugMode){
      need(frozen.nativeToolchain==='LLVM22MSVC142'&&!frozen.compilerTail&&frozen.llvm?.debugBinding&&Buffer.isBuffer(debugReportBytes)&&Buffer.isBuffer(debugInputBytes),'Explicit dual-root actual inputs required');
      const binding=frozen.llvm.debugBinding,c=json(debugInputBytes,'host observer input');
      need(sha(debugInputBytes)===binding.input.sha256&&sha(debugReportBytes)===binding.raw.sha256,'Actual debug input/raw hashes differ');
      const reader=require('./host64-build-debug-observer.cjs');
      need(c.root===context.cwd&&c.sdkArchitecture===frozen.architecture&&c.configuration===frozen.configuration,'Debug SDK target differs');
      debugProof=reader.validateEvidence(debugReportBytes,{...c,inputPin:binding.input},{ready,result,contextBytes,rawReport},binding.observer,buildArguments(context.cwd,frozen.invocationOptions));
      need(isDeepStrictEqual(observed.llvm?.debugProof,debugProof),'Independent debug evidence differs');
      answer.outerObserver=debugProof.outerObserver;answer.innerBuild=debugProof.innerBuild;
      answer.actualDebugRawSha256=debugProof.actualDebugRawSha256;
    }else validateInvocation(context,frozen);
    need(Array.isArray(frozen.tools)&&frozen.tools.length>0&&Array.isArray(frozen.scripts)&&frozen.scripts.length>0,'Missing actual source/tool raw inputs');
    need(/^[a-f0-9]{64}$/.test(frozen.rawSource?.sha256||'')&&observed.rawSourceSha256===frozen.rawSource.sha256&&
      observed.sourceSha256===frozen.source.sha256&&observed.scriptSha256===frozen.scriptSha256&&
      isDeepStrictEqual(observed.tools,frozen.tools)&&isDeepStrictEqual(observed.scripts,frozen.scripts),'Current source/script/tool bytes differ');
    need(observed.compilerVerified===true,'Actual CMake/compiler/SDK was not verified');
    const {start,terminal}=verifyStream(rawReport,context,ready,result);
    if(frozen.nativeToolchain==='LLVM22MSVC142') {
      const llvm=observed.llvm;
      need(!frozen.compilerTail&&context.drainMs===2000&&llvm?.profile==='LLVM22MSVC142'&&llvm.architecture===frozen.architecture&&
        llvm.configuration===frozen.configuration&&llvm.inputIdentity===frozen.llvm?.inputIdentity&&
        llvm.bridgeIdentity===frozen.llvm?.bridgeIdentity&&llvm.observedPointerBytes===(frozen.architecture==='x64'?8:4)&&
        llvm.buildPid===(debugMode?debugProof.innerBuild.pid:start.pid)&&llvm.buildCreationFileTime===(debugMode?debugProof.innerBuild.creationFileTime:start.creationFileTime)&&llvm.observerNaturalClosure===true&&
        /^[a-f0-9]{64}$/.test(llvm.observation?.sha256||'')&&llvm.observerCapture?.ready,
        'LLVM build lacks the actual closed Ninja observation bound to this Build');
    }
    const build=json(buildReportBytes,'build report');
    if(frozen.nativeToolchain==='LLVM22MSVC142')need(build.nativeToolchain?.profile==='LLVM22MSVC142'&&
      build.nativeToolchain.request?.profile==='LLVM22MSVC142'&&build.nativeToolchain.request.architecture===frozen.architecture&&
      build.nativeToolchain.request.discovery?.InputIdentity===frozen.llvm.inputIdentity&&
      build.nativeToolchain.request.discovery.BridgeIdentity===frozen.llvm.bridgeIdentity&&
      /^[a-f0-9]{64}$/.test(build.nativeToolchain.requestFile?.sha256||'')&&
      isDeepStrictEqual(build.nativeToolchain.requestFile,observed.llvm.requestFile),
      'Current source-build request differs from the actual LLVM observation');
    need(build.schema==='soui.source-build/v2'&&build.nativeBuild==='passed'&&build.releaseReady===false&&
      /^[a-f0-9]{8}-(?:[a-f0-9]{4}-){3}[a-f0-9]{12}$/.test(build.buildId)&&build.buildId!==frozen.oldBuild?.buildId&&
      versioning.isVersion(build.version)&&build.version===frozen.version&&build.architecture===frozen.architecture&&
      build.configuration===frozen.configuration&&build.sdkOnly===frozen.sdkOnly&&build.source?.sha256===frozen.source.sha256&&
      time(build.startedAt)>=BigInt(debugMode?debugProof.innerBuild.creationFileTime:start.creationFileTime)&&time(build.finishedAt)>=time(build.startedAt),'Old/stale/incomplete build report');
    need(isDeepStrictEqual(artifacts(build),observed.artifacts),'Current payload/library differs');
    const uiaSelected=debugMode?frozen.invocationOptions.withUia===true:context.args.includes('-WithUia');
    const uia=build.optionalLibraries?.uia;
    need(uiaSelected===!!uia,'Requested UIA selection differs from current optional archive');
    if(uia)need(Array.isArray(uia.files)&&uia.files.length===1&&
      uia.files[0].path===(build.sdkOnly?'':'sdk/')+'accessibility/uia/'+build.configuration+'/soui_uia.lib',
      'Current UIA archive path differs from the selected SDK layout');
    const autoSelected=debugMode?frozen.invocationOptions.withAutoHost===true:context.args.includes('-WithAutoHost');
    const autoHost=build.optionalLibraries?.autoHost;
    need(!frozen.compilerTail||!autoHost,'Compiler-tail policy excludes AutoHost, including generated auto projects');
    need(!autoSelected||!!autoHost,'Requested AutoHost archive is missing');
    need(!!autoHost===!!build.autoHost,'AutoHost selection differs from current optional archive');
    if(autoHost)need(!!build.optionalLibraries.webview2&&Array.isArray(autoHost.files)&&autoHost.files.length===1&&
      autoHost.files[0].path===(build.sdkOnly?'':'sdk/')+'renderers/webview2/'+build.configuration+'/soui_auto_host.lib',
      'Current AutoHost archive path or WebView2 prerequisite differs');
    need(terminal.rootExited===true&&terminal.rootExitCode===0&&result.exitCode===0,'Compiler root did not finish successfully');
    answer.compilationCompleted=true;
    answer.genericSupervisionPassed=result.passed===true&&result.supervisorExitCode===0&&terminal.passed===true&&
      terminal.reason==='completed'&&terminal.activeBeforeCleanup===0&&terminal.terminatedOwnedJob===false;
    answer.buildId=build.buildId;answer.sourceSha256=frozen.source.sha256;answer.artifacts=observed.artifacts;
    if(answer.genericSupervisionPassed){answer.accepted=true;return answer;}
    need(frozen.compilerTail===true,'Default build requires generic natural completion');
    const review={schema:'soui.reviewed-compiler-invocation/v1',executable:context.executable,executableSha256:context.executableSha256,
      runnerSha256:context.runnerSha256,args:context.args,cwd:context.cwd,scriptSha256:frozen.scriptSha256,sourceSha256:frozen.source.sha256,
      buildId:build.buildId,version:build.version,architecture:build.architecture,configuration:build.configuration,sdkOnly:build.sdkOnly,
      contextSha256:sha(contextBytes),reportSha256:sha(rawReport),buildReportSha256:sha(buildReportBytes),artifacts:observed.artifacts};
    answer.compilerTailPolicy=acceptCompilerTail({reviewedInvocation:review,context,contextBytes,rawReport,buildReportBytes,result,
      actualScriptSha256:observed.scriptSha256,currentSourceSha256:observed.sourceSha256,currentArtifacts:observed.artifacts});
    need(answer.compilerTailPolicy.compilerTailAcceptance===true,answer.compilerTailPolicy.failure||'Compiler-tail policy refused');
    answer.compilerTailAcceptance=true;answer.accepted=true;
  } catch(error) {answer.failure=error.message;}
  return answer;
}

function acquireLease(root,architecture) {
  need(['x86','x64'].includes(architecture),'Invalid lease architecture');
  const parent=identity.safePath(root,'out/standard-build');fs.mkdirSync(parent,{recursive:true});
  for(const name of ['lease-x86','lease-x64'])need(!fs.existsSync(identity.safePath(parent,name)),
    'Legacy build lease exists; inspect it without guessing stale PID');
  const directory=identity.safePath(root,'out/standard-build/lease-workspace');
  try{fs.mkdirSync(directory);}catch(error){if(error.code==='EEXIST')throw new Error('Build lease already exists; inspect it without guessing stale PID: '+directory);throw error;}
  const owner=encode({schema:'soui.standard-build-lease/v1',nonce:crypto.randomUUID(),pid:process.pid,architecture,createdAt:new Date().toISOString()});
  const fileName=path.join(directory,'owner.json');fs.writeFileSync(fileName,owner,{flag:'wx'});let released=false;
  return {directory,release(){if(released)return;identity.safePath(directory);need(fs.readdirSync(directory).join(',')==='owner.json'&&
      fs.lstatSync(fileName).isFile()&&fs.lstatSync(fileName).nlink===1&&fs.readFileSync(fileName).equals(owner),'Build lease owner differs; lease retained');
    fs.unlinkSync(fileName);fs.rmdirSync(directory);released=true;}};
}
const scriptNames=['tools/Build.ps1','tools/internal/standard-build.cjs','tools/internal/owned-process.cjs',
  'tools/internal/sdk-version.cjs','tools/internal/owned-tail-evidence.cjs','tools/internal/compiler-tail-policy.cjs','tools/internal/build-evidence.cjs',
  'tools/internal/delivery-identity.cjs','tools/internal/Find-Toolchain.ps1','tools/internal/CMake-Configure.ps1','cmake/SouiWindowsSdk.cmake',
  'tools/internal/measurement-validation.cjs'];
function scripts(root,v={}){return [...new Set([...scriptNames,...(v.nativeToolchain==='LLVM22MSVC142'?
  require('./llvm-build-observer.cjs').codeNames:[])])].map(name=>({...file(identity.safePath(root,name)),path:name}));}
function write(folder,name,bytes){const target=identity.safePath(folder,name);fs.writeFileSync(target,Buffer.isBuffer(bytes)?bytes:encode(bytes),{flag:'wx'});return target;}
function childEnvironment(){return {...process.env,MSBUILDDISABLENODEREUSE:'1'};}
async function capture(folder,name,executable,args,config) {
  const report=path.join(folder,name+'.ndjson');let out,err,run,finished;
  try {
    out=fs.openSync(path.join(folder,name+'-stdout.bin'),'wx');err=fs.openSync(path.join(folder,name+'-stderr.bin'),'wx');
    run=launchOwned(executable,args,{...config,report,stdout:out,stderr:err});
    let ready=null,readyError=null;try{ready=await run.ready;}catch(error){readyError=error.message;}
    // Persist only the ready value actually published by launchOwned. A PID
    // copied from the terminal result is not a publication observation.
    write(folder,name+'-ready.json',ready);
    finished=await run.finished;write(folder,name+'-finished.json',finished);
    const errors=[];const read=fileName=>{try{return fs.readFileSync(fileName);}catch(error){errors.push({path:fileName,error:error.message});return null;}};
    const contextBytes=read(report+'.context.json'),rawReport=read(report);
    if(errors.length)write(folder,name+'-observation-errors.json',errors);
    const closeoutDiagnostics=name==='build'?compilerCloseoutDiagnostics(rawReport,finished):undefined;
    if(closeoutDiagnostics)write(folder,name+'-closeout.json',closeoutDiagnostics);
    return {ready,readyError,result:finished,report,contextBytes,rawReport,...(closeoutDiagnostics?{closeoutDiagnostics}:{})};
  } finally {if(run&&!finished){run.stop();await run.finished;}if(out!==undefined)fs.closeSync(out);if(err!==undefined)fs.closeSync(err);}
}
const probeSource=String.raw`param([string]$Root,[string]$Architecture,[string]$Output)
$ErrorActionPreference='Stop'
. (Join-Path $Root 'tools/internal/Find-Toolchain.ps1')
$vs=Get-SouiToolchain
$versionFile=Join-Path $vs.Root 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
$vcVersion=([IO.File]::ReadAllText($versionFile)).Trim()
if($vcVersion -notmatch '^14\.\d+\.\d+$'){throw 'Unknown VC default toolset'}
$toolBin=Join-Path $vs.Root "VC/Tools/MSVC/$vcVersion/bin/Hostx64/$Architecture"
$tools=[ordered]@{cmake=(Get-Command cmake.exe -ErrorAction Stop).Source;node=(Get-Command node.exe -ErrorAction Stop).Source;
 npm=(Get-Command npm.cmd -ErrorAction Stop).Source;msbuild=(Join-Path $vs.Root 'MSBuild/Current/Bin/amd64/MSBuild.exe');
 compiler=(Join-Path $toolBin 'cl.exe');linker=(Join-Path $toolBin 'link.exe');devcmd=$vs.DevCmd;
 vcVersionFile=$versionFile;vswhere=(Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe')}
$value=[ordered]@{schema='soui.standard-build-tools/v1';installation=$vs.Root;vcVersion=$vcVersion;tools=$tools}
[IO.File]::WriteAllText($Output,($value|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
`;
function compilerObservation(root,v,toolPaths,rawReport) {
  const buildDir=identity.safePath(root,`out/${v.sdkOnly?'sdk-':''}${v.architecture}`);
  const cache=fs.readFileSync(identity.safePath(buildDir,'CMakeCache.txt'),'utf8');const values=new Map();
  for(const line of cache.split(/\r?\n/)){const m=/^([^#/:][^:]*):[^=]+=(.*)$/.exec(line);if(m){need(!values.has(m[1]),'Duplicate actual CMake cache entry');values.set(m[1],m[2]);}}
  need(values.get('CMAKE_GENERATOR')==='Visual Studio 17 2022'&&values.get('CMAKE_SYSTEM_VERSION')==='10.0.19041.0','Actual CMake generator/SDK differs');
  const platform=values.get('CMAKE_GENERATOR_PLATFORM');need([v.architecture==='x86'?'Win32':'x64',(v.architecture==='x86'?'Win32':'x64')+',version=10.0.19041.0'].includes(platform),'Actual CMake architecture differs');
  const equal=(a,b)=>typeof a==='string'&&typeof b==='string'&&
    fs.realpathSync.native(a).toLowerCase()===fs.realpathSync.native(b).toLowerCase();
  need(equal(values.get('CMAKE_GENERATOR_INSTANCE'),toolPaths.installation),'Actual VS instance differs from frozen tool probe');
  need(equal(values.get('CMAKE_COMMAND'),toolPaths.tools.cmake),'Actual CMake executable differs');
  const version=['MAJOR','MINOR','PATCH'].map(key=>values.get('CMAKE_CACHE_'+key+'_VERSION'));
  need(version.every(value=>/^\d+$/.test(value||'')),'Missing actual CMake cache version');
  const compilerFile=identity.safePath(buildDir,'CMakeFiles/'+version.join('.')+'/CMakeCXXCompiler.cmake');
  const compilerText=fs.readFileSync(compilerFile,'utf8'),compiler=/^set\(CMAKE_CXX_COMPILER "([^"]+)"\)/m.exec(compilerText);
  need(compiler&&equal(compiler[1],toolPaths.tools.compiler),'Actual configured compiler differs');
  // The VS generator does not persist CMAKE_MAKE_PROGRAM. Require an actual
  // native observation instead of inventing that cache field or trusting a
  // version line from the redirected MSBuild log.
  const rows=rawReport.toString('utf8').trimEnd().split('\n').map(parseStrictJson);
  for(const role of ['cmake','msbuild']) {
    const seen=rows.filter(row=>row.event==='owned-member'&&typeof row.image==='string'&&
      path.win32.basename(row.image).toLowerCase()===path.win32.basename(toolPaths.tools[role]).toLowerCase());
    need(seen.length>0&&seen.every(row=>equal(row.image,toolPaths.tools[role])),'Actual native '+role+' image differs or is unreadable');
  }
  const names=identity.walk(buildDir,'',(_relative,leaf)=>['bin','_deps'].includes(leaf)).filter(name=>name.endsWith('.vcxproj'));
  need(names.length>0,'No actual vcxproj to verify');
  for(const name of names) {
    const text=fs.readFileSync(identity.safePath(buildDir,name),'utf8');
    const sdks=[...text.matchAll(/<WindowsTargetPlatformVersion>([^<]+)<\/WindowsTargetPlatformVersion>/g)].map(m=>m[1]);
    const toolsets=[...text.matchAll(/<PlatformToolset>([^<]+)<\/PlatformToolset>/g)].map(m=>m[1]);
    need(sdks.length>0&&sdks.every(sdk=>sdk==='10.0.19041.0')&&toolsets.length>0&&toolsets.every(tool=>tool==='v143'),'Actual vcxproj SDK/toolset differs: '+name);
  }
  return {compilerVerified:true,files:[file(path.join(buildDir,'CMakeCache.txt')),file(compilerFile),
    file(path.join(buildDir,`native-invocation-${v.configuration}.json`)),...names.map(name=>file(identity.safePath(buildDir,name)))]};
}
function archiveOutput(folder,root,v,compiler) {
  const directory=identity.safePath(root,`out/${v.sdkOnly?'sdk-':''}${v.architecture}`);
  const logs=fs.readdirSync(directory).filter(name=>name.endsWith('.log')&&!name.startsWith('native-'));
  logs.push(`native-${v.configuration}.log`);
  const inputs=[...compiler.files,...logs.map(name=>file(identity.safePath(directory,name)))];
  const records=inputs.map((input,index)=>{const bytes=fs.readFileSync(input.path);need(sha(bytes)===input.sha256,'Output changed while archiving');
    const archive=`output-${index}.bin`;write(folder,archive,bytes);return {originalPath:input.path,archive,bytes:bytes.length,sha256:sha(bytes)};});
  write(folder,'output-map.json',records);
}
function evidenceIndex(folder) {
  const files=identity.walk(folder).filter(name=>name!=='index.json').map(name=>({...file(identity.safePath(folder,name)),path:name}));
  write(folder,'index.json',{schema:'soui.standard-build-evidence-index/v1',files,releaseReady:false});
}
async function runBuild(value) {
  const v=options(value);need(process.platform==='win32','Standard owned build requires Windows');
  const root=identity.safePath(path.resolve(v.root||path.join(__dirname,'../..')));
  need(root.toLowerCase()===path.resolve(__dirname,'../..').toLowerCase(),'The caller must belong to the requested workspace');
  need(!v.withVendorTests||!fs.existsSync(identity.safePath(root,'project.json')),'Vendor tests require the complete source devkit, not a generated application');
  const base=identity.safePath(root,'out/standard-build');fs.mkdirSync(base,{recursive:true});
  const folder=path.join(base,'attempt-'+crypto.randomUUID());fs.mkdirSync(folder);let lease,answer,launched;
  try {
    lease=acquireLease(root,v.architecture);
    if(v.nativeToolchain==='LLVM22MSVC142') {
      answer=await require('./llvm-build-observer.cjs').run(root,v,folder,{
        buildEvidence,llvmPaths,buildArguments,scripts,inspectTool,currentTool,verifyStream,artifacts,evaluateBuild,
        verifyToolCompatible(row){need(isDeepStrictEqual(currentTool(row),row),'Selected tool bytes or identity changed');},
        async capture(...args){const value=await capture(...args);if(args[1]==='build')launched=value;return value;}
      });
    } else {
    const powershell=path.resolve(v.powershell||path.join(process.env.SystemRoot||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe'));
    need(['powershell.exe','pwsh.exe'].includes(path.basename(powershell).toLowerCase()),'PowerShell executable required');
    const setup=require('./measurement-validation.cjs').prepared(v.architecture);
    const runner=path.resolve(v.runner||setup.tools.OwnedProcessRunner);
    need(runner.toLowerCase()===setup.tools.OwnedProcessRunner.toLowerCase(),'Build runner must be the current selected-architecture prepared tool');
    write(folder,'selected-preparation.json',setup.report);
    const version=buildEvidence.version(root),source=identity.source(root),rawSource=identity.snapshot(root,source.files.map(item=>item.path)),
      scriptRows=scripts(root),ps=inspectTool(powershell),runnerFile=file(runner);
    const args=buildArguments(root,v),buildPath=identity.safePath(root,`out/${v.sdkOnly?'sdk-':''}${v.architecture}/build-${v.configuration}.json`);
    let oldBuild={exists:false,buildId:null};
    if(fs.existsSync(buildPath)) {
      const bytes=fs.readFileSync(buildPath);let buildId=null,parseError=null;
      try{buildId=json(bytes,'old build').buildId||null;}catch(error){parseError=error.message;}
      write(folder,'previous-build.json.bin',bytes);oldBuild={exists:true,buildId,parseError,bytes:bytes.length,sha256:sha(bytes)};
    }
    write(folder,'previous-build-index.json',oldBuild);
    const probe=write(folder,'tool-probe.ps1',Buffer.from(probeSource));
    const probeOutput=path.join(folder,'tool-paths.json'),env=childEnvironment();
    const environment={PATH:env.PATH??env.Path??null,PATHEXT:env.PATHEXT??null,SystemRoot:env.SystemRoot??null,
      MSBUILDDISABLENODEREUSE:env.MSBUILDDISABLENODEREUSE};
    write(folder,'pre-probe.json',{schema:'soui.standard-build-pre-probe/v1',version,source,rawSource,scripts:scriptRows,powershell:ps,runner:runnerFile,args,cwd:root,environment});
    const probeResult=await capture(folder,'tool-probe',powershell,['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',probe,'-Root',root,'-Architecture',v.architecture,'-Output',probeOutput],
      {runner,cwd:root,timeoutMs:30000,drainMs:2000,env});
    need(probeResult.result.passed===true,'Tool probe must naturally complete: '+(probeResult.readyError||probeResult.result.terminal?.reason));
    const toolPaths=json(fs.readFileSync(probeOutput),'tool probe');need(toolPaths.schema==='soui.standard-build-tools/v1'&&toolPaths.tools,'Missing actual tool probe');
    const roles=['cmake','node','npm','msbuild','compiler','linker','devcmd','vcVersionFile','vswhere'];
    need(Object.keys(toolPaths.tools).sort().join(',')===[...roles].sort().join(','),'Tool probe role set differs');
    const tools=[{role:'powershell',...ps},{role:'runner',...runnerFile},{role:'callerNode',...inspectTool(process.execPath)},
      ...roles.map(role=>({role,...inspectTool(toolPaths.tools[role])}))];
    let pin;
    if(v.compilerTail) {
      const pinFile=file(v.tailImage);need(pinFile.sha256===v.tailSha256&&path.win32.normalize(v.tailImage).toLowerCase()===
        path.win32.join(path.win32.dirname(toolPaths.tools.compiler),'vctip.exe').toLowerCase()&&toolPaths.vcVersion==='14.44.35207','Tail pin differs from actual compiler toolset');
      tools.push({role:'tailImage',...pinFile});pin=validateTailImagePin({image:v.tailImage,sha256:v.tailSha256,nonce:crypto.randomBytes(16).toString('hex')});
    }
    const report=path.join(folder,'build.ndjson');
    const context={schema:'soui.owned-process-context/v1',runnerSha256:runnerFile.sha256,executableSha256:ps.sha256,executable:powershell,args,cwd:root,timeoutMs:v.timeoutMs,drainMs:v.drainMs};
    if(pin){context.tailImagePin=pin;context.report=report;context.runnerArgs=['--timeout-ms',String(v.timeoutMs),'--drain-ms',String(v.drainMs),'--stop-on-stdin','--report',report,'--cwd',root,
      '--pin-tail-image',pin.image,'--pin-tail-sha256',pin.sha256,'--snapshot-nonce',pin.nonce,'--',powershell,...args];}
    const frozen={schema:'soui.standard-build-inputs/v1',version,architecture:v.architecture,configuration:v.configuration,sdkOnly:!!v.sdkOnly,
      compilerTail:!!v.compilerTail,source,rawSource,scripts:scriptRows,tools,scriptSha256:scriptRows.find(row=>row.path==='tools/Build.ps1').sha256,
      context,contextSha256:sha(encode(context)),oldBuild,environment};
    need(identity.source(root).sha256===source.sha256&&identity.snapshot(root,source.files.map(item=>item.path)).sha256===rawSource.sha256&&isDeepStrictEqual(scripts(root),scriptRows)&&
      tools.every(row=>isDeepStrictEqual(currentTool(row),row)),'Source/tools changed before Build launch');
    write(folder,'inputs.json',frozen);
    launched=await capture(folder,'build',powershell,args,{runner,cwd:root,timeoutMs:v.timeoutMs,drainMs:v.drainMs,env,...(pin?{tailImagePin:pin}:{})});
    const buildBytes=fs.readFileSync(buildPath);write(folder,'current-build.json.bin',buildBytes);
    const observedBuild=buildEvidence.verifyBuild(root,v.architecture,v.configuration,!!v.sdkOnly);
    need(isDeepStrictEqual(json(buildBytes,'build report'),observedBuild),'Build report changed while verifying payload');
    const compiler=compilerObservation(root,v,toolPaths,launched.rawReport);write(folder,'compiler-observation.json',compiler);archiveOutput(folder,root,v,compiler);
    const currentSource=identity.source(root);
    const observed={sourceSha256:currentSource.sha256,rawSourceSha256:identity.snapshot(root,currentSource.files.map(item=>item.path)).sha256,
      scriptSha256:file(path.join(root,'tools/Build.ps1')).sha256,
      scripts:scripts(root),tools:tools.map(currentTool),artifacts:artifacts(observedBuild),compilerVerified:compiler.compilerVerified};
    write(folder,'observed.json',observed);
    require('./measurement-validation.cjs').prepared(v.architecture);
    answer=evaluateBuild({...launched,frozen,buildReportBytes:buildBytes,observed});
    }
  } catch(error) {answer={schema:'soui.standard-build-result/v1',compilationCompleted:false,genericSupervisionPassed:false,
    clientGenericSupervisionPassed:launched?.result.passed===true,closureConfirmed:launched?.result.closureConfirmed===true,
    rootExitCode:launched?.result.exitCode??null,supervisorExitCode:launched?.result.supervisorExitCode??null,
    compilerTailAcceptance:false,accepted:false,releaseReady:false,
    ...(launched?.closeoutDiagnostics?{closeoutDiagnostics:launched.closeoutDiagnostics}:{}),failure:error.message};}
  finally {if(lease){try{lease.release();}catch(error){answer={...answer,accepted:false,failure:(answer?.failure?answer.failure+'; ':'')+error.message};}}}
  write(folder,'result.json',answer);evidenceIndex(folder);return {...answer,evidenceDirectory:folder};
}
module.exports={parseArgs,buildArguments,llvmPaths,evaluateBuild,compilerCloseoutDiagnostics,acquireLease,inspectTool,verifyTool,verifyStream,runBuild,capture};
if(require.main===module)runBuild(parseArgs(process.argv.slice(2))).then(answer=>{
  console.log(JSON.stringify(answer));process.exitCode=answer.accepted?0:answer.supervisorExitCode===124?124:1;
}).catch(error=>{console.error(error.message);process.exitCode=1;});
