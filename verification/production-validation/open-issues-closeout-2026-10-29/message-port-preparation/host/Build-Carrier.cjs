'use strict';
// Data-only, fresh exact copies. No guest or product process is launched.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),os=require('node:os'),assert=require('node:assert/strict');
const here=__dirname.replaceAll('\\','/'),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const read=p=>fs.readFileSync(p),json=p=>JSON.parse(read(p).toString('utf8'));
function pin(p){const s=fs.lstatSync(p);assert(s.isFile()&&!s.isSymbolicLink());const b=read(p);return{path:p.replaceAll('\\','/'),bytes:b.length,sha256:sha(b)}}
function check(p){const a=pin(p.path);assert.equal(a.bytes,p.bytes);assert.equal(a.sha256,p.sha256);return a;}
function save(file,value){fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});return pin(file);}
function walk(root,prefix=''){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const name=prefix+e.name,p=root+'/'+e.name;return e.isDirectory()?walk(p,name+'/'):(assert(e.isFile()),[name]);}).sort();}
const rootReview=pin(here+'/RootBrowserReview.actual.json');assert.equal(rootReview.sha256,'a2f9917230046036673d553c5d95068724cfa3f60f886b08224b05ce6f06f4a2');
const draft=json(here+'/BrowserABInputs.draft.json');assert(!draft.executionEnabled&&draft.rootIndependentReviewPending);
const approved={...draft,executionEnabled:true,rootIndependentReviewPending:false,rootBrowserReview:rootReview,
 rootApprovedScope:'Finite historical 0.5.1-preview.1 SDK fixture, task VM only after independent guest byte preflight.',
 pins:[...draft.pins,rootReview],hostProductExecutionAuthorized:false};
const approvedPin=save(here+'/BrowserABInputs.approved.json',approved);
const oldPlan=json(here+'/GuestReadClosure.plan.json');
const mappings=new Map(oldPlan.files.map(row=>[row.guestPath.toLowerCase(),row]));
function add(source,guestPath=source,reason='Reviewed task controller or metadata'){
 const p=pin(source);guestPath=guestPath.replaceAll('\\','/');assert(guestPath.startsWith('F:/'));
 const existing=mappings.get(guestPath.toLowerCase());if(existing){assert.equal(existing.sha256,p.sha256);assert.equal(existing.bytes,p.bytes);return;}
 mappings.set(guestPath.toLowerCase(),{source:p.path,guestPath,bytes:p.bytes,sha256:p.sha256,reason});
}
add(approvedPin.path);add(rootReview.path);
for(const name of ['Guest-MessagePort.cjs','Guest-MessagePort.ps1','Guest-Copy.ps1','Export-MessagePort.ps1','Decode-MessagePortExport.cjs'])add(here+'/'+name);
const rows=[...mappings.values()].sort((a,b)=>a.guestPath.localeCompare(b.guestPath));
for(const row of rows)check({...row,path:row.source});
const closure=save(here+'/GuestReadClosure.approved.json',{...oldPlan,schema:'soui.message-port-approved-guest-read-closure/v1',
 createdUtc:new Date().toISOString(),originalDraftPlan:pin(here+'/GuestReadClosure.plan.json'),rootBrowserReview:rootReview,
 executionInput:approvedPin,files:rows,fileCount:rows.length,totalRawBytes:rows.reduce((n,r)=>n+r.bytes,0),
 rootIndependentReviewPending:false,guestMachineIdentityPending:true,guestCopyAndBytePreflightStillRequired:true,
 guestOperatorMustReviewNewWrapperBeforeExecution:true,browserBehaviorExecuted:false,releaseReady:false});
add(closure.path);
const inputs=[...mappings.values()].sort((a,b)=>a.guestPath.localeCompare(b.guestPath));
const carrier=here+'/carrier-'+crypto.randomUUID(),tree=carrier+'/iso-tree';fs.mkdirSync(tree,{recursive:true});
const copied=[];
function copy(source,target,expected){
 const before=check({...expected,path:source});fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);
 const after=check({...expected,path:source}),actual=check({...expected,path:target});copied.push({sourceBefore:before,sourceAfter:after,copied:actual});
}
for(const row of inputs){const target=path.resolve(tree,'F',row.guestPath.slice(3));assert(target.startsWith(path.resolve(tree,'F')+path.sep));copy(row.source,target,row);}
const tray='F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-build/handoff-6d09f9b2-1585-4187-95ba-2cd3d40cc832/payload';
const trayManifest=json(tray+'/Payload.manifest.json');assert.deepEqual(walk(tray),[...trayManifest.files.map(r=>r.path),'Payload.manifest.json'].sort());
for(const row of trayManifest.files)check({...row,path:tray+'/'+row.path});
for(const name of walk(tray))copy(tray+'/'+name,tree+'/issues8/'+name,pin(tray+'/'+name));
copy(here+'/Guest-Copy.ps1',tree+'/Guest-Copy.ps1',pin(here+'/Guest-Copy.ps1'));
const manifest={schema:'soui.message-port-vm-carrier/v1',createdUtc:new Date().toISOString(),preparationHost:os.hostname(),
 guestControlRoot:here,selfGuestPath:here+'/Carrier.manifest.json',executionInput:approvedPin,rootBrowserReview:rootReview,
 approvedClosure:closure,files:inputs,closedReaderRoots:[oldPlan.sourceRoot,here,'F:/work/sm05/out/benchmark-protocol/Release'],
 trayPayload:{isoPath:'issues8',guestPath:'C:/issues8',manifest:pin(tray+'/Payload.manifest.json'),filesIncludingManifest:walk(tray).length,bytes:walk(tray).reduce((n,name)=>n+fs.statSync(tray+'/'+name).size,0)},
 sourceVersion:'0.5.1-preview.1',currentSdk060Qualified:false,sourceAndPayloadRawCopied:true,
 guestIdentityAndPreflightStillRequired:true,hostProductExecutionAuthorized:false,browserBehaviorExecuted:false,releaseReady:false};
const manifestPin=save(carrier+'/MessagePort.manifest.json',manifest);
copy(manifestPin.path,tree+'/MessagePort.manifest.json',manifestPin);
copy(manifestPin.path,path.join(tree,'F',manifest.selfGuestPath.slice(3)),manifestPin);
const treeRows=walk(tree).map(name=>({...pin(tree+'/'+name),path:name}));
assert.equal(treeRows.length,inputs.length+walk(tray).length+3);
for(const row of inputs)check({...row,path:row.source});for(const name of walk(tray))check({...pin(tree+'/issues8/'+name),path:tray+'/'+name});
const result=save(carrier+'/CarrierCopy.actual.json',{schema:'soui.issue77-and-tray-carrier-copy-actual/v1',createdUtc:new Date().toISOString(),
 carrier,tree,script:pin(__filename),argv:process.argv,cwd:process.cwd(),node:pin(fs.realpathSync(process.execPath)),
 manifest:manifestPin,sourceFilesBeforeAfterExact:true,carrierFilesRawVerified:true,closedFileSetVerified:true,
 originalApprovedClosure:closure,guestFiles:inputs.length,isoFiles:treeRows.length,totalIsoPayloadBytes:treeRows.reduce((n,r)=>n+r.bytes,0),
 copies:copied,allOriginalProducerPathsAndTimesRetainedInTheirBytes:true,
 nodeAndSdkOrProductExecuted:false,vmTouched:false,browserBehaviorExecuted:false,releaseReady:false});
const directories=new Set();for(const row of treeRows){let d=path.posix.dirname(row.path);while(d!=='.'){directories.add(d);d=path.posix.dirname(d);}}
const isoPlan=save(carrier+'/IsoSource.plan.json',{schema:'soui.issue77-and-tray-data-iso-source/v1',sourceRoot:tree,
 output:carrier+'/SouiOpenIssues.iso',scratch:carrier+'/imapi-scratch',files:treeRows,directories:[...directories].sort(),
 maxIsoBytes:1400000000,fileSystems:7,volumeName:'SOUI_ISSUES8',initialWholeJobTimeoutMs:120000,drainMs:2000,
 copyActual:result,manifest:manifestPin,dataOnly:true,productExecuted:false,vmTouched:false,releaseReady:false});
console.log(JSON.stringify({carrier,tree,approvedInput:approvedPin,approvedClosure:closure,manifest:manifestPin,copyActual:result,
 isoPlan,files:treeRows.length,payloadBytes:treeRows.reduce((n,r)=>n+r.bytes,0),isoCreated:false,vmTouched:false,releaseReady:false}));
