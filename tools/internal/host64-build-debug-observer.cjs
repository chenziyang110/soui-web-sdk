'use strict';
// Pure data validation. This module never imports a process/COM/SDK actor.
const crypto=require('node:crypto'),path=require('node:path'),fs=require('node:fs');const schema='soui.host64-build-debug-addendum/2';
const need=(ok,code)=>{if(!ok)throw Error(code);};
const norm=s=>typeof s==='string'?s.replace(/^\\\\\?\\/,'').replaceAll('\\','/').toLowerCase():'';
const eq=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const count=n=>Number.isSafeInteger(n)&&n>=0&&n<=0xffffffff;
const positive=n=>count(n)&&n>0;
const ft=s=>typeof s==='string'&&/^[1-9]\d{0,19}$/.test(s)&&BigInt(s)<=18446744073709551615n;
const pointer=s=>typeof s==='string'&&/^(?:0|[1-9]\d{0,19})$/.test(s)&&BigInt(s)<=18446744073709551615n;
const guid=s=>typeof s==='string'&&/^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/.test(s);
function strictJson(text){let at=0;const ws=()=>{while(/\s/.test(text[at]||'x'))at++;};function value(depth){need(depth<=16,'json-depth');ws();if(text[at]==='{'){at++;ws();const keys=new Set();if(text[at]==='}'){at++;return;}for(;;){ws();const key=string();need(!keys.has(key),'json-duplicate');keys.add(key);ws();need(text[at++]===':','json-colon');value(depth+1);ws();const end=text[at++];if(end==='}')return;need(end===',','json-object');}}else if(text[at]==='['){at++;ws();if(text[at]===']'){at++;return;}let items=0;for(;;){need(++items<=4096,'json-array-bound');value(depth+1);ws();const end=text[at++];if(end===']')return;need(end===',','json-array');}}else if(text[at]==='"')string();else{const m=/^(?:true|false|null|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?)/.exec(text.slice(at));need(m,'json-scalar');at+=m[0].length;}}function string(){const m=/^"(?:[^"\\\u0000-\u001f]|\\(?:["\\/bfnrt]|u[0-9a-fA-F]{4}))*"/.exec(text.slice(at));need(m,'json-string');at+=m[0].length;return JSON.parse(m[0]);}value(0);ws();need(at===text.length,'json-trailing');return JSON.parse(text);}
function argv(raw){need(typeof raw==='string'&&raw.length>0&&raw.length<=32766&&!/[\x00-\x08\x0a-\x1f]/.test(raw),'argv-raw');let i=0,quoted=false,program='';while(/[ \t]/.test(raw[i]||'x'))i++;while(i<raw.length&&(quoted||!/[ \t]/.test(raw[i]))){if(raw[i]==='"')quoted=!quoted;else program+=raw[i];i++;}need(program&&!quoted,'argv-program');const result=[program];while(i<raw.length){while(/[ \t]/.test(raw[i]||'x'))i++;if(i===raw.length)break;let out='';quoted=false;while(i<raw.length&&(quoted||!/[ \t]/.test(raw[i]))){let slashes=0;while(raw[i]==='\\'){slashes++;i++;}if(raw[i]==='"'){out+='\\'.repeat(Math.floor(slashes/2));if(slashes%2){out+='"';i++;}else if(quoted&&raw[i+1]==='"'){out+='"';i+=2;}else{quoted=!quoted;i++;}}else{out+='\\'.repeat(slashes);if(i===raw.length||(!quoted&&/[ \t]/.test(raw[i])))break;out+=raw[i++];}}need(!quoted,'argv-unterminated');result.push(out);need(result.length<=4096,'argv-count');}return result;}
const quote=s=>'"'+s.replace(/(\\*)"/g,'$1$1\\"').replace(/(\\+)$/,'$1$1')+'"';
function image(file,pin){need(file&&Object.keys(file).length===6&&['machine','bytes','sha256','finalPath','fileIndex','volumeSerial'].every(k=>Object.hasOwn(file,k))&&count(file.machine)&&file.machine===34404&&typeof file.bytes==='string'&&/^[1-9]\d*$/.test(file.bytes)&&BigInt(file.bytes)===BigInt(pin.bytes)&&file.sha256===pin.sha256&&norm(file.finalPath)===norm(pin.path)&&ft(file.fileIndex)&&typeof file.volumeSerial==='string'&&/^(?:0|[1-9]\d{0,9})$/.test(file.volumeSerial)&&BigInt(file.volumeSerial)<=4294967295n,'physical-image');}
function analyze(raw,c,identity){
 const rootRole=c.schema==='soui.host64-build-debug-inputs/3'?'CMAKE':'POWERSHELL';
 need(Buffer.isBuffer(raw)&&raw.length>0&&raw.length<=67108864&&raw.at(-1)===10,'debug-raw-bound');need(guid(identity.nonce)&&positive(identity.observerPid)&&ft(identity.observerCreationFileTime),'outer-identity');
 const text=new TextDecoder('utf-8',{fatal:true}).decode(raw);const lines=text.slice(0,-1).split('\n');need(lines.length<=100000,'record-count');
 const fields={'observer-start':['observerPid','observerCreationFileTime','observerBits','outerJobPresent','sdkProof'],'build-created':['pid','creationFileTime','mainTid','assignedBeforeResume','breakawayAllowed'],'create-paused':['pid','creationFileTime','mainTid','role','buildRoot','inInnerJob','alive','wow64','imageFile'],'commandline-paused':['pid','creationFileTime','role','buildRoot','raw','wholeArgv','argv0File','argv0HeldBeforeAndAfter','provider','heldBeforeAndAfter','wmiThreadExited','providerCancellationProved'],'create-continued':['pid','creationFileTime','sameHeldProcess'],'ntdll-load':['pid','creationFileTime','moduleBase','moduleFile','peLayout'],'exception-disposition':['pid','creationFileTime','tid','code','address','firstChance','initialLoader','continueStatus'],'member-exit':['pid','creationFileTime','exitCode'],'rip':['pid','error'],'wmi-closeout-pending':['pid','passed','wmiThreadExited','providerCancellationProved'],'terminal':['passed','failure','failureCode','rootPid','rootCreationFileTime','rootExitCode','historicalMembers','totalProcesses','activeProcesses','limitTerminatedProcesses','allMembersSignaled','allPausedQueriesClosed','allLoaderSequences','debugProtocolValid','forced','queryCount','eventCount','sdkProof','normalSdkIntegrated','win7Proof']};
 const rows=lines.map((line,i)=>{need(Buffer.byteLength(line)<=262144&&!line.includes('\r'),'frame-bound');const r=strictJson(line);need(r.schema===schema&&r.nonce===identity.nonce&&r.inputSha256===identity.inputSha256&&/^[a-f0-9]{64}$/.test(identity.inputSha256||'')&&r.sequence===i+1&&typeof r.event==='string'&&fields[r.event],'raw-identity');const expected=['schema','sequence','nonce','inputSha256','event',...fields[r.event]];need(Object.keys(r).length===expected.length&&expected.every(k=>Object.hasOwn(r,k)),'exact-frame-fields');return r;});
 const first=rows[0],last=rows.at(-1);need(first.event==='observer-start'&&first.observerPid===identity.observerPid&&first.observerCreationFileTime===identity.observerCreationFileTime&&first.observerBits===64&&first.outerJobPresent===true&&first.sdkProof===false,'observer-start');
 need(last.event==='terminal'&&rows.filter(r=>r.event==='terminal').length===1&&last.sdkProof===false&&last.normalSdkIntegrated===false&&last.win7Proof===false&&typeof last.passed==='boolean','terminal');
 const ledger=new Map(),active=new Map(),tools={CLANG:0,RC:0,MT:0};let root=null,created=null,queries=0,secondChance=false,rip=false,pending=false;
 const lookup=r=>{const p=ledger.get(r.pid+':'+r.creationFileTime);need(p,'registered-history');return p;};
 const imageMap=new Map(c.images.map(p=>[p.role,p]));need(imageMap.size===12&&c.images.every(p=>p.machine===34404),'fixed-image-manifest');
 for(const r of rows.slice(1,-1)){
  if(r.event==='build-created'){need(!created&&positive(r.pid)&&ft(r.creationFileTime)&&positive(r.mainTid)&&r.assignedBeforeResume===true&&r.breakawayAllowed===false,'build-created');created=r;}
  else if(r.event==='create-paused'){
   need(created&&ledger.size<2048&&positive(r.pid)&&ft(r.creationFileTime)&&positive(r.mainTid)&&!active.has(r.pid)&&!ledger.has(r.pid+':'+r.creationFileTime)&&r.inInnerJob===true&&r.alive===true&&r.wow64===false&&typeof r.buildRoot==='boolean'&&imageMap.has(r.role),'create-identity');image(r.imageFile,imageMap.get(r.role));
   const p={...r,continued:false,query:false,loader:false,module:null,exit:null};ledger.set(r.pid+':'+r.creationFileTime,p);active.set(r.pid,p);
   if(r.buildRoot){need(!root&&r.pid===created.pid&&r.creationFileTime===created.creationFileTime&&r.mainTid===created.mainTid&&r.role===rootRole,'build-root');root=p;}
  }else if(r.event==='commandline-paused'){
   const p=lookup(r);need(!p.continued&&!p.query&&p.exit===null&&(p.buildRoot||['CLANG','RC','MT'].includes(p.role))&&r.role===p.role&&r.buildRoot===p.buildRoot&&r.provider==='Win32_Process.CommandLine'&&r.heldBeforeAndAfter===true&&r.wmiThreadExited===true&&r.providerCancellationProved===false&&Array.isArray(r.wholeArgv),'paused-query');
   const parsed=argv(r.raw);need(eq(parsed,r.wholeArgv)&&/^[a-z]:[\\/]/i.test(parsed[0])&&r.argv0HeldBeforeAndAfter===true,'whole-argv');image(r.argv0File,imageMap.get(p.role));need(r.argv0File.volumeSerial===p.imageFile.volumeSerial&&r.argv0File.fileIndex===p.imageFile.fileIndex,'argv0-create-physical-identity');if(p.buildRoot)need(eq(parsed,[imageMap.get(rootRole).path,...c.rootArguments])&&r.raw===[imageMap.get(rootRole).path,...c.rootArguments].map(quote).join(' '),'fixed-root-argv');
   p.query=true;need(++queries<=2048,'query-count');if(!p.buildRoot)tools[p.role]++;
  }else if(r.event==='create-continued'){const p=lookup(r);need(!p.continued&&p.exit===null&&r.sameHeldProcess===true&&(!(p.buildRoot||['CLANG','RC','MT'].includes(p.role))||p.query),'create-before-continue');p.continued=true;}
  else if(r.event==='ntdll-load'){const p=lookup(r);need(p.continued&&!p.module&&p.exit===null&&ft(r.moduleBase)&&eq(r.peLayout,c.modules[0].codeLayout),'ntdll-order');image(r.moduleFile,c.modules[0]);p.module=r;}
  else if(r.event==='exception-disposition'){
   const p=lookup(r);need(p.continued&&p.exit===null&&positive(r.tid)&&count(r.code)&&pointer(r.address)&&typeof r.firstChance==='boolean'&&typeof r.initialLoader==='boolean','exception-fields');
   if(r.initialLoader){need(!p.loader&&p.module&&r.firstChance&&r.tid===p.mainTid&&r.code===0x80000003&&r.continueStatus===0x10002,'initial-loader-once');const offset=BigInt(r.address)-BigInt(p.module.moduleBase);need(offset>=0n&&offset<BigInt(p.module.peLayout.sizeOfImage)&&p.module.peLayout.executableRanges.some(x=>offset>=BigInt(x.rva)&&offset<BigInt(x.rva+x.bytes)),'loader-executable-address');p.loader=true;}
   else {need(r.continueStatus===0x80010001,'exception-propagated');if(!r.firstChance)secondChance=true;}
  }else if(r.event==='member-exit'){const p=lookup(r);need(p.continued&&p.exit===null&&count(r.exitCode),'exit-order');p.exit=r.exitCode;active.delete(p.pid);}
  else if(r.event==='rip'){need(active.has(r.pid)&&count(r.error),'rip-fields');rip=true;}
  else if(r.event==='wmi-closeout-pending'){need(r.passed===false&&r.wmiThreadExited===false&&r.providerCancellationProved===false,'pending-query');pending=true;}
  else throw Error('unknown-debug-event');
 }
 const complete=last.passed===true;
 if(complete){need(created&&root&&ledger.size>0&&ledger.size===last.historicalMembers&&last.totalProcesses===ledger.size&&last.activeProcesses===0&&last.limitTerminatedProcesses===0&&last.rootPid===root.pid&&last.rootCreationFileTime===root.creationFileTime&&last.rootExitCode===0&&root.exit===0&&last.failure===''&&last.failureCode===0&&last.forced===false&&last.debugProtocolValid===true&&last.allMembersSignaled===true&&last.allPausedQueriesClosed===true&&last.allLoaderSequences===true&&last.queryCount===queries&&!secondChance&&!rip&&!pending&&active.size===0,'complete-native-lifecycle');for(const p of ledger.values())need(p.continued&&p.loader&&p.exit!==null&&(!(p.buildRoot||['CLANG','RC','MT'].includes(p.role))||p.query),'complete-member');need(count(last.eventCount)&&last.eventCount<=100000&&last.eventCount>=rows.filter(r=>['create-paused','ntdll-load','exception-disposition','member-exit','rip'].includes(r.event)).length,'event-budget');}
 return {schema:'soui.host64-build-debug-projection/2',nonce:identity.nonce,actualDebugRawSha256:crypto.createHash('sha256').update(raw).digest('hex'),actualDebugRawBytes:raw.length,experimentalLifecycleObserved:complete,outerObserver:{pid:first.observerPid,creationFileTime:first.observerCreationFileTime},innerBuild:root?{pid:root.pid,creationFileTime:root.creationFileTime}:null,historicalMembers:ledger.size,pausedQueryCount:queries,actualToolCounts:tools,mtObserved:tools.MT>0,allToolsZeroRequired:false,normalSdkAccepted:false,originalSemanticGatesReplaced:false,semanticGateIntegrationPending:true,sdkCompatibilityProved:false,win7Qualified:false,qualificationPassed:false,releaseReady:false};
}
function outerNatural(rows,ready,finished,context,observerPin,projection){need(Array.isArray(rows)&&rows.length>0&&ready&&finished&&context,'outer-proof');const a=rows[0],t=rows.at(-1);need(a.schema==='soui.owned-process-run/v1'&&a.event==='start'&&a.pid===projection.outerObserver.pid&&a.creationFileTime===projection.outerObserver.creationFileTime&&ready.pid===a.pid&&ready.creationFileTime===a.creationFileTime&&a.jobAssignedBeforeResume===true&&a.breakawayAllowed===false&&context.timeoutMs===900000&&context.drainMs===2000&&a.timeoutMs===900000&&a.drainMs===2000&&norm(context.executable)===norm(observerPin.path)&&context.executableSha256===observerPin.sha256,'outer-real-observer-root');need(finished.passed===true&&finished.exitCode===0&&finished.supervisorExitCode===0&&finished.signal===null&&finished.spawnError===null&&finished.reportError===null&&finished.stopRequested===false&&finished.fallbackUsed===false&&finished.closureConfirmed===true&&finished.pid===a.pid&&finished.creationFileTime===a.creationFileTime&&t.event==='terminal'&&t.reason==='completed'&&t.passed===true&&t.rootExitCode===0&&t.rootExited===true&&t.activeProcesses===0&&t.activeBeforeCleanup===0&&t.terminatedOwnedJob===false&&t.limitTerminatedProcesses===0&&t.closureConfirmed===true&&t.totalProcesses===projection.historicalMembers+1,'outer-natural-whole-history');return true;}

function observerPe(bytes){need(Buffer.isBuffer(bytes)&&bytes.length>=96&&bytes.length<=134217728,'observer-pe-size');need(bytes[0]===77&&bytes[1]===90,'observer-pe-mz');const at=bytes.readUInt32LE(60);need(at>=64&&at<=1048576&&at+24+112<=bytes.length&&bytes.readUInt32LE(at)===17744,'observer-pe-offset');const optional=at+24,optionalBytes=bytes.readUInt16LE(at+20);need(optionalBytes>=112&&optional+optionalBytes<=bytes.length&&bytes.readUInt16LE(at+4)===34404&&bytes.readUInt16LE(optional)===0x20b&&bytes.readUInt16LE(optional+68)===2,'headless-amd64-observer');const entry=bytes.readUInt32LE(optional+16),imageBytes=bytes.readUInt32LE(optional+56);need(entry>0&&entry<imageBytes,'positive-observer-entry-rva');return {machine:34404,subsystem:2,entryPointRva:entry};}
const winPath=(base,p)=>norm(path.win32.resolve(base,p));
function producerCommands(db,buildDirectory,cpp,object,executable,commandsRaw){
 need(Array.isArray(db)&&db.length>0&&db.length<=4096&&/^[a-z]:[\\/]/i.test(buildDirectory)&&winPath(buildDirectory,object).startsWith(norm(buildDirectory).replace(/\/$/,'')+'/')&&winPath(buildDirectory,executable).startsWith(norm(buildDirectory).replace(/\/$/,'')+'/'),'producer-database-scope');
 const rows=db.filter(e=>e&&typeof e.directory==='string'&&typeof e.file==='string'&&typeof e.output==='string'&&winPath(e.directory,e.file)===norm(cpp)&&winPath(e.directory,e.output)===norm(object));need(rows.length===1&&norm(rows[0].directory)===norm(buildDirectory),'single-actual-observer-database-entry');
 const row=rows[0],compile=Array.isArray(row.arguments)?row.arguments:argv(row.command);need(compile.length>2&&compile.every(x=>typeof x==='string')&&/^[a-z]:[\\/]/i.test(compile[0])&&compile.some(x=>x==='/c'||x==='-c')&&compile.some(x=>winPath(buildDirectory,x)===norm(cpp)),'observer-compile-command');
 let output=null;for(let i=1;i<compile.length;i++){if(compile[i].toLowerCase()==='/fo')output=compile[++i];else if(compile[i].slice(0,3).toLowerCase()==='/fo')output=compile[i].slice(3);}need(typeof output==='string'&&winPath(buildDirectory,output)===norm(object),'observer-object-output');
 need(typeof commandsRaw==='string'&&commandsRaw.length>0&&Buffer.byteLength(commandsRaw)<=1048576&&!commandsRaw.includes('\0'),'actual-ninja-command-output');
 const found=[];for(let line of commandsRaw.split(/\r?\n/)){if(!line.trim())continue;let raw=line.trim();const marker=raw.lastIndexOf(' -- ');if(marker>=0)raw=raw.slice(marker+4);const tail=raw.lastIndexOf(' && cd .');if(tail>=0)raw=raw.slice(0,tail);let tokens;try{tokens=argv(raw);}catch{continue;}
 const out=tokens.find(x=>x.slice(0,5).toLowerCase()==='/out:');if(out&&winPath(buildDirectory,out.slice(5))===norm(executable)){need(tokens.some(x=>x.toLowerCase()==='/subsystem:windows')&&tokens.some(x=>x.toLowerCase()==='/entry:wmaincrtstartup')&&tokens.some(x=>winPath(buildDirectory,x)===norm(object))&&/^[a-z]:[\\/]/i.test(tokens[0]),'actual-headless-link-command');found.push(tokens);}}
 need(found.length===1,'single-actual-observer-link-command');return {compileArgv:compile,linkArgv:found[0]};
}

// Portable adapters below read physical files. The functions above are pure
// parsers; neither importing this module nor its plan entry starts an actor.
const sourceNames=['benchmarks/protocol/host64-build-debug-observer.cpp',
 'benchmarks/protocol/host64-build-debug-observer/CMakeLists.txt',
 'tools/internal/Prepare-Host64BuildObserver.ps1','tools/internal/host64-build-debug-observer.cjs',
 'tools/internal/standard-build.cjs','tools/internal/llvm-build-observer.cjs','tools/internal/Observe-LlvmBuild.ps1',
 'third_party/include/json/json.hpp'];
const imageRoles=['POWERSHELL','CMAKE','NODE','CMD','NINJA','CLANG','LINKER','ARCHIVER','RC','MT','CMCLDEPS','CONHOST'];
const budgets=Object.freeze({outerMs:900000,innerMs:880000,drainMs:2000,queryMs:10000,closeMs:5000,members:2048,queries:2048,events:100000,reportBytes:67108864});
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const encode=v=>Buffer.from(JSON.stringify(v,null,2)+'\n');
const samePin=(a,b)=>!!a&&!!b&&norm(a.path)===norm(b.path)&&a.bytes===b.bytes&&a.sha256===b.sha256;
function pin(filename){
 const selected=require('./delivery-identity.cjs').safePath(path.resolve(filename));
 const held=fs.openSync(selected,'r');try{
  const before=fs.fstatSync(held,{bigint:true});need(before.isFile()&&before.size>=0n&&before.size<=134217728n,'bounded-physical-input');
  const bytes=fs.readFileSync(held),after=fs.fstatSync(held,{bigint:true});
  need(['dev','ino','size','mtimeNs','ctimeNs'].every(k=>before[k]===after[k])&&BigInt(bytes.length)===after.size,'physical-input-stability');
  return {path:selected,bytes:bytes.length,sha256:digest(bytes)};
 }finally{fs.closeSync(held);}
}
function match(p){need(p&&Number.isSafeInteger(p.bytes)&&p.bytes>=0&&/^[a-f0-9]{64}$/.test(p.sha256),'physical-pin-shape');const a=pin(p.path);need(samePin(a,p),'physical-pin-changed');return a;}
function read(p){const a=pin(p);need(a.bytes<=67108864,'bounded-json');return strictJson(fs.readFileSync(a.path,'utf8'));}
function write(folder,name,data){const p=path.join(folder,name);fs.writeFileSync(p,Buffer.isBuffer(data)?data:encode(data),{flag:'wx'});return pin(p);}
function sourcePins(root){return sourceNames.map(p=>pin(path.join(root,p)));}
function sourceSet(rows,root){return digest(encode(rows.map(p=>({path:path.relative(root,p.path).replaceAll('\\','/'),bytes:p.bytes,sha256:p.sha256}))));}
function peInfo(raw){
 need(Buffer.isBuffer(raw)&&raw.length>=96&&raw.length<=134217728&&raw[0]===77&&raw[1]===90,'bounded-pe');const at=raw.readUInt32LE(60);
 need(at>=64&&at<=1048576&&at+24+112<=raw.length&&raw.readUInt32LE(at)===17744,'pe-signature');const opt=at+24,size=raw.readUInt16LE(at+20),sections=raw.readUInt16LE(at+6);
 need(raw.readUInt16LE(at+4)===34404&&size>=112&&opt+size<=raw.length&&raw.readUInt16LE(opt)===0x20b&&sections>0&&sections<=96&&opt+size+sections*40<=raw.length,'amd64-pe32-plus');
 const sizeOfImage=raw.readUInt32LE(opt+56),ranges=[];for(let i=0;i<sections;i++){const p=opt+size+i*40,rva=raw.readUInt32LE(p+12),bytes=raw.readUInt32LE(p+8);if(raw.readUInt32LE(p+36)&0x20000000){need(bytes>0&&rva+bytes<=sizeOfImage,'pe-executable-range');ranges.push({rva,bytes});}}
 need(sizeOfImage>0&&ranges.length>0,'pe-code-layout');return {machine:34404,subsystem:raw.readUInt16LE(opt+68),entryPointRva:raw.readUInt32LE(opt+16),codeLayout:{sizeOfImage,executableRanges:ranges}};
}
function executable(p,role){const actual=match(p);peInfo(fs.readFileSync(actual.path));return {...actual,role,machine:34404};}
function physicalAlias(spelling,expected){
 require('./delivery-identity.cjs').safePath(path.resolve(spelling));let one,two;
 try{one=fs.openSync(spelling,'r');two=fs.openSync(expected.path,'r');const a=fs.fstatSync(one,{bigint:true}),b=fs.fstatSync(two,{bigint:true});
  need(a.isFile()&&b.isFile()&&a.ino>0n&&a.ino===b.ino&&a.dev===b.dev&&a.size===BigInt(expected.bytes),'producer-argv0-file-identity');
  const bytes=fs.readFileSync(one);need(digest(bytes)===expected.sha256&&peInfo(bytes).machine===34404,'producer-argv0-image-pin');
  const after=fs.fstatSync(one,{bigint:true});need(['dev','ino','size','mtimeNs','ctimeNs'].every(k=>a[k]===after[k]),'producer-argv0-post');
 }finally{if(one!==undefined)fs.closeSync(one);if(two!==undefined)fs.closeSync(two);}
}
function natural(raw,context,ready,result){
 const stream=require('./standard-build.cjs').verifyStream(raw,context,ready,result),t=stream.terminal;
 need(result.passed===true&&result.exitCode===0&&result.supervisorExitCode===0&&t.passed===true&&t.reason==='completed'&&t.rootExited===true&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob,'natural-whole-job');return stream;
}
function captureFiles(folder,name){return {raw:pin(path.join(folder,name+'.ndjson')),context:pin(path.join(folder,name+'.ndjson.context.json')),
 ready:pin(path.join(folder,name+'-ready.json')),finished:pin(path.join(folder,name+'-finished.json')),
 stdout:pin(path.join(folder,name+'-stdout.bin')),stderr:pin(path.join(folder,name+'-stderr.bin'))};}
function currentCapture(files){for(const p of Object.values(files))match(p);return {raw:fs.readFileSync(files.raw.path),context:read(files.context.path),ready:read(files.ready.path),result:read(files.finished.path)};}
// Private capability: public callers cannot opt into an intermediate receipt.
const preparationValidationPhase=Symbol('internal preparation validation input');
function validatePrepared(p,root,capability){
 need(p.schema==='soui.host64-build-observer-prepared/2'&&p.actualCompiled===true&&p.syntheticOnly===false&&p.hostArchitecture==='AMD64'&&p.profile==='LLVM22MSVC142'&&p.qualificationPassed===false&&p.releaseReady===false,'actual-prepared-role');
 const current=sourcePins(root);need(Array.isArray(p.sourceBefore)&&Array.isArray(p.sourceAfter)&&current.length===p.sourceBefore.length&&current.every((row,i)=>samePin(row,p.sourceBefore[i])&&samePin(row,p.sourceAfter[i]))&&p.sourceSetSha256===sourceSet(current,root),'prepared-current-source');
 for(const row of [...p.sourceBefore,...p.sourceAfter,p.profileRequest,p.compiler,p.linker,p.cmake,p.ninja,p.outerRunner,p.outerRunnerPreparation,p.executable,p.compileDatabase,p.object,p.ninjaCommands,p.producerCloseout,...p.dependencyPins])match(row);
 const closeout=read(p.producerCloseout.path),requiredPost=[...p.sourceBefore,p.profileRequest,p.cmake,p.outerRunner,...p.dependencyPins];
 const phase=capability===preparationValidationPhase?'validation-input':'validated';
 need(closeout.schema==='soui.host64-producer-closeout/2'&&closeout.phase===phase&&closeout.sourcePostCheckPassed===true&&closeout.failed===false&&Array.isArray(closeout.checks)&&closeout.checks.length===requiredPost.length&&
  closeout.checks.every((x,i)=>x.matched===true&&samePin(x.expected,x.actual)&&samePin(x.expected,requiredPost[i])),'producer-post-check-receipt');
 const profile=require('./llvm-profile-inputs.cjs').readRequest(p.profileRequest.path,'x64',p.devkitRoot).value.discovery;
 need(profile.InputIdentity===p.inputIdentity&&profile.BridgeIdentity===p.bridgeIdentity&&samePin(profile.ToolPins.CLANG,p.compiler)&&samePin(profile.ToolPins.LINKER,p.linker)&&samePin(profile.ToolPins.NINJA,p.ninja),'producer-profile-binding');
 const owned=require('./measurement-validation.cjs').preparedAt(p.devkitRoot,'x64',{toolchainRequest:p.profileRequest.path});
 need(norm(owned.tools.OwnedProcessRunner)===norm(p.outerRunner.path)&&samePin(pin(path.join(p.devkitRoot,'out/benchmark-protocol/measurement-tools.json')),p.outerRunnerPreparation),'producer-original-owned-preparation');
 peInfo(fs.readFileSync(p.outerRunner.path));const pe=observerPe(fs.readFileSync(p.executable.path));
 const object=fs.readFileSync(p.object.path);need(object.length>=20&&object.readUInt16LE(0)===34404,'actual-producer-object');
 const proof=producerCommands(read(p.compileDatabase.path),p.buildDirectory,path.join(root,sourceNames[0]),p.object.path,p.executable.path,fs.readFileSync(p.ninjaCommands.path,'utf8'));
 physicalAlias(proof.compileArgv[0],p.compiler);physicalAlias(proof.linkArgv[0],p.linker);
 need(p.subsystem===2&&p.entrySymbol==='wmainCRTStartup'&&p.entryPointRva===pe.entryPointRva&&p.captures.length===3,'headless-producer-proof');
 const purposes=new Set();for(const record of p.captures){need(['configure','build','commands'].includes(record.purpose)&&!purposes.has(record.purpose),'producer-purpose');purposes.add(record.purpose);const c=currentCapture(record.files);natural(c.raw,c.context,c.ready,c.result);need(c.context.runnerSha256===p.outerRunner.sha256&&norm(c.context.cwd)===(record.purpose==='commands'?norm(p.buildDirectory):norm(root)),'producer-owned-context');
  const args=record.purpose==='configure'?p.configureArguments:record.purpose==='build'?['--build',p.buildDirectory,'--config','Release','--target','Host64BuildDebugObserver']:['-f','build-Release.ninja','-t','commands','Host64BuildDebugObserver'];
  const tool=record.purpose==='commands'?p.ninja:p.cmake;need(norm(c.context.executable)===norm(tool.path)&&c.context.executableSha256===tool.sha256&&eq(c.context.args,args),'producer-complete-argv');
  if(record.purpose==='commands')need(samePin(record.files.stdout,p.ninjaCommands),'producer-command-capture-output');
 }
 const expected=configureArguments(root,p.buildDirectory,profile);need(eq(p.configureArguments,expected)&&norm(p.compileDatabase.path)===norm(path.join(p.buildDirectory,'compile_commands.json'))&&norm(p.executable.path)===norm(path.join(p.buildDirectory,'Release/Host64BuildDebugObserver.exe')),'producer-source-output-boundary');
 return p;
}
function loadPrepared(filename,sha256,root){const p=pin(filename);need(p.sha256===sha256,'prepared-receipt-pin');return {pin:p,value:validatePrepared(read(p.path),root)};}
function configureArguments(root,build,profile){return ['-S',path.join(root,'benchmarks/protocol/host64-build-debug-observer'),'-B',build,'-G','Ninja Multi-Config',
 '-DCMAKE_TOOLCHAIN_FILE='+path.join(root,'cmake/toolchains/SouiLLVM22.cmake'),...Object.entries(profile.CMakeDefinitions).map(([k,v])=>'-D'+k+'='+v)];}
function producerObjectEntry(database,build,cpp){
 need(Array.isArray(database)&&database.length>0&&database.length<=4096,'producer-database-count');
 const rows=database.filter(x=>x&&typeof x.file==='string'&&typeof x.output==='string'&&norm(path.win32.resolve(x.directory,x.file))===norm(cpp)&&
  norm(path.win32.resolve(x.directory,x.output)).startsWith(norm(build).replace(/\/$/,'')+'/cmakefiles/host64builddebugobserver.dir/release/'));
 need(rows.length===1,'single-release-producer-source');return rows[0];
}
function preparePlan(root,profileRequest,devkitRoot,cmake,folder){
 root=require('./delivery-identity.cjs').safePath(path.resolve(root));need(norm(root)===norm(path.join(__dirname,'../..')),'producer-must-belong-to-sdk');
 const request=require('./llvm-profile-inputs.cjs').readRequest(profileRequest,'x64',devkitRoot),rows=sourcePins(root),cmakePin=pin(cmake);executable(cmakePin,'CMAKE');
 need(path.isAbsolute(folder)&&!fs.existsSync(folder)&&!norm(folder).startsWith(norm(path.join(root,'sdk'))+'/'),'fresh-producer-directory');
 return {schema:'soui.host64-build-observer-plan/2',root,devkitRoot:path.resolve(devkitRoot),profileRequest:request.pin,discovery:request.value.discovery,cmake:cmakePin,
  buildDirectory:path.join(folder,'native'),recordDirectory:folder,sourceBefore:rows,sourceSetSha256:sourceSet(rows,root),actualCompiled:null,executable:null,qualificationPassed:false,releaseReady:false};
}
async function prepare(plan){
 need(process.platform==='win32'&&plan.actualCompiled===null&&!fs.existsSync(plan.recordDirectory),'explicit-fresh-preparation');
 const dependencies=[],postPins=[],records=[],cleanupErrors=[];
 let attemptCreated=false,failure=null,value=null,finalCloseoutPin=null,resultPin=null;
 const finishAttempt=()=>{
  let checked={passed:false,rows:[]};
  try{checked=checkPostPins(postPins);if(!checked.passed&&!failure)failure=Error('producer-input-post-check');}
  catch(error){cleanupErrors.push(failureInfo(error));if(!failure)failure=error;}
  if(failure){
   const failed=()=>({schema:'soui.host64-producer-closeout/2',phase:'failed',sourcePostCheckPassed:checked.passed,checks:checked.rows,captures:records,
    failed:true,primaryFailure:failureInfo(failure),cleanupErrors:[...cleanupErrors],qualificationPassed:false,releaseReady:false});
   try{
    if(finalCloseoutPin){
     // Only this attempt's unsealed record may change. A mismatched pin is
     // never overwritten; the failure then gets its own fresh sidecar.
     match(finalCloseoutPin);fs.writeFileSync(finalCloseoutPin.path,encode(failed()),{flag:'w'});
    }else write(plan.recordDirectory,'producer-closeout.json',failed());
   }catch(error){
    cleanupErrors.push(failureInfo(error));
    try{write(plan.recordDirectory,'producer-failed-closeout.json',failed());}
    catch(finalError){cleanupErrors.push(failureInfo(finalError));}
   }
  }
 };
 try{
 fs.mkdirSync(plan.recordDirectory,{recursive:false});attemptCreated=true;
 postPins.push(...plan.sourceBefore,plan.profileRequest,plan.cmake);
 write(plan.recordDirectory,'plan.json',plan);
 const standard=require('./standard-build.cjs'),llvm=require('./llvm-build-observer.cjs'),p=plan.discovery;
 const owned=require('./measurement-validation.cjs').preparedAt(plan.devkitRoot,'x64',{toolchainRequest:plan.profileRequest.path});
 const runner=pin(owned.tools.OwnedProcessRunner);postPins.push(runner);peInfo(fs.readFileSync(runner.path));
 dependencies.push(...p.RequiredInputPins,...p.BridgePins,...Object.values(p.ToolPins));postPins.push(...dependencies);
 for(const file of [path.join(plan.root,'cmake/toolchains/SouiLLVM22.cmake'),process.execPath]){const frozen=pin(file);dependencies.push(frozen);postPins.push(frozen);}
 dependencies.forEach(match);
 const env=llvm.cleanEnvironment(process.env,p.ToolPins.CMD.path),args=configureArguments(plan.root,plan.buildDirectory,p);
 for(const [purpose,tool,vector,cwd] of [['configure',plan.cmake.path,args,plan.root],['build',plan.cmake.path,['--build',plan.buildDirectory,'--config','Release','--target','Host64BuildDebugObserver'],plan.root],['commands',p.ToolPins.NINJA.path,['-f','build-Release.ninja','-t','commands','Host64BuildDebugObserver'],plan.buildDirectory]]){
  const record={purpose,files:null};records.push(record);
  const capture=await standard.capture(plan.recordDirectory,purpose,tool,vector,{runner:runner.path,cwd,timeoutMs:900000,drainMs:2000,env});
  record.files=captureFiles(plan.recordDirectory,purpose);natural(capture.rawReport,strictJson(capture.contextBytes.toString('utf8')),capture.ready,capture.result);
 }
 const db=pin(path.join(plan.buildDirectory,'compile_commands.json')),entry=producerObjectEntry(read(db.path),plan.buildDirectory,path.join(plan.root,sourceNames[0]));
 const object=pin(path.win32.resolve(entry.directory,entry.output)),exe=pin(path.join(plan.buildDirectory,'Release/Host64BuildDebugObserver.exe')),pe=observerPe(fs.readFileSync(exe.path));
 value={schema:'soui.host64-build-observer-prepared/2',actualCompiled:true,syntheticOnly:false,hostArchitecture:'AMD64',profile:'LLVM22MSVC142',devkitRoot:plan.devkitRoot,
  sourceBefore:plan.sourceBefore,sourceAfter:sourcePins(plan.root),sourceSetSha256:plan.sourceSetSha256,profileRequest:plan.profileRequest,inputIdentity:p.InputIdentity,bridgeIdentity:p.BridgeIdentity,
  compiler:p.ToolPins.CLANG,linker:p.ToolPins.LINKER,cmake:plan.cmake,ninja:p.ToolPins.NINJA,outerRunner:runner,outerRunnerPreparation:pin(path.join(plan.devkitRoot,'out/benchmark-protocol/measurement-tools.json')),
  executable:exe,buildDirectory:plan.buildDirectory,compileDatabase:db,object,ninjaCommands:records[2].files.stdout,configureArguments:args,captures:records,dependencyPins:dependencies,
  subsystem:2,entrySymbol:'wmainCRTStartup',entryPointRva:pe.entryPointRva,qualificationPassed:false,releaseReady:false};
 const checked=checkPostPins(postPins);need(checked.passed,'producer-input-post-check');
 // This immutable validation input is not the final attempt closeout and
 // cannot publish prepared.json. The complete existing validator still runs.
 value.producerCloseout=write(plan.recordDirectory,'producer-validation-closeout.json',{
  schema:'soui.host64-producer-closeout/2',phase:'validation-input',sourcePostCheckPassed:true,checks:checked.rows,captures:records,
  failed:false,qualificationPassed:false,releaseReady:false});
 validatePrepared(value,plan.root,preparationValidationPhase);
 finalCloseoutPin=write(plan.recordDirectory,'producer-closeout.json',{
  schema:'soui.host64-producer-closeout/2',phase:'validated',sourcePostCheckPassed:true,checks:checked.rows,captures:records,
  failed:false,primaryFailure:null,cleanupErrors:[],qualificationPassed:false,releaseReady:false});
 value.producerCloseout=finalCloseoutPin;validatePrepared(value,plan.root);
 }catch(error){failure=error;}finally{
  if(attemptCreated)finishAttempt();
 }
 // A failed final postcheck has never published a consumable preparation.
 // Publication itself is guarded by the identical failure/postcheck policy.
 if(!failure)try{resultPin=write(plan.recordDirectory,'prepared.json',value);}
 catch(error){failure=error;finishAttempt();}
 if(failure){
  // Never mask the original rejection with a postcheck, pin or sink error.
  // Secondary failures remain on the same Error even if every sink failed.
  if(cleanupErrors.length&&failure&&typeof failure==='object')try{Object.defineProperty(failure,'closeoutErrors',{value:cleanupErrors,configurable:true});}catch{}
  throw failure;
 }
 return resultPin;
}
function failureInfo(error){return {name:typeof error?.name==='string'?error.name:'Error',message:typeof error?.message==='string'?error.message:String(error)};}
function checkPostPins(pins){let passed=true;const rows=[];for(const p of pins){try{rows.push({expected:p,actual:match(p),matched:true});}catch{passed=false;rows.push({expected:p,actual:null,matched:false});}}return {passed,rows};}
function validateInput(c,rootArguments){
 const keys=['schema','nonce','root','rootArguments','images','modules','controlPins','report','profile','sdkArchitecture','configuration','sourceSetSha256','producer','outerRunner','budgets'];
 const consumer=c?.schema==='soui.host64-build-debug-inputs/3';if(consumer)keys.push('consumerKind','buildDirectory');
 need(c&&keys.every(k=>Object.hasOwn(c,k))&&Object.keys(c).every(k=>keys.includes(k)||k==='inputPin'),'debug-exact-input-fields');
 need((consumer||c.schema==='soui.host64-build-debug-inputs/2')&&guid(c.nonce)&&['x64','x86'].includes(c.sdkArchitecture)&&['Release','Debug'].includes(c.configuration)&&c.profile==='LLVM22MSVC142'&&eq(c.budgets,budgets)&&/^[a-f0-9]{64}$/.test(c.sourceSetSha256),'debug-input-scope');
 need(typeof c.root==='string'&&/^[a-z]:[\\/]/i.test(c.root)&&typeof c.report==='string'&&path.win32.basename(c.report)==='host64-debug.ndjson'&&/^[a-z]:[\\/]/i.test(c.report),'debug-local-paths');
 need(Array.isArray(c.rootArguments)&&c.rootArguments.every(x=>typeof x==='string')&&eq(c.rootArguments,rootArguments),'debug-expected-root-vector');
 if(consumer){
  need(['acceptance','release'].includes(c.consumerKind)&&typeof c.buildDirectory==='string'&&/^[a-z]:[\\/]/i.test(c.buildDirectory)&&!c.buildDirectory.split(/[\\/]/).some(x=>x==='.'||x==='..'),'debug-consumer-scope');
  const suite=path.win32.dirname(c.buildDirectory),parent=path.win32.join(c.root,c.consumerKind==='acceptance'?'out/acceptance':'out/release-validation/'+c.sdkArchitecture);
  need(path.win32.basename(c.buildDirectory)==='consumer'&&norm(path.win32.dirname(suite))===norm(parent)&&(c.consumerKind==='acceptance'?guid(path.win32.basename(suite)):/^[a-f0-9]{32}$/.test(path.win32.basename(suite))),'debug-consumer-owned-directory');
  need(eq(c.rootArguments,['--build',c.buildDirectory,'--config',c.configuration,'--parallel','2','--verbose']),'debug-fixed-consumer-vector');
 }else{
 need(eq(c.rootArguments,rootArguments)&&norm(c.rootArguments[5])===norm(path.win32.join(c.root,'tools/Build.ps1')),'debug-fixed-root-vector');
 need(eq(c.rootArguments.slice(0,5),['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File'])&&c.rootArguments[6]==='-Architecture'&&c.rootArguments[7]===c.sdkArchitecture&&c.rootArguments[8]==='-Configuration'&&c.rootArguments[9]===c.configuration,'debug-build-target-vector');
 }
 need(c.images.length===12&&c.images.every((x,i)=>x.role===imageRoles[i]&&x.machine===34404&&positive(x.bytes)&&/^[a-f0-9]{64}$/.test(x.sha256))&&c.modules.length===1&&c.modules[0].role==='NTDLL'&&c.modules[0].machine===34404,'debug-fixed-image-slots');
 need(c.controlPins.length>0&&c.controlPins.length<=128&&new Set(c.controlPins.map(x=>norm(x.path))).size===c.controlPins.length,'debug-control-members');return c;
}
function makeInput(root,v,folder,profile,tools,controlPins,prepared,args,consumer=null){
 const byRole=Object.fromEntries(tools.map(x=>[x.role,x]));const selected={POWERSHELL:profile.ToolPins.POWERSHELL,CMAKE:byRole.cmake,NODE:byRole.node,CMD:profile.ToolPins.CMD,
  NINJA:profile.ToolPins.NINJA,CLANG:profile.ToolPins.CLANG,LINKER:profile.ToolPins.LINKER,ARCHIVER:profile.ToolPins.ARCHIVER,RC:profile.ToolPins.RC,MT:profile.ToolPins.MT,CMCLDEPS:byRole.resourceWrapper,
  CONHOST:pin(path.join(process.env.SystemRoot,'System32/conhost.exe'))};
 const ntdll=pin(path.join(process.env.SystemRoot,'System32/ntdll.dll')),module={...executable(ntdll,'NTDLL'),codeLayout:peInfo(fs.readFileSync(ntdll.path)).codeLayout};
 const current=sourcePins(root);const unique=new Map([...controlPins,...current,prepared.pin,prepared.value.executable].map(x=>[norm(x.path),{path:x.path,bytes:x.bytes,sha256:x.sha256}]));
 const c={schema:'soui.host64-build-debug-inputs/2',nonce:crypto.randomUUID(),root,rootArguments:args,images:imageRoles.map(role=>executable(selected[role],role)),modules:[module],controlPins:[...unique.values()],
  report:path.join(folder,'host64-debug.ndjson'),profile:'LLVM22MSVC142',sdkArchitecture:v.architecture,configuration:v.configuration,sourceSetSha256:sourceSet(current,root),producer:prepared.pin,outerRunner:prepared.value.outerRunner,budgets};
 if(consumer){need(Object.keys(consumer).sort().join(',')==='buildDirectory,kind','debug-consumer-plan-fields');Object.assign(c,{schema:'soui.host64-build-debug-inputs/3',consumerKind:consumer.kind,buildDirectory:consumer.buildDirectory});}
 validateInput(c,args);return {value:c,pin:write(folder,'host64-inputs.json',c)};
}
function validateEvidence(raw,c,capture,observer,expectedArgs){
 validateInput(c,expectedArgs);const ctx=strictJson(capture.contextBytes.toString('utf8')),s=natural(capture.rawReport,ctx,capture.ready,capture.result);
 const projection=analyze(raw,c,{nonce:c.nonce,inputSha256:c.inputPin.sha256,observerPid:s.start.pid,observerCreationFileTime:s.start.creationFileTime});
 outerNatural(capture.rawReport.toString('utf8').trimEnd().split('\n').map(strictJson),capture.ready,capture.result,ctx,observer,projection);
 need(eq(ctx.args,['--input',c.inputPin.path,'--input-sha256',c.inputPin.sha256,'--report',c.report,'--nonce',c.nonce])&&ctx.runnerSha256===c.outerRunner.sha256&&norm(ctx.cwd)===norm(c.root)&&projection.experimentalLifecycleObserved,'debug-actual-outer-context');
 const rows=raw.toString('utf8').trimEnd().split('\n').map(strictJson),created=rows.filter(x=>x.event==='create-paused'),queries=rows.filter(x=>x.event==='commandline-paused');
 return {...projection,actualInput:c.inputPin,originalPollingUnmodified:true,created,queries};
}
async function captureBuild(root,v,folder,profile,tools,pins,prepared,args,env,api,planned){
 const frozen=planned||makeInput(root,v,folder,profile,tools,pins,prepared,args),c=frozen.value;
 const vector=['--input',frozen.pin.path,'--input-sha256',frozen.pin.sha256,'--report',c.report,'--nonce',c.nonce];
 let capture=null,failure=null;
 try{
  capture=await api.capture(folder,'build',prepared.value.executable.path,vector,{runner:prepared.value.outerRunner.path,cwd:root,timeoutMs:900000,drainMs:2000,env});
  const input={...c,inputPin:frozen.pin};const proof=validateEvidence(fs.readFileSync(c.report),input,capture,prepared.value.executable,args);
  return {capture,input:frozen.pin,raw:pin(c.report),prepared:prepared.pin,proof};
 }catch(error){failure=error;throw error;}finally{
  const checked=checkPostPins([...c.controlPins,...c.images,...c.modules,c.outerRunner,prepared.pin,frozen.pin]);
  write(folder,'host64-closeout.json',{schema:'soui.host64-build-closeout/2',nonce:c.nonce,sourcePostCheckPassed:checked.passed,checks:checked.rows,
   outerClosureConfirmed:capture?.result.closureConfirmed===true,failed:failure!==null||!checked.passed,qualificationPassed:false,releaseReady:false});
  need(checked.passed,'debug-input-post-check');
 }
}
function recordedEvidence(binding,capture,root,args){
 for(const p of [binding.input,binding.raw,binding.prepared])match(p);const input=read(binding.input.path),prepared=loadPrepared(binding.prepared.path,binding.prepared.sha256,root);
 need(sourceSet(sourcePins(root),root)===input.sourceSetSha256&&samePin(binding.observer,prepared.value.executable)&&samePin(input.outerRunner,prepared.value.outerRunner)&&samePin(input.producer,prepared.pin),'debug-current-source-producer');for(const p of [...input.controlPins,...input.images,...input.modules,input.outerRunner])match(p);
 return validateEvidence(fs.readFileSync(binding.raw.path),{...input,inputPin:binding.input},capture,prepared.value.executable,args);
}
module.exports={analyze,outerNatural,strictJson,argv,quote,norm,schema,observerPe,producerCommands,sourceNames,budgets,peInfo,
 validateInput,validateEvidence,validatePrepared,loadPrepared,preparePlan,prepare,makeInput,captureBuild,recordedEvidence,pin,match,sourcePins,producerObjectEntry};
if(require.main===module){(async()=>{
 const [mode,root,request,devkit,cmake,folder]=process.argv.slice(2);need(['plan','prepare'].includes(mode)&&process.argv.length===8,'Use plan|prepare ROOT HOST_X64_PROFILE_REQUEST DEVKIT CMAKE FRESH_DIRECTORY');
 const plan=preparePlan(root,request,devkit,cmake,folder);const output=mode==='prepare'?await prepare(plan):plan;process.stdout.write(JSON.stringify(output,null,2)+'\n');
})().catch(e=>{process.stderr.write(e.message+'\n');process.exitCode=1;});}
