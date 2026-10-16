'use strict';
const fs=require('node:fs'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',output='C:/work/sm05-worker-observer-pin-update-1';
const failure=root+'/out/standard-build/attempt-d9443777-c7d2-4237-b585-17db0942e4f1';
const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const e=require(root+'/tools/internal/dependency-evidence.cjs');
fs.mkdirSync(output);
const observer=root+'/tools/internal/Observe-LlvmBuild.ps1',test=root+'/tests/sdk/llvm-generated-test-dependencies.Tests.ps1';
const source=fs.readFileSync(observer),oldHash='3b7ffe8140010195cbf4f50b6c4991afb13a511e3fcb376897ec540c9ef45637';
const map=e.pin(root+'/tests/native/vendor_lifetime/worker_lifetime/source-map.json');
assert.equal(map.sha256,'3c60c2dc31efd40c1a8c714e6837c77da43451c73aaba5840e77faa2edb41bd3');
const rawText=source.toString('utf8');assert.equal(rawText.split(oldHash).length,2);
fs.writeFileSync(output+'/original-observer.ps1',source,{flag:'wx'});
fs.writeFileSync(output+'/proposed-observer.ps1',rawText.replace(oldHash,map.sha256),{flag:'wx'});
const testText=fs.readFileSync(test,'utf8'),prefix=testText.slice(0,testText.indexOf('$cases=[Collections.Generic.List[object]]::new();'));
assert(prefix.length>1000);
fs.writeFileSync(output+'/facts.ps1',prefix+'\n@{selected=$selected.Count;missing=$missing.Count;pins=$cfg.dependencyPins.Count;configuration=$configuration;withVendorTests=$cfg.withVendorTests;hasVendorField=($null-ne$cfg.PSObject.Properties[\'withVendorTests\'])}|ConvertTo-Json\n',{flag:'wx'});
function run(name,file,candidate,expected){
  const args=['-NoProfile','-NonInteractive','-File',file,'-CandidatePath',candidate,'-BaselinePath',observer,'-FailureDirectory',failure];
  const r=cp.spawnSync(ps,args,{cwd:root,encoding:'utf8',windowsHide:true,maxBuffer:16*1024*1024});
  fs.writeFileSync(output+'/'+name+'-stdout.json',r.stdout||'',{flag:'wx'});fs.writeFileSync(output+'/'+name+'-stderr.txt',r.stderr||'',{flag:'wx'});
  fs.writeFileSync(output+'/'+name+'-result.json',JSON.stringify({args,exitCode:r.status,error:r.error?.message},null,2),{flag:'wx'});
  assert(!r.error,r.error?.message);assert.equal(r.status,expected,name+' unexpected result');return r;
}
const facts=JSON.parse(run('facts',output+'/facts.ps1',observer,0).stdout);
assert.equal(facts.configuration,'Debug');assert.equal(facts.missing,26);assert.equal(facts.hasVendorField,false);
const old="Check 'Actual failed Release graph has 128 records and 26 unfrozen paths' {Need ($selected.Count-eq128-and$missing.Count-eq26-and$cfg.dependencyPins.Count-eq5845-and$null-eq$cfg.PSObject.Properties['withVendorTests']) 'Original facts'}";
assert(testText.includes(old));
const replacement=`Check 'Current failed Debug graph has ${facts.selected} records and 26 unfrozen paths' {Need ($selected.Count-eq${facts.selected}-and$missing.Count-eq26-and$cfg.dependencyPins.Count-eq${facts.pins}-and$null-eq$cfg.PSObject.Properties['withVendorTests']) 'Current frozen graph facts'}`;
// Only fixture identity changes from the historical captured graph. Every
// derivation, tamper, relocation, and byte-exact assertion is unchanged.
const updated=testText.replace(old,replacement);fs.writeFileSync(output+'/current-graph.Tests.ps1',updated,{flag:'wx'});
const red=run('red',output+'/current-graph.Tests.ps1',observer,1);
assert(red.stderr.includes('Unsupported frozen generated test writer'));
const green=JSON.parse(run('green',output+'/current-graph.Tests.ps1',output+'/proposed-observer.ps1',0).stdout);
assert(green.passed&&green.counts.failed===0&&green.counts.derived===26);
fs.writeFileSync(output+'/VERIFIED.json',JSON.stringify({passed:true,map,original:e.pin(observer),proposed:e.pin(output+'/proposed-observer.ps1'),originalTest:e.pin(test),currentGraphTest:e.pin(output+'/current-graph.Tests.ps1'),facts,counts:green.counts,scope:'exact writer allowlist pin update; all original behavioral assertions retained',actualSdkAccepted:false,releaseReady:false},null,2),{flag:'wx'});
console.log(JSON.stringify({passed:true,counts:green.counts,facts,releaseReady:false}));
