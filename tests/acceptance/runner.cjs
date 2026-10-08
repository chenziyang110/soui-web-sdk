'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const {StringDecoder} = require('node:string_decoder');
const {spawn} = require('node:child_process');
const identity = require('../../tools/internal/delivery-identity.cjs');
const builds = require('../../tools/internal/build-evidence.cjs');
const {captureTimeout} = require('./timeout-diagnostics.cjs');
const {mixedSettings, validateMixedRecords} = require('./mixed-validation.cjs');
const root = path.resolve(__dirname, '../..');
const modes = ['reload', 'recreate', 'multiwindow', 'bridge'];

function settings(mode, cycles, warmup, settleMs, timeoutMs) {
  if (!modes.includes(mode)) throw new Error('Unknown acceptance mode');
  const values = {cycles, warmup, settleMs, timeoutMs};
  for (const [key, maximum] of Object.entries({cycles:10000,warmup:100,settleMs:60000,timeoutMs:120000})) {
    if (!Number.isSafeInteger(values[key]) || values[key] < 0 || values[key] > maximum) throw new Error(`Invalid ${key}`);
  }
  if (!cycles || !timeoutMs) throw new Error('Cycles and timeout must be positive');
  return {mode, ...values};
}
function expectedSteps(config) {
  const {mode, cycles, warmup} = settings(...['mode','cycles','warmup','settleMs','timeoutMs'].map(k=>config[k]));
  const total = cycles + warmup;
  const steps = [];
  const add = (action, window, cycle) => steps.push({action, window, cycle});
  if (mode === 'reload') {
    add('mount', 1, 0);
    add('runtime.reject-busy',0,0);
    for (let cycle=1;cycle<=total;cycle++) add('reload', 1, cycle);
    add('close', 1, total+1);
  } else for (let cycle=1;cycle<=total;cycle++) {
    add('mount', 1, cycle);
    if (cycle===1) add('runtime.reject-busy',0,cycle);
    if (mode === 'multiwindow') {
      add('mount', 2, cycle); add('mount', 3, cycle); add('close', 2, cycle);
      add('reload', 1, cycle); add('reload', 3, cycle); add('mount', 2, cycle);
      add('close', 3, cycle); add('close', 2, cycle);
    }
    add('close', 1, cycle);
  }
  add('runtime.shutdown', 0, total+1);
  add('runtime.reject-restart', 0, total+1);
  add('runtime.shutdown-idempotent', 0, total+1);
  return steps;
}
function validateRecords(records, config, outcome) {
  if (outcome.timedOut !== false || outcome.exitCode !== 0 || outcome.signal || !Number.isInteger(outcome.pid) || outcome.pid <= 0)
    throw new Error('Process failed, timed out, or has no valid identity');
  if (!Array.isArray(records) || records.length < 4 || records[0].event !== 'run.begin' || records.at(-1).event !== 'run.complete')
    throw new Error('Missing run boundaries');
  const start = records[0];
  if (start.pid !== outcome.pid || Object.keys(config).some(k=>start[k]!==config[k])) throw new Error('Scenario parameters or process identity differ');
  const steps = expectedSteps(config);
  const expectedEvents = steps.flatMap(step=>[{event:'step.begin',...step},{event:'step.complete',...step}]);
  const actualEvents=[];
  const samples=[];
  const samplePositions=[];
  let elapsed=-1;
  for (let i=0;i<records.length;i++) {
    const record=records[i];
    if (!Number.isSafeInteger(record.elapsedMs) || record.elapsedMs < elapsed) throw new Error('Invalid event timestamp');
    elapsed=record.elapsedMs;
    if (record.event==='run.begin' && i===0 || record.event==='run.complete' && i===records.length-1) continue;
    if (record.event==='sample') {
      for (const k of ['privateBytes','workingSetBytes','handles','threads','gdiObjects','userObjects'])
        if (!Number.isSafeInteger(record[k]) || record[k] < 0) throw new Error(`Missing resource measurement: ${k}`);
      samples.push(record);
      samplePositions.push([record.phase,record.cycle,actualEvents.length/2]);
      continue;
    }
    if (!['step.begin','step.complete'].includes(record.event)) throw new Error(`Failed or unknown event: ${record.event}`);
    actualEvents.push(Object.fromEntries(['event','action','window','cycle'].map(k=>[k,record[k]])));
  }
  if (JSON.stringify(actualEvents)!==JSON.stringify(expectedEvents)) throw new Error('Missing, duplicated, or reordered lifecycle steps');
  const total=config.cycles+config.warmup;
  const completedAtCycle=cycle=>config.mode==='reload'?2+cycle:(config.mode==='multiwindow'?10:2)*cycle+1;
  const expectedSamples=[['process',0,0],...(config.mode==='reload'?[['mounted',0,2]]:[]),
    ...Array.from({length:total},(_,i)=>['cycle',i+1,completedAtCycle(i+1)]),
    ['closed',total+1,steps.length-3],['runtime-closed',total+1,steps.length-2]];
  if (JSON.stringify(samplePositions)!==JSON.stringify(expectedSamples)) throw new Error('Incomplete or misplaced resource sampling');
  // Preserve every sample. This result proves operation completion, not a memory/soak gate.
  return {completedSteps:steps.length, measuredCycles:config.cycles, samples, resourceGate:'not-evaluated', releaseReady:false};
}
function directory(suite) {
  const result=identity.safePath(suite);
  const parent=path.join(root,'out','acceptance')+path.sep;
  if (!result.startsWith(parent) || path.dirname(result)!==parent.slice(0,-1)) throw new Error('Suite must be a direct child of out/acceptance');
  return result;
}
function write(file,value) { fs.writeFileSync(file,identity.encoded(value),{flag:'wx'}); }
function payload(bin) {
  return identity.snapshot(bin,identity.walk(bin,'',name=>name==='resources/source-navigation.log'));
}
function prepare(suite,architecture,configuration,runtimeBinary) {
  suite=directory(suite);
  const build=builds.verifyBuild(root,architecture,configuration,true);
  const webRecord=identity.readJson(path.join(root,'out','web-sdk.json'));
  if(webRecord.schema!=='soui.web-build/v1' || webRecord.sourceSha256!==build.source.sha256 ||
     identity.snapshot(root,identity.walk(root,'sdk/web/dist')).sha256!==webRecord.artifacts.sha256)
    throw new Error('The public Web SDK must be built from the same source');
  const prebuilt=path.join(root,'third_party','prebuilt',architecture,'miniblink','node.dll');
  const input=identity.safePath(runtimeBinary||prebuilt);
  if(!fs.statSync(input).isFile()) throw new Error('Runtime candidate must be a regular DLL file');
  const bytes=fs.readFileSync(input);
  const kernel={kind:input===prebuilt?'prebuilt':'explicit-candidate',bytes:bytes.length,sha256:identity.hash(bytes)};
  fs.mkdirSync(path.join(suite,'kernel-input'));
  fs.writeFileSync(path.join(suite,'kernel-input','node.dll'),bytes,{flag:'wx'});
  const original=path.join(root,'tests','acceptance-app');
  const consumerSource=identity.snapshot(original,identity.walk(original));
  const copy=path.join(suite,'consumer-source');
  fs.cpSync(original,copy,{recursive:true,errorOnExist:true,force:false});
  if(identity.snapshot(copy,identity.walk(copy)).sha256!==consumerSource.sha256) throw new Error('Consumer source changed during snapshot');
  const webSdkRoot=path.join(suite,'web-sdk');
  fs.cpSync(path.join(root,'sdk/web/dist'),path.join(webSdkRoot,'dist'),{recursive:true,errorOnExist:true,force:false});
  fs.copyFileSync(path.join(root,'sdk/web/package.json'),path.join(webSdkRoot,'package.json'));
  const webSdk=identity.snapshot(webSdkRoot,identity.walk(webSdkRoot));
  for(const file of webRecord.artifacts.files) {
    if(identity.hash(fs.readFileSync(path.join(webSdkRoot,file.path.slice('sdk/web/'.length))))!==file.sha256)
      throw new Error('Public Web SDK changed during snapshot');
  }
  write(path.join(suite,'pending.json'),{architecture,configuration,sourceSha256:build.source.sha256,buildId:build.buildId,version:build.version,consumerSource,webSdk,kernel});
  return {buildId:build.buildId,sourceSha256:build.source.sha256};
}
function stage(suite) {
  suite=directory(suite);
  const pending=identity.readJson(path.join(suite,'pending.json'));
  const {architecture,configuration}=pending;
  const build=builds.verifyBuild(root,architecture,configuration,true);
  if (build.buildId!==pending.buildId || build.source.sha256!==pending.sourceSha256) throw new Error('SDK/source changed during consumer build');
  const consumerSource=path.join(suite,'consumer-source');
  if(identity.snapshot(consumerSource,identity.walk(consumerSource)).sha256!==pending.consumerSource.sha256)
    throw new Error('Independent consumer source changed during build');
  const web=verifyWeb(suite,pending);
  const kernel=fs.readFileSync(path.join(suite,'kernel-input','node.dll'));
  if(identity.hash(kernel)!==pending.kernel.sha256) throw new Error('Runtime candidate changed during build');
  const install=path.join(suite,'install');
  const library=fs.readFileSync(path.join(install,'lib',configuration,'soui_sdk.lib'));
  if (identity.hash(library)!==build.library.files[0].sha256) throw new Error('Installed library is not the verified SDK');
  const sourceHeaders=identity.walk(root,'sdk/include');
  for (const name of sourceHeaders) {
    if (identity.hash(fs.readFileSync(path.join(root,name)))!==identity.hash(fs.readFileSync(path.join(install,name.slice(4)))))
      throw new Error('Installed SDK header differs: '+name);
  }
  const bin=path.join(suite,'runtime');
  fs.mkdirSync(bin);
  fs.copyFileSync(path.join(suite,'consumer','bin',configuration,'SouiAcceptance.exe'),path.join(bin,'SouiAcceptance.exe'));
  fs.writeFileSync(path.join(bin,'node.dll'),kernel,{flag:'wx'});
  fs.cpSync(path.join(suite,'consumer','resources'),path.join(bin,'resources'),{recursive:true,errorOnExist:true,force:false});
  for(let window=1;window<=3;window++) {
    const resources=path.join(bin,'resources',`window-${window}`);
    if(identity.snapshot(resources,identity.walk(resources)).sha256!==web.output.sha256)
      throw new Error('Compiled consumer did not deploy the verified Web output');
  }
  const dependency=`third_party/prebuilt/${architecture}`;
  const dlls=[...identity.walk(root,`${dependency}/runtime`).filter(f=>/\.dll$/i.test(f)),
    ...identity.walk(root,`${dependency}/soui/bin`).filter(f=>/\.dll$/i.test(f)&&!/(souid|utilitiesd|render-gdid|imgdecoder-gdipd|soui-sys-resourced)\.dll$/i.test(f))];
  for (const file of dlls) {
    const destination=path.join(bin,path.basename(file));
    if (fs.existsSync(destination)) throw new Error('Runtime basename collision: '+file);
    fs.copyFileSync(path.join(root,file),destination);
  }
  const evidence={schema:'soui.acceptance-build/v3',...pending,web,consumerBuildId:crypto.randomUUID(),
    installedSdk:identity.snapshot(install,identity.walk(install)),payload:payload(bin),
    environment:{platform:os.platform(),release:os.release(),architecture:os.arch(),node:process.version,cpu:os.cpus()[0]?.model,logicalProcessors:os.cpus().length},
    completedAt:new Date().toISOString(),releaseReady:false};
  write(path.join(suite,'build.json'),evidence);
  return {consumerBuildId:evidence.consumerBuildId,payloadSha256:evidence.payload.sha256};
}
function verifyWeb(suite,build) {
  const web=identity.readJson(path.join(suite,'web-build.json'));
  const sdkRoot=path.join(suite,'web-sdk'),output=path.join(suite,'web-output');
  const typecheck=web.typecheck;
  const typecheckConfig=path.join(suite,'web-typecheck.tsconfig.json');
  const typecheckLog=path.join(suite,'web-typecheck.log');
  const expectedTypeScript=identity.readJson(path.join(root,'tests','acceptance','package.json')).devDependencies.typescript;
  if(web.schema!=='soui.acceptance-web/v1' || web.consumerSourceSha256!==build.consumerSource.sha256 ||
     web.webSdkSha256!==build.webSdk.sha256 || identity.snapshot(sdkRoot,identity.walk(sdkRoot)).sha256!==build.webSdk.sha256 ||
     !typecheck || typecheck.passed!==true || typecheck.typescript!==expectedTypeScript ||
     identity.hash(fs.readFileSync(typecheckConfig))!==typecheck.configSha256 ||
     identity.hash(fs.readFileSync(typecheckLog))!==typecheck.logSha256 ||
     identity.snapshot(output,identity.walk(output)).sha256!==web.output.sha256)
    throw new Error('Independent public Web SDK inputs/output drift');
  return web;
}
function verify(suite) {
  const build=identity.readJson(path.join(suite,'build.json'));
  if(build.schema!=='soui.acceptance-build/v3') throw new Error('A fresh acceptance build with the public Web SDK consumer is required');
  if(JSON.stringify(verifyWeb(suite,build))!==JSON.stringify(build.web))throw new Error('Independent Web build record drift');
  const sdk=builds.verifyBuild(root,build.architecture,build.configuration,true);
  if (sdk.buildId!==build.buildId || sdk.source.sha256!==build.sourceSha256 || payload(path.join(suite,'runtime')).sha256!==build.payload.sha256)
    throw new Error('Acceptance source, SDK build, or runtime drift');
  const install=path.join(suite,'install');
  if (identity.snapshot(install,identity.walk(install)).sha256!==build.installedSdk.sha256) throw new Error('Installed SDK drift');
  const consumerSource=path.join(suite,'consumer-source');
  if(identity.snapshot(consumerSource,identity.walk(consumerSource)).sha256!==build.consumerSource.sha256)
    throw new Error('Independent consumer source drift');
  if(identity.hash(fs.readFileSync(path.join(suite,'kernel-input','node.dll')))!==build.kernel.sha256)
    throw new Error('Runtime candidate input drift');
  return build;
}
async function run(suite,config) {
  suite=directory(suite);
  const build=verify(suite);
  const runId=crypto.randomUUID();
  const output=path.join(suite,`${config.mode}-${runId}`);
  fs.mkdirSync(output);
  write(path.join(output,'request.json'),{schema:'soui.acceptance-request/v1',runId,...config,consumerBuildId:build.consumerBuildId,
    payloadSha256:build.payload.sha256,sourceSha256:build.sourceSha256,startedAt:new Date().toISOString()});
  const log=path.join(output,'events.ndjson');
  const stdout=fs.openSync(path.join(output,'stdout.log'),'wx');
  const stderr=fs.openSync(path.join(output,'stderr.log'),'wx');
  const bin=path.join(suite,'runtime');
  const mixed=config.mode==='mixed';
  const args=mixed?
    [`--mode=mixed`,`--duration-ms=${config.durationMs}`,`--idle-ms=${config.idleMs}`,
      `--settle-ms=${config.settleMs}`,`--timeout-ms=${config.timeoutMs}`,`--report=${log}`]:
    [`--mode=${config.mode}`,`--cycles=${config.cycles}`,`--warmup=${config.warmup}`,
      `--settle-ms=${config.settleMs}`,`--timeout-ms=${config.timeoutMs}`,`--report=${log}`];
  const processTimeoutMs=mixed?config.durationMs+Math.max(180000,2*config.timeoutMs+config.idleMs):
    Math.min(12*60*60*1000,Math.max(60000,expectedSteps(config).length*(config.timeoutMs+config.settleMs)+30000));
  let timedOut=false;
  let timeoutReason=null;
  let child;
  let outcome;
  let timeoutDiagnosticPromise=Promise.resolve(null);
  try {
    outcome=await new Promise(resolve=>{
      child=spawn(path.join(bin,'SouiAcceptance.exe'),args,{cwd:bin,windowsHide:true,stdio:['ignore',stdout,stderr]});
      let spawnError=null;
      let observedBytes=0;
      let partial='';
      const decoder=new StringDecoder('utf8');
      let stepStarted=null;
      let lastProgress=Date.now();
      let lastNativeRecord=null;
      const expire=reason=>{
        if(timedOut)return;
        timedOut=true;
        timeoutReason=reason;
        const observedAt=Date.now();
        timeoutDiagnosticPromise=captureTimeout({pid:child.pid,output,nativeLog:log,
          snapshot:{schema:'soui.acceptance-timeout-observation/v1',runId,reason,pid:child.pid,
            observedAt:new Date(observedAt).toISOString(),observedBytes,partialBytes:Buffer.byteLength(partial,'utf8'),
            lastProgressAgeMs:observedAt-lastProgress,lastNativeRecord,stepStarted},
          debuggerExe:process.env.SOUI_ACCEPTANCE_DEBUGGER,
          dumpRoot:process.env.SOUI_ACCEPTANCE_DUMP_ROOT})
          .catch(error=>({debugger:{status:'failed',error:error.message}}))
          .finally(()=>child.kill());
      };
      const timer=setTimeout(()=>expire('process-deadline'),processTimeoutMs);
      // Per-step supervision also covers a blocked native mount/destroy where
      // the in-process readiness timer cannot run. Read only appended bytes.
      const monitor=setInterval(()=>{
        try {
          if(fs.existsSync(log)) {
            const size=fs.statSync(log).size;
            if(size<observedBytes) return expire('native-log-truncated');
            if(size>observedBytes) {
              const file=fs.openSync(log,'r');
              const buffer=Buffer.alloc(size-observedBytes);
              let bytesRead;
              try{bytesRead=fs.readSync(file,buffer,0,buffer.length,observedBytes);}finally{fs.closeSync(file);}
              observedBytes+=bytesRead;
              const lines=(partial+decoder.write(buffer.subarray(0,bytesRead))).split('\n'); partial=lines.pop();
              for(const line of lines){
                const record=JSON.parse(line);
                lastProgress=Date.now();
                lastNativeRecord=Object.fromEntries(['event','elapsedMs','action','window','cycle']
                  .filter(key=>Object.hasOwn(record,key)).map(key=>[key,record[key]]));
                if(record.event==='step.begin')stepStarted={at:Date.now(),action:record.action,
                  window:record.window,cycle:record.cycle};
                if(record.event==='step.complete'||record.event==='step.failed')stepStarted=null;
              }
            }
          }
          if(stepStarted && Date.now()-stepStarted.at>config.timeoutMs+2000)expire('step-deadline:'+stepStarted.action);
          else if(!stepStarted && Date.now()-lastProgress>Math.max(config.timeoutMs,config.settleMs)+5000)expire('no-native-progress');
        } catch(error){expire('native-observation-failed:'+error.message);}
      },250);
      child.once('error',error=>{spawnError=error.message;});
      child.once('close',(exitCode,signal)=>{clearTimeout(timer);clearInterval(monitor);resolve({pid:child.pid??null,exitCode,signal,timedOut,timeoutReason,spawnError,processTimeoutMs});});
    });
  } finally { fs.closeSync(stdout); fs.closeSync(stderr); }
  outcome.timeoutDiagnostic=await timeoutDiagnosticPromise;
  const result={schema:'soui.acceptance-run/v1',runId,config,consumerBuildId:build.consumerBuildId,
    sourceSha256:build.sourceSha256,payloadSha256:build.payload.sha256,outcome,passed:false,releaseReady:false,
    finishedAt:new Date().toISOString()};
  try {
    verify(suite);
    const bytes=fs.readFileSync(log);
    result.logSha256=identity.hash(bytes);
    if (!bytes.toString('utf8').endsWith('\n')) throw new Error('Truncated native log');
    const records=bytes.toString('utf8').trimEnd().split('\n').map(line=>JSON.parse(line));
    result.validation=mixed?validateMixedRecords(records,config,outcome):validateRecords(records,config,outcome);
    result.passed=true;
  } catch(error) { result.failure=error.message; }
  write(path.join(output,'result.json'),result);
  return {passed:result.passed,mode:config.mode,runId,result:path.relative(root,path.join(output,'result.json')).replaceAll('\\','/'),failure:result.failure,outcome};
}
module.exports={settings,expectedSteps,validateRecords,mixedSettings,validateMixedRecords};
if(require.main===module) (async()=>{
  const [command,suite,...args]=process.argv.slice(2);
  let result;
  if(command==='prepare'&&(args.length===2||args.length===3))result=prepare(suite,...args);
  else if(command==='stage'&&!args.length)result=stage(suite);
  else if(command==='run'&&args.length===5)result=await run(suite,settings(args[0],...args.slice(1).map(Number)));
  else if(command==='run-mixed'&&args.length===4)result=await run(suite,mixedSettings(...args.map(Number)));
  else throw new Error('Invalid acceptance command');
  console.log(identity.encoded(result));
  if(result.passed===false)process.exitCode=1;
})().catch(error=>{console.error(error.message);process.exitCode=1;});
