'use strict';
// Data-only host finalizer. Root reviews it first; never starts builds, products, ISO tools or VMs.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const base=__dirname,payload=path.join(base,'payload'),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const [summaryFile,summarySha,gate]=process.argv.slice(2);
assert.equal(process.argv.length,5);assert.equal(gate,'ROOT_REVIEWED_TRAY_RECOVERY_DATA_FINALIZER_V1');assert(/^[a-f0-9]{64}$/.test(summarySha));
assert.equal(path.resolve(base).toLowerCase(),'f:\\work\\soui_web\\out\\open-issues-closeout-20261029\\tray-recovery-vm-current');
const pin=f=>{const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);const after=fs.lstatSync(f);assert.equal(s.size,after.size);assert.equal(s.mtimeMs,after.mtimeMs);assert.equal(s.ctimeMs,after.ctimeMs);return{path:f,bytes:b.length,sha256:hash(b)};};
const write=(name,value)=>fs.writeFileSync(path.join(base,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const summaryPin=pin(summaryFile);assert.equal(summaryPin.sha256,summarySha);const summary=JSON.parse(fs.readFileSync(summaryFile,'utf8'));
assert.equal(summary.schema,'soui.tray-recovery-consumer-build-only/v1');assert(summary.passed&&summary.failure===null&&!summary.productExecuted&&!summary.executableExecuted);assert.deepEqual(summary.verdicts.map(x=>x.architecture),['x64','x86']);
const controls=['Export-TrayRecovery.ps1','Guest-TrayRecovery.cjs','Guest-TrayRecovery.ps1','Run-TrayRecovery.ps1','Stage-TrayRecovery.ps1','TrayRecovery-Lib.ps1'];assert.deepEqual(fs.readdirSync(payload).sort(),controls);
const sourceRoot='C:/sc07/tests/tray-recovery-consumer',sourcePins=[['CMakeLists.txt',742,'dfcae26f8af6be654b2e81c3d44954cb99111b4c8d88a956ede05b7cb762ac31'],['main.cpp',23798,'71026eeffad9e4efa532db3d6903061db4a38fc03fbbb0c2fcfa04a6e18a24c2'],['README.md',3639,'001d75eaabacca23009c1797dc45aca1e106e6b14221202141a05e40b80b7fbd']].map(([name,bytes,sha256])=>{const actual=pin(path.join(sourceRoot,name));assert.equal(actual.bytes,bytes);assert.equal(actual.sha256,sha256);return actual;});
const originalFile='F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-build/handoff-6d09f9b2-1585-4187-95ba-2cd3d40cc832/payload/Payload.manifest.json';
const originalPin=pin(originalFile);assert.equal(originalPin.bytes,285033);assert.equal(originalPin.sha256,'c6bb34afb81252ca16dd2bec955e3a8b36bfe830041e52ba417e9c34445eb897');const original=JSON.parse(fs.readFileSync(originalFile,'utf8'));
const originalRow=p=>{const rows=original.files.filter(x=>x.path===p);assert.equal(rows.length,1);return rows[0];};
const tool=(member,target)=>{const row=originalRow(member);return{path:target,bytes:row.bytes,sha256:row.sha256};};
const tools={node:tool('tools/node.exe','C:/issues8/tools/node.exe'),runner:tool('tools/OwnedProcessRunner.exe','C:/issues8/tools/OwnedProcessRunner.exe'),ownedModule:tool('tools/owned-process.cjs','C:/issues8/tools/owned-process.cjs'),ownedTail:tool('tools/owned-tail-evidence.cjs','C:/issues8/tools/owned-tail-evidence.cjs'),exporter:tool('Guest-Export.ps1','C:/issues8/Guest-Export.ps1')};
assert.equal(tools.exporter.sha256,'f1cc23cfa6d76d35b2a6d8dd0779ceb332a42b51f74271bf097b5e9b44f1ce7c');
const copies=[],arms=[];
for(const verdict of summary.verdicts){
  const arch=verdict.architecture,source=verdict.compiledExecutable;
  assert(verdict.genericSupervisionPassed&&verdict.wholeJobNaturallyClosed&&!verdict.executableExecuted);assert.equal(verdict.selectedWindowsSdk,'10.0.19041.0');assert.equal(path.resolve(verdict.installedSdkPrefix).toLowerCase(),path.resolve(`C:/sr06/out/release-install-${arch}-release`).toLowerCase());
  const before=pin(source.path);assert.equal(before.bytes,source.bytes);assert.equal(before.sha256,source.sha256);assert.equal(path.basename(source.path),'SouiTrayRecoveryConsumer.exe');
  const bytes=fs.readFileSync(source.path),pe=bytes.readUInt32LE(0x3c);assert.equal(bytes.toString('ascii',0,2),'MZ');assert.equal(bytes.toString('ascii',pe,pe+4),'PE\0\0');assert.equal(bytes.readUInt16LE(pe+4),arch==='x64'?0x8664:0x14c);
  const name=`SouiTrayRecoveryConsumer-${arch}.exe`,destination=path.join(payload,name);fs.copyFileSync(source.path,destination,fs.constants.COPYFILE_EXCL);const copy=pin(destination);assert.equal(copy.bytes,before.bytes);assert.equal(copy.sha256,before.sha256);assert.deepEqual(pin(source.path),before);copies.push({source:before,copy});
  const crt=original.files.filter(x=>x.path.startsWith(`tray-${arch}/`)&&x.path.endsWith('.dll'));assert.equal(crt.length,arch==='x64'?15:14);
  arms.push({architecture:arch,runtimeDirectory:`runtime-${arch}`,files:[{relative:'SouiTrayRecoveryConsumer.exe',source:name,fromPayload:true,bytes:copy.bytes,sha256:copy.sha256},...crt.map(x=>({relative:path.posix.basename(x.path),source:'C:/issues8/'+x.path,fromPayload:false,bytes:x.bytes,sha256:x.sha256}))],installedSdkPrefix:verdict.installedSdkPrefix,actualBuildExecutable:source,timeoutMs:100000,drainMs:2000,innerDeadlineMs:90000});
}
const payloadFiles=fs.readdirSync(payload).sort().map(relative=>{const p=pin(path.join(payload,relative));return{relative,bytes:p.bytes,sha256:p.sha256};});assert.equal(payloadFiles.length,8);
const manifest={schema:'soui.current-tray-recovery-light-payload/v1',version:'0.6.0',preparationHost:os.hostname(),expectedGuest:'SOUI-7AEMMK5Q0T',guestRoot:'C:/trayrc-a1',actualBuildSummary:summaryPin,sourcePins,originalR1Manifest:originalPin,tools,payloadFiles,arms,oneArchitecturePerCommand:true,automaticStageToRun:false,inputInjection:false,controllerRestartsExplorer:false,externalRestartProofRequired:true,focusQualified:false,win7Qualified:false,productionQualificationComplete:false,releaseReady:false};
fs.writeFileSync(path.join(payload,'TrayRecovery.manifest.json'),JSON.stringify(manifest,null,2)+'\n',{flag:'wx'});
const selection=fs.readdirSync(payload).sort().map(name=>({name,...pin(path.join(payload,name))}));assert.equal(selection.length,9);
assert.deepEqual(pin(summaryFile),summaryPin);assert.deepEqual(pin(originalFile),originalPin);for(const p of sourcePins)assert.deepEqual(pin(p.path),p);
write('ControlIso.selection.actual.json',selection);
write('Finalization.actual.json',{schema:'soui.tray-recovery-data-finalization/v1',finalizer:pin(__filename),actualBuildSummary:summaryPin,sourcePins,originalR1Manifest:originalPin,copies,manifest:pin(path.join(payload,'TrayRecovery.manifest.json')),selectionFiles:9,payloadFiles:8,guestRuntimeCopies:31,reusedCrtDllFiles:29,productExecuted:false,isoCreated:false,vmExecuted:false,releaseReady:false});
console.log(JSON.stringify({manifest:pin(path.join(payload,'TrayRecovery.manifest.json')),selection:pin(path.join(base,'ControlIso.selection.actual.json')),files:9,productExecuted:false,isoCreated:false}));
