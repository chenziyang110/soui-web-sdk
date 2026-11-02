'use strict';
// Data-only preparation. No child launch, VM input or C#/native compilation.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=__dirname,backup=path.join(root,'loader-a2-before-path-fix');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function pin(file){const b=fs.readFileSync(file);return {path:file,bytes:b.length,sha256:sha(b)};}
function read(name){return fs.readFileSync(path.join(root,name),'utf8');}
function writeNew(name,value){fs.writeFileSync(path.join(root,name),typeof value==='string'?value:JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
function replaceOne(value,oldText,newText){assert.equal(value.split(oldText).length,2,'Exactly one original source site required');return value.replace(oldText,()=>newText);}
assert(!fs.existsSync(backup),'Keep every prior unexecuted prepared identity');
const previousNames=[...new Set([...JSON.parse(read('LoaderA2.selection.prepared.json')).map(p=>p.name),
  'LoaderA2.selection.prepared.json','LoaderA2.prepared.json','LoaderA2.review.diff','Prepare-LoaderA2.cjs'])];
const previous=previousNames.map(name=>({name,...pin(path.join(root,name))}));
const oldOwned=read('Focus-Owned-a2.cjs'),oldInvoke=read('Invoke-NativeFocusControl.ps1'),oldPrep=JSON.parse(read('Preparation.actual.json'));
assert.equal(sha(Buffer.from(oldOwned)),'df95b018b2545479d29dec803b856c8072fb69064915b07dfcbb67c3664ac8f2');
assert.equal(sha(Buffer.from(oldInvoke)),'9227597e840ddf12317ae3940c7f78db69faa601adc62347715f4b23500814c2');
const oldGuard=oldInvoke.split(/\r?\n/).find(line=>line.startsWith('if($EvidenceDirectory-notmatch'));
assert(oldGuard&&oldGuard.includes('focusc-'),'Exact original path guard required');
const newGuard=String.raw`$requestedEvidenceDirectory=$EvidenceDirectory
if($requestedEvidenceDirectory-notmatch'^[cC]:[\\/]'-or$requestedEvidenceDirectory-match'(^|[\\/])\.{1,2}([\\/]|$)'){throw 'Absolute task C: evidence path without navigation required'}
$EvidenceDirectory=[IO.Path]::GetFullPath($requestedEvidenceDirectory)
if($EvidenceDirectory-notmatch'^C:\\issue-closeout-evidence\\focusc-[a-z0-9-]+$'-or$EvidenceDirectory-ine'C:\issue-closeout-evidence\focusc-a2'-or[IO.Path]::GetDirectoryName($EvidenceDirectory)-ine'C:\issue-closeout-evidence'-or(Test-Path -LiteralPath $EvidenceDirectory)){throw 'Fresh exact a2 task-VM evidence directory required'}`;
let invoke=replaceOne(oldInvoke,oldGuard,newGuard);
invoke=replaceOne(invoke,"$preparationFile=Join-Path $PSScriptRoot 'Preparation.actual.json'","$preparationFile=Join-Path $PSScriptRoot 'Preparation-a2.actual.json'");
const invokeBytes=Buffer.from(invoke),invokeName='Invoke-NativeFocusControl-a2.ps1';
const preparation={...oldPrep,preparedUtc:new Date().toISOString(),
  files:oldPrep.files.map(p=>p.relative==='Invoke-NativeFocusControl.ps1'?{relative:invokeName,bytes:invokeBytes.length,sha256:sha(invokeBytes)}:p),
  previousPreparation:pin(path.join(root,'Preparation.actual.json')),syntaxCorrectionOnly:false,
  pathCorrection:{scope:'Only normalize the supplied absolute evidence path and confine it to the fresh a2 leaf; original a1/native/control/deadlines retained',
    originalInvoke:pin(path.join(root,'Invoke-NativeFocusControl.ps1')),exactLeaf:'C:/issue-closeout-evidence/focusc-a2',
    rejectRawNavigation:true,immediatePureConsoleCasesPassed:11,filesCreatedByConsoleCases:false,nativeCompiledByConsoleCases:false,vmTouchedByConsoleCases:false},
  nativeSourceUnchanged:true,deadlineUnchanged:true,compiled:false,executed:false,vmTouched:false};
const prepText=JSON.stringify(preparation,null,2)+'\n';
let owned=oldOwned;
owned=replaceOne(owned,"const prepSha='6b5fd304995a766913b5d6d765b7a71e4eb5755a6043a05c36865cbb27e609e6'",`const prepSha='${sha(Buffer.from(prepText))}'`);
owned=replaceOne(owned,"path.join(__dirname,'Preparation.actual.json')","path.join(__dirname,'Preparation-a2.actual.json')");
owned=replaceOne(owned,"path.join(__dirname,'Invoke-NativeFocusControl.ps1')","path.join(__dirname,'Invoke-NativeFocusControl-a2.ps1')");
for(const name of [invokeName,'Preparation-a2.actual.json','LoaderA2.PathCorrection.prepared.json','LoaderA2.path-fix.review.json'])assert(!fs.existsSync(path.join(root,name)));
fs.mkdirSync(backup);
for(const p of previous){const file=path.join(backup,p.name);fs.copyFileSync(p.path,file,fs.constants.COPYFILE_EXCL);const copied=pin(file);assert.equal(copied.bytes,p.bytes);assert.equal(copied.sha256,p.sha256);assert.equal(pin(p.path).sha256,p.sha256);}
writeNew(invokeName,invoke);writeNew('Preparation-a2.actual.json',prepText);
// These pre-execution a2 files may be replaced only after every previous byte
// has been preserved and pinned above. Actual a1 preparations remain untouched.
fs.writeFileSync(path.join(root,'Focus-Owned-a2.cjs'),owned);
const selectionNames=['NativeFocusControl.cs',invokeName,'README.md','Preparation-a2.actual.json','Focus-Owned-a2.cjs','FocusExport-a2.ps1','Loader-Crt.inputs.json'];
const selection=selectionNames.map(name=>({name,...pin(path.join(root,name))}));
fs.writeFileSync(path.join(root,'LoaderA2.selection.prepared.json'),JSON.stringify(selection,null,2)+'\n');
const oldRecord=JSON.parse(fs.readFileSync(path.join(backup,'LoaderA2.prepared.json'),'utf8'));
const updatedRecord={...oldRecord,preparedUtc:new Date().toISOString(),sourceInputs:selection,
  pathCorrection:'LoaderA2.PathCorrection.prepared.json',previousUnexecutedPreparation:{directory:backup,files:previous.map(p=>({name:p.name,...pin(path.join(backup,p.name))}))},
  changes:[...oldRecord.changes,'a2-specific PowerShell/preparation normalize slash form and confine the raw absolute path to the exact fresh a2 leaf; raw dot navigation rejected'],
  compiled:false,executed:false,vmTouched:false};
fs.writeFileSync(path.join(root,'LoaderA2.prepared.json'),JSON.stringify(updatedRecord,null,2)+'\n');
writeNew('LoaderA2.PathCorrection.prepared.json',{schema:'soui.native-focus-a2-path-correction/v1',preparedUtc:new Date().toISOString(),
  backupDirectory:backup,previousPreparedFiles:previous,newSelection:pin(path.join(root,'LoaderA2.selection.prepared.json')),
  newInputs:selection,rawGuardBefore:oldGuard,normalizedGuardAfter:newGuard,
  consoleEvaluation:{actualToolOutput:'Immediate Root-task console evaluation before preparing files; 11 classifications passed',validSlashForms:2,invalidSiblingNavigationLeafDriveOrUncForms:9,filesystemWrites:false,nativeExecution:false,vmTouched:false},
  originalA1FilesUnchanged:true,nativeCSharpUnchanged:true,outerTimeoutMs:90000,drainMs:2000,innerDeadlineMs:60000,
  compiled:false,executed:false,vmTouched:false,testPassed:null,releaseReady:false});
writeNew('LoaderA2.path-fix.review.json',{schema:'soui.native-focus-a2-path-fix-review-sites/v1',
  previousOwned:pin(path.join(backup,'Focus-Owned-a2.cjs')),currentOwned:pin(path.join(root,'Focus-Owned-a2.cjs')),
  originalInvoke:pin(path.join(root,'Invoke-NativeFocusControl.ps1')),newInvoke:pin(path.join(root,invokeName)),
  sourceChanges:['Owned prepSha, Preparation-a2 filename and Invoke-a2 filename only','PS evidence normalization/exact leaf guard and Preparation-a2 filename only'],
  sourcePins:selection,vmExecuted:false,compiled:false});
for(const p of previous){assert.equal(pin(path.join(backup,p.name)).sha256,p.sha256);if(!['Focus-Owned-a2.cjs','LoaderA2.selection.prepared.json','LoaderA2.prepared.json'].includes(p.name))assert.equal(pin(p.path).sha256,p.sha256);}
console.log(JSON.stringify({preparedOnly:true,backupFiles:previous.length,record:pin(path.join(root,'LoaderA2.PathCorrection.prepared.json')),selection:pin(path.join(root,'LoaderA2.selection.prepared.json')),files:selection}));
