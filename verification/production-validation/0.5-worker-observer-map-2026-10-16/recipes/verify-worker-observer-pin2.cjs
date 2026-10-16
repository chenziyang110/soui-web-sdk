'use strict';
const fs=require('node:fs'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',output='C:/work/sm05-worker-observer-pin-update-1';
const failure=root+'/out/standard-build/attempt-d9443777-c7d2-4237-b585-17db0942e4f1';
const observer=root+'/tools/internal/Observe-LlvmBuild.ps1',test=root+'/tests/sdk/llvm-generated-test-dependencies.Tests.ps1';
const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe',e=require(root+'/tools/internal/dependency-evidence.cjs');
const facts=JSON.parse(fs.readFileSync(output+'/facts-stdout.json'));
assert.deepEqual({...facts},{pins:6038,withVendorTests:null,hasVendorField:false,selected:138,configuration:'Debug',missing:29});
const original=fs.readFileSync(test,'utf8');
const marker='$cases=[Collections.Generic.List[object]]::new();';assert.equal(original.split(marker).length,2);
const scope=`# This fixture tests only GeneratedTestDependencies' three existing roles.
# The complete current graph also has exactly three WebView2 inputs admitted by
# the observer's separate existing package/preload validators. Retain their
# identities and exclude only these explicit non-vendor inputs from this fixture.
$outsideGeneratedTest=@((Join-Path $root 'out/webview2-deps/1.0.4258.31/package/build/native/include/WebView2.h'),(Join-Path $root 'out/webview2-deps/1.0.4258.31/package/build/native/include/WebView2EnvironmentOptions.h'),(Join-Path $build 'sdk/renderers/webview2/webview2_preload.h'))
Need ($missing.Count-eq29) 'Exact complete current dependency difference'
foreach($path in $outsideGeneratedTest){Need ($missing.ContainsKey((Norm $path))) 'Exact non-vendor generated dependency';$missing.Remove((Norm $path))}
Need ($missing.Count-eq26) 'Exactly the original three vendor writer families'
`;
const old="Check 'Actual failed Release graph has 128 records and 26 unfrozen paths' {Need ($selected.Count-eq128-and$missing.Count-eq26-and$cfg.dependencyPins.Count-eq5845-and$null-eq$cfg.PSObject.Properties['withVendorTests']) 'Original facts'}";
const next="Check 'Current failed Debug graph has 138 records and 26 vendor paths plus three explicit WebView2 inputs' {Need ($selected.Count-eq138-and$missing.Count-eq26-and$cfg.dependencyPins.Count-eq6038-and$null-eq$cfg.PSObject.Properties['withVendorTests']) 'Current frozen graph facts'}";
assert(original.includes(old));
fs.writeFileSync(output+'/current-graph2.Tests.ps1',original.replace(marker,scope+marker).replace(old,next),{flag:'wx'});
function run(name,candidate,expected){const args=['-NoProfile','-NonInteractive','-File',output+'/current-graph2.Tests.ps1','-CandidatePath',candidate,'-BaselinePath',observer,'-FailureDirectory',failure];
 const r=cp.spawnSync(ps,args,{cwd:root,encoding:'utf8',windowsHide:true,maxBuffer:16*1024*1024});
 fs.writeFileSync(output+'/'+name+'-stdout.json',r.stdout||'',{flag:'wx'});fs.writeFileSync(output+'/'+name+'-stderr.txt',r.stderr||'',{flag:'wx'});
 fs.writeFileSync(output+'/'+name+'-result.json',JSON.stringify({args,exitCode:r.status,error:r.error?.message},null,2),{flag:'wx'});
 assert(!r.error,r.error?.message);assert.equal(r.status,expected,name+' failed; original records retained');return r;}
const red=run('red2',observer,1);assert(red.stderr.includes('Unsupported frozen generated test writer'));
const green=JSON.parse(run('green2',output+'/proposed-observer.ps1',0).stdout);assert(green.passed&&green.counts.failed===0&&green.counts.derived===26);
fs.writeFileSync(output+'/VERIFIED.json',JSON.stringify({passed:true,original:e.pin(observer),proposed:e.pin(output+'/proposed-observer.ps1'),map:e.pin(root+'/tests/native/vendor_lifetime/worker_lifetime/source-map.json'),originalTest:e.pin(test),currentGraphTest:e.pin(output+'/current-graph2.Tests.ps1'),facts,counts:green.counts,scope:'Only original three vendor writer roles; current three WebView2 headers remain covered by their separate unmodified observer validators',allOriginalBehavioralAssertionsRetained:true,actualSdkAccepted:false,releaseReady:false},null,2),{flag:'wx'});
console.log(JSON.stringify({passed:true,counts:green.counts,releaseReady:false}));
