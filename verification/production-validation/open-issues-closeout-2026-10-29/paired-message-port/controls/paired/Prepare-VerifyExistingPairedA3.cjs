'use strict';
// Data-only preparation, never a second copy attempt or a VM/product invocation.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const base=__dirname,sha=b=>crypto.createHash('sha256').update(b).digest('hex'),json=f=>JSON.parse(fs.readFileSync(f,'utf8'));
function pin(f){const a=fs.lstatSync(f,{bigint:true});assert(a.isFile()&&!a.isSymbolicLink()&&a.nlink===1n&&a.size<=16777216n);const b=fs.readFileSync(f),z=fs.lstatSync(f,{bigint:true});for(const n of ['dev','ino','size','mtimeNs','ctimeNs','nlink'])assert.equal(a[n],z[n]);return{path:path.resolve(f),bytes:b.length,sha256:sha(b)};}
const equal=(a,b)=>{assert.equal(a.bytes,b.bytes);assert.equal(a.sha256,b.sha256);},write=(f,o)=>fs.writeFileSync(f,JSON.stringify(o,null,2)+'\n',{flag:'wx'});
function main(){
 assert(process.platform==='win32'&&process.argv.length===3&&process.argv[2]==='ROOT_REVIEWED_EXISTING_PAIRED_COPY_VERIFY_A3_V1');
 const carrier=path.join(base,'minimal-carrier-6164bdf8-f132-4b2e-8ef1-97a27cb96721'),planFile=path.join(carrier,'payload/Paired.transport.json'),planPin=pin(planFile),plan=json(planFile);assert.equal(planPin.sha256,'8210135890765852ca89e5e90d4076f09718ee0ac58c8dcdba4b247b18fdc576');
 const raw=path.resolve(base,'../current-decoded/mp77p-a2-stage-failed/raw'),decodeFile=path.join(path.dirname(raw),'Decode.actual.json'),decode=json(decodeFile),failureFile=path.join(raw,'Failure.actual.json'),failure=json(failureFile),ps=json(path.join(raw,'PowerShell.finished.json')),start=json(path.join(raw,'Start.actual.json')),closed=json(path.join(raw,'ExportPreflight.actual.json'));
 assert.equal(pin(failureFile).sha256,'789e0d20753dd137ff40190c9f9a46f09cc9d2648e2c3cf138ce01d4a7a39d5a');assert.equal(pin(path.join(raw,'PowerShell.finished.json')).sha256,'0375014c8ce98d61245ac4e7a2a13bd2fd171f448bb6fb3e9decc3a753bccde4');
 assert(decode.allOriginalBytesAndSha256Verified===true&&decode.caseInsensitiveClosedSetVerified===true&&decode.browserPassClaimed===false);assert(failure.stagingCompleted===false&&failure.copies.length===1596&&failure.created.length===5&&failure.productExecuted===false);assert(/^Linked path: C:\\Users\\Administrator\\templates$/i.test(failure.failure.message));
 assert(ps.exitCode===1&&ps.actualNativeExitCode===1&&ps.environmentRestored===true&&ps.leaseReleased===true&&ps.restoreErrors.length===0);equal(start.plan,planPin);assert.equal(start.stager.sha256,'d8ad34efeb83036c2492ac23cc9900b4fc20e2c0a03ec5dc4d51e3b2bf8d6c8b');
 assert(closed.schema==='soui.failed-paired-copy-stage-closed-export-preflight/v1'&&closed.originalParentIdentityAbsent===true&&closed.recordedNodePidAbsent===true&&closed.stagingPassed===false&&closed.failureReclassified===false);
 const names=['Failure.actual.json','Start.actual.json','PowerShell.finished.json','Lease.actual.json','stderr.log','stdout.log','ExportPreflight.actual.json','ExportFiles.actual.json'],receipts={};
 for(const n of names){const actual=pin(path.join(raw,n)),original=n==='ExportFiles.actual.json'?decode.catalog:decode.files.find(f=>f.path===n);assert(original);equal(actual,original);receipts[n]={path:'C:/issue-closeout-evidence/mp77p-stage-a2/'+n,bytes:actual.bytes,sha256:actual.sha256};}
 const out=path.join(base,'verify-existing-preparation-'+crypto.randomUUID()),payload=path.join(out,'payload');fs.mkdirSync(out);fs.mkdirSync(payload);const copies=[];
 function copy(f,name){const before=pin(f),dest=path.join(payload,name);fs.copyFileSync(f,dest,fs.constants.COPYFILE_EXCL);const after=pin(f),copied=pin(dest);assert.deepEqual(after,before);equal(copied,before);copies.push({source:before,copied,sourceAfter:after});}
 copy(planFile,'Paired.transport.json');copy(path.join(base,'Verify-ExistingPairedA3.cjs'),'Verify-ExistingPairedA3.cjs');
 const input={schema:'soui.paired-existing-copy-verification-inputs/v1',newProducer:true,createdUtc:new Date().toISOString(),preparationHost:os.hostname(),evidenceRoot:'C:/issue-closeout-evidence/mp77p-verify-a3',originalA2Plan:planPin,originalA2HostDecode:pin(decodeFile),originalA2DecodeTag:decode.tag,originalA2Receipts:receipts,requiredExecutionNode:plan.laterLaunchContract.requiredExecutionNode,verifier:pin(path.join(payload,'Verify-ExistingPairedA3.cjs')),actualCwdRequired:'F:/work/soui-issue96',copyExecutedByVerification:false,originalA2StageRemainsFailed:true,executionEnabled:false,productExecuted:false,releaseReady:false};write(path.join(payload,'Verify.inputs.json'),input);const inputPin=pin(path.join(payload,'Verify.inputs.json'));
 const psSource=`$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root='C:\\issue-closeout-evidence\\mp77p-verify-a3'
$node='F:\\work\\SOUI_WEB\\out\\open-issues-closeout-20261029\\message-port\\tools\\node.exe'
$inputFile=Join-Path $PSScriptRoot 'Verify.inputs.json'
$script=Join-Path $PSScriptRoot 'Verify-ExistingPairedA3.cjs'
if($env:COMPUTERNAME-ine'SOUI-7AEMMK5Q0T'-or[IntPtr]::Size-ne8){throw 'Task VM x64 only'}
if(Test-Path -LiteralPath $root){throw 'Fresh verification evidence required'}
if((Get-FileHash -LiteralPath $inputFile -Algorithm SHA256).Hash.ToLowerInvariant()-cne'${inputPin.sha256}'){throw 'Reviewed verification input differs'}
if((Get-FileHash -LiteralPath $script -Algorithm SHA256).Hash.ToLowerInvariant()-cne'${input.verifier.sha256}'){throw 'Reviewed verification source differs'}
if((Get-FileHash -LiteralPath $node -Algorithm SHA256).Hash.ToLowerInvariant()-cne'${input.requiredExecutionNode.sha256}'){throw 'Exact existing Node differs'}
$names=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER')
$saved=@{};$errors=[Collections.Generic.List[string]]::new();$exitCode=1;$nativeExit=$null;$held=$false;$released=$false;$locationSaved=$false;$priorLocation=$null
$mutex=[Threading.Mutex]::new($false,'Local\\SouiDevKit.DesktopValidation')
try{
 try{$held=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$held=$true;throw 'Abandoned validation mutex'}
 if(-not$held){throw 'Validation mutex occupied'}
 New-Item -ItemType Directory -Path $root|Out-Null
 $owner=[Diagnostics.Process]::GetCurrentProcess()
 $lease=@{name='Local\\SouiDevKit.DesktopValidation';acquired=$true;mode='verify-existing';ownerProcessId=$PID;ownerCreationFileTime=$owner.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}
 [IO.File]::WriteAllText((Join-Path $root 'Lease.actual.json'),($lease|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
 foreach($name in $names){$v=[Environment]::GetEnvironmentVariable($name,'Process');$saved[$name]=@{present=($null-ne$v);value=$v};[Environment]::SetEnvironmentVariable($name,$null,'Process')}
 $priorLocation=Get-Location;Set-Location -LiteralPath 'F:\\work\\soui-issue96';$locationSaved=$true
 $priorPreference=$ErrorActionPreference
 try{$ErrorActionPreference='Continue';$global:LASTEXITCODE=$null;& $node $script 'ROOT_REVIEWED_VERIFY_EXISTING_PAIRED_A3_V1' $inputFile '${inputPin.sha256}' 1>(Join-Path $root 'stdout.log') 2>(Join-Path $root 'stderr.log');$nativeExit=$LASTEXITCODE}finally{$ErrorActionPreference=$priorPreference}
 if($null-eq$nativeExit){throw 'Native verification exit absent'};$exitCode=[int]$nativeExit
}catch{[Console]::Error.WriteLine($_.Exception.Message);$exitCode=1}
finally{
 foreach($name in $saved.Keys){try{[Environment]::SetEnvironmentVariable($name,$saved[$name].value,'Process');$now=[Environment]::GetEnvironmentVariable($name,'Process');if(($null-ne$now)-ne$saved[$name].present-or($saved[$name].present-and$now-cne$saved[$name].value)){throw 'Restore mismatch'}}catch{$errors.Add($name)}}
 if($locationSaved){try{Set-Location -LiteralPath $priorLocation.Path}catch{$errors.Add('CWD')}}
 if($held){try{$mutex.ReleaseMutex();$released=$true}catch{$errors.Add('mutex')}};$mutex.Dispose()
 if(Test-Path -LiteralPath $root){[IO.File]::WriteAllText((Join-Path $root 'PowerShell.finished.json'),(@{exitCode=$exitCode;actualNativeExitCode=$nativeExit;restoreErrors=@($errors.ToArray());environmentRestored=($errors.Count-eq0);cwdRestored=($errors-notcontains'CWD');pathChangedByStage=$false;leaseReleased=$released;copyExecutedByVerification=$false;originalA2StageRemainsFailed=$true;productExecuted=$false;releaseReady=$false}|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))}
}
if($errors.Count){exit 1};exit $exitCode
`;
 fs.writeFileSync(path.join(payload,'Verify-Existing.ps1'),psSource,{flag:'wx'});const selection=fs.readdirSync(payload).sort().map(name=>({name,...pin(path.join(payload,name))}));assert.equal(selection.length,4);assert(selection.reduce((n,p)=>n+p.bytes,0)<=33554432);
 write(path.join(out,'ControlIso.selection.prepared.json'),selection);write(path.join(out,'Preparation.actual.json'),{schema:'soui.paired-existing-copy-verification-preparation/v1',producer:pin(__filename),originalA2HostDecode:pin(decodeFile),originalA2Failure:pin(failureFile),originalA2Plan:planPin,newInput:inputPin,copies,selection,originalA2StageRemainsFailed:true,duplicateSdkOrConsumerCopyPerformed:false,verificationExecutedByPreparation:false,vmTouched:false,productExecuted:false,releaseReady:false});console.log(JSON.stringify({output:out,input:inputPin,selection:pin(path.join(out,'ControlIso.selection.prepared.json')),verificationExecuted:false,copyExecuted:false,productExecuted:false}));
}
try{main();}catch(e){console.error(e.stack);process.exitCode=1;}
