'use strict';
// A new same-product contract, not a renamed SOUI/Electron comparison.
const path = require('node:path');
const fs = require('node:fs');
const crypto = require('node:crypto');
const {isDeepStrictEqual} = require('node:util');
const owned = require('./owned-trace-subject.cjs');
const trace = require('./input-frame-trace.cjs');
const {checkBurst} = require('./burst-input.cjs');
const {insist} = trace;
const digest=value=>typeof value==='string' && /^[a-f0-9]{64}$/.test(value);
function verifiedDescriptor(root,item,expectedPath) {
  insist(item?.path===expectedPath && digest(item.sha256) && Number.isSafeInteger(item.bytes), 'Missing fixed evidence descriptor');
  insist(isDeepStrictEqual(owned.describe(root,expectedPath),item), 'Evidence bytes/hash mismatch');
}
function traceRows(root,item) {
  const filename=path.join(fs.realpathSync(root),item.path),stat=fs.lstatSync(filename);
  insist(stat.isFile() && !stat.isSymbolicLink() && stat.nlink===1 && stat.size>0 && stat.size<=64*1024*1024,
    'Trace evidence must be a bounded ordinary file');
  const file=fs.openSync(filename,fs.constants.O_RDONLY|(fs.constants.O_NOFOLLOW||0));
  try {
    const held=fs.fstatSync(file);
    insist(held.dev===stat.dev && held.ino===stat.ino && held.size===stat.size,'Trace changed while opening');
    const raw=trace.readHeldFile(file,held.size);
    insist(raw.length===item.bytes && raw.length===held.size &&
      crypto.createHash('sha256').update(raw).digest('hex')===item.sha256,'Trace evidence bytes/hash mismatch');
    return trace.parseNdjson(raw);
  } finally {fs.closeSync(file);}
}
function checkRun(root) {
  const record=owned.readRunJson(root,'input-ui-run.json');
  insist(record?.schema==='soui.same-product-input-run/v1' && typeof record.product_id==='string' &&
    /^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/.test(record.product_id) &&
    /^[a-f0-9]{40}$/.test(record.source_commit) && /^[a-f0-9]{32}$/.test(record.run_id) &&
    typeof record.capture_root==='string' && path.isAbsolute(record.capture_root) &&
    ['miniblink-software','webview2'].includes(record.renderer) &&
    typeof record.runtime_version==='string' && record.runtime_version.length>0 && record.runtime_version.length<=256 &&
    digest(record.host_exe_sha256) && digest(record.tools?.input_sha256) && digest(record.tools?.owned_runner_sha256),
    'Invalid same-product source/payload identity');
  for (const [key,filename] of [['payload_manifest','payload-manifest.json'],['environment','environment.json'],
    ['configuration','configuration.json'],['workload','workload.json'],['calibration','calibration.json'],
    ['input','burst-input.ndjson'],['desktop','burst-desktop.ndjson']])
    verifiedDescriptor(root,record.evidence?.[key],filename);
  const payload=owned.readRunJson(root,'payload-manifest.json');
  insist(payload?.schema==='soui.input-ui-payload/v1' && payload.source_commit===record.source_commit &&
    Array.isArray(payload.files) && payload.files.length>0 && payload.files.length<=256 &&
    payload.files.every(file=>file && typeof file.role==='string' && typeof file.path==='string' &&
      digest(file.sha256) && Number.isSafeInteger(file.bytes) && file.bytes>=0), 'Invalid frozen payload manifest');
  const payloadPaths=new Set();
  for(const file of payload.files) {
    insist(file.path.length<=1024 && !file.path.includes('\\') && !file.path.includes(':') && !file.path.includes('\0') &&
      !path.isAbsolute(file.path) && file.path.split('/').every(part=>part && part!=='.' && part!=='..') &&
      !payloadPaths.has(file.path) && file.bytes<=1024*1024*1024,'Unsafe/duplicate payload manifest path');
    payloadPaths.add(file.path);
  }
  const hostFiles=payload.files.filter(file=>file.role==='host');
  insist(hostFiles.length===1 && hostFiles[0].sha256===record.host_exe_sha256, 'Host payload is not bound to owned launch');
  const select=(items,name)=>{
    insist(Array.isArray(items) && items.length===2, 'Two owned processes required');
    const matches=items.filter(item=>item?.name===name);
    insist(matches.length===1, 'Missing/duplicate owned role'); return matches[0];
  };
  const processes={};
  for(const name of ['host','input']) {
    processes[name]=owned.verifyOwnedProcess(root,name,select(record.processes,name),select(record.owned_evidence,name),
      {runnerSha256:record.tools.owned_runner_sha256,executableSha256:name==='host'?record.host_exe_sha256:record.tools.input_sha256},record.capture_root);
    insist(processes[name].naturalCompletion, 'Same-product run requires naturally completed owned jobs');
  }
  const inputRows=owned.readRunLines(root,'burst-input.ndjson'), begin=inputRows[0];
  insist(begin?.run_id===record.run_id, 'Input run differs from frozen run identity');
  const expectedArgs=[String(processes.host.subject.pid),processes.host.subject.creationFileTime,record.run_id,
    String(begin.hz),String(begin.requested),path.join(record.capture_root,'burst-input.ndjson'),
    path.join(record.capture_root,'burst-desktop.ndjson'),begin.kind];
  insist(isDeepStrictEqual(processes.input.context.args,expectedArgs), 'Owned burst launch did not bind held target/session');
  insist(Array.isArray(record.host_arguments) && isDeepStrictEqual(processes.host.context.args,record.host_arguments),
    'Host arguments differ from frozen launch');
  insist(Array.isArray(record.traces) && record.traces.length>0 && record.traces.length<=4, 'Missing bounded trace streams');
  const names=new Set();
  const streams=record.traces.map(item=>{
    insist(/^trace-(sdk|vendor|webview2|application)\.ndjson$/.test(item?.path) && !names.has(item.path), 'Invalid duplicate trace path');
    names.add(item.path); return traceRows(root,item);
  });
  const result=checkBurst(inputRows,streams,owned.readRunLines(root,'burst-desktop.ndjson'),
    {ownedTarget:processes.host.subject,ownedInjector:processes.input.subject,
      calibration:owned.readRunJson(root,'calibration.json')});
  const workload=owned.readRunJson(root,'workload.json');
  insist(workload?.schema==='soui.burst-workload/v1' && workload.kind===begin.kind && workload.hz===begin.hz &&
    workload.requested===begin.requested && workload.content_width===1100 && workload.content_height===720 &&
    digest(workload.page_html_sha256) && digest(workload.page_script_sha256), 'Workload differs from frozen injection/page');
  insist(owned.describe(root,'page-bundle/burst-page.html').sha256===workload.page_html_sha256 &&
    owned.describe(root,'page-bundle/burst-page.js').sha256===workload.page_script_sha256,
    'Frozen page bytes differ from workload identity');
  const environment=owned.readRunJson(root,'environment.json');
  insist(environment?.schema==='soui.input-ui-environment/v1' &&
    environment.isolated_physical_target===true && environment.desktop_locked===false &&
    environment.rdp===false && environment.display_topology_changed===false &&
    Array.isArray(environment.invalid_reasons) && environment.invalid_reasons.length===0,
    'Environment records an invalid measurement round');
  return {record,result,workload,environment};
}
function checkPair(left,right) {
  const baseline=checkRun(left), candidate=checkRun(right), before=baseline.record, after=candidate.record;
  for(const key of ['product_id','renderer','runtime_version']) insist(before[key]===after[key], 'Different products/renderers/runtime cannot form this pair');
  insist(before.source_commit!==after.source_commit && before.run_id!==after.run_id, 'Two distinct source commits/runs required');
  for(const key of ['environment','configuration','workload'])
    insist(before.evidence[key].sha256===after.evidence[key].sha256, 'Pair environment/configuration/workload differs');
  for(const key of ['input_sha256','owned_runner_sha256']) insist(before.tools[key]===after.tools[key], 'Pair tool versions differ');
  return {schema:'soui.same-product-input-pair/v1',product_id:before.product_id,
    baseline_commit:before.source_commit,candidate_commit:after.source_commit,
    baseline_run:before.run_id,candidate_run:after.run_id,sameProductBindingAccepted:true,
    baseline:baseline.result,candidate:candidate.result,
    required_valid_pairs:5,valid_pairs:1,trace_overhead_gate:'not-evaluated',
    // This is an evidence consistency check. Metadata is not independent proof
    // of loaded DLL memory, OS isolation, physical devices or instrument accuracy.
    physicalPresentation:false,performanceGate:false,releaseReady:false};
}
if(require.main===module) {
  try {
    insist(process.argv.length===4,'Usage: node same-product-input-pair.cjs BASELINE_RUN CANDIDATE_RUN');
    console.log(JSON.stringify(checkPair(process.argv[2],process.argv[3]),null,2));
  } catch(error) { console.error(error.message); process.exitCode=1; }
}
module.exports={checkRun,checkPair,verifiedDescriptor};
