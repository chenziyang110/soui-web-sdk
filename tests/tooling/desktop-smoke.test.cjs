'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const smoke=require('../../tools/internal/desktop-smoke.cjs');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const encode=value=>JSON.stringify(value,null,2)+'\n';
const write=(root,name,value)=>{const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,typeof value==='string'?value:encode(value));return file;};

test('desktop CLI rejects unknown, duplicate/extra and unsupported identity arguments',()=>{
  assert.deepEqual(smoke.settings(['run','x86','Debug','C:/FullDevkit']).architecture,'x86');
  for(const args of [['run','arm64','Release','C:/Full'],['run','x64','Release','\\server\sdk'],
    ['run','x64','Release','C:/x*'],['run','x64','Release','C:/x','extra'],['shell','x64','Release','C:/x']])
    assert.throws(()=>smoke.settings(args),/Invalid/);
});
test('actual legacy scenario argv and forty-five second runtime budget remain distinct',()=>{
  const root=path.resolve(__dirname,'../..');
  const result=smoke.plans(root,{architecture:'x64',configuration:'Release',scenarios:[
    {target:'SouiStarter',scenario:'default'},{target:'SouiWorkbench',scenario:'default'},{target:'SouiWorkbench',scenario:'react'}]});
  assert.deepEqual(result.map(row=>row.args),[['--smoke-test'],['--webview-smoke-test','--arch=x64'],['--webview-smoke-test','--arch=x64','--react']]);
  assert(result.every(row=>row.timeoutMs===45000&&row.drainMs===2000));
  assert.throws(()=>smoke.plans(root,{architecture:'x64',configuration:'Release',scenarios:[{target:'SouiStarter',scenario:'react'}]}));
});

// Files and records below are deliberately synthetic. They exercise rejection
// and compatibility only, never actual application, Job, document or OS proof.
function fixture(t){
  const root=fs.mkdtempSync(path.join(os.tmpdir(),'soui-smoke-reader-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
  write(root,'project.json',{nativeTarget:'SampleApp'});
  const metadata={schema:'soui.native-smoke/v2',architecture:'x64',configuration:'Release',buildId:'synthetic-build',
    sourceSha256:'f'.repeat(64),scenarios:[{target:'SampleApp',scenario:'default'}]};
  const plan=smoke.plans(root,metadata)[0],subject=Buffer.from('synthetic application bytes');
  write(root,path.relative(root,plan.executable),subject.toString());
  const runnerPath=write(root,'synthetic-runner.exe','synthetic runner bytes');
  const runner={path:runnerPath,bytes:fs.statSync(runnerPath).size,sha256:hash(fs.readFileSync(runnerPath))};
  const attempt='out/x64/smoke-owned-11111111-1111-4111-8111-111111111111',stage=attempt+'/SampleApp-default';
  const ready={pid:42,creationFileTime:'134360236628931864'};
  const expected={...plan,executableSha256:hash(subject),runnerSha256:runner.sha256};
  const context={schema:'soui.owned-process-context/v1',executable:plan.executable,args:plan.args,cwd:plan.cwd,
    timeoutMs:45000,drainMs:2000,executableSha256:expected.executableSha256,runnerSha256:runner.sha256};
  const contextBytes=Buffer.from(encode(context));expected.contextSha256=hash(contextBytes);
  const rows=[{event:'start',schema:'soui.owned-process-run/v1',...ready,timeoutMs:45000,drainMs:2000,
    jobAssignedBeforeResume:true,breakawayAllowed:false},{event:'root-exit',exitCode:0},
    {event:'terminal',reason:'completed',rootExited:true,rootExitCode:0,passed:true,closureConfirmed:true,
      activeProcesses:0,activeBeforeCleanup:0,limitTerminatedProcesses:0,terminatedOwnedJob:false,totalProcesses:1}];
  const finished={...ready,exitCode:0,supervisorExitCode:0,signal:null,spawnError:null,reportError:null,stopRequested:false,
    fallbackUsed:false,closureConfirmed:true,passed:true,terminal:rows.at(-1),executableSha256:expected.executableSha256,runnerSha256:runner.sha256};
  const log='application-ready\nhost-close-complete\n';
  const run={target:plan.target,scenario:plan.scenario,passed:true,exitCode:0,timedOut:false,log,...ready,
    ownedSupervision:{schema:'soui.native-smoke-app-owned/v1',ready,expected,report:stage+'/owned.ndjson',
      context:stage+'/owned.ndjson.context.json',finished:stage+'/finished.json',log:stage+'/app.log',logSha256:hash(Buffer.from(log))}};
  const input={schema:'soui.native-smoke-inputs/v1',buildId:metadata.buildId,sourceSha256:metadata.sourceSha256,
    architecture:'x64',configuration:'Release',runner,plans:[plan]};
  const report={...metadata,runs:[run],ownedSupervision:{schema:'soui.native-smoke-owned/v1',attempt,runner,inputs:attempt+'/inputs.json'}};
  const build={buildId:metadata.buildId,source:{sha256:metadata.sourceSha256},payloads:{SampleApp:{files:[{path:'SampleApp.exe',sha256:hash(subject)}]}}};
  function persist(){
    const raw=Buffer.from(rows.map(row=>JSON.stringify(row)).join('\n')+'\n');finished.reportSha256=hash(raw);
    write(root,run.ownedSupervision.report,raw.toString());write(root,run.ownedSupervision.context,contextBytes.toString());
    write(root,run.ownedSupervision.finished,finished);run.ownedSupervision.finishedSha256=hash(fs.readFileSync(path.join(root,run.ownedSupervision.finished)));
    write(root,run.ownedSupervision.log,log);write(root,report.ownedSupervision.inputs,input);
    report.ownedSupervision.inputsSha256=hash(fs.readFileSync(path.join(root,report.ownedSupervision.inputs)));
  }
  persist();return {root,report,build,rows,finished,run,context,input,persist};
}
test('complete supplied reader fixture binds child identity and leaves controller closure external',t=>{
  const f=fixture(t);assert.deepEqual(smoke.validateReport(f.root,f.report,f.build),{applications:1,applicationNaturalClosure:true});
});
test('forced compiler tail, timeout, STOP, fallback and active members remain application failures',t=>{
  const f=fixture(t);
  for(const mutate of [()=>{f.finished.supervisorExitCode=124;},()=>{f.finished.terminal.reason='timeout';},
    ()=>{f.finished.stopRequested=true;},()=>{f.finished.fallbackUsed=true;},()=>{f.finished.terminal.activeBeforeCleanup=1;},
    ()=>{f.finished.terminal.terminatedOwnedJob=true;},()=>{f.finished.terminal.limitTerminatedProcesses=1;}]){
    const baseline=structuredClone(f.finished);mutate();f.persist();assert.throws(()=>smoke.validateReport(f.root,f.report,f.build));
    Object.assign(f.finished,baseline);f.rows[2]=f.finished.terminal;
  }
});
test('numeric identity, duplicate terminals, wrong argv and swapped current payload reject child receipts',t=>{
  const f=fixture(t);
  f.rows[0].creationFileTime=134360236628931864;f.persist();assert.throws(()=>smoke.validateReport(f.root,f.report,f.build));
  f.rows[0].creationFileTime=f.finished.creationFileTime;f.rows.push(f.rows[2]);f.persist();assert.throws(()=>smoke.validateReport(f.root,f.report,f.build));
  f.rows.pop();f.persist();f.run.ownedSupervision.expected.args=['--not-smoke'];assert.throws(()=>smoke.validateReport(f.root,f.report,f.build));
  f.run.ownedSupervision.expected.args=f.input.plans[0].args;f.build.payloads.SampleApp.files[0].sha256='e'.repeat(64);
  assert.throws(()=>smoke.validateReport(f.root,f.report,f.build));
});
test('replaced raw/log/context/runner files and evidence outside the exact attempt cannot satisfy smoke',t=>{
  for(const field of ['report','context','log']){
    const f=fixture(t);fs.appendFileSync(path.join(f.root,f.run.ownedSupervision[field]),'changed');
    assert.throws(()=>smoke.validateReport(f.root,f.report,f.build));
  }
  const f=fixture(t);fs.appendFileSync(f.report.ownedSupervision.runner.path,'changed');
  assert.throws(()=>smoke.validateReport(f.root,f.report,f.build));
  const g=fixture(t);g.run.ownedSupervision.report='out/x64/other-attempt/owned.ndjson';
  assert.throws(()=>smoke.validateReport(g.root,g.report,g.build),/outside/);
});
test('ready and close strings alone do not bypass actual application natural closure',t=>{
  const f=fixture(t);delete f.run.ownedSupervision;
  assert.throws(()=>smoke.validateReport(f.root,f.report,f.build),/supervision/);
  const g=fixture(t);g.run.log='application-ready';assert.throws(()=>smoke.validateReport(g.root,g.report,g.build));
});
test('public wrapper keeps one desktop lease and mandatory reader tests without root-PID cleanup',()=>{
  const root=path.resolve(__dirname,'../..'),text=fs.readFileSync(path.join(root,'tools/Test.ps1'),'utf8');
  assert.equal(/Start-Process|Stop-Process/.test(text),false);
  assert.equal((text.match(/Local\\SouiDevKit\.DesktopValidation/g)||[]).length,1);
  assert(text.indexOf('if($UnitOnly)')<text.indexOf('$lease='));
  for(const file of ['resource-validation.test.cjs','session-validation.test.cjs','devkit-command.test.cjs','devkit-delivery.test.cjs'])assert(text.includes(file));
  assert(text.includes('Required adopted test is missing'));assert(text.includes('desktop-smoke.cjs'));
});
