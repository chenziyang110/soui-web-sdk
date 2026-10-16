'use strict';
// Synthetic classifier data only. No native process, helper or SDK is run.
const {test:nodeTest}=require('node:test'),assert=require('node:assert/strict'),crypto=require('node:crypto'),path=require('node:path');
const r=require('../../tools/internal/host64-build-debug-observer.cjs'),standard=require('../../tools/internal/standard-build.cjs');
const root=path.resolve(__dirname,'../..'),nonce='12345678-1234-1234-1234-123456789abc';
const options=standard.parseArgs(['--native-toolchain','LLVM22MSVC142','--llvm-root','F:/mock/llvm','--msvc-root','F:/mock/msvc','--windows-sdk-root','F:/mock/sdk','--ninja-exe','F:/mock/ninja.exe','--windows-cmd-exe','F:/mock/cmd.exe','--powershell-exe','F:/mock/pwsh.exe']);
const c={schema:'soui.host64-build-debug-inputs/2',nonce,root:'F:/mock-sdk',rootArguments:standard.buildArguments('F:/mock-sdk',options),
 images:['POWERSHELL','CMAKE','NODE','CMD','NINJA','CLANG','LINKER','ARCHIVER','RC','MT','CMCLDEPS','CONHOST'].map(role=>({role,path:'F:/mock/'+(role==='POWERSHELL'?'pwsh':role.toLowerCase())+'.exe',bytes:100,sha256:'a'.repeat(64),machine:34404})),
 modules:[{role:'NTDLL',path:'F:/mock/ntdll.dll',bytes:100,sha256:'b'.repeat(64),machine:34404,codeLayout:{sizeOfImage:8192,executableRanges:[{rva:4096,bytes:2048}]}}],
 controlPins:[{path:'F:/mock-sdk/tools/Build.ps1',bytes:1,sha256:'c'.repeat(64)}],report:'F:/mock-records/host64-debug.ndjson',profile:'LLVM22MSVC142',sdkArchitecture:'x64',configuration:'Release',sourceSetSha256:'c'.repeat(64),producer:{path:'F:/mock/prepared.json',bytes:1,sha256:'d'.repeat(64)},outerRunner:{path:'F:/mock/OwnedProcessRunner.exe',bytes:1,sha256:'f'.repeat(64)},budgets:r.budgets};
const inputBytes=Buffer.from(JSON.stringify(c,null,2)+'\n'),inputPin={path:'F:/mock-records/host64-inputs.json',bytes:inputBytes.length,sha256:crypto.createHash('sha256').update(inputBytes).digest('hex')};
const identity={nonce,inputSha256:inputPin.sha256,observerPid:7000,observerCreationFileTime:'135000000000000000'};
const test=(name,f)=>nodeTest(name,()=>assert.equal(f(),true));
const physical=pin=>({volumeSerial:'123',fileIndex:'1',bytes:String(pin.bytes),sha256:pin.sha256,finalPath:'\\\\?\\'+pin.path.replaceAll('/','\\'),machine:34404});
const frame=(event,x)=>({schema:r.schema,sequence:0,nonce,inputSha256:identity.inputSha256,event,...x});const key=(pid,ft)=>({pid,creationFileTime:ft});
function baseline(roles=['CLANG','CLANG','RC','MT']){
 const rows=[frame('observer-start',{observerPid:identity.observerPid,observerCreationFileTime:identity.observerCreationFileTime,observerBits:64,outerJobPresent:true,sdkProof:false}),frame('build-created',{...key(7001,'135000000000000001'),mainTid:8001,assignedBeforeResume:true,breakawayAllowed:false})];
 const all=['POWERSHELL',...roles],created=[];for(let i=0;i<all.length;i++){
  const role=all[i],pid=7001+i,ft=String(135000000000000001n+BigInt(i)),pin=c.images.find(p=>p.role===role),buildRoot=i===0,tokens=buildRoot?[pin.path,...c.rootArguments]:[pin.path,'/nologo','/c','D:/synthetic/source '+i+'.cpp'];
  const id=key(pid,ft);created.push(id);rows.push(frame('create-paused',{...id,mainTid:8001+i,role,buildRoot,inInnerJob:true,alive:true,wow64:false,imageFile:physical(pin)}));
  if(buildRoot||['CLANG','RC','MT'].includes(role))rows.push(frame('commandline-paused',{...id,role,buildRoot,raw:tokens.map(r.quote).join(' '),wholeArgv:tokens,argv0File:physical(pin),argv0HeldBeforeAndAfter:true,provider:'Win32_Process.CommandLine',heldBeforeAndAfter:true,wmiThreadExited:true,providerCancellationProved:false}));
  rows.push(frame('create-continued',{...id,sameHeldProcess:true}),frame('ntdll-load',{...id,moduleBase:'1000000',moduleFile:physical(c.modules[0]),peLayout:c.modules[0].codeLayout}),frame('exception-disposition',{...id,tid:8001+i,code:0x80000003,address:String(1000000+c.modules[0].codeLayout.executableRanges[0].rva),firstChance:true,initialLoader:true,continueStatus:0x10002}));
 }
 // A normal caught CLR exception belongs to the original runtime, not an observer sticky failure.
 rows.push(frame('exception-disposition',{...created[0],tid:8001,code:0xe0434352,address:'2000000',firstChance:true,initialLoader:false,continueStatus:0x80010001}));
 for(let i=created.length-1;i>=0;i--)rows.push(frame('member-exit',{...created[i],exitCode:i===1?1:0}));
 rows.push(frame('terminal',{passed:true,failure:'',failureCode:0,rootPid:7001,rootCreationFileTime:created[0].creationFileTime,rootExitCode:0,historicalMembers:created.length,totalProcesses:created.length,activeProcesses:0,limitTerminatedProcesses:0,allMembersSignaled:true,allPausedQueriesClosed:true,allLoaderSequences:true,debugProtocolValid:true,forced:false,queryCount:created.length,eventCount:created.length*4+10,sdkProof:false,normalSdkIntegrated:false,win7Proof:false}));return rows;
}
function bytes(rows){return Buffer.from(rows.map((p,i)=>JSON.stringify({...p,sequence:i+1})).join('\n')+'\n');}
function denied(rows,id=identity){try{return !r.analyze(bytes(rows),c,id).experimentalLifecycleObserved;}catch{return true;}}
test('synthetic repeated paused role queries/CLR caught exception/nonzero tool natural closure',()=>{const p=r.analyze(bytes(baseline()),c,identity);return p.experimentalLifecycleObserved&&p.actualToolCounts.CLANG===2&&p.actualToolCounts.RC===1&&p.actualToolCounts.MT===1&&!p.normalSdkAccepted&&!p.originalSemanticGatesReplaced&&p.semanticGateIntegrationPending&&!p.sdkCompatibilityProved;});
test('MT absent is unobserved, never injected or declared normal SDK accepted',()=>{const p=r.analyze(bytes(baseline(['CLANG','RC'])),c,identity);return p.experimentalLifecycleObserved&&!p.mtObserved&&p.actualToolCounts.MT===0;});
const edits=[['schema1 rejected',a=>a[0].schema='soui.host-tool-paused-probe/v1'],['outer PID mismatch',a=>a[0].observerPid++],['outer FT mismatch',a=>a[0].observerCreationFileTime='1'],['SDK claim',a=>a.at(-1).sdkProof=true],['normal integration claim',a=>a.at(-1).normalSdkIntegrated=true],['unapproved role',a=>a.find(x=>x.event==='create-paused').role='ALIAS'],['wrong native PE',a=>a.find(x=>x.event==='create-paused').imageFile.machine=332],['wrong image SHA',a=>a.find(x=>x.event==='create-paused').imageFile.sha256='0'.repeat(64)],['wrong image path',a=>a.find(x=>x.event==='create-paused').imageFile.finalPath='C:/unapproved/pwsh.exe'],['zero held FT',a=>a.find(x=>x.event==='create-paused').creationFileTime='0'],['WOW64 role',a=>a.find(x=>x.event==='create-paused').wow64=true],['not owned',a=>a.find(x=>x.event==='create-paused').inInnerJob=false],['query not closed',a=>a.find(x=>x.event==='commandline-paused').wmiThreadExited=false],['fake provider cancellation',a=>a.find(x=>x.event==='commandline-paused').providerCancellationProved=true],['wrong provider',a=>a.find(x=>x.event==='commandline-paused').provider='PEB'],['partial argv',a=>a.find(x=>x.event==='commandline-paused').wholeArgv.pop()],['wrong root compiler profile',a=>{const p=a.find(x=>x.event==='commandline-paused');p.wholeArgv[p.wholeArgv.indexOf('LLVM22MSVC142')]='MSVC143';p.raw=p.wholeArgv.map(r.quote).join(' ');}],['lastchance rejects',a=>a.find(x=>x.event==='exception-disposition'&&!x.initialLoader).firstChance=false],['CLR swallowed rejects',a=>a.find(x=>x.event==='exception-disposition'&&!x.initialLoader).continueStatus=0x10002],['wrong loader address',a=>a.find(x=>x.event==='exception-disposition'&&x.initialLoader).address='1'],['wrong loader thread',a=>a.find(x=>x.event==='exception-disposition'&&x.initialLoader).tid++],['loader wrong code',a=>a.find(x=>x.event==='exception-disposition'&&x.initialLoader).code=0xe0434352],['wrong ntdll pin',a=>a.find(x=>x.event==='ntdll-load').moduleFile.sha256='f'.repeat(64)],['historical total gap',a=>a.at(-1).totalProcesses++],['missing signaled held history',a=>a.at(-1).allMembersSignaled=false],['active remains',a=>a.at(-1).activeProcesses=1],['forced closure',a=>a.at(-1).forced=true],['limit cleanup',a=>a.at(-1).limitTerminatedProcesses=1],['root nonzero',a=>a.at(-1).rootExitCode=2],['excess event budget',a=>a.at(-1).eventCount=100001],['fake complete query count',a=>a.at(-1).queryCount--],['unexpected fields rejected',a=>a[0].qualified=true]];
for(const [name,edit] of edits)test(name,()=>{const a=baseline();edit(a);return denied(a);});
test('second initial loader rejects',()=>{const a=baseline(),at=a.findIndex(x=>x.event==='exception-disposition'&&x.initialLoader);a.splice(at+1,0,{...a[at]});return denied(a);});
test('Continue before completed query rejects',()=>{const a=baseline();const p=a.findIndex(x=>x.event==='commandline-paused');[a[p],a[p+1]]=[a[p+1],a[p]];return denied(a);});
test('missing exit history rejects',()=>denied(baseline().filter((p,i,a)=>!(p.event==='member-exit'&&p.pid!==7001&&i===a.findIndex(x=>x.event==='member-exit')))));
test('unknown RIP rejects false pass',()=>{const a=baseline();a.splice(a.findIndex(x=>x.event==='member-exit'),0,frame('rip',{pid:7001,error:1}));return denied(a);});
test('pending provider never becomes clean success',()=>{const a=baseline();a.splice(a.length-1,0,frame('wmi-closeout-pending',{pid:7001,passed:false,wmiThreadExited:false,providerCancellationProved:false}));return denied(a);});
test('duplicate raw key rejected',()=>{try{r.analyze(Buffer.from(bytes(baseline()).toString().replace('"sequence":1','"sequence":2,"sequence":1')),c,identity);return false;}catch{return true;}});
test('actual raw hash remains separate from projection bytes',()=>{const b=bytes(baseline()),p=r.analyze(b,c,identity);return p.actualDebugRawSha256===crypto.createHash('sha256').update(b).digest('hex')&&p.actualDebugRawBytes===b.length;});
const projection=r.analyze(bytes(baseline()),c,identity),observerPin={path:'D:/synthetic/Observer.exe',bytes:10,sha256:'a'.repeat(64)},start={schema:'soui.owned-process-run/v1',event:'start',pid:7000,creationFileTime:identity.observerCreationFileTime,jobAssignedBeforeResume:true,breakawayAllowed:false,timeoutMs:900000,drainMs:2000},terminal={event:'terminal',reason:'completed',passed:true,rootExited:true,rootExitCode:0,activeProcesses:0,activeBeforeCleanup:0,terminatedOwnedJob:false,limitTerminatedProcesses:0,closureConfirmed:true,totalProcesses:projection.historicalMembers+1},ready={pid:start.pid,creationFileTime:start.creationFileTime},finished={...ready,passed:true,exitCode:0,supervisorExitCode:0,signal:null,spawnError:null,reportError:null,stopRequested:false,fallbackUsed:false,closureConfirmed:true},context={timeoutMs:900000,drainMs:2000,executable:observerPin.path,executableSha256:observerPin.sha256};
test('two roots remain distinct with actual outer historical conservation',()=>r.outerNatural([start,terminal],ready,finished,context,observerPin,projection));
for(const [name,edit] of [['outer root forged pwsh',x=>x.ctx.executable=c.images[0].path],['outer zero-history lie',x=>x.t.totalProcesses=1],['outer forced cleanup',x=>x.t.terminatedOwnedJob=true],['outer stopped',x=>x.f.stopRequested=true],['outer x86 runner failure',x=>x.f.supervisorExitCode=124],['outer held FT moved to inner',x=>x.a.creationFileTime=projection.innerBuild.creationFileTime]])test(name,()=>{const x=structuredClone({a:start,t:terminal,f:finished,ctx:context});edit(x);try{r.outerNatural([x.a,x.t],ready,x.f,x.ctx,observerPin,projection);return false;}catch{return true;}});
for(const tokens of [['C:/Program Files/tool.exe','', 'x"y','D:/a\\'],['C:/tool.exe','two words','a\\"b']])test('pure quoted argv roundtrip '+tokens[0],()=>JSON.stringify(r.argv(tokens.map(r.quote).join(' ')))===JSON.stringify(tokens));

test('8.3 tool argv0 accepted only with CREATE-matched physical held proof',()=>{const a=baseline(),q=a.find(x=>x.event==='commandline-paused'&&x.role==='CLANG');q.wholeArgv[0]='C:/PROGRA~1/MICROS~1/18/COMMUN~1/VC/Tools/Llvm/x64/bin/clang-cl.exe';q.raw=q.wholeArgv.map(r.quote).join(' ');return r.analyze(bytes(a),c,identity).experimentalLifecycleObserved;});
for(const [name,change] of [['alias physical fileindex differs',q=>q.argv0File.fileIndex='2'],['alias physical volume differs',q=>q.argv0File.volumeSerial='124'],['alias wrong content pin',q=>q.argv0File.sha256='0'.repeat(64)],['alias wrong AMD64 image',q=>q.argv0File.machine=332],['alias physical hold unproved',q=>q.argv0HeldBeforeAndAfter=false],['alias missing physical identity',q=>delete q.argv0File],['relative argv0 rejects',q=>q.wholeArgv[0]='clang-cl.exe'],['root alias never substitutes exact root vector',q=>q.wholeArgv[0]='C:/UNPROV~1/pwsh.exe']])test(name,()=>{const a=baseline(),q=a.find(x=>x.event==='commandline-paused'&&(name.startsWith('root')?x.buildRoot:x.role==='CLANG'));change(q);q.raw=q.wholeArgv.map(r.quote).join(' ');return denied(a);});
test('PID reuse is accepted only after prior member EXIT and with distinct FT',()=>{const a=baseline(),old=a.findIndex(x=>x.event==='member-exit'&&x.pid===7002),exit=a.splice(old,1)[0],next=a.findIndex(x=>x.event==='create-paused'&&x.pid===7003);a.splice(next,0,exit);for(const x of a)if(x.pid===7003)x.pid=7002;return r.analyze(bytes(a),c,identity).historicalMembers===5;});
test('live PID reuse rejects even with distinct FT',()=>{const a=baseline();for(const x of a)if(x.pid===7003)x.pid=7002;return denied(a);});
test('support CMAKE/NINJA/CONHOST members retain held history without extra paused-query role',()=>{const a=baseline(['CMAKE','NINJA','CONHOST','CLANG']);a.at(-1).queryCount=2;const p=r.analyze(bytes(a),c,identity);return p.experimentalLifecycleObserved&&p.historicalMembers===5&&p.pausedQueryCount===2;});
test('wrong nonce rejects all records even with valid outer identity',()=>{const a=baseline();a[1].nonce='12345678-1234-1234-1234-123456789abd';return denied(a);});
test('raw truncation rejects',()=>{const raw=bytes(baseline());try{r.analyze(raw.subarray(0,-1),c,identity);return false;}catch{return true;}});
test('invalid UTF8 rejects rather than replacing',()=>{const raw=bytes(baseline());raw[1]=0xff;try{r.analyze(raw,c,identity);return false;}catch{return true;}});
test('sequence gap rejects',()=>{const raw=bytes(baseline()).toString().replace('"sequence":2','"sequence":200');try{r.analyze(Buffer.from(raw),c,identity);return false;}catch{return true;}});
const fakePe=()=>{const b=Buffer.alloc(512);b.write('MZ');b.writeUInt32LE(64,60);b.writeUInt32LE(17744,64);b.writeUInt16LE(34404,68);b.writeUInt16LE(240,84);b.writeUInt16LE(0x20b,88);b.writeUInt32LE(4096,104);b.writeUInt32LE(8192,144);b.writeUInt16LE(2,156);return b;};
test('synthetic headless PE2 positive entry is classifier only',()=>{const x=r.observerPe(fakePe());return x.machine===34404&&x.subsystem===2&&x.entryPointRva===4096;});
for(const [name,edit] of [['console PE3 rejects',b=>b.writeUInt16LE(3,156)],['x86 observer rejects',b=>b.writeUInt16LE(332,68)],['zero observer entry rejects',b=>b.writeUInt32LE(0,104)],['entry outside image rejects',b=>b.writeUInt32LE(8192,104)],['PE optional64 magic rejects',b=>b.writeUInt16LE(0x10b,88)],['malformed PE offset rejects',b=>b.writeUInt32LE(1048576,60)]])test(name,()=>{const b=fakePe();edit(b);try{r.observerPe(b);return false;}catch{return true;}});
const build='D:/synthetic/build',cppSource=root+'/build-debug-observer.cpp',object=build+'/CMakeFiles/Host64BuildDebugObserver.dir/Release/build-debug-observer.cpp.obj',exe=build+'/Release/Host64BuildDebugObserver.exe',clang=c.images.find(p=>p.role==='CLANG').path,link=c.images.find(p=>p.role==='LINKER').path;
const compileRow=()=>({directory:build,file:cppSource,output:object,command:[clang,'/nologo','/c',cppSource,'/Fo'+object].map(r.quote).join(' ')});
const linkRaw=()=>['cmd.exe /C "cd . && "C:/Program Files/CMake/bin/cmake.exe" -E vs_link_exe --intdir=CMakeFiles/ --rc=unchanged --mt=unchanged --manifests -- '+[link,'/out:'+exe,'/subsystem:windows','/ENTRY:wmainCRTStartup',object].map(r.quote).join(' ')+' && cd ."'].join('\n');
test('actual producer command classifier allows CMake/Ninja outer entry with separate compileDB/object/link proof',()=>r.producerCommands([compileRow()],build,cppSource,object,exe,linkRaw()).compileArgv[0]===clang);
for(const [name,change] of [['compileDB duplicate selected Source output rejects',x=>x.db.push(compileRow())],['compileDB wrong Source rejects',x=>x.db[0].file='D:/other.cpp'],['compileDB wrong object output rejects',x=>x.db[0].output=build+'/wrong.obj'],['compile command wrong object rejects',x=>x.db[0].command=x.db[0].command.replace('/Fo'+object,'/Fo'+build+'/wrong.obj')],['link subsystem console rejects',x=>x.raw=x.raw.replace('/subsystem:windows','/subsystem:console')],['link direct wmain without CRT rejects',x=>x.raw=x.raw.replace('wmainCRTStartup','wmain')],['link missing object rejects',x=>x.raw=x.raw.replace(r.quote(object),'')],['link duplicate matching executable rejects',x=>x.raw+='\n'+x.raw]])test(name,()=>{const x={db:[compileRow()],raw:linkRaw()};change(x);try{r.producerCommands(x.db,build,cppSource,object,exe,x.raw);return false;}catch{return true;}});


test('synthetic exact maximum 2048 historical members and paused queries is bounded classifier data',()=>{const p=r.analyze(bytes(baseline(Array(2047).fill('CLANG'))),c,identity);return p.experimentalLifecycleObserved&&p.historicalMembers===2048&&p.pausedQueryCount===2048;});
test('2049th historical member rejects rather than truncating',()=>denied(baseline(Array(2048).fill('CLANG'))));
test('claimed event count below actual represented debug events rejects',()=>{const a=baseline();a.at(-1).eventCount=1;return denied(a);});
test('excess row bytes rejects',()=>{const a=baseline();a[0].extra='x'.repeat(262144);return denied(a);});
test('strict JSON depth and excess array bounds reject',()=>{for(const text of ['['.repeat(18)+'0'+']'.repeat(18),'['+Array(4097).fill('0').join(',')+']']){try{r.strictJson(text);return false;}catch{}}return true;});
test('physical volume overflow rejects',()=>{const a=baseline();a.find(x=>x.event==='create-paused').imageFile.volumeSerial='4294967296';return denied(a);});
test('held process FT unsigned64 overflow rejects',()=>{const a=baseline();a.find(x=>x.event==='create-paused').creationFileTime='18446744073709551616';return denied(a);});
test('outer extra startup conhost cannot be guessed away',()=>{const t={...terminal,totalProcesses:projection.historicalMembers+2};try{r.outerNatural([start,t],ready,finished,context,observerPin,projection);return false;}catch{return true;}});


test('ordinary firstchance ACCESS_VIOLATION zero pointer preserves NOT_HANDLED and complete natural control',()=>{const a=baseline(),x=a.find(x=>x.event==='exception-disposition'&&!x.initialLoader);x.code=0xc0000005;x.address='0';const p=r.analyze(bytes(a),c,identity);return p.experimentalLifecycleObserved&&!p.normalSdkAccepted&&p.semanticGateIntegrationPending;});
test('zero address initial loader rejects despite firstchance/mainthread/breakpoint',()=>{const a=baseline();a.find(x=>x.event==='exception-disposition'&&x.initialLoader).address='0';return denied(a);});
test('zero address lastchance rejects complete control',()=>{const a=baseline(),x=a.find(x=>x.event==='exception-disposition'&&!x.initialLoader);x.code=0xc0000005;x.address='0';x.firstChance=false;return denied(a);});
for(const [name,address] of [['numeric pointer',0],['negative pointer','-1'],['unsigned64 pointer overflow','18446744073709551616'],['leading zero pointer','00'],['leading zero positive pointer','01'],['fractional pointer','0.0'],['whitespace pointer',' 0']])test(name+' rejects',()=>{const a=baseline();a.find(x=>x.event==='exception-disposition'&&!x.initialLoader).address=address;return denied(a);});
test('maximum canonical unsigned64 noninitial pointer allowed without handling exception',()=>{const a=baseline();a.find(x=>x.event==='exception-disposition'&&!x.initialLoader).address='18446744073709551615';return r.analyze(bytes(a),c,identity).experimentalLifecycleObserved;});


test('compile-only exact -c works with separated -- Source',()=>{const x=compileRow();x.command=[clang,'-c','--',cppSource,'/Fo'+object].map(r.quote).join(' ');return r.producerCommands([x],build,cppSource,object,exe,linkRaw()).compileArgv.includes('-c');});
for(const flag of ['/C','-C','/E','-E',''])test('preprocess or missing compile-only rejects '+flag,()=>{const x=compileRow();x.command=[clang,flag,cppSource,'/Fo'+object].map(r.quote).join(' ');try{r.producerCommands([x],build,cppSource,object,exe,linkRaw());return false;}catch{return true;}});



function dualRoot(){
 const rows=baseline(),debug=bytes(rows),p=r.analyze(debug,c,identity),t={...terminal,elapsedMs:200,totalProcesses:p.historicalMembers+1},a={...start};
 const ctx={schema:'soui.owned-process-context/v1',runnerSha256:c.outerRunner.sha256,executableSha256:observerPin.sha256,executable:observerPin.path,cwd:c.root,timeoutMs:900000,drainMs:2000,args:['--input',inputPin.path,'--input-sha256',inputPin.sha256,'--report',c.report,'--nonce',c.nonce]};
 const raw=Buffer.from([a,{event:'root-exit',elapsedMs:100,exitCode:0},t].map(JSON.stringify).join('\n')+'\n');
 const result={...finished,terminal:t,runnerSha256:ctx.runnerSha256,executableSha256:ctx.executableSha256,reportSha256:crypto.createHash('sha256').update(raw).digest('hex')};
 return {debug,input:{...c,inputPin},observer:observerPin,capture:{ready,result,contextBytes:Buffer.from(JSON.stringify(ctx,null,2)+'\n'),rawReport:raw}};
}
nodeTest('independent producer selects only the actual Release object from a Multi-Config database',()=>{
 const build='F:/mock/native',cpp='F:/mock/source.cpp';
 const row=config=>({directory:build,file:cpp,output:'CMakeFiles/Host64BuildDebugObserver.dir/'+config+'/source.cpp.obj'});
 const release=r.producerObjectEntry([row('Debug'),row('Release'),row('RelWithDebInfo')],build,cpp);assert.equal(release.output,row('Release').output);
 assert.throws(()=>r.producerObjectEntry([row('Debug')],build,cpp));assert.throws(()=>r.producerObjectEntry([row('Release'),row('Release')],build,cpp));
});
nodeTest('explicit portable input and actual dual-root classifier conserve both histories',()=>{const f=dualRoot(),p=r.validateEvidence(f.debug,f.input,f.capture,f.observer,c.rootArguments);assert.equal(p.innerBuild.pid,7001);assert.equal(p.outerObserver.pid,7000);assert.equal(p.originalPollingUnmodified,true);assert.equal(p.normalSdkAccepted,false);assert.equal(p.queries.length,5);});
for(const [name,change] of [
 ['raw input SHA',f=>{f.debug=Buffer.from(f.debug.toString().replaceAll(inputPin.sha256,'0'.repeat(64)));}],
 ['root Build argv',f=>f.input.rootArguments.pop()],
 ['guessed outer count plus conhost',f=>{const lines=f.capture.rawReport.toString().trim().split('\n').map(JSON.parse);lines.at(-1).totalProcesses++;f.capture.rawReport=Buffer.from(lines.map(JSON.stringify).join('\n')+'\n');f.capture.result.terminal=lines.at(-1);f.capture.result.reportSha256=crypto.createHash('sha256').update(f.capture.rawReport).digest('hex');}],
 ['wrong original runner',f=>f.input.outerRunner.sha256='0'.repeat(64)],
 ['schema1 input downgrade',f=>f.input.schema='soui.host64-build-debug-inputs/1'],
 ['different SDK target',f=>f.input.sdkArchitecture='arm64'],
 ['mismatched allowed SDK target',f=>f.input.sdkArchitecture='x86'],
 ['unknown input field',f=>f.input.arbitraryCommand='cmd.exe'],
 ['relaxed deadlines',f=>f.input.budgets={...r.budgets,innerMs:900001}],
 ['missing fixed image',f=>f.input.images.pop()],
 ['x86 host role',f=>f.input.images[0].machine=332],
 ['unknown image role',f=>f.input.images[0].role='ANY'],
 ['duplicate Source slot',f=>f.input.controlPins.push(f.input.controlPins[0])],
 ['unclosed outer',f=>f.capture.result.closureConfirmed=false],
 ['outer observer mislabeled Build',f=>{const ctx=JSON.parse(f.capture.contextBytes);ctx.executable=c.images[0].path;f.capture.contextBytes=Buffer.from(JSON.stringify(ctx));}]
])nodeTest('dual-root rejects '+name,()=>{const f=structuredClone(dualRoot());f.debug=Buffer.from(f.debug);f.capture.rawReport=Buffer.from(f.capture.rawReport);f.capture.contextBytes=Buffer.from(f.capture.contextBytes);change(f);assert.throws(()=>r.validateEvidence(f.debug,f.input,f.capture,f.observer,c.rootArguments));});
nodeTest('SHA of projection cannot stand in for raw SHA',()=>{const f=dualRoot(),p=r.validateEvidence(f.debug,f.input,f.capture,f.observer,c.rootArguments);assert.notEqual(p.actualDebugRawSha256,crypto.createHash('sha256').update(JSON.stringify(p)).digest('hex'));});
function buildClassification(){
 const f=dualRoot(),encode=x=>Buffer.from(JSON.stringify(x,null,2)+'\n'),hash=x=>crypto.createHash('sha256').update(x).digest('hex');
 const proof=r.validateEvidence(f.debug,f.input,f.capture,f.observer,c.rootArguments),source={sha256:'c'.repeat(64)},tools=[{role:'compiler',path:'F:/mock/clang.exe',bytes:100,sha256:'a'.repeat(64)}],scripts=[{path:'tools/Build.ps1',bytes:1,sha256:'e'.repeat(64)}];
 const requestFile={path:'F:/mock-sdk/out/x64/llvm-profile-request-Release.json',bytes:1,sha256:'9'.repeat(64)},context=JSON.parse(f.capture.contextBytes);
 const frozen={schema:'soui.standard-build-inputs/v2',version:'0.5.0-preview.1',architecture:'x64',configuration:'Release',sdkOnly:false,compilerTail:false,nativeToolchain:'LLVM22MSVC142',source,rawSource:{sha256:'7'.repeat(64)},scriptSha256:'e'.repeat(64),scripts,tools,context,contextSha256:hash(f.capture.contextBytes),invocationOptions:options,oldBuild:{buildId:'22222222-2222-4222-a222-222222222222'},llvm:{inputIdentity:'a'.repeat(64),bridgeIdentity:'b'.repeat(64),debugBinding:{input:inputPin,raw:{path:c.report,bytes:f.debug.length,sha256:hash(f.debug)},observer:f.observer}}};
 const stamp=Number(BigInt(proof.innerBuild.creationFileTime)/10000n-11644473600000n),artifacts={payloads:{},optionalLibraries:{}};
 const build={schema:'soui.source-build/v2',version:frozen.version,buildId:'11111111-1111-4111-a111-111111111111',architecture:'x64',configuration:'Release',sdkOnly:false,nativeBuild:'passed',releaseReady:false,source,startedAt:new Date(stamp+100).toISOString(),finishedAt:new Date(stamp+200).toISOString(),...artifacts,
  nativeToolchain:{profile:'LLVM22MSVC142',request:{profile:'LLVM22MSVC142',architecture:'x64',discovery:{InputIdentity:'a'.repeat(64),BridgeIdentity:'b'.repeat(64)}},requestFile}};
 const observed={sourceSha256:source.sha256,rawSourceSha256:frozen.rawSource.sha256,scriptSha256:frozen.scriptSha256,scripts:structuredClone(scripts),tools:structuredClone(tools),compilerVerified:true,artifacts,llvm:{profile:'LLVM22MSVC142',architecture:'x64',configuration:'Release',inputIdentity:'a'.repeat(64),bridgeIdentity:'b'.repeat(64),observedPointerBytes:8,buildPid:proof.innerBuild.pid,buildCreationFileTime:proof.innerBuild.creationFileTime,observerNaturalClosure:true,observation:{path:'F:/mock/observation.json',bytes:1,sha256:'a'.repeat(64)},observerCapture:{ready},requestFile,debugProof:proof}};
 return {...f.capture,frozen,observed,buildReportBytes:encode(build),debugInputBytes:inputBytes,debugReportBytes:f.debug};
}
nodeTest('schema2 build classification retains semantic proof and accepts distinct actual roots',()=>{const f=buildClassification(),answer=standard.evaluateBuild(f);assert.equal(answer.accepted,true,answer.failure);assert.equal(answer.genericSupervisionPassed,true);assert.equal(answer.releaseReady,false);assert.notEqual(f.result.pid,f.observed.llvm.buildPid);});
for(const [name,change] of [['synthesized inner Owned subject',f=>f.observed.llvm.buildPid=f.result.pid],['input projection SHA',f=>f.frozen.llvm.debugBinding.raw.sha256=crypto.createHash('sha256').update(JSON.stringify(f.observed.llvm.debugProof)).digest('hex')],['missing actual raw',f=>delete f.debugReportBytes],['schema1 laundering',f=>f.frozen.schema='soui.standard-build-inputs/v1'],['source drift',f=>f.observed.sourceSha256='0'.repeat(64)],['SDK compiler unverified',f=>f.observed.compilerVerified=false],['wrong SDK width',f=>f.observed.llvm.observedPointerBytes=4],['old semantic gate bypass',f=>delete f.observed.llvm.observerCapture.ready],['false fast evidence',f=>f.observed.llvm.debugProof.innerBuild.pid++],['disguised Test entry',f=>f.frozen.invocationOptions={...options,configuration:'Debug'}]])nodeTest('schema2 build retains rejection: '+name,()=>{const f=buildClassification();change(f);assert.equal(standard.evaluateBuild(f).accepted,false);});
module.exports={dualRoot,c,inputBytes,inputPin,options};

function consumerInput(kind='acceptance'){
 const value=structuredClone(c),base=kind==='acceptance'?'out/acceptance/'+nonce:'out/release-validation/x64/'+'a'.repeat(32);
 value.schema='soui.host64-build-debug-inputs/3';value.consumerKind=kind;value.buildDirectory=value.root+'/'+base+'/consumer';
 value.rootArguments=['--build',value.buildDirectory,'--config',value.configuration,'--parallel','2','--verbose'];
 return value;
}
function consumerCapture(kind){
 const f=dualRoot(),input=consumerInput(kind),rawInput=Buffer.from(JSON.stringify(input,null,2)+'\n');
 input.inputPin={...inputPin,bytes:rawInput.length,sha256:crypto.createHash('sha256').update(rawInput).digest('hex')};
 const rows=JSON.parse('['+f.debug.toString().trim().split('\n').join(',')+']'),cmake=input.images.find(x=>x.role==='CMAKE');
 for(const row of rows){row.inputSha256=input.inputPin.sha256;if(row.buildRoot){row.role='CMAKE';if(row.event==='create-paused')row.imageFile=physical(cmake);else if(row.event==='commandline-paused'){row.wholeArgv=[cmake.path,...input.rootArguments];row.raw=row.wholeArgv.map(r.quote).join(' ');row.argv0File=physical(cmake);}}}
 const ctx=JSON.parse(f.capture.contextBytes);ctx.args[3]=input.inputPin.sha256;
 return {...f,input,debug:bytes(rows),capture:{...f.capture,contextBytes:Buffer.from(JSON.stringify(ctx))}};
}
for(const kind of ['acceptance','release'])nodeTest('explicit '+kind+' CMake root keeps original outer and complete inner history',()=>{
 const f=consumerCapture(kind),p=r.validateEvidence(f.debug,f.input,f.capture,f.observer,f.input.rootArguments);
 assert.equal(p.innerBuild.pid,7001);assert.equal(p.outerObserver.pid,7000);assert.equal(p.mtObserved,true);assert.equal(p.normalSdkAccepted,false);
});
for(const [name,change] of [
 ['schema2 command substitution',x=>{delete x.consumerKind;delete x.buildDirectory;x.schema=c.schema;}],
 ['unknown consumer kind',x=>x.consumerKind='arbitrary'],['wrong kind directory',x=>x.consumerKind='release'],
 ['unowned directory',x=>x.buildDirectory='F:/unowned/consumer'],['traversal directory',x=>x.buildDirectory+='/../consumer'],
 ['SDK build directory',x=>x.buildDirectory=x.root+'/out/sdk-x64'],['target configuration',x=>x.configuration='Debug'],
 ['extra target',x=>x.rootArguments.push('--target','all')],['altered parallelism',x=>x.rootArguments[5]='8'],
 ['unknown field',x=>x.extra=true]
])nodeTest('installed native input rejects '+name,()=>{const x=consumerInput();change(x);assert.throws(()=>r.validateInput(x,x.rootArguments));});
nodeTest('release native input rejects a different architecture directory',()=>{const x=consumerInput('release');x.sdkArchitecture='x86';assert.throws(()=>r.validateInput(x,x.rootArguments));});
nodeTest('consumer stream cannot relabel the actual CMake process as PowerShell',()=>{
 const f=consumerCapture('acceptance'),rows=f.debug.toString().trim().split('\n').map(JSON.parse),ps=c.images[0];
 for(const row of rows)if(row.buildRoot){row.role='POWERSHELL';if(row.imageFile)row.imageFile=physical(ps);if(row.wholeArgv){row.wholeArgv[0]=ps.path;row.raw=row.wholeArgv.map(r.quote).join(' ');row.argv0File=physical(ps);}}
 assert.throws(()=>r.validateEvidence(bytes(rows),f.input,f.capture,f.observer,f.input.rootArguments));
});

// Exact preparation Source in a VM. Every capture is controlled data and
// child-process/native entry points are unavailable, including on failures.
function preparationFault(kind){
 const fs=require('node:fs'),vm=require('node:vm'),text=fs.readFileSync(path.join(root,'tools/internal/host64-build-debug-observer.cjs'),'utf8');
 const body=text.slice(text.indexOf('async function prepare(plan)'),text.indexOf('function checkPostPins('));
 const original=Error(kind),cleanup=Error('cleanup failure'),writes=new Map(),events=[],pinned=p=>({path:p,bytes:128,sha256:'a'.repeat(64)});
 let captures=0,postchecks=0,validations=0;
 const put=(p,bytes)=>{writes.set(path.win32.basename(p),JSON.parse(Buffer.isBuffer(bytes)?bytes.toString():JSON.stringify(bytes)));};
 const sandbox={Buffer,path,strictJson:r.strictJson,sourceNames:r.sourceNames,process:{platform:'win32',execPath:'F:/mock/node.exe',env:{}},encode:x=>Buffer.from(JSON.stringify(x,null,2)+'\n'),
  fs:{existsSync:()=>false,mkdirSync:()=>events.push('attempt-created'),readFileSync:()=>Buffer.alloc(128),writeFileSync:(p,bytes)=>{
   events.push('amend-closeout');put(p,bytes);
  }},
  need:(ok,msg)=>{if(!ok)throw Error(msg);},pin:p=>{if(kind==='pin-construction'&&p.endsWith('SouiLLVM22.cmake'))throw original;return pinned(p);},peInfo:()=>({machine:34404}),observerPe:()=>({entryPointRva:4096}),
  write:(folder,name,value)=>{events.push('write:'+name);if((kind==='final-closeout-write'&&name==='producer-closeout.json')||(kind==='all-sinks'&&['producer-closeout.json','producer-failed-closeout.json'].includes(name)))throw cleanup;if(kind==='publication'&&name==='prepared.json')throw original;put(path.win32.join(folder,name),value);return pinned(path.win32.join(folder,name));},
  match:p=>{if(['early-pin','final-closeout-write','all-sinks'].includes(kind)&&p.path==='F:/mock/required')throw original;if(kind==='cleanup-pin-mismatch'&&path.win32.basename(p.path)==='producer-closeout.json')throw cleanup;return p;},
  checkPostPins:pins=>{postchecks++;events.push('postcheck');if(kind==='cleanup-throws'&&postchecks>1)throw cleanup;if(kind==='final-postcheck-throws'&&postchecks>1)throw original;return {passed:!(['cleanup-post-mismatch','final-postcheck-mismatch'].includes(kind)&&postchecks>1),rows:pins.map(p=>({expected:p,actual:p,matched:true}))};},
  sourcePins:()=>[],configureArguments:()=>{if(kind==='argv')throw original;return [];},producerObjectEntry:()=>({directory:'F:/mock/native',output:'source.obj'}),read:()=>[],
  captureFiles:(folder,name)=>({raw:pinned(folder+'/'+name+'.ndjson'),context:pinned(folder+'/'+name+'.context'),ready:pinned(folder+'/'+name+'.ready'),finished:pinned(folder+'/'+name+'.finished'),stdout:pinned(folder+'/'+name+'.stdout'),stderr:pinned(folder+'/'+name+'.stderr')}),
  preparationValidationPhase:Symbol('pure internal validation'),
  natural:()=>{},validatePrepared:()=>{validations++;events.push('semantic-validation');if(['late-semantic','cleanup-post-mismatch','cleanup-throws'].includes(kind)||(['final-validation','cleanup-pin-mismatch'].includes(kind)&&validations===2))throw original;},
  require:name=>name==='./measurement-validation.cjs'?{preparedAt:()=>{if(kind==='runner-precheck')throw original;return {tools:{OwnedProcessRunner:'F:/mock/Owned.exe'}};}}:name==='./standard-build.cjs'?{capture:async()=>{captures++;events.push('mock-capture');if(kind==='capture-rejection')throw original;return {rawReport:Buffer.alloc(1),contextBytes:Buffer.from('{}'),ready:{pid:1},result:{exitCode:0}};}}:name==='./llvm-build-observer.cjs'?{cleanEnvironment:()=>{if(kind==='environment')throw original;return {};}}:{safePath:p=>p}
 };
 const prepare=vm.runInNewContext(body+'\nprepare;',sandbox);
 const plan={actualCompiled:null,recordDirectory:'F:/mock/fresh',devkitRoot:'F:/mock/devkit',profileRequest:pinned('F:/mock/request'),cmake:pinned('F:/mock/cmake'),sourceBefore:[pinned('F:/mock/source')],root:'F:/mock/sdk',buildDirectory:'F:/mock/native',sourceSetSha256:'a'.repeat(64),discovery:{RequiredInputPins:[pinned('F:/mock/required')],BridgePins:[],ToolPins:Object.fromEntries(['CMD','NINJA','CLANG','LINKER'].map(role=>[role,pinned('F:/mock/'+role)])),CMakeDefinitions:{}}};
 return {run:()=>prepare(plan),original,cleanup,writes,events,get captures(){return captures;}};
}
for(const kind of ['early-pin','runner-precheck','pin-construction','environment','argv','capture-rejection','late-semantic','final-validation','cleanup-post-mismatch','cleanup-throws','cleanup-pin-mismatch','final-closeout-write','all-sinks','final-postcheck-throws','publication'])nodeTest('preparation mock retains failure closeout and primary error: '+kind,async()=>{
 const f=preparationFault(kind);await assert.rejects(f.run(),error=>error===f.original);
 assert.ok(f.events.includes('postcheck'));
 assert.equal(f.writes.has('prepared.json'),false,'rejected validation must never publish prepared receipt');
 if(['final-closeout-write','all-sinks'].includes(kind)){assert.ok(f.original.closeoutErrors?.some(x=>x.message==='cleanup failure'));if(kind==='all-sinks')assert.equal(f.original.closeoutErrors.length,2);}
 else {const close=f.writes.get('producer-failed-closeout.json')||f.writes.get('producer-closeout.json');assert.equal(close?.failed,true);assert.equal(close.primaryFailure.message,kind);}
 if(['early-pin','runner-precheck','pin-construction','environment','argv','final-closeout-write','all-sinks'].includes(kind))assert.equal(f.captures,0);
 else if(kind==='capture-rejection'){assert.equal(f.captures,1);assert.equal(f.writes.get('producer-closeout.json').captures[0].purpose,'configure');assert.equal(f.writes.get('producer-closeout.json').captures[0].files,null);}
 else {assert.equal(f.captures,3);assert.equal((f.writes.get('producer-failed-closeout.json')||f.writes.get('producer-closeout.json')).captures.length,3);}
 if(kind==='cleanup-pin-mismatch'){assert.ok(f.original.closeoutErrors.some(x=>x.message==='cleanup failure'));assert.equal(f.writes.get('producer-closeout.json').phase,'validated','a mismatched record must remain untouched');}
});
nodeTest('final postcheck mismatch rejects after validation without publishing prepared.json',async()=>{
 const f=preparationFault('final-postcheck-mismatch');await assert.rejects(f.run(),/producer-input-post-check/);
 assert.equal(f.writes.has('prepared.json'),false);assert.equal(f.writes.get('producer-closeout.json').failed,true);assert.equal(f.captures,3);
});
nodeTest('preparation mock success publishes only after both complete validations and keeps the snapshot distinct',async()=>{
 const f=preparationFault('success');await f.run();assert.equal(f.captures,3);assert.equal(f.events.filter(x=>x==='semantic-validation').length,2);
 assert.ok(f.writes.has('prepared.json'));assert.equal(f.writes.get('producer-validation-closeout.json').phase,'validation-input');assert.equal(f.writes.get('producer-closeout.json').failed,false);
 assert.equal(f.events.lastIndexOf('semantic-validation')<f.events.indexOf('write:prepared.json'),true);
});
nodeTest('hash backing lifetime has the documented Source declaration/destruction order',()=>{
 const fs=require('node:fs'),cpp=fs.readFileSync(path.join(root,'benchmarks/protocol/host64-build-debug-observer.cpp'),'utf8');
 const hash=cpp.slice(cpp.indexOf('std::string sha256(HANDLE file)'),cpp.indexOf('file_identity identity(HANDLE file)'));
 const algorithm=hash.indexOf('bcrypt_algorithm_owner algorithm;'),backing=hash.indexOf('std::vector<unsigned char> object(objectBytes);'),ownedHash=hash.indexOf('bcrypt_hash_owner hash;');
 assert.ok(algorithm>=0&&backing>algorithm&&ownedHash>backing,'reverse destruction must release hash, backing, then provider');
 assert.ok(cpp.includes('~bcrypt_hash_owner() noexcept { if (value) BCryptDestroyHash(value); }'));
 assert.ok(cpp.includes('bcrypt_hash_owner(const bcrypt_hash_owner&) = delete;'));
 assert.ok(!hash.includes('catch (...)'));
});

function preparedCloseoutRoleFixture(){
 const fs=require('node:fs'),vm=require('node:vm'),text=fs.readFileSync(path.join(root,'tools/internal/host64-build-debug-observer.cjs'),'utf8');
 const start=text.indexOf('const preparationValidationPhase=Symbol(');
 const body=text.slice(start,text.indexOf('function configureArguments('));
 assert.ok(start>=0,'internal phase capability must stay private to the module');
 const pinned=name=>({path:'F:/pure-role/'+name,bytes:128,sha256:'a'.repeat(64)}),before=[pinned('source.cpp')];
 const request=pinned('request.json'),cmake=pinned('cmake.exe'),runner=pinned('OwnedProcessRunner.exe'),dependency=pinned('dependency');
 const required=[...before,request,cmake,runner,dependency];let phase='validated';
 const closeout={schema:'soui.host64-producer-closeout/2',sourcePostCheckPassed:true,failed:false,checks:required.map(p=>({expected:p,actual:p,matched:true}))};
 const prepared={schema:'soui.host64-build-observer-prepared/2',actualCompiled:true,syntheticOnly:false,hostArchitecture:'AMD64',profile:'LLVM22MSVC142',qualificationPassed:false,releaseReady:false,
  sourceBefore:before,sourceAfter:before,sourceSetSha256:'pure-source-set',profileRequest:request,compiler:pinned('clang-cl.exe'),linker:pinned('lld-link.exe'),cmake,ninja:pinned('ninja.exe'),
  outerRunner:runner,outerRunnerPreparation:pinned('measurement-tools.json'),executable:pinned('Release/Host64BuildDebugObserver.exe'),compileDatabase:pinned('compile_commands.json'),
  object:pinned('object.obj'),ninjaCommands:pinned('commands.stdout'),producerCloseout:pinned('producer-closeout.json'),dependencyPins:[dependency],devkitRoot:'F:/pure-role/devkit',
  buildDirectory:'F:/pure-role',inputIdentity:'input',bridgeIdentity:'bridge',subsystem:2,entrySymbol:'wmainCRTStartup',entryPointRva:4096,configureArguments:[]};
 prepared.outerRunnerPreparation.path=path.join(prepared.devkitRoot,'out/benchmark-protocol/measurement-tools.json');
 const profile={InputIdentity:'input',BridgeIdentity:'bridge',ToolPins:{CLANG:prepared.compiler,LINKER:prepared.linker,NINJA:prepared.ninja}};
 const same=(a,b)=>a.bytes===b.bytes&&a.sha256===b.sha256&&a.path===b.path;
 const sandbox={path,Symbol,need:(v,m)=>{if(!v)throw Error(m);},norm:p=>path.win32.resolve(p).replaceAll('\\','/').toLowerCase(),eq:(a,b)=>JSON.stringify(a)===JSON.stringify(b),
  sourcePins:()=>before,sourceSet:()=>prepared.sourceSetSha256,samePin:same,match:p=>p,pin:p=>({path:p,bytes:128,sha256:'a'.repeat(64)}),
  read:p=>p.includes('producer-')?{...closeout,phase}:p.endsWith('compile_commands.json')?[]:prepared,
  fs:{readFileSync:p=>p.endsWith('object.obj')?Buffer.from([0x64,0x86,...Array(18).fill(0)]):Buffer.alloc(128)},peInfo:()=>({machine:34404}),observerPe:()=>({entryPointRva:4096}),
  sourceNames:['source.cpp'],producerCommands:()=>({compileArgv:[prepared.compiler.path],linkArgv:[prepared.linker.path]}),physicalAlias:()=>{},natural:()=>{},configureArguments:()=>[],
  currentCapture:files=>({raw:Buffer.alloc(0),ready:{},result:{},context:{runnerSha256:runner.sha256,cwd:files.cwd,executable:files.tool.path,executableSha256:files.tool.sha256,args:files.args}}),
  require:name=>name==='./llvm-profile-inputs.cjs'?{readRequest:()=>({value:{discovery:profile}})}:{preparedAt:()=>({tools:{OwnedProcessRunner:runner.path}})}};
 prepared.captures=[{purpose:'configure',files:{cwd:'F:/pure-role/sdk',tool:cmake,args:[]}},{purpose:'build',files:{cwd:'F:/pure-role/sdk',tool:cmake,args:['--build',prepared.buildDirectory,'--config','Release','--target','Host64BuildDebugObserver']}},
  {purpose:'commands',files:{cwd:prepared.buildDirectory,tool:prepared.ninja,args:['-f','build-Release.ninja','-t','commands','Host64BuildDebugObserver'],stdout:prepared.ninjaCommands}}];
 const reader=vm.runInNewContext(body+'\n({validatePrepared,loadPrepared,internal:p=>validatePrepared(p,"F:/pure-role/sdk",preparationValidationPhase)});',sandbox);
 return {reader,prepared,setPhase:p=>{phase=p;},load:()=>reader.loadPrepared('F:/pure-role/prepared.json','a'.repeat(64),'F:/pure-role/sdk')};
}
nodeTest('public prepared loader accepts a fully validated final closeout in the pure role fixture',()=>{
 const f=preparedCloseoutRoleFixture();assert.equal(f.load().value,f.prepared);
});
for(const phase of ['validation-input',undefined,'failed','unknown'])nodeTest('public prepared loader rejects non-final closeout phase: '+String(phase),()=>{
 const f=preparedCloseoutRoleFixture();f.setPhase(phase);assert.throws(f.load,/producer-post-check-receipt/);
});
nodeTest('a caller-created symbol cannot authorize the internal validation-only receipt',()=>{
 const f=preparedCloseoutRoleFixture();f.setPhase('validation-input');
 assert.throws(()=>f.reader.validatePrepared(f.prepared,'F:/pure-role/sdk',Symbol('internal preparation validation input')),/producer-post-check-receipt/);
});
nodeTest('only the internal capability validates the preparation snapshot and never the public load',()=>{
 const f=preparedCloseoutRoleFixture();f.setPhase('validation-input');assert.equal(f.reader.internal(f.prepared),f.prepared);
 assert.throws(f.load,/producer-post-check-receipt/);
});
