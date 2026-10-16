'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const sdk='C:/work/sm05-release-050';
const standard=require('D:/work/sm05-private-storage-readers-integrated-v3/tools/internal/standard-build.cjs');
const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const runner='F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe';
const [phase,arch='x64',configuration='Release',attempt='1']=process.argv.slice(2);
assert(['sdk','prepare','resource','long-reload','long-recreate','long-multiwindow','long-bridge','long-mixed','long-session'].includes(phase));
assert(['x64','x86'].includes(arch)&&['Release','Debug'].includes(configuration));
const out=path.join(__dirname,'acceptance-'+phase+'-'+arch+'-'+configuration+'-'+attempt);fs.mkdirSync(out);
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const paths={LlvmRoot:'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64',
  MsvcRoot:'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.29.30133',
  WindowsSdkRoot:'C:/Program Files (x86)/Windows Kits/10',
  NinjaExe:'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe',
  WindowsCmdExe:'C:/Windows/System32/cmd.exe',PowerShellExe:ps};
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json','utf8')).environment;
for(const k of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===k.toUpperCase()))delete env[k];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';
env.Path=path.dirname(process.execPath)+';C:\\Program Files\\CMake\\bin;C:\\Program Files\\Git\\cmd;C:\\Windows\\System32;C:\\Windows\\System32\\WindowsPowerShell\\v1.0;C:\\Windows';
let executable=ps,timeoutMs=1800000,args;
if(phase==='sdk'){
  const observer=sdk+'/out/release-observer-x64-9/prepared.json';
  const flags={LlvmRoot:'--llvm-root',MsvcRoot:'--msvc-root',WindowsSdkRoot:'--windows-sdk-root',NinjaExe:'--ninja-exe',WindowsCmdExe:'--windows-cmd-exe',PowerShellExe:'--powershell-exe'};
  executable=process.execPath;
  args=[sdk+'/tools/internal/standard-build.cjs','--root',sdk,'--architecture',arch,'--configuration',configuration,
    '--native-toolchain','LLVM22MSVC142','--powershell',ps,...Object.entries(paths).flatMap(([k,v])=>[flags[k],v]),
    '--devkit-root',sdk,'--timeout-ms','900000','--drain-ms','2000','--fresh-configure','--sdk-only','--skip-web','--with-gui-bindings','--with-webview2','--install',
    '--host64-build-observer','--host64-observer-prepared',observer,'--host64-observer-sha256',hash(observer)];
}else{
  args=['-NoProfile','-NonInteractive','-File',sdk+'/tools/Test-Acceptance.ps1','-Architecture',arch,'-Configuration',configuration,
    '-NativeToolchain','LLVM22MSVC142',...Object.entries(paths).flatMap(([k,v])=>['-'+k,v]),'-DevkitRoot',sdk,'-SkipSdkBuild'];
  if(phase==='prepare')args.push('-PrepareOnly');
  else if(phase==='resource'){
    // Whole orchestration includes source/tool verification before and after the
    // original native protocol. The native worst-case budget already exceeds
    // the former 30-minute wrapper. No step, settle or native drain is changed.
    timeoutMs=90*60*1000;args.push('-ResourceCapture','-CacheObservation');
  }
  else {args.push('-LongRunCapture','-Scenario',phase.slice(5));timeoutMs=43200000;}
}
const pins=[__filename,sdk+'/source-manifest.json',sdk+'/tools/internal/Observe-LlvmBuild.ps1',sdk+'/tools/Test-Acceptance.ps1'].map(p=>({path:p,sha256:hash(p)}));
(async()=>{
  fs.writeFileSync(out+'/INPUTS.json',JSON.stringify({phase,arch,configuration,pins,executable,args,timeoutMs},null,2));
  console.log(JSON.stringify({phase,arch,configuration,out,started:true}));
  const c=await standard.capture(out,phase,executable,args,{runner,cwd:sdk,env,timeoutMs,drainMs:2000});
  let v;try{v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);}
  catch(error){fs.writeFileSync(out+'/RESULT.json',JSON.stringify({phase,stable:pins.every(p=>hash(p.path)===p.sha256),natural:false,result:c.result,terminal:c.result.terminal,pins,verificationFailure:error.message},null,2));throw error;}
  const stable=pins.every(p=>hash(p.path)===p.sha256);
  const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
  fs.writeFileSync(out+'/RESULT.json',JSON.stringify({phase,stable,natural,result:c.result,terminal:v.terminal,pins},null,2));
  console.log(JSON.stringify({phase,stable,natural,exitCode:c.result.exitCode}));
  assert(stable&&natural,'Phase failed; retain records in '+out);
})().catch(e=>{console.error(e);process.exitCode=1;});
