'use strict';
// Component reports remain original producer records. This reader links their
// actual capture and DLLs to the accepted application build and desktop smoke.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isDeepStrictEqual}=require('node:util');
const {isUtf8}=require('node:buffer');
const id=require('./delivery-identity.cjs');
const {parseStrictJson}=require('./owned-tail-evidence.cjs');
const {validateStage}=require('./devkit-command.cjs');
const standard=require('./standard-build.cjs');
const evidence=require('./build-evidence.cjs');
const kinds=['miniblink','soui','utilities','render-gdi','imgdecoder-gdip','soui-sys-resource'];
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function need(ok,message){if(!ok)throw new Error(message);}
function samePath(a,b){return typeof a==='string'&&typeof b==='string'&&path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();}
function fileBytes(file,max=536870912){const safe=id.safePath(file),stat=fs.statSync(safe);need(stat.isFile()&&stat.size<=max,'Missing or oversized dependency input');return fs.readFileSync(safe);}
function pin(file){
 const safe=id.safePath(file),fd=fs.openSync(safe,'r');
 try{
  const before=fs.fstatSync(fd,{bigint:true});
  need(before.isFile()&&before.size<=BigInt(Number.MAX_SAFE_INTEGER),'Dependency pin requires a regular, safely sized file');
  const hash=crypto.createHash('sha256'),chunk=Buffer.allocUnsafe(1024*1024);let bytes=0;
  for(;;){const count=fs.readSync(fd,chunk,0,chunk.length,null);if(count===0)break;hash.update(chunk.subarray(0,count));bytes+=count;need(BigInt(bytes)<=before.size,'Dependency file grew while hashing');}
  const after=fs.fstatSync(fd,{bigint:true}),current=fs.statSync(safe,{bigint:true});
  need(BigInt(bytes)===before.size&&['dev','ino','size','mtimeNs','ctimeNs'].every(key=>before[key]===after[key]&&after[key]===current[key]),'Dependency file changed while hashing');
  return {path:safe,bytes,sha256:hash.digest('hex')};
 }finally{fs.closeSync(fd);}
}
function checkPin(expected){
 need(expected&&typeof expected.path==='string'&&path.isAbsolute(expected.path)&&Number.isSafeInteger(expected.bytes)&&expected.bytes>=0&&/^[a-f0-9]{64}$/.test(expected.sha256),'Invalid dependency file pin');
 const actual=pin(expected.path);need(actual.bytes===expected.bytes&&actual.sha256===expected.sha256,'Dependency input changed: '+expected.path);return actual;
}
function read(file,max=67108864){const bytes=fileBytes(file,max);need(isUtf8(bytes),'Dependency JSON is not UTF-8');return parseStrictJson(bytes.toString('utf8').replace(/^\uFEFF/,''));}
function pinnedJson(value){checkPin(value);return read(value.path);}
function verifyInputPins(root,inputs,archiveFile){
 need(Array.isArray(inputs?.inputPins)&&inputs.inputPins.length>0,'Missing component input pins');
 if(archiveFile==null){inputs.inputPins.forEach(checkPin);return null;}
 need(typeof archiveFile==='string'&&path.isAbsolute(archiveFile),'Absolute input archive required');
 const manifestPin=pin(archiveFile),archive=pinnedJson(manifestPin);
 need(archive&&isDeepStrictEqual(Object.keys(archive).sort(),['files','producerRoot','schema'])&&
  archive.schema==='soui.dependency-input-archive/v1'&&samePath(archive.producerRoot,root)&&
  Array.isArray(archive.files)&&archive.files.length>0&&archive.files.length<=inputs.inputPins.length,'Invalid producer input archive');
 const key=file=>path.resolve(file).toLowerCase(),entries=new Map(),files=[],used=new Set(),archivePaths=new Set();
 const liveTools=[...Object.values(inputs.discovery?.ToolPins||{}),...(inputs.discovery?.RequiredInputPins||[])];
 for(const row of archive.files){
  need(row&&isDeepStrictEqual(Object.keys(row).sort(),['file','original'])&&row.original&&
   isDeepStrictEqual(Object.keys(row.original).sort(),['bytes','path','sha256']),'Invalid archived input record');
  const original=row.original;
  need(typeof original.path==='string'&&path.isAbsolute(original.path),'Absolute original script path required');
  const relative=path.relative(path.join(root,'tools'),original.path).replaceAll('\\','/');
  need(relative&&!path.isAbsolute(relative)&&!relative.split('/').includes('..')&&
   /\.(ps1|cjs|mjs|js|json|cmd|bat|targets|props|cmake)$/i.test(relative),'Input archive is limited to producer scripts under tools');
  need(!liveTools.some(p=>samePath(p.path,original.path)),'Toolchain inputs must remain live physical inputs');
  const matching=inputs.inputPins.filter(p=>samePath(p.path,original.path));
  need(matching.length>0&&matching.every(p=>p.bytes===original.bytes&&p.sha256===original.sha256),'Archived script does not match original input pin');
  const name=key(original.path);need(!entries.has(name),'Duplicate archived input');
  need(typeof row.file==='string'&&row.file&&!path.isAbsolute(row.file),'Relative archive member required');
  const retained=id.safePath(path.dirname(manifestPin.path),row.file),retainedKey=key(retained);
  need(!samePath(retained,manifestPin.path)&&!archivePaths.has(retainedKey),'Duplicate or self-referencing archive member');archivePaths.add(retainedKey);
  const archived=checkPin({...original,path:retained});entries.set(name,{original,archived});
 }
 for(const expected of inputs.inputPins){
  const name=key(expected.path),entry=entries.get(name);
  if(entry){if(!used.has(name)){used.add(name);files.push(entry);}}
  else checkPin(expected);
 }
 need(used.size===entries.size,'Unused archive records');checkPin(manifestPin);
 return {manifest:manifestPin,files};
}
function selection(value,arch){
 need(['x86','x64'].includes(arch)&&value&&isDeepStrictEqual(Object.keys(value).sort(),['architecture','kernel','profile','runner','schema','soui'])&&
  ['soui.dependency-selection/v1','soui.dependency-selection/v2','soui.dependency-selection/v3'].includes(value.schema)&&value.profile==='LLVM22MSVC142'&&value.architecture===arch,'Invalid explicit dependency selection');
 need(typeof value.runner==='string'&&path.isAbsolute(value.runner),'Explicit actual component runner required');
 for(const component of ['kernel','soui']){
  const row=value[component],archived=value.schema==='soui.dependency-selection/v3';
  const keys=archived?['capture','inputArchive','producerRoot','report']:value.schema==='soui.dependency-selection/v2'?['capture','producerRoot','report']:['capture','report'];
  need(row&&isDeepStrictEqual(Object.keys(row).sort(),keys),'Missing component selection');
  for(const name of keys){
   if(name==='inputArchive'&&row[name]===null)continue;
   need(typeof row[name]==='string'&&path.isAbsolute(row[name])&&!/[\x00-\x1f]/.test(row[name]),'Absolute component report/capture/producer root/archive required');
  }
 }
 need(!samePath(value.kernel.report,value.soui.report)&&!samePath(value.kernel.capture,value.soui.capture),'Components cannot share a report or capture');return value;
}
function snapshot(source){
 source=id.safePath(source);const files=[],directories=[];
 function walk(relative){for(const entry of fs.readdirSync(path.join(source,relative),{withFileTypes:true})){
  const name=relative?relative+'/'+entry.name:entry.name,full=id.safePath(source,name);
  if(entry.isDirectory()){directories.push(name);walk(name);}else{need(entry.isFile(),'Source contains a non-file');const p=pin(full);files.push({path:name,bytes:p.bytes,sha256:p.sha256});}
 }}walk('');files.sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);directories.sort();
 const lines=[...directories.map(n=>'D|'+n),...files.map(f=>'F|'+f.path+'|'+f.bytes+'|'+f.sha256)];
 return {schema:'soui.kernel-source-files/v1',root:source.replaceAll('\\','/'),files,directories,fileCount:files.length,directoryCount:directories.length,
  totalBytes:files.reduce((sum,f)=>sum+f.bytes,0),identity:sha(Buffer.from(lines.join('\n')))};
}
function verifySource(receipt,source){
 need(receipt?.schema==='soui.kernel-source-files/v1'&&samePath(receipt.root,source),'Dependency source owner differs');
 const actual=snapshot(source);need(isDeepStrictEqual({...receipt,root:actual.root},actual),'Dependency source roster or physical bytes differ');return actual;
}
function pe(bytes,arch){
 need(Buffer.isBuffer(bytes)&&bytes.length>=64&&bytes.readUInt16LE(0)===0x5a4d,'Dependency is not a DLL PE');
 const p=bytes.readUInt32LE(60);need(p>=64&&p<=bytes.length-26&&bytes.readUInt32LE(p)===0x4550,'Incomplete dependency PE header');
 const optional=bytes.readUInt16LE(p+20),sections=bytes.readUInt16LE(p+6),wide=arch==='x64';
 need(['x86','x64'].includes(arch)&&bytes.readUInt16LE(p+4)===(wide?0x8664:0x14c)&&(bytes.readUInt16LE(p+22)&0x2000)!==0&&
  optional>=(wide?112:96)&&p+24+optional<=bytes.length&&bytes.readUInt16LE(p+24)===(wide?0x20b:0x10b)&&sections>0&&
  p+24+optional+sections*40<=bytes.length,'Dependency DLL architecture or image extent differs');
}
function invocation(context,root,arch,component,report,inputs){
 const script=component==='kernel'?'Build-Dependencies.ps1':'Build-SouiLlvm.ps1';
 need(samePath(context.cwd,root)&&['pwsh.exe','powershell.exe'].includes(path.basename(context.executable).toLowerCase()),'Component capture owner differs');
 const args=context.args;need(Array.isArray(args),'Missing actual component arguments');const i=args.indexOf('-File');
 need(i>=0&&samePath(args[i+1],path.join(root,'tools',script)),'Capture did not run the selected public component entry');
 const pre=args.slice(0,i);need(new Set(pre).size===pre.length&&pre.includes('-NoProfile')&&pre.includes('-NonInteractive')&&pre.every(x=>['-NoProfile','-NonInteractive','-NoLogo'].includes(x)),'Unknown component PowerShell setup');
 const values=new Map(),allowed=['-Architecture','-NativeToolchain','-OutputRoot','-LlvmRoot','-MsvcRoot','-WindowsSdkRoot','-NinjaExe','-WindowsCmdExe','-PowerShellExe','-VsInstallDir',component==='kernel'?'-Component':'-NodeExe'];
 for(let n=i+2;n<args.length;n+=2){need(allowed.includes(args[n])&&!values.has(args[n])&&typeof args[n+1]==='string','Unknown/repeated component argument');values.set(args[n],args[n+1]);}
 need(values.size===allowed.length&&values.get('-Architecture')===arch&&values.get('-NativeToolchain')==='LLVM22MSVC142'&&
  samePath(values.get('-OutputRoot'),path.dirname(report))&&(component!=='kernel'||values.get('-Component')==='Miniblink'),'Component build selection differs');
 const pins=inputs.inputPins;need(Array.isArray(pins)&&pins.length>10,'Missing original component input pins');
 need(pins.some(p=>samePath(p.path,path.join(root,'tools',script))),'Component producer is not pinned');
 const d=inputs.discovery;
 const roles={'-NinjaExe':'NINJA','-WindowsCmdExe':'CMD','-PowerShellExe':'POWERSHELL'};
 for(const [flag,role] of Object.entries(roles))need(samePath(values.get(flag),d.ToolPins[role]?.path),'Selected tool differs: '+flag);
 need(samePath(values.get('-LlvmRoot'),path.resolve(d.ToolPins.CLANG.path,'../..'))&&samePath(values.get('-MsvcRoot'),inputs.vsOwner.msvcRoot)&&
  samePath(values.get('-VsInstallDir'),inputs.vsOwner.vsInstallDir)&&samePath(context.executable,d.ToolPins.POWERSHELL.path),'Component tool roots differ');
 need(samePath(values.get('-WindowsSdkRoot'),path.resolve(d.ToolPins.RC.path,'../../../..')),'Component Windows SDK root differs');
 if(component==='soui')need(pins.some(p=>samePath(p.path,values.get('-NodeExe'))),'SOUI stage interpreter is not pinned');
 return values;
}
function capture(root,arch,component,choice,inputs,runner){
 const base=id.safePath(choice.capture),contextBytes=fileBytes(base+'.ndjson.context.json',1048576),context=parseStrictJson(contextBytes.toString('utf8'));
 invocation(context,root,arch,component,choice.report,inputs);
 const raw=fileBytes(base+'.ndjson',16777216),ready=read(base+'-ready.json'),finished=read(base+'-finished.json');
 const expected={executable:context.executable,cwd:context.cwd,args:context.args,timeoutMs:component==='kernel'?14400000:1800000,drainMs:2000,
  executableSha256:pin(context.executable).sha256,runnerSha256:pin(runner).sha256,contextSha256:sha(contextBytes)};
 // Both readers bind the native terminal record and exact pre-resume process identity.
 standard.verifyStream(raw,context,ready,finished);validateStage(raw,contextBytes,ready,finished,expected);
 return {context:pin(base+'.ndjson.context.json'),raw:pin(base+'.ndjson'),ready:pin(base+'-ready.json'),finished:pin(base+'-finished.json')};
}
function verifyComponent(root,arch,component,choice,runner){
 const report=read(choice.report),isKernel=component==='kernel',ext=report?.[isKernel?'kernelLlvm':'souiLlvm'];
 need(report?.schema==='soui.dependency-build/v1'&&report.architecture===arch&&report.component===(isKernel?'Miniblink':'SOUI')&&
  report.status==='built'&&report.releaseReady===false&&report.experimentalGpu===false,'Component report identity differs');
 need(ext?.schema===(isKernel?'soui.public-kernel-llvm-build-files/v1':'soui.public-soui-llvm-build-files/v1')&&
  ext.profile==='LLVM22MSVC142'&&ext.architecture===arch&&ext.sourceImmutable===true&&ext.inputFilesStable===true&&ext.releaseReady===false,'Missing actual LLVM component files');
 const inputs=pinnedJson(ext.inputs),before=pinnedJson(ext.sourceBefore),after=pinnedJson(ext.sourceAfter);
 need(inputs.schema===(isKernel?'soui.public-kernel-llvm-inputs/v1':'soui.public-soui-llvm-inputs/v1')&&inputs.profile==='LLVM22MSVC142'&&inputs.architecture===arch,'Component input identity differs');
 if(isKernel)need(samePath(inputs.sourceRoot,path.join(root,'third_party/sources/miniblink'))&&samePath(inputs.outputRoot,path.dirname(choice.report)),'Kernel compiler source/output routing differs');
 need(isDeepStrictEqual(before,after),'Source changed during component build');verifySource(before,path.join(root,'third_party/sources',isKernel?'miniblink':'soui'));
 const inputArchive=verifyInputPins(root,inputs,choice.inputArchive);
 const supervision=capture(root,arch,component,choice,inputs,runner);
 if(isKernel){
  need(Array.isArray(ext.libraries)&&ext.libraries.length===11,'Kernel library set is incomplete');ext.libraries.forEach(checkPin);checkPin(ext.dll);checkPin(ext.map);
 }else{
  verifySource(pinnedJson(ext.atl),path.join(inputs.vsOwner.msvcRoot,'atlmfc'));
  const stage=pinnedJson(ext.stageBefore);need(samePath(stage.root,report.souiSourceStage)&&isDeepStrictEqual(stage.files,before.files),'SOUI compiler stage differs from selected source');
  for(const f of stage.files)checkPin({...f,path:path.join(stage.root,f.path)});
  need(Array.isArray(ext.records)&&ext.records.length===5&&new Set(ext.records.map(r=>r.component)).size===5,'SOUI compiler record set differs');
  const projects={utilities:'utilities/utilities',soui:'SOUI/soui','render-gdi':'components/render-gdi/render-gdi',
   'imgdecoder-gdip':'components/imgdecoder-gdip/imgdecoder-gdip','soui-sys-resource':'soui-sys-resource/soui-sys-resource'};
  for(const record of ext.records){
   for(const key of ['project','arguments','log','binlog','dll','importLibrary','map'])checkPin(record[key]);
   const expectedProject=projects[record.component];need(expectedProject&&samePath(record.project.path,path.join(stage.root,expectedProject+(arch==='x64'?'64':'')+'.vcxproj')),'SOUI compiler project routing differs');
   const args=pinnedJson(record.arguments);need(Array.isArray(args)&&samePath(args[0],record.project.path)&&
    args.includes('/p:Platform='+(arch==='x64'?'x64':'Win32'))&&args.includes('/p:Configuration=Release')&&
    args.includes('/p:PlatformToolset=ClangCL')&&args.includes('/p:BuildProjectReferences=false'),'SOUI actual compiler arguments differ');
  }
 }
 const expected=isKernel?['miniblink']:kinds.slice(1);
 need(Array.isArray(report.artifacts)&&report.artifacts.length===expected.length&&isDeepStrictEqual(report.artifacts.map(a=>a.kind).sort(),[...expected].sort()),'Component DLL roster differs');
 const artifacts=report.artifacts.map(a=>{const actual=pin(a.path);need(actual.sha256===a.sha256.toLowerCase(),'Component DLL changed');pe(fileBytes(a.path),arch);
  if(isKernel)need(samePath(a.path,ext.dll.path),'Kernel DLL report routing differs');
  else need(ext.records.some(r=>r.component===a.kind&&samePath(r.dll.path,a.path)&&r.dll.sha256===actual.sha256),'SOUI DLL lacks its actual link record');
  return {...actual,kind:a.kind};});
 return {report:pin(choice.report),sourceIdentity:before.identity,inputs:ext.inputs,supervision,artifacts,...(inputArchive?{inputArchive}:{}),scope:'actual-component-source-and-natural-process-closure',toolchainQualification:false};
}
function matchDeliveredSource(root,component,proof){
 need(['kernel','soui'].includes(component)&&/^[a-f0-9]{64}$/.test(proof?.sourceIdentity),'Invalid component source identity');
 const current=snapshot(path.join(root,'third_party/sources',component==='kernel'?'miniblink':'soui'));
 need(current.identity===proof.sourceIdentity,'Delivered component source differs from original producer bytes or directories');
 return {root:current.root,sourceIdentity:current.identity,fileCount:current.fileCount,directoryCount:current.directoryCount,totalBytes:current.totalBytes};
}
function verifyBuildPair(root,arch,value){
 selection(value,arch);root=id.safePath(root);
 const relocated=value.schema!=='soui.dependency-selection/v1';
 function component(kind){
  const producer=relocated?id.safePath(value[kind].producerRoot):root;
  const proof=verifyComponent(producer,arch,kind,value[kind],value.runner);
  return relocated?{...proof,producerRoot:producer,deliveredSource:matchDeliveredSource(root,kind,proof)}:proof;
 }
 const kernel=component('kernel'),soui=component('soui');
 const kd=pinnedJson(kernel.inputs).discovery,sd=pinnedJson(soui.inputs).discovery;
 for(const role of ['CLANG','LINKER','ARCHIVER','RC','MT'])need(isDeepStrictEqual(kd.ToolPins[role],sd.ToolPins[role]),'Component compiler inputs differ');
 return {kernel,soui,artifacts:[...kernel.artifacts,...soui.artifacts]};
}
function matchPayloads(build,artifacts){
 need(build?.payloads&&Object.keys(build.payloads).length>0&&artifacts.length===6,'Missing application payloads or six rebuilt DLLs');
 for(const payload of Object.values(build.payloads))for(const artifact of artifacts){
  const name=artifact.kind==='miniblink'?'node.dll':artifact.kind+'.dll',rows=payload.files.filter(f=>f.path===name);
  need(rows.length===1&&rows[0].bytes===artifact.bytes&&rows[0].sha256===artifact.sha256,'Application payload differs from rebuilt '+name);
 }
}
function accepted(root,arch,pair,selected){
 need(selected?.nativeToolchain==='LLVM22MSVC142'&&path.isAbsolute(selected.profileRequest),'Explicit accepted SDK profile required');
 const sdkProfile=require('./llvm-profile-inputs.cjs').readRequest(selected.profileRequest,arch,root).value.discovery;
 for(const component of [pair.kernel,pair.soui]){
  const built=pinnedJson(component.inputs).discovery;
  need(isDeepStrictEqual(built.ToolPins,sdkProfile.ToolPins)&&isDeepStrictEqual(built.RequiredInputPins,sdkProfile.RequiredInputPins),
   'Component compiler/SDK inputs differ from the selected application build');
 }
 const {build}=evidence.verifyAcceptedBuild(root,arch,'Release',false,selected);matchPayloads(build,pair.artifacts);
 for(const a of pair.artifacts){const rel='third_party/prebuilt/'+arch+'/'+(a.kind==='miniblink'?'miniblink/node.dll':'soui/bin/'+a.kind+'.dll');
  const current=pin(path.join(root,rel));need(current.bytes===a.bytes&&current.sha256===a.sha256,'Delivered dependency differs from rebuilt DLL');}
 return build;
}
function verifyRuntime(root,arch,value,selected){
 const pair=verifyBuildPair(root,arch,value),build=accepted(root,arch,pair,selected),{smoke}=evidence.verifySmoke(root,arch,'Release',selected);
 return {schema:'soui.rebuilt-dependency-smoke/v2',architecture:arch,profile:'LLVM22MSVC142',buildId:build.buildId,sourceSha256:build.source.sha256,
  selection:value,components:pair,kernelAndSouiBuiltFromBundledSource:true,passed:true,smoke,releaseReady:false};
}
function verifyRecorded(root,arch,selected){
 const location=path.join(root,'out/dependencies',arch,'rebuilt-smoke-llvm.json'),recorded=read(location);
 const actual=verifyRuntime(root,arch,recorded.selection,selected);
 need(isDeepStrictEqual(actual,recorded),'Recorded dependency verification differs from current inputs');return actual;
}
async function run(root,arch,value,selected,toolsRoot){
 need(/^[a-f0-9]{32}$/.test(process.env.SOUI_TEST_DEPENDENCIES_NONCE||''),'Use the lease-holding Test-Dependencies.ps1 entry');
 const pair=verifyBuildPair(root,arch,value);accepted(root,arch,pair,selected);
 const result=await require('./desktop-smoke.cjs').runSmoke(root,{architecture:arch,configuration:'Release',toolchainRequest:selected.profileRequest,toolsRoot});
 need(result.passed,'Dependency desktop validation failed; retain the actual attempt');
 const report=verifyRuntime(root,arch,value,selected),folder=id.safePath(root,'out/dependencies/'+arch);fs.mkdirSync(folder,{recursive:true});
 const file=path.join(folder,'rebuilt-smoke-llvm.json');if(fs.existsSync(file))fs.copyFileSync(file,path.join(folder,'rebuilt-smoke-llvm-'+crypto.randomUUID()+'.json'),fs.constants.COPYFILE_EXCL);
 fs.writeFileSync(file,JSON.stringify(report,null,2)+'\n');return {passed:true,architecture:arch,evidence:file,releaseReady:false};
}
module.exports={selection,pin,checkPin,verifyInputPins,snapshot,verifySource,pe,invocation,matchPayloads,matchDeliveredSource,verifyComponent,verifyBuildPair,verifyRuntime,verifyRecorded,run};
if(require.main===module)(async()=>{
 const [command,arch,kernelReport,kernelCapture,souiReport,souiCapture,request,toolsRoot,runner,...extra]=process.argv.slice(2);
 need(command==='run'&&[0,2,4].includes(extra.length)&&runner,'Usage: dependency-evidence.cjs run ARCH KERNEL_REPORT KERNEL_CAPTURE SOUI_REPORT SOUI_CAPTURE REQUEST TOOLS_ROOT RUNNER [KERNEL_PRODUCER_ROOT SOUI_PRODUCER_ROOT [KERNEL_INPUT_ARCHIVE_OR_DASH SOUI_INPUT_ARCHIVE_OR_DASH]]');
 const relocated=extra.length>=2,archived=extra.length===4;
 const value=selection({schema:archived?'soui.dependency-selection/v3':relocated?'soui.dependency-selection/v2':'soui.dependency-selection/v1',profile:'LLVM22MSVC142',architecture:arch,runner,
  kernel:{report:kernelReport,capture:kernelCapture,...(relocated?{producerRoot:extra[0]}:{}),...(archived?{inputArchive:extra[2]==='-'?null:extra[2]}:{})},
  soui:{report:souiReport,capture:souiCapture,...(relocated?{producerRoot:extra[1]}:{}),...(archived?{inputArchive:extra[3]==='-'?null:extra[3]}:{})}},arch);
 console.log(JSON.stringify(await run(path.resolve(__dirname,'../..'),arch,value,{nativeToolchain:'LLVM22MSVC142',profileRequest:request},toolsRoot)));
})().catch(error=>{console.error(error.message);process.exitCode=1;});
