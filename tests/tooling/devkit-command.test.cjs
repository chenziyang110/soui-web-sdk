'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const path=require('node:path');
const crypto=require('node:crypto');
const kit=require('../../tools/internal/devkit-command.cjs');
const root=path.resolve(__dirname,'../..');
const ps='C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe';
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const bad=(args,code='E_ARGUMENT')=>assert.throws(()=>kit.parseArgs(args),error=>error.code===code);

test('unknown commands and arbitrary command-line escape hatches are refused',()=>{
  bad(['shell'],'E_COMMAND');bad(['doctor','--command','Write-Host 1']);
  bad(['build','--extra-args','/p:BeforeBuild=evil']);bad(['dev','--open']);
  bad(['upgrade','--project','C:/p','--sdk-package','C:/s','--force']);
});
test('duplicate, option-as-value, fractional and unbounded budgets are refused',()=>{
  for(const args of [['doctor','--format','json','--format','human'],['dev','--port','--format'],
    ['test','--timeout-ms','01'],['test','--timeout-ms','1.1'],['dev','--duration-ms','3600001'],
    ['dev','--port','0'],['build','--architecture','arm64'],['doctor','--format','yaml']])bad(args);
});
test('control characters, device/UNC paths and wildcard project paths are refused',()=>{
  bad(['doctor','\n']);bad(['upgrade','--project','\\\\server\\project','--sdk-package','C:/sdk'],'E_PATH');
  bad(['upgrade','--project','C:/project*','--sdk-package','C:/sdk'],'E_PATH');
  bad(['create','--name','CON','--output-root','C:/app']);
});
test('file arguments remain literal argv; they are never shell commands',()=>{
  const v=kit.parseArgs(['create','--name','App1','--output-root','C:/Apps/safe; $(whoami)','--sdk-package','C:/Sdk/Pkg']);
  const plan=kit.planCommand(v,root,ps);
  assert.equal(plan.executable,ps);assert.equal(plan.args[4],'-File');
  assert.equal(plan.args[plan.args.indexOf('-OutputRoot')+1],'C:/Apps/safe; $(whoami)');
  assert.equal(plan.args.includes('-Command'),false);
});
test('build delegates only the real standard build CLI and leaves tail opt-in explicit',()=>{
  const v=kit.parseArgs(['build','--sdk-only','--skip-web','--architecture','x86']);v.runner='C:/Tools/OwnedProcessRunner.exe';
  const plan=kit.planCommand(v,root,ps);
  assert.equal(plan.args[0],path.join(root,'tools/internal/standard-build.cjs'));
  assert(plan.args.includes('--sdk-only'));assert.equal(plan.args.includes('--compiler-tail'),false);
  bad(['build','--tail-sha256','a'.repeat(64)]);
});
test('unit/native retain distinct behavior and desktop selects the common helper without a second mutex owner',()=>{
  const unit=kit.planCommand(kit.parseArgs(['test']),root,ps,'C:/Tools/ctest.exe');
  assert(unit.args.includes('-UnitOnly'));
  const native=kit.planCommand(kit.parseArgs(['test','--scope','native']),root,ps,'C:/Tools/ctest.exe');
  assert.equal(native.executable,'C:/Tools/ctest.exe');assert(native.args.includes('--output-on-failure'));
  const desktop=kit.planCommand(kit.parseArgs(['test','--scope','desktop']),root,ps);
  assert.deepEqual(desktop.args,[path.join(root,'tools/internal/desktop-smoke.cjs'),'run','x64','Release',root]);
  assert.equal(desktop.executable,process.execPath);assert.equal(desktop.args.some(arg=>arg.endsWith('Test.ps1')),false);
});
test('upgrade selects persistent install with no privileged replay, recover shortcut or force',()=>{
  const plan=kit.planCommand(kit.parseArgs(['upgrade','--sdk-package','C:/Sdk/pkg','--project','C:/App']),root,ps);
  assert.deepEqual(plan.args,[path.join(root,'tools/sdk/soui-sdk.cjs'),'install','--package','C:/Sdk/pkg','--project','C:/App']);
});

// Synthetic native records verify reader rejection only, never actual Job/OS closure.
function receipt() {
  const ready={pid:42,creationFileTime:'134360236628931864'};
  const expected={executable:ps,args:['-NoProfile','-File','C:/sdk/tools/Doctor.ps1'],cwd:'C:/sdk',timeoutMs:1000,drainMs:2000,
    executableSha256:'a'.repeat(64),runnerSha256:'b'.repeat(64)};
  const contextBytes=Buffer.from(JSON.stringify({schema:'soui.owned-process-context/v1',...expected}));expected.contextSha256=hash(contextBytes);
  const start={event:'start',schema:'soui.owned-process-run/v1',...ready,timeoutMs:1000,drainMs:2000,jobAssignedBeforeResume:true,breakawayAllowed:false};
  const terminal={event:'terminal',reason:'completed',rootExited:true,rootExitCode:0,passed:true,closureConfirmed:true,
    activeProcesses:0,activeBeforeCleanup:0,limitTerminatedProcesses:0,terminatedOwnedJob:false,totalProcesses:1};
  const rows=[start,{event:'root-exit',exitCode:0},terminal];
  const result={...ready,exitCode:0,supervisorExitCode:0,signal:null,spawnError:null,reportError:null,stopRequested:false,
    fallbackUsed:false,closureConfirmed:true,passed:true,terminal,executableSha256:expected.executableSha256,runnerSha256:expected.runnerSha256};
  return {rows,result,contextBytes,ready,expected};
}
function read(f) {const raw=Buffer.from(f.rows.map(x=>JSON.stringify(x)).join('\n')+'\n');f.result.reportSha256=hash(raw);return kit.validateStage(raw,f.contextBytes,f.ready,f.result,f.expected);}
test('exact finite reader accepts only its complete supplied synthetic receipt',()=>{
  const f=receipt();assert.equal(read(f).genericSupervisionPassed,true);
});
test('duplicate terminal, numeric FILETIME, breakaway and argv drift fail the reader',()=>{
  for(const mutate of [f=>f.rows.push(f.rows.at(-1)),f=>f.rows[0].creationFileTime=134360236628931864,
    f=>f.rows[0].breakawayAllowed=true,f=>f.expected.args.push('extra')]){
    const f=receipt();mutate(f);assert.throws(()=>read(f),error=>error.code==='E_SUPERVISION');
  }
});
test('forced compiler tail, runtime timeout, STOP and fallback stay false',()=>{
  for(const mutate of [f=>{f.result.supervisorExitCode=124;f.result.passed=false;f.result.terminal.reason='owned-descendants-remain';},
    f=>{f.result.terminal.reason='timeout';},f=>{f.result.stopRequested=true;},f=>{f.result.fallbackUsed=true;},
    f=>{f.result.terminal.activeBeforeCleanup=1;f.result.terminal.terminatedOwnedJob=true;}]) {
    const f=receipt();mutate(f);assert.throws(()=>read(f),error=>error.code==='E_STAGE_FAILED');
  }
});

test('explicit vendor testing stays a source build capability and forwards literal flags',()=>{
  const llvm=['--native-toolchain','LLVM22MSVC142','--llvm-root','F:/llvm','--msvc-root','F:/msvc',
    '--windows-sdk-root','F:/winsdk','--ninja-exe','F:/ninja.exe','--windows-cmd-exe','C:/Windows/System32/cmd.exe',
    '--powershell-exe',ps];
  for(const selection of [[],llvm]){
    const value=kit.parseArgs(['build',...selection,'--with-vendor-tests']);
    value.runner='C:/Tools/OwnedProcessRunner.exe';
    const plan=kit.planCommand(value,root,ps);
    assert.equal(plan.args.filter(flag=>flag==='--with-vendor-tests').length,1);
    const build=require('../../tools/internal/standard-build.cjs');
    assert.equal(build.buildArguments(root,build.parseArgs(kit.buildOptions(value))).filter(flag=>flag==='-WithVendorTests').length,1);
    bad(['build',...selection,'--with-vendor-tests','--sdk-only']);
  }
  for(const command of ['doctor','test','package'])bad([command,'--with-vendor-tests']);
  bad(['build','--with-vendor-tests','false']);
  bad(['build','--with-vendor-tests','--with-vendor-tests']);
});

test('generated applications reject vendor testing before SDK verification or build preparation',()=>{
  const fs=require('node:fs');
  const folder=fs.mkdtempSync(path.join(root,'out/vendor-generated-boundary-'));
  fs.writeFileSync(path.join(folder,'project.json'),JSON.stringify({schema:'soui.application/v1',runtimeProfile:'compatibility'}));
  assert.throws(()=>kit.workspace(folder,kit.parseArgs(['build','--with-vendor-tests'])),
    error=>error.code==='E_WORKSPACE_CAPABILITY'&&/private dependency tests/.test(error.fix));
  assert.equal(fs.existsSync(path.join(folder,'out')),false);
  assert.equal(fs.existsSync(path.join(folder,'sdk-lock.json')),false);
});

test('generated LLVM builds reach the existing standard controller with both roots and one workspace lease',()=>{
  const llvm=['--native-toolchain','LLVM22MSVC142','--llvm-root','F:/llvm','--msvc-root','F:/msvc',
    '--windows-sdk-root','F:/winsdk','--ninja-exe','F:/ninja.exe','--windows-cmd-exe','C:/Windows/System32/cmd.exe',
    '--powershell-exe',ps];
  for(const architecture of ['x64','x86']){
    const value=kit.parseArgs(['build',...llvm,'--architecture',architecture,'--devkit-root','F:/SDK/ToolsOwner',
      '--with-webview2','--fresh-configure']);value.runner='F:/SDK/ToolsOwner/OwnedProcessRunner.exe';
    const dispatch=kit.commandDispatch(value,{generated:true},root,ps);
    assert.equal(dispatch.standardBuild,true);assert.equal(dispatch.workspaceLeaseRequired,false);
    assert.equal(dispatch.invocation.executable,process.execPath);
    assert.equal(dispatch.invocation.args[0],path.join(root,'tools/internal/standard-build.cjs'));
    for(const [flag,expected] of [['--root',root],['--devkit-root','F:/SDK/ToolsOwner'],['--architecture',architecture],
      ['--native-toolchain','LLVM22MSVC142'],['--runner',value.runner],['--powershell',ps]])
      assert.equal(dispatch.invocation.args[dispatch.invocation.args.indexOf(flag)+1],expected);
    assert(dispatch.invocation.args.includes('--with-webview2'));assert(dispatch.invocation.args.includes('--fresh-configure'));
    assert.equal(dispatch.timeoutMs,value.timeoutMs+value.drainMs+45000);
    assert.equal(value.drainMs,2000);assert.equal(dispatch.invocation.args.includes('--compiler-tail'),false);
  }
});
test('generated MSVC and source standard build routes retain original producer, lease and budgets',()=>{
  const value=kit.parseArgs(['build']);value.runner='C:/Tools/OwnedProcessRunner.exe';
  const generated=kit.commandDispatch(value,{generated:true},root,ps);
  assert.equal(generated.standardBuild,false);assert.equal(generated.workspaceLeaseRequired,true);
  assert.equal(generated.invocation.executable,ps);assert.equal(generated.timeoutMs,value.timeoutMs);
  assert.equal(generated.invocation.args[generated.invocation.args.indexOf('-File')+1],path.win32.join(root,'tools/Build.ps1'));
  const source=kit.commandDispatch(value,{generated:false},root,ps);
  assert.equal(source.standardBuild,true);assert.equal(source.workspaceLeaseRequired,false);
  assert.deepEqual(source.invocation,kit.planCommand(value,root,ps));
  assert.equal(source.timeoutMs,value.timeoutMs+value.drainMs+45000);
});
test('non-build commands keep their original command argv, external lease and finite budget',()=>{
  const value=kit.parseArgs(['test','--scope','native']),ctest='C:/Tools/ctest.exe';
  const dispatch=kit.commandDispatch(value,{generated:true},root,ps,ctest);
  assert.equal(dispatch.standardBuild,false);assert.equal(dispatch.workspaceLeaseRequired,true);
  assert.equal(dispatch.invocation,null); // Original non-build planning stays inside its existing execution branch.
  assert.equal(kit.planCommand(value,root,ps,ctest).executable,ctest);
  assert.equal(dispatch.timeoutMs,value.timeoutMs);
  const dev=kit.parseArgs(['dev']);
  assert.equal(kit.commandDispatch(dev,{generated:true},root,ps).invocation,null);
});
