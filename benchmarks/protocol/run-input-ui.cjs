'use strict';
// New bounded one-run orchestration. No build, VM or autonomous retry is run.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {launch}=require('./run-one.cjs');
const owned=require('./owned-trace-subject.cjs');
const trace=require('./input-frame-trace.cjs');
const {checkRun}=require('./same-product-input-pair.cjs');
const {requireFormalHarness}=require('./input-ui-harness-policy.cjs');
const frozen=require('./freeze-input-ui-request.cjs');
const {insist}=trace;
function file(value) {
  insist(typeof value==='string'&&path.isAbsolute(value),'Absolute input file required');
  const resolved=fs.realpathSync(value);insist(fs.statSync(resolved).isFile(),'Input is not a file');return resolved;
}
function hash(filename) {
  const descriptor=fs.openSync(filename,'r'),buffer=Buffer.allocUnsafe(65536),digest=crypto.createHash('sha256');
  try {
    const stat=fs.fstatSync(descriptor);insist(stat.isFile()&&stat.size<=1024*1024*1024,'Payload file bound exceeded');
    let remaining=stat.size;
    while(remaining){const count=fs.readSync(descriptor,buffer,0,Math.min(buffer.length,remaining),null);
      insist(count>0,'Payload truncated while hashing');digest.update(buffer.subarray(0,count));remaining-=count;}
    insist(fs.readSync(descriptor,buffer,0,1,null)===0,'Payload grew while hashing');
    return {bytes:stat.size,sha256:digest.digest('hex')};
  }finally{fs.closeSync(descriptor);}
}
function json(filename) {
  const descriptor=fs.openSync(file(filename),'r');
  try{const size=fs.fstatSync(descriptor).size;insist(size>0&&size<=65536,'Metadata bound exceeded');
    return JSON.parse(trace.readHeldFile(descriptor,size).toString('utf8'));}finally{fs.closeSync(descriptor);}
}
function write(root,name,value){fs.writeFileSync(path.join(root,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
async function waitReady(root,subject,hostRun) {
  const deadline=Date.now()+30000;let ended=false;void hostRun.finished.then(()=>{ended=true;},()=>{ended=true;});
  while(Date.now()<deadline&&!ended) {
    try {
      const ready=owned.readRunJson(root,'host-ready.json');
      insist(ready.schema==='soui.input-ui-host-ready/v1'&&ready.pid===subject.pid&&
        ready.creationFileTime===subject.creationFileTime,'Host ready identity differs from held subject');
      requireFormalHarness(ready); // Refuse before launching the injector.
      return ready;
    // Native publication uses an exclusive handle through its final flush.
    // File existence is not readiness; sharing conflicts consume this same deadline.
    }catch(error){if(!['ENOENT','EBUSY'].includes(error.code))throw error;}
    await new Promise(resolve=>setTimeout(resolve,20));
  }
  throw new Error('Public SDK Host did not publish a valid ready record');
}
async function runRequest(requestFile,output) {
  insist(process.platform==='win32','Real input run requires Windows');
  const request=json(requestFile);
  insist(request?.schema==='soui.input-ui-run-request/v2'&&/^[a-f0-9]{40}$/.test(request.source_commit)&&
    /^[a-f0-9]{40}$/.test(request.measurement_source_commit)&&
    typeof request.product_id==='string'&&/^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/.test(request.product_id)&&['full','e2e'].includes(request.trace_mode)&&
    ['pointer_move','wheel'].includes(request.kind)&&[125,500,1000].includes(request.hz)&&
    Number.isSafeInteger(request.count)&&request.count>0&&request.count<=Math.min(100000,request.hz*120)&&
    typeof request.runtime_version==='string'&&request.runtime_version.length>0&&request.runtime_version.length<=256,
    'Invalid frozen run request');
  const captureBackend=frozen.selectedCaptureBackend(request);
  frozen.verify(request); // Before output creation, process launch or input.
  insist(path.isAbsolute(output)&&!fs.existsSync(output),'Output must be a new absolute directory');
  const host=file(request.host_executable),input=file(request.input_executable),runner=file(request.owned_runner),
    binary=file(request.miniblink_binary),library=file(request.sdk_library);
  const pageRoot=fs.realpathSync(request.page_root);
  insist(path.isAbsolute(request.page_root)&&fs.statSync(pageRoot).isDirectory(),'Frozen page directory required');
  const pageHtml=file(path.join(pageRoot,'burst-page.html')),pageScript=file(path.join(pageRoot,'burst-page.js'));
  const environment=file(request.frozen_environment),configuration=file(request.frozen_configuration);
  const calibration=json(request.calibration_template);
  insist(calibration?.schema==='soui.burst-calibration-template/v1'&&calibration.device_pixel_ratio===1&&
    Number.isFinite(calibration.observation_error_ms)&&calibration.observation_error_ms>=0&&
    calibration.observation_error_ms<=1000,'Explicit observation calibration template required');
  trace.exact(calibration.qpc_frequency,false);
  const identities={host:hash(host),input:hash(input),runner:hash(runner),binary:hash(binary),library:hash(library),
    html:hash(pageHtml),script:hash(pageScript)};
  fs.mkdirSync(output,{recursive:true});
  write(output,'frozen-request.json',request);
  fs.copyFileSync(environment,path.join(output,'environment.json'),fs.constants.COPYFILE_EXCL);
  fs.copyFileSync(configuration,path.join(output,'configuration.json'),fs.constants.COPYFILE_EXCL);
  const randomRun=crypto.randomBytes(16);randomRun[0]|=1;const run=randomRun.toString('hex');
  write(output,'calibration.json',{...calibration,schema:'soui.burst-calibration/v1',run_id:run,
    calibration_template_sha256:hash(file(request.calibration_template)).sha256});
  write(output,'workload.json',{schema:'soui.burst-workload/v1',kind:request.kind,hz:request.hz,requested:request.count,
    content_width:1100,content_height:720,capture_backend:captureBackend,
    page_html_sha256:identities.html.sha256,page_script_sha256:identities.script.sha256});
  write(output,'payload-manifest.json',{schema:'soui.input-ui-payload/v1',source_commit:request.source_commit,files:[
    {role:'host',path:'runtime/host.exe',...identities.host},{role:'runtime',path:'runtime/miniblink.dll',...identities.binary},
    {role:'sdk-library',path:'runtime/soui_sdk.lib',...identities.library}]});
  const arguments_=[`--miniblink=${binary}`,`--page-root=${pageRoot}`,`--result-dir=${output}`,
    `--run=${run}`,`--kind=${request.kind}`,`--trace=${request.trace_mode}`];
  const record={schema:'soui.same-product-input-run/v1',product_id:request.product_id,source_commit:request.source_commit,
    measurement_source_commit:request.measurement_source_commit,
    measurement_sources_sha256:frozen.sourceDigest(request.frozen_inputs.measurement_sources),
    run_id:run,capture_root:output,renderer:'miniblink-software',runtime_version:request.runtime_version,
    trace_mode:request.trace_mode,capture_backend:captureBackend,host_exe_sha256:identities.host.sha256,
    tools:{input_sha256:identities.input.sha256,owned_runner_sha256:identities.runner.sha256},host_arguments:arguments_,
    frozen_request:owned.describe(output,'frozen-request.json'),
    processes:[],owned_evidence:[],traces:[],evidence:{},status:'incomplete',performanceGate:false,releaseReady:false};
  const runs=[],completions=[],stopErrors=[];let watchdog;
  record.stop_errors=stopErrors;
  const stopHeld=children=>{
    for(const child of children)try{child.stop();}
    catch(error){stopErrors.push({name:child.name,error:error.message||String(error)});}
  };
  const track=run=>{
    runs.push(run);
    const finished=run.finished.catch(error=>{
      // A broken completion channel cannot prove natural Job closure. Request
      // only its held supervisor to stop and retain the missing proof as failure.
      stopHeld([run]);
      return {name:run.name,pid:run.pid,code:null,signal:null,error:error.message||String(error),
        completion_rejected:true};
    });
    completions.push(finished);return finished;
  };
  try {
    const cleanEnvironment={...process.env};
    for(const name of ['SOUI_BRIDGE_TRACE','SOUI_INTERACTION_TRACE','SOUI_KERNEL_FRAME_TRACE'])delete cleanEnvironment[name];
    const hostRun=launch('host',host,arguments_,path.dirname(host),output,cleanEnvironment,{runner,timeoutMs:190000});
    const hostFinished=track(hostRun);
    watchdog=setTimeout(()=>stopHeld(runs),195000);
    const subject=await hostRun.ready,ready=await waitReady(output,subject,hostRun);
    insist(ready.run_id===run&&ready.qpc_frequency===calibration.qpc_frequency,'Ready run/QPC differs from frozen calibration');
    const inputEnvironment={...cleanEnvironment,SOUI_DESKTOP_CAPTURE_BACKEND:captureBackend};
    const inputRun=launch('input',input,[String(subject.pid),subject.creationFileTime,run,String(request.hz),String(request.count),
      path.join(output,'burst-input.ndjson'),path.join(output,'burst-desktop.ndjson'),request.kind],
      path.dirname(input),output,inputEnvironment,{runner,timeoutMs:155000});track(inputRun);
    // A failed injector may already have signaled its FinishEvent. Do not
    // preempt Host drain; leave its original owned deadline in force.
    // A failed Host has no input target left; stop only the held injector.
    void hostFinished.then(result=>{if(result.code!==0||result.error)stopHeld(runs.filter(child=>child.name==='input'));});
    await inputRun.ready;record.processes=await Promise.all(completions);
    insist(record.processes.every(child=>child.code===0&&!child.error)&&stopErrors.length===0,'Native owned Host/input run failed');
    record.owned_evidence=owned.captureOwnedEvidence(output,record.processes);
    for(const [key,name]of [['payload_manifest','payload-manifest.json'],['environment','environment.json'],
      ['configuration','configuration.json'],['workload','workload.json'],['calibration','calibration.json'],
      ['host_ready','host-ready.json'],['host_result','host-result.json'],['input','burst-input.ndjson'],['desktop','burst-desktop.ndjson']])
      record.evidence[key]=owned.describe(output,name);
    for(const name of ['trace-sdk.ndjson','trace-vendor.ndjson'])if(fs.existsSync(path.join(output,name)))
      record.traces.push({path:name,...hash(path.join(output,name))});
    for(const [name,filename]of [['host',host],['input',input],['runner',runner],['binary',binary],['library',library],['html',pageHtml],['script',pageScript]])
      insist(hash(filename).sha256===identities[name].sha256,'Frozen payload changed during run');
    frozen.verify(request);
    record.status='completed';write(output,'input-ui-run.json',record);
    const checked=checkRun(output);write(output,'input-ui-check.json',checked.result);
    return {output,status:'completed',run_id:run,stage_evidence:checked.result.stage_evidence,
      classified_inputs:checked.result.classified_input_count,t7_samples:checked.result.desktop_samples,performanceGate:false,releaseReady:false};
  }catch(error) {
    stopHeld(runs.filter(child=>child.name==='input'));
    // Keep the original 190s Host supervision and 195s total watchdog alive
    // through failure cleanup. Never restart a timer or turn failed input green.
    record.processes=await Promise.all(completions);
    record.status='failed';record.error=error.message;
    try{record.owned_evidence=owned.captureOwnedEvidence(output,record.processes);}catch(evidenceError){record.evidence_error=evidenceError.message;}
    if(!fs.existsSync(path.join(output,'input-ui-run.json')))write(output,'input-ui-run.json',record);
    else write(output,'post-check-failure.json',{error:error.message,performanceGate:false});
    // Classification of an environmental cause requires verifier review of
    // retained raw evidence. The runner never auto-retries or guesses a cause.
    throw new Error('Input/UI run failed; raw evidence retained: '+error.message);
  }finally{clearTimeout(watchdog);}
}
if(require.main===module){runRequest(process.argv[2],process.argv[3]).then(result=>console.log(JSON.stringify(result,null,2)))
  .catch(error=>{console.error(error.message);process.exitCode=1;});}
module.exports={runRequest,hash};
