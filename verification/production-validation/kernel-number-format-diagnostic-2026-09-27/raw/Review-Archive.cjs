'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const input=id.readJson(path.join(root,'probe-input.json')),triage=id.readJson(path.join(root,'triage.json'));
assert.equal(id.hash(fs.readFileSync(path.join(repo,input.source))),input.sourceSha256);
assert.equal(id.hash(fs.readFileSync(path.join(root,'format-number-probe.cpp'))),input.generatedSha256);
assert.equal(triage.sourceUnchangedFromBase,true);
const commands={};
for(const name of ['build-x86-asan','run-x86-asan','imports-x86']){
 const base=path.join(root,'command-logs',name),r=id.readJson(path.join(base,'result.json'));
 assert.equal(r.exitCode,0);assert.equal(r.error,null);assert.equal(r.timedOut,false);
 for(const stream of ['stdout','stderr']){const b=fs.readFileSync(path.join(base,stream+'.log'));assert.equal(b.length,r[stream].bytes);assert.equal(id.hash(b),r[stream].sha256);}
 commands[name]=r;
}
const stdout=fs.readFileSync(path.join(root,'command-logs/run-x86-asan/stdout.log'),'utf8');
assert.match(stdout,/format-cases=833668; locales=4; longest-output=22/);
assert.equal(fs.readFileSync(path.join(root,'command-logs/run-x86-asan/stderr.log'),'utf8'),'');
const imports=fs.readFileSync(path.join(root,'command-logs/imports-x86/stdout.log'),'utf8');
assert.match(imports,/clang_rt\.asan_dynamic-i386\.dll/);assert.match(imports,/__asan_report_load1/);assert.match(imports,/__asan_report_store1/);
const binary=fs.readFileSync(path.join(root,'format-number-probe.exe'));
const review={reviewedAt:new Date().toISOString(),warning:'C4789',status:'not-resolved',sourceSha256:input.sourceSha256,
 probeSha256:id.hash(binary),architecture:'x86',bufferSize:99,cases:833668,locales:['C','en-US','zh-CN','de-DE'],longestObservedOutput:22,
 addressSanitizerLinked:true,asanFindingsInThisRun:0,exactFormattingBodyExtracted:true,classificationAdapters:input.classificationAdapters,
 limits:['Extracted formatting function only; not the complete libxml/SDK binary','No exhaustive floating-point or custom-locale proof','No production or broad native security verdict'],
 commands,fullKernelSecurityVerified:false,productionPass:false};
fs.writeFileSync(path.join(root,'review.json'),id.encoded(review),{flag:'wx'});
const target=path.join(repo,'verification/production-validation/kernel-number-format-diagnostic-2026-09-27');assert(!fs.existsSync(target));const files=[];
const selected=['Create-Probe.cjs','Capture-Command.cjs','Build.cmd','Run.cmd','format-number-probe.cpp','libxml-Copyright.txt','probe-input.json','triage.json','Review-Archive.cjs','review.json',...id.walk(path.join(root,'command-logs')).map(n=>'command-logs/'+n)];
for(const name of selected){const original=path.join(root,name),b=fs.readFileSync(original),relative='raw/'+name,dest=path.join(target,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,b,{flag:'wx'});files.push({path:relative,bytes:b.length,sha256:id.hash(b),original});}
files.sort((a,b)=>a.path.localeCompare(b.path));fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(id.encoded({rawFiles:files.length,cases:review.cases,asanFindingsInThisRun:0,status:review.status,fullKernelSecurityVerified:false}));
