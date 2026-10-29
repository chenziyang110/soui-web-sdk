'use strict';
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const {spawnSync}=require('node:child_process');
const sdkRoot='C:/sc07';
const frozenRoot='C:/sr06';
const work='C:/s06-closeout-native-940cee1533ee4474a3f5a985cb9a9f73';
const evidence=__dirname;
const cmake='C:/Program Files/CMake/bin/cmake.exe';
const powershell='C:/WINDOWS/System32/WindowsPowerShell/v1.0/powershell.exe';
const baseline='8b7b57341d78d101ebc317de6cef8ac19b4a7531';
const {launchOwned}=require(frozenRoot+'/tools/internal/owned-process.cjs');
const measurement=require(frozenRoot+'/tools/internal/measurement-validation.cjs');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const pin=file=>{const bytes=fs.readFileSync(file);return {path:file,bytes:bytes.length,sha256:hash(bytes)};};
const write=(file,value)=>fs.writeFileSync(path.join(evidence,file),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
function collect(directory) {
  return fs.readdirSync(directory,{withFileTypes:true}).sort((a,b)=>a.name<b.name?-1:a.name>b.name?1:0)
    .flatMap(entry=>entry.isDirectory()?collect(path.join(directory,entry.name)):[pin(path.join(directory,entry.name))]);
}
function installedPins(arch) {
  const prefix=`${frozenRoot}/out/release-install-${arch}-release`;
  return [pin(prefix+'/lib/Release/soui_sdk.lib'),...collect(prefix+'/include'),...collect(prefix+'/lib/cmake/SouiSdk')];
}
function productionDiff() {
  const result=spawnSync('git',['diff','--name-only',baseline,'--','sdk/src','sdk/include','sdk/renderers',
    'third_party/sources','third_party/prebuilt'],{cwd:sdkRoot,encoding:'utf8',windowsHide:true});
  if(result.status!==0)throw new Error('Cannot check production source diff: '+result.stderr);
  if(result.stdout.trim())throw new Error('Production sources changed: '+result.stdout);
  return {baseline,paths:['sdk/src','sdk/include','sdk/renderers','third_party/sources','third_party/prebuilt'],diff:result.stdout};
}
async function owned(folder,name,exe,args,runner,budgets,env) {
  const stdout=fs.openSync(path.join(folder,name+'-stdout.bin'),'wx');
  const stderr=fs.openSync(path.join(folder,name+'-stderr.bin'),'wx');
  let run,result,ready=null,readyError=null;
  try {
    run=launchOwned(exe,args,{runner,cwd:work,report:path.join(folder,name+'.ndjson'),...budgets,env,stdout,stderr});
    try {ready=await run.ready;}catch(error){readyError=error.message;}
    result=await run.finished;
    fs.writeFileSync(path.join(folder,name+'-ready.json'),JSON.stringify({ready,readyError},null,2)+'\n',{flag:'wx'});
    fs.writeFileSync(path.join(folder,name+'-finished.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
    return result;
  } finally {
    if(run&&!result){run.stop();await run.finished;}
    fs.closeSync(stdout);fs.closeSync(stderr);
  }
}
async function main() {
  const beforeSource=productionDiff();
  const testPin=pin(sdkRoot+'/tests/native/devtools_host_tests.cpp');
  const sourcePins=[pin(work+'/CMakeLists.txt'),pin(work+'/build-one.ps1'),pin(__filename),
    pin(frozenRoot+'/tools/internal/owned-process.cjs'),pin(frozenRoot+'/tools/internal/measurement-validation.cjs')];
  const toolPins=[pin(cmake),pin(powershell),pin(process.execPath)];
  const installations=Object.fromEntries(['x64','x86'].map(arch=>[arch,installedPins(arch)]));
  const prepared=Object.fromEntries(['x64','x86'].map(arch=>[arch,measurement.prepared(arch)]));
  const input={schema:'soui.devtools-native-short-inputs/v1',createdAtUtc:new Date().toISOString(),sourceRoot:sdkRoot,
    externalProject:work,frozenRoot,productionSource:beforeSource,test:testPin,sourcePins,toolPins,installations,
    runners:Object.fromEntries(Object.entries(prepared).map(([arch,p])=>[arch,pin(p.tools.OwnedProcessRunner)])),
    budgets:{compile:{timeoutMs:900000,drainMs:30000},console:{timeoutMs:120000,drainMs:2500}},
    environment:{MSBUILDDISABLENODEREUSE:'1'},runtimeScope:'controlled adapter only; no Engine, browser GUI or VM'};
  write('inputs.json',input);
  const results=[];
  const env={...process.env,MSBUILDDISABLENODEREUSE:'1'};
  for(const arch of ['x64','x86']) {
    const folder=path.join(evidence,arch);fs.mkdirSync(folder);
    const prefix=`${frozenRoot}/out/release-install-${arch}-release`;
    const args=['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',work+'/build-one.ps1',
      '-Architecture',arch,'-InstallPrefix',prefix,'-EvidenceDirectory',folder,'-CMakeExe',cmake];
    const build=await owned(folder,'compile',powershell,args,prepared[arch].tools.OwnedProcessRunner,input.budgets.compile,env);
    const row={architecture:arch,configuration:'Release',compile:build,compileInvocation:null,console:null,output:null};
    const invocation=path.join(folder,'compile-invocation.json');
    if(fs.existsSync(invocation))row.compileInvocation=JSON.parse(fs.readFileSync(invocation,'utf8'));
    const exe=path.join(work,'build-'+arch,'Release','soui_devtools_host_tests.exe');
    if(build.exitCode===0&&row.compileInvocation?.configureExitCode===0&&row.compileInvocation?.buildExitCode===0&&fs.existsSync(exe)) {
      row.executable=pin(exe);
      const pe=fs.readFileSync(exe),offset=pe.readUInt32LE(60);
      row.coffMachine=pe.readUInt16LE(offset+4);
      if(row.coffMachine!==(arch==='x64'?0x8664:0x14c))throw new Error('Actual native contract PE architecture differs');
      row.console=await owned(folder,'console',exe,[],prepared[arch].tools.OwnedProcessRunner,input.budgets.console,env);
      row.output=fs.readFileSync(path.join(folder,'console-stdout.bin'),'utf8');
      row.stderr=fs.readFileSync(path.join(folder,'console-stderr.bin'),'utf8');
      row.tenControlledScenariosPassed=row.console.passed===true&&row.output===
        'Public DevTools facade contract: 10 scenarios passed; backend behavior untested.\r\n';
      if(!row.tenControlledScenariosPassed&&row.console.passed===true&&row.output===
        'Public DevTools facade contract: 10 scenarios passed; backend behavior untested.\n')row.tenControlledScenariosPassed=true;
      fs.copyFileSync(exe,path.join(folder,'soui_devtools_host_tests.exe'),fs.constants.COPYFILE_EXCL);
      row.archivedExecutable=pin(path.join(folder,'soui_devtools_host_tests.exe'));
      for(const filename of ['CMakeCache.txt','soui_devtools_host_tests.vcxproj']) {
        const from=path.join(work,'build-'+arch,filename),to=path.join(folder,filename);
        if(fs.existsSync(from))fs.copyFileSync(from,to,fs.constants.COPYFILE_EXCL);
      }
    }
    results.push(row);write(arch+'-result.json',row);
    console.log(JSON.stringify({architecture:arch,compileRoot:build.exitCode,compileSupervisor:build.supervisorExitCode,
      compileNaturalJob:build.passed,consoleRoot:row.console?.exitCode,consoleSupervisor:row.console?.supervisorExitCode,
      consoleNaturalJob:row.console?.passed,tenControlledScenariosPassed:row.tenControlledScenariosPassed??false}));
  }
  const afterSource=productionDiff();
  const afterInstallations=Object.fromEntries(['x64','x86'].map(arch=>[arch,installedPins(arch)]));
  const unchanged=hash(Buffer.from(JSON.stringify(installations)))===hash(Buffer.from(JSON.stringify(afterInstallations)));
  const sourceUnchanged=pin(testPin.path).sha256===testPin.sha256&&sourcePins.every(item=>pin(item.path).sha256===item.sha256);
  const toolsUnchanged=toolPins.every(item=>pin(item.path).sha256===item.sha256)&&
    Object.values(input.runners).every(item=>pin(item.path).sha256===item.sha256);
  write('originals-after.json',{productionSource:afterSource,installations:afterInstallations,
    installationPinsUnchanged:unchanged,testAndHarnessPinsUnchanged:sourceUnchanged,toolAndRunnerPinsUnchanged:toolsUnchanged});
  const summary={schema:'soui.devtools-native-short-result/v1',finishedAtUtc:new Date().toISOString(),
    test:testPin,externalProject:work,results,installationPinsUnchanged:unchanged,testAndHarnessPinsUnchanged:sourceUnchanged,
    toolAndRunnerPinsUnchanged:toolsUnchanged,bothArchitecturesTenControlledScenariosPassed:
      results.length===2&&results.every(row=>row.tenControlledScenariosPassed===true)&&unchanged&&sourceUnchanged&&toolsUnchanged,
    allCompileJobsNaturallyClosed:results.every(row=>row.compile.passed===true),
    releaseReady:false,scope:'Two Release native facade console tests only. No browser, real inspector, Engine, GUI, VM or native renderer drain proof.'};
  write('summary.json',summary);
  console.log(JSON.stringify({summary:path.join(evidence,'summary.json'),
    bothArchitecturesTenControlledScenariosPassed:summary.bothArchitecturesTenControlledScenariosPassed,
    allCompileJobsNaturallyClosed:summary.allCompileJobsNaturallyClosed}));
  process.exitCode=summary.bothArchitecturesTenControlledScenariosPassed?0:1;
}
main().catch(error=>{write('controller-failure.json',{message:error.message,stack:error.stack});console.error(error.stack);process.exitCode=1;});
