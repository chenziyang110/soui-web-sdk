'use strict';

// Explicit operator entry only. Importing this module or preparing a profile
// never starts a process. No injector, cursor positioning or activation helper.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isDeepStrictEqual:equal}=require('node:util');
const {performance}=require('node:perf_hooks');
const profile=require('./input-ui-profile.cjs');
const owned=require('./owned-trace-subject.cjs');
const {insist}=require('./input-frame-trace.cjs');

async function waitReady(directory,subject,run,request) {
  const deadline=performance.now()+30000; let ended=false;
  void run.finished.then(()=>{ended=true;});
  while(performance.now()<deadline) {
    try {
      const value=owned.readRunJson(directory,'host-ready.json');
      insist(value.schema==='soui.input-ui-host-ready/v1'&&value.pid===subject.pid&&value.creationFileTime===subject.creationFileTime,
        'Manual ready identity differs from held native subject');
      insist(value.input_mode==='manual'&&value.deterministic_replay===false&&value.measurement_path==='public-sdk'&&
        equal(value.diagnostics,{suppress_focus:false,borderless:false,foreground:false,logging:false}), 'Unexpected intervention/mode');
      insist(value.compiled_sdk_version===request.sdk_raw_version&&value.compiled_sdk_package_version===request.sdk_package_version,
        'Manual ready compiled SDK version differs from request');
      insist(value.manual_report_capability===profile.MANUAL_REPORT_CAPABILITY,'Manual Host lacks the current one-shot page report capability');
      insist(performance.now()<deadline,'Manual ready read exceeded the original startup deadline');
      return value;
    } catch(error) {
      // Native CREATE_NEW publishes the name before its exclusive writer closes.
      // Retry only these file-access transitions; never retry malformed content
      // or an identity failure. The original absolute deadline is unchanged.
      if(!['ENOENT','EACCES','EPERM','EBUSY'].includes(error.code))throw error;
    }
    if(ended)break; // One final read was attempted even if the child already exited.
    await new Promise(resolve=>setTimeout(resolve,50));
  }
  throw new Error('Manual Host ready record did not arrive within startup deadline');
}
async function launchManual(requestFile,directory) {
  insist(process.platform==='win32','Manual native Host requires Windows');
  const request=profile.readJson(path.resolve(requestFile)); profile.verifyRequest(request);
  insist(typeof directory==='string'&&path.isAbsolute(directory)&&!fs.existsSync(directory),'Output must be a new absolute directory');
  for(const source of [request.runtime_root,request.page_root]) {
    const relative=path.relative(fs.realpathSync(source),path.resolve(directory));
    insist(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative),'Output must be outside frozen runtime/page directories');
  }
  const runId=crypto.randomBytes(16).toString('hex'), args=profile.hostArguments(request,directory,runId);
  fs.mkdirSync(directory); profile.writeNew(path.join(directory,'frozen-request.json'),request);
  const record={schema:'soui.input-ui-profile-run/v1',capture_root:directory,run_id:runId,status:'incomplete',error:null,
    request:owned.describe(directory,'frozen-request.json'),host_arguments:args,processes:[],owned_evidence:[],artifacts:{},page_bundle:[],
    foreground_validity:'unknown',workload_comparable:'unknown',performanceGate:false,releaseReady:false};
  let run,watchdog,interrupted=false;
  const interrupt=()=>{interrupted=true;if(run)run.stop();};
  process.on('SIGINT',interrupt);process.on('SIGTERM',interrupt);
  try {
    // Import the existing native Job owner only on this explicit launch path.
    const {launch}=require('./run-one.cjs');
    run=launch('host',request.host_executable,args,path.dirname(request.host_executable),directory,
      {...request.frozen_inputs.child_environment},{runner:request.owned_runner,timeoutMs:190000});
    watchdog=setTimeout(()=>{interrupted=true;run.stop();},195000);
    const subject=await run.ready,ready=await waitReady(directory,subject,run,request);
    insist(ready.run_id===runId,'Native ready run id differs');
    console.log(JSON.stringify({ready:true,pid:subject.pid,creationFileTime:subject.creationFileTime,
      instruction:request.configuration.instruction,requested_window_ms:request.workload.requested_window_ms,
      note:'Manually activate, perform input, click Finish and wait for acceptance, then close normally to persist. Foreground and equal work remain unverified.'}));
    record.processes=[await run.finished];
    insist(!interrupted&&record.processes[0].code===0&&!record.processes[0].error,'Manual Host did not complete naturally');
    record.owned_evidence=owned.captureOwnedEvidence(directory,record.processes);
    for(const [key,name]of Object.entries(profile.artifactFiles(ready)))record.artifacts[key]=owned.describe(directory,name);
    record.page_bundle=owned.describeTree(directory,'page-bundle');
    profile.verifyRequest(request); // No stale source/payload accepted after the run.
    record.status='completed'; profile.writeNew(path.join(directory,'input-ui-profile-run.json'),record);
    const result=profile.readRun(directory); profile.writeNew(path.join(directory,'input-ui-profile-check.json'),result);
    return {output:directory,run_id:runId,status:'completed',performanceGate:false,adoption:'pending_measurement'};
  } catch(error) {
    if(run) {run.stop();record.processes=[await run.finished];}
    record.status='failed';record.error=error.message;
    try {record.owned_evidence=owned.captureOwnedEvidence(directory,record.processes);}catch(captureError){record.error+='; evidence: '+captureError.message;}
    if(!fs.existsSync(path.join(directory,'input-ui-profile-run.json')))profile.writeNew(path.join(directory,'input-ui-profile-run.json'),record);
    else profile.writeNew(path.join(directory,'input-ui-profile-check-failure.json'),{error:error.message,performanceGate:false});
    throw new Error('Manual profile failed; original artifacts retained: '+error.message);
  } finally {clearTimeout(watchdog);process.removeListener('SIGINT',interrupt);process.removeListener('SIGTERM',interrupt);}
}
if(require.main===module) {
  if(process.argv.length!==4){console.error('Usage: node launch-input-ui-profile.cjs FROZEN_REQUEST ABS_NEW_RUN_DIRECTORY');process.exitCode=1;}
  else launchManual(process.argv[2],process.argv[3]).then(value=>console.log(JSON.stringify(value,null,2)))
    .catch(error=>{console.error(error.message);process.exitCode=1;});
}
module.exports={launchManual,waitReady};
