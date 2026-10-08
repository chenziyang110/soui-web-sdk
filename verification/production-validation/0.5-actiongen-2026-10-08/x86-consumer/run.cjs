'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {launchOwned}=require('F:/work/sm05/tools/internal/owned-process.cjs');
const root=__dirname,workspace='F:/work/sm05-actions',cmake='C:/Program Files/CMake/bin/cmake.exe';
const runner='F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe';
const powershell='C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe';
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const write=(file,value)=>fs.writeFileSync(file,Buffer.isBuffer(value)||typeof value==='string'?value:JSON.stringify(value,null,2)+'\n',{flag:'wx'});
function walk(directory){return fs.readdirSync(directory,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(directory,e.name)):[path.join(directory,e.name)]);}
const sources=[...walk(path.join(workspace,'sdk/src')),...walk(path.join(workspace,'sdk/include')),...walk(path.join(workspace,'tests/action-consumer')),...walk(path.join(workspace,'tools/sdk')),...walk(path.join(workspace,'sdk/web/src')),...['sdk/CMakeLists.txt','sdk/cmake/SouiSdkConfig.cmake.in','third_party/include/json/json.hpp','VERSION'].map(n=>path.join(workspace,n))].sort();
const snapshot=()=>sources.map(file=>({file,sha256:digest(fs.readFileSync(file)),bytes:fs.statSync(file).size}));
const before=snapshot();write(path.join(root,'source-before.json'),before);
fs.cpSync(path.join(workspace,'tests/action-consumer'),path.join(root,'consumer-source'),{recursive:true,errorOnExist:true,force:false});
const quotePS=value=>"'"+value.replaceAll("'","''")+"'";
const header=["$ErrorActionPreference='Stop'","Set-StrictMode -Version Latest",". 'F:/work/sm05/tools/internal/CMake-Configure.ps1'","$cmake="+quotePS(cmake),"$root="+quotePS(root),"$commands=[Collections.Generic.List[object]]::new()","function Run-CMake([string[]]$Arguments) { $commands.Add(@{executable=$cmake;arguments=$Arguments}); & $cmake @Arguments; if($LASTEXITCODE -ne 0){throw ('CMake command failed: '+$LASTEXITCODE)} }","try {"].join('\r\n')+'\r\n';
function driver(name,body){
const end=["[IO.File]::WriteAllText((Join-Path $root '"+name+"-result.json'),(@{passed=$true;commands=@($commands.ToArray())}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))","exit 0","} catch {","[IO.File]::WriteAllText((Join-Path $root '"+name+"-result.json'),(@{passed=$false;error=$_.Exception.Message;commands=@($commands.ToArray())}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))","Write-Error $_","exit 1","}"].join('\r\n')+'\r\n';
const file=path.join(root,name+'.ps1');write(file,header+body.join('\r\n')+'\r\n'+end);return file;
}
const sdkDriver=driver('sdk',[
"$argsConfig=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot 'F:/work/sm05-actions' -SourceDirectory 'F:/work/sm05-actions/sdk' -BuildDirectory (Join-Path $root 'sdk-build') -Architecture x86)",
"$argsConfig+=@('-T','v143','-DSOUI_BUILD_WEBVIEW2=OFF',('-DCMAKE_INSTALL_PREFIX='+ (Join-Path $root 'install-original').Replace('\\','/')))",
"Run-CMake $argsConfig",
"Run-CMake @('--build',(Join-Path $root 'sdk-build'),'--config','Release','--target','soui_sdk','--','/nr:false','/m:1')",
"Run-CMake @('--install',(Join-Path $root 'sdk-build'),'--config','Release')",
"$original=[IO.Path]::GetFullPath((Join-Path $root 'install-original'))","$relocated=[IO.Path]::GetFullPath((Join-Path $root 'install-relocated'))","$owned=[IO.Path]::GetFullPath($root).TrimEnd('\\','/')+'\\'",
"foreach($target in @($original,$relocated)){if(-not $target.StartsWith($owned,[StringComparison]::OrdinalIgnoreCase)){throw 'Relocation path escaped owned fixture'}}",
"if(Test-Path -LiteralPath $relocated){throw 'Relocation destination already exists'}","Assert-SouiCMakePath $original","Move-Item -LiteralPath $original -Destination $relocated","if(Test-Path -LiteralPath $original){throw 'Original install path remains'}"]);
const consumerDriver=driver('consumer',[
"$argsConfig=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot 'F:/work/sm05-actions' -SourceDirectory (Join-Path $root 'consumer-source') -BuildDirectory (Join-Path $root 'consumer-build') -Architecture x86)",
"$argsConfig+=@('-T','v143',('-DCMAKE_PREFIX_PATH='+ (Join-Path $root 'install-relocated').Replace('\\','/')))",
"Run-CMake $argsConfig","Run-CMake @('--build',(Join-Path $root 'consumer-build'),'--config','Release','--target','soui_action_consumer','--','/nr:false','/m:1')"]);
const env={...process.env,MSBUILDDISABLENODEREUSE:'1',PSModulePath:'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\Modules;C:\\Program Files\\WindowsPowerShell\\Modules'};
async function phase(name,executable,args,cwd,timeoutMs){
const folder=path.join(root,name+'-owned');fs.mkdirSync(folder);
const owned=launchOwned(executable,args,{runner,cwd,report:path.join(folder,'owned.ndjson'),timeoutMs,drainMs:2000,env,stdout:'pipe',stderr:'pipe'});
const stdout=[],stderr=[];owned.child.stdout.on('data',b=>stdout.push(b));owned.child.stderr.on('data',b=>stderr.push(b));let ready;try{ready=await owned.ready;}catch(e){ready={error:e.message};}
const finished=await owned.finished;write(path.join(folder,'stdout.bin'),Buffer.concat(stdout));write(path.join(folder,'stderr.bin'),Buffer.concat(stderr));
const bytes=fs.readFileSync(finished.report);if(digest(bytes)!==finished.reportSha256)throw Error('Report bytes changed');
const rows=bytes.toString('utf8').trim().split('\n').map(JSON.parse);
const result={...finished,ready,environment:{MSBUILDDISABLENODEREUSE:env.MSBUILDDISABLENODEREUSE,PSModulePath:env.PSModulePath},ownedMembers:rows.filter(r=>r.event==='owned-member'),stdoutSha256:digest(Buffer.concat(stdout)),stderrSha256:digest(Buffer.concat(stderr))};write(path.join(folder,'summary.json'),result);
console.log(JSON.stringify({name,exitCode:result.exitCode,passed:result.passed,closureConfirmed:result.closureConfirmed,terminal:result.terminal}));return result;
}
(async()=>{
const sdk=await phase('sdk',powershell,['-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',sdkDriver],workspace,300000);
const sdkResult=JSON.parse(fs.readFileSync(path.join(root,'sdk-result.json'),'utf8'));if(!sdkResult.passed)throw Error('SDK compilation/install failed');
const consumer=await phase('consumer',powershell,['-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',consumerDriver],workspace,180000);
const consumerResult=JSON.parse(fs.readFileSync(path.join(root,'consumer-result.json'),'utf8'));if(!consumerResult.passed)throw Error('Consumer compilation failed');
const ctest=await phase('ctest','C:/Program Files/CMake/bin/ctest.exe',['--test-dir',path.join(root,'consumer-build'),'-C','Release','--output-on-failure'],workspace,30000);
const projects=[path.join(root,'sdk-build/soui_sdk.vcxproj'),path.join(root,'consumer-build/soui_action_consumer.vcxproj')].map(file=>{const text=fs.readFileSync(file,'utf8');return{file,sha256:digest(fs.readFileSync(file)),sdk:[...new Set([...text.matchAll(/<WindowsTargetPlatformVersion>([^<]+)</g)].map(m=>m[1]))],toolset:[...new Set([...text.matchAll(/<PlatformToolset>([^<]+)</g)].map(m=>m[1]))],win32:text.includes('|Win32')};});
if(projects.some(p=>p.sdk.length!==1||p.sdk[0]!=='10.0.19041.0'||p.toolset.length!==1||p.toolset[0]!=='v143'||!p.win32))throw Error('Actual generated project toolchain mismatch');
const after=snapshot();write(path.join(root,'source-after.json'),after);const sourceUnchanged=JSON.stringify(before)===JSON.stringify(after);
const files=[...walk(path.join(root,'install-relocated/include')),...walk(path.join(root,'install-relocated/lib')),...['consumer-source/main.cpp','consumer-source/app/generated/actions.hpp','consumer-build/Release/soui_action_consumer.exe'].map(f=>path.join(root,f))].map(file=>({file,sha256:digest(fs.readFileSync(file)),bytes:fs.statSync(file).size}));
const report={schema:'soui.actiongen-x86-independent/v1',architecture:'x86',configuration:'Release',gui:false,workspace,sourceUnchanged,installRelocated:!fs.existsSync(path.join(root,'install-original'))&&fs.existsSync(path.join(root,'install-relocated')),sdkCompiledInstalled:sdkResult.passed,consumerCompiled:consumerResult.passed,ctestPassed:ctest.passed,projects,files,supervision:{sdk,consumer,ctest},limits:['Current Windows only; no Win7/PE matrix','Each compilation root exit and Job cleanup evaluated separately','No GUI, no full SDK renderer build']};
write(path.join(root,'validated-report.json'),report);console.log(JSON.stringify({report:path.join(root,'validated-report.json'),sourceUnchanged,ctestPassed:ctest.passed,compileRootsPassed:sdkResult.passed&&consumerResult.passed,sdkJobPassed:sdk.passed,consumerJobPassed:consumer.passed}));process.exitCode=sourceUnchanged&&ctest.passed?0:1;
})().catch(e=>{write(path.join(root,'failure.json'),{error:e.stack});console.error(e);process.exitCode=1;});

