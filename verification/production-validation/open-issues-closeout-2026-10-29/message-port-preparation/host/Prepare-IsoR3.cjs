'use strict';
const fs=require('node:fs'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const here=__dirname.replaceAll('\\','/'),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>{const b=fs.readFileSync(p);return{path:p,bytes:b.length,sha256:sha(b)}};
const before=fs.readFileSync(here+'/Write-IssuesIso.r2.ps1','utf8');assert.equal(sha(Buffer.from(before)),'bfcfeced2b2c808624aab70c56205e72a0faed37ff278d897c00ff5d8b1af56a');
const needle=',$frozen.comServer,$frozen.sourcePlan';assert.equal(before.split(needle).length,2);
const after=before.replace(needle,',$frozen.sourcePlan').replace("$base+'\\iso-writer-r2-'","$base+'\\iso-writer-r3-'");
const writer=here+'/Write-IssuesIso.r3.ps1';fs.writeFileSync(writer,after,{flag:'wx'});
let wrapper=fs.readFileSync(here+'/Run-IsoWriter.r2.cjs','utf8');
wrapper=wrapper.replaceAll('Write-IssuesIso.r2.ps1','Write-IssuesIso.r3.ps1').replaceAll('iso-writer-r2-','iso-writer-r3-')
 .replace("'bfcfeced2b2c808624aab70c56205e72a0faed37ff278d897c00ff5d8b1af56a'","'"+pin(writer).sha256+"'");
wrapper=wrapper.replace("  minimalChange:","  previousAttemptFailure:pin(carrier+'/iso-writer-r2-c2d2ed7e-ccbe-45fa-8600-7ef2f5e67091/IsoWriter.failure.json'),\n  previousAttemptOutcome:pin(carrier+'/iso-writer-r2-c2d2ed7e-ccbe-45fa-8600-7ef2f5e67091/finished.json'),\n  minimalChange:");
const script=here+'/Run-IsoWriter.r3.cjs';fs.writeFileSync(script,wrapper,{flag:'wx'});
const record={schema:'soui.issue77-iso-r3-system-component-boundary/v1',originalR2Writer:pin(here+'/Write-IssuesIso.r2.ps1'),
 writer:pin(writer),wrapper:pin(script),helper:pin(here+'/BoundedIsoIO.longpath.cs'),
 scope:'Restore the trusted writer system-COM verification boundary; no payload hardlink/reparse/share/hash rule changed.',
 systemComponent:pin('C:/Windows/System32/imapi2fs.dll'),observedSystemComponentLinks:fs.lstatSync('C:/Windows/System32/imapi2fs.dll').nlink,
 metadataLeaseRemovedOnly:'$frozen.comServer',systemBoundedSmallBeforeAfterUnchanged:true,originalCLSIDGuardUnchanged:true,
 sourcePlanUnchanged:true,timeoutMs:120000,drainMs:2000,previousAttemptPreserved:true,rootIndependentReviewPending:true,
 productExecuted:false,vmTouched:false,releaseReady:false};
fs.writeFileSync(here+'/IsoR3.boundary.json',JSON.stringify(record,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(record));
