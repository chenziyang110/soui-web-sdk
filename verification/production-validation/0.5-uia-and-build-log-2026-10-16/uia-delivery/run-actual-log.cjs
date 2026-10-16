'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',work=__dirname,standard=require(root+'/tools/internal/standard-build.cjs'),e=require(root+'/tools/internal/dependency-evidence.cjs');
const output=work+'/actual-log-run';fs.mkdirSync(output);
const pins=[__filename,work+'/actual-configure-log.ps1',work+'/native-log-probe/CMakeLists.txt',work+'/proposal/tools/internal/Capture-CMakeConfigureLog.ps1',root+'/tools/internal/Observe-LlvmBuild.ps1',root+'/source-manifest.json'].map(e.pin);
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];Object.assign(env,policy.set);
env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:/Program Files/CMake/bin;C:/Program Files/Git/cmd;C:/Windows/System32;C:/Windows';
(async()=>{
 const c=await standard.capture(output,'configure-log','C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe',['-NoProfile','-NonInteractive','-File',work+'/actual-configure-log.ps1'],{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:work,env,timeoutMs:300000,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result),t=v.terminal;
 const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&t.activeBeforeCleanup===0&&!t.terminatedOwnedJob&&t.activeProcesses===0;
 fs.writeFileSync(work+'/ACTUAL-LOG-RESULT.json',JSON.stringify({natural,pins,result:c.result,terminal:t},null,2),{flag:'wx'});
 pins.forEach(e.checkPin);assert(natural);
 console.log(JSON.stringify({actualRepeatedConfigurePassed:true,natural:true}));
})().catch(error=>{console.error(error);process.exitCode=1;});
