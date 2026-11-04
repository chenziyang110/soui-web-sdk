'use strict';
const fs=require('node:fs'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const here=__dirname.replaceAll('\\','/'),original='F:/work/SOUI_WEB/out/release-v0.6.0-20261028/vm-short-prep/media-tooldeps-4/BoundedIsoIO.cs';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),pin=p=>{const b=fs.readFileSync(p);return{path:p,bytes:b.length,sha256:hash(b)}};
const source=fs.readFileSync(original,'utf8');assert.equal(hash(Buffer.from(source)),'fbedad4f3f1dd8357df8615f15917572e191681c676eba56d2b23507bf24dc72');
const before='handle=Native.CreateFileW(PathName,isDirectory?',after='handle=Native.CreateFileW(@"\\\\?\\"+PathName,isDirectory?';
assert.equal(source.split(before).length,2);const derived=source.replace(before,after);assert.equal(derived.replace(after,before),source);
const helper=here+'/BoundedIsoIO.longpath.cs';fs.writeFileSync(helper,derived,{flag:'wx'});
let writer=fs.readFileSync(here+'/Write-IssuesIso.ps1','utf8');
writer=writer.replace("Need (-not(Test-Path -LiteralPath $plan.output)-and-not(Test-Path -LiteralPath $plan.scratch)-and-not(Test-Path -LiteralPath (Join-Path $base 'IsoWriter.result.json'))) 'Preserve prior ISO outputs'",
 "$attempt=Native $frozen.attemptRoot\nNeed ($attempt.StartsWith($base+'\\iso-writer-r2-',[StringComparison]::OrdinalIgnoreCase)) 'Fresh bounded attempt root required'\n$scratch=Join-Path $attempt 'imapi-scratch'\nNeed (-not(Test-Path -LiteralPath $plan.output)-and-not(Test-Path -LiteralPath $scratch)-and-not(Test-Path -LiteralPath (Join-Path $attempt 'IsoWriter.result.json'))) 'Preserve prior ISO outputs'");
writer=writer.replace("Need ($frozen.helper.bytes-eq11926-and$frozen.helper.sha256-ceq'fbedad4f3f1dd8357df8615f15917572e191681c676eba56d2b23507bf24dc72') 'Unmodified original bounded helper required'",
 "Need ($frozen.originalHelper.bytes-eq11926-and$frozen.originalHelper.sha256-ceq'fbedad4f3f1dd8357df8615f15917572e191681c676eba56d2b23507bf24dc72'-and$frozen.helper.sha256-ceq'"+pin(helper).sha256+"') 'Exact one-line long-path derivative and original helper required'");
writer=writer.replace("$null=Small $frozen.writer;", "$null=Small $frozen.originalHelper;$null=Small $frozen.writer;");
writer=writer.replace('@($frozen.helper,$frozen.writer,','@($frozen.helper,$frozen.originalHelper,$frozen.writer,');
writer=writer.replaceAll("Join-Path $base 'IsoWriter.","Join-Path $attempt 'IsoWriter.");
writer=writer.replaceAll('(Native $plan.scratch)','(Native $scratch)');
writer=writer.replace('$rootItem.AddTree((Native $plan.sourceRoot),$false)',"$rootItem.AddTree(('\\\\?\\'+(Native $plan.sourceRoot)),$false)");
writer=writer.replace('originalBoundedHelper=$frozen.helper;','boundedHelper=$frozen.helper;originalHelper=$frozen.originalHelper;extendedPathsOnly=$true;');
writer=writer.replaceAll('foreach($error in @(', 'foreach($releaseError in @(').replace('if($error){$cleanup.Add([string]$error)}','if($releaseError){$cleanup.Add([string]$releaseError)}');
assert(writer.includes('Native $scratch')&&writer.includes('$attempt=')&&writer.includes('extendedPathsOnly=$true'));
const writerFile=here+'/Write-IssuesIso.r2.ps1';fs.writeFileSync(writerFile,writer,{flag:'wx'});
const delta={schema:'soui.issue77-iso-extended-path-derivative/v1',originalHelper:pin(original),derivedHelper:pin(helper),
 before,after,exactSingleReversibleReplacement:true,originalWriter:pin(here+'/Write-IssuesIso.ps1'),derivedWriter:pin(writerFile),
 preservedFences:['Plain normal absolute path validation','Ancestor reparse rejection','Held final-path identity comparison','Single hardlink requirement','Read-only sharing denies writes/deletion','Bounded hash/EOF/read loops','Source closed file/directory sets','Original120000/2000 budgets'],
 nativeChange:'Only CreateFileW receives the extended local path spelling; PathName and final identity remain the original normal spelling.',
 comChange:'AddTree receives the same validated source root with extended prefix; payload relative names and bytes are unchanged.',
 firstFailure:'Original helper failed opening an existing262-character directory with Win32 error3; no ISO/VM/product execution.',
 rootIndependentReviewPending:true,productExecuted:false,vmTouched:false,releaseReady:false};
fs.writeFileSync(here+'/LongPathIso.derivative.json',JSON.stringify(delta,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(delta));
