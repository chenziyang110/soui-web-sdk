'use strict';
const versioning = require('./sdk-version.cjs');
const crypto = require('node:crypto');
const path = require('node:path');
const {isDeepStrictEqual} = require('node:util');
const {isUtf8} = require('node:buffer');
const {readTailEvidence,validateTailImagePin,parseStrictJson} = require('./owned-tail-evidence.cjs');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encoded = value => JSON.stringify(value,null,2)+'\n';
const pins = Object.freeze({
  x86:{path:'C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x86\\vctip.exe',
    bytes:'335560',sha256:'1c1ca5f9c04171c02caca6863e29eca3550333219486d23e65bf01fb02951311'},
  x64:{path:'C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x64\\vctip.exe',
    bytes:'335520',sha256:'651345513aae26090c813b1ded859a5680c3823ca716a80e1d337bac0ca797ef'},
});
function need(condition,message) { if(!condition)throw new Error(message); }
function digest(value) { need(typeof value==='string'&&/^[a-f0-9]{64}$/.test(value),'Missing explicit SHA256'); return value; }
function absolute(value) {
  need(typeof value==='string'&&/^[A-Za-z]:[\\/]/.test(value)&&!/[\x00-\x1f]/.test(value),'Invalid reviewed absolute path');
  const normalized=value.replace(/\//g,'\\');
  need(path.win32.normalize(normalized)===normalized&&!normalized.slice(3).split('\\').some(p=>!p||p==='.'||p==='..'||/[. :<>"|?*]/.test(p.slice(-1))||p.includes(':')),'Aliased reviewed path');
  return normalized.toLowerCase();
}
function snapshot(value) {
  need(value && Array.isArray(value.files)&&value.files.length>0,'Missing artifact files'); digest(value.sha256);
  let bytes=0; const seen=new Set();
  for(const file of value.files) {
    need(typeof file.path==='string'&&file.path&&!/[\\:\x00-\x1f]/.test(file.path)&&
      !file.path.split('/').some(p=>!p||p==='.'||p==='..'||/[. ]$/.test(p))&&!seen.has(file.path.toLowerCase()),'Invalid artifact path');
    seen.add(file.path.toLowerCase()); digest(file.sha256);
    need(Number.isSafeInteger(file.bytes)&&file.bytes>=0,'Invalid artifact byte count'); bytes+=file.bytes;
  }
  need(Number.isSafeInteger(bytes)&&bytes===value.bytes&&hash(encoded(value.files))===value.sha256,'Artifact manifest hash/size differs');
}
function artifacts(report) {
  const result={payloads:report.payloads,optionalLibraries:report.optionalLibraries};
  need(result.payloads&&typeof result.payloads==='object'&&!Array.isArray(result.payloads)&&result.optionalLibraries&&
    typeof result.optionalLibraries==='object'&&!Array.isArray(result.optionalLibraries),'Missing build artifact identity');
  Object.values(result.payloads).forEach(snapshot); Object.values(result.optionalLibraries).forEach(snapshot);
  if(report.sdkOnly) { need(Object.keys(result.payloads).length===0,'SDK-only report contains application payload'); snapshot(report.library); result.library=report.library; }
  else need(Object.keys(result.payloads).length>0,'Missing application payloads');
  if(report.optionalPayloads!==undefined) {
    need(report.optionalPayloads&&typeof report.optionalPayloads==='object'&&!Array.isArray(report.optionalPayloads),'Invalid optional payloads');
    Object.values(report.optionalPayloads).forEach(snapshot); result.optionalPayloads=report.optionalPayloads;
  }
  return result;
}
function jsonBytes(bytes,expectedHash,name) {
  need(Buffer.isBuffer(bytes)&&bytes.length>0&&bytes.length<=16777216&&isUtf8(bytes),'Missing actual '+name+' bytes');
  need(hash(bytes)===digest(expectedHash),name+' raw SHA256 differs');
  return parseStrictJson(bytes.toString('utf8'));
}
function invocation(context,review) {
  need(review&&review.schema==='soui.reviewed-compiler-invocation/v1','Explicit reviewed Build.ps1 invocation required');
  need(absolute(context.executable)===absolute(review.executable)&&absolute(context.cwd)===absolute(review.cwd)&&
    isDeepStrictEqual(context.args,review.args),'Reviewed command/cwd/argv differ');
  need(['powershell.exe','pwsh.exe'].includes(path.win32.basename(context.executable).toLowerCase()),'Reviewed command is not a PowerShell Build.ps1 invocation');
  const args=context.args; const fileIndex=args.indexOf('-File');
  need(fileIndex>0&&args[fileIndex+1]&&absolute(args[fileIndex+1])===absolute(path.win32.join(context.cwd,'tools','Build.ps1')),'Exact Build.ps1 script required');
  const prefix=args.slice(0,fileIndex); const flags=new Set();
  for(let i=0;i<prefix.length;i++) {
    const flag=prefix[i]; need(!flags.has(flag),'Repeated PowerShell option'); flags.add(flag);
    if(flag==='-ExecutionPolicy')need(prefix[++i]==='Bypass','Execution policy differs');
    else need(['-NoProfile','-NoLogo','-NonInteractive'].includes(flag),'Unexpected PowerShell option');
  }
  need(flags.has('-NoProfile')&&flags.has('-ExecutionPolicy'),'Reviewed PowerShell setup required');
  const options=new Map();
  for(let i=fileIndex+2;i<args.length;i++) {
    const flag=args[i]; need(!options.has(flag),'Repeated Build.ps1 option');
    if(flag==='-Architecture'||flag==='-Configuration') {
      const value=args[++i]; need(typeof value==='string'&&!value.startsWith('-'),'Missing build option value'); options.set(flag,value);
    } else { need(['-SdkOnly','-SkipWeb','-WithWebView2','-Install','-FreshConfigure'].includes(flag),'Unexpected Build.ps1 option'); options.set(flag,true); }
  }
  need(pins[review.architecture]&&['Debug','Release'].includes(review.configuration)&&typeof review.sdkOnly==='boolean'&&
    options.get('-Architecture')===review.architecture&&options.get('-Configuration')===review.configuration&&
    options.has('-SdkOnly')===review.sdkOnly,'Reviewed build configuration differs');
  need(context.schema==='soui.owned-process-context/v1'&&context.executableSha256===digest(review.executableSha256)&&
    context.runnerSha256===digest(review.runnerSha256),'Reviewed tool hashes differ');
}
function fileTime(date) {
  need(typeof date==='string'&&/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/.test(date),'Missing precise build timestamp');
  const time=Date.parse(date); need(Number.isSafeInteger(time)&&new Date(time).toISOString()===date,'Invalid build timestamp');
  return (BigInt(time)+11644473600000n)*10000n;
}

// All "actual/current" values are mandatory observations supplied by the
// reviewed caller. This is evidence consistency, not authentication against
// that same user rewriting the entire bundle or patching the supervisor.
function acceptCompilerTail({reviewedInvocation:review,context,contextBytes,rawReport,buildReportBytes,result,
  actualScriptSha256,currentSourceSha256,currentArtifacts}={}) {
  const answer={schema:'soui.compiler-tail-policy/v1',compilationCompleted:false,genericSupervisionPassed:false,
    compilerTailAcceptance:false,failure:null,diskBytesOnly:true};
  try {
    invocation(context,review);
    const readContext=jsonBytes(contextBytes,review.contextSha256,'context');
    need(isDeepStrictEqual(readContext,context),'Actual context bytes differ from supplied context');
    need(digest(actualScriptSha256)===digest(review.scriptSha256),'Actual Build.ps1 script SHA differs');
    need(digest(currentSourceSha256)===digest(review.sourceSha256),'Current source identity differs');
    const build=jsonBytes(buildReportBytes,review.buildReportSha256,'build report');
    need(build.schema==='soui.source-build/v2'&&build.nativeBuild==='passed'&&build.releaseReady===false&&
      typeof build.buildId==='string'&&/^[a-f0-9]{8}-(?:[a-f0-9]{4}-){3}[a-f0-9]{12}$/.test(build.buildId)&&build.buildId===review.buildId&&
      build.version===review.version&&versioning.isVersion(build.version)&&
      build.architecture===review.architecture&&build.configuration===review.configuration&&build.sdkOnly===review.sdkOnly&&
      build.source?.sha256===currentSourceSha256,'Build report is stale or not the reviewed completed build');
    const actualArtifacts=artifacts(build);
    need(currentArtifacts&&review.artifacts&&isDeepStrictEqual(actualArtifacts,currentArtifacts)&&
      isDeepStrictEqual(actualArtifacts,review.artifacts),'Current/reviewed artifacts differ from completed build');
    need(Buffer.isBuffer(rawReport)&&hash(rawReport)===digest(review.reportSha256),'Actual owned report SHA differs');
    need(result&&result.reportSha256===review.reportSha256&&result.contextSha256===review.contextSha256&&
      result.runnerSha256===context.runnerSha256&&result.executableSha256===context.executableSha256&&
      result.spawnError===null&&result.reportError===null&&result.signal===null&&result.stopRequested===false&&
      result.fallbackUsed===false&&result.closureConfirmed===true,'Actual supervision/client observations are incomplete');
    need(rawReport.length<=16777216&&isUtf8(rawReport)&&rawReport.at(-1)===10,'Owned report is truncated');
    const records=rawReport.toString('utf8').slice(0,-1).split('\n').map(parseStrictJson); const start=records[0]; const terminal=records.at(-1);
    need(start?.event==='start'&&start.schema==='soui.owned-process-run/v1'&&start.jobAssignedBeforeResume===true&&start.breakawayAllowed===false&&
      start.pid===result.pid&&typeof start.creationFileTime==='string'&&/^[1-9]\d{0,19}$/.test(start.creationFileTime)&&
      BigInt(start.creationFileTime)<=18446744073709551615n&&start.creationFileTime===result.creationFileTime&&
      terminal?.event==='terminal'&&isDeepStrictEqual(result.terminal,terminal)&&result.exitCode===terminal.rootExitCode,'Published subject/terminal differs');
    need(fileTime(build.startedAt)>=BigInt(start.creationFileTime)&&fileTime(build.finishedAt)>=fileTime(build.startedAt),'Historical build report predates owned subject');
    need(terminal.rootExited===true&&terminal.rootExitCode===0&&result.exitCode===0,'Build root did not complete successfully');
    const generic=result.passed===true&&result.supervisorExitCode===0&&terminal.reason==='completed'&&terminal.passed===true&&terminal.activeProcesses===0&&
      terminal.closureConfirmed===true&&terminal.activeBeforeCleanup===0&&terminal.terminatedOwnedJob===false&&terminal.limitTerminatedProcesses===0&&
      Number.isSafeInteger(terminal.totalProcesses)&&terminal.totalProcesses>=1&&terminal.tailEvidenceComplete!==true;
    const tail=result.passed===false&&result.supervisorExitCode===124&&terminal.reason==='owned-descendants-remain'&&terminal.passed===false;
    need(generic||tail,'STOP/timeout/root-failure is not a compiler tail exception');
    answer.compilationCompleted=true; answer.genericSupervisionPassed=generic;
    if(generic) { answer.failure='Generic supervision passed; no compiler-tail exception required'; return answer; }
    const pin=validateTailImagePin(context.tailImagePin); const allowed=pins[review.architecture];
    need(absolute(pin.image)===absolute(allowed.path)&&pin.sha256===allowed.sha256,'Pin is not the reviewed MSVC 14.44.35207 Hostx64 target object');
    const evidence=readTailEvidence(rawReport,{context,reportSha256:review.reportSha256});
    need(evidence.accepted,evidence.failure||'Exact prelaunch tail evidence unavailable');
    need(evidence.pin.size===allowed.bytes&&evidence.pin.sha256===allowed.sha256&&
      absolute(evidence.member.image)===absolute(allowed.path),'Pinned MSVC bytes/path differ');
    answer.compilerTailAcceptance=true; answer.failure=null; answer.tailEvidence=evidence;
    return answer;
  }catch(error) { answer.failure=error.message; return answer; }
}
module.exports={acceptCompilerTail};
