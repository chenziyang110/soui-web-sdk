'use strict';
// File preparation only: do not compile or start desktop/VM workloads here.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),cp=require('node:child_process');
const root=__dirname,prior='C:/SOUI-SDK-production-validation/preview11-real-renewal-preparation-v2-20260926',clock='C:/SOUI-SDK-production-validation/preview11-renewal-supervisor-race-20260926';
const sourceRoot='C:/SOUI-SDK-source-validation/preview15-no-git',pkg='C:/SOUI-SDK-production-validation/sdk-core-native-backups-preview15-20260927/sdk-package';
const id=require(path.join(sourceRoot,'tools/internal/delivery-identity.cjs')),sdk=require(path.join(sourceRoot,'tools/sdk/soui-sdk.cjs'));
const SOURCE='b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07',VERSION='0.3.0-preview.15';
const CLOCK_FREEZE='b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef',V2_FREEZE='80d19d4e2008a505bbb8a9293849f3756d4bf07ad7838ad38aa5bb054ac0ee3a';
const read=f=>id.readJson(f),write=(n,b)=>{const file=path.join(root,n);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,b,{flag:'wx'});};
assert(!fs.existsSync(path.join(root,'staging')),'Refuse to replace preparation');
const originalSource=id.source(sourceRoot);assert.equal(originalSource.sha256,SOURCE);assert.equal(originalSource.files.length,717);
const manifest=sdk.readPackage(pkg);assert.equal(manifest.version,VERSION);assert.equal(id.hash(fs.readFileSync(path.join(pkg,'sdk-manifest.json'))),'44839f856df8d9542231e7bcd38af4ca9a37663aa8d4e6fb755868faacb076da');
const clockBytes=fs.readFileSync(path.join(clock,'freeze.json'));assert.equal(id.hash(clockBytes),CLOCK_FREEZE);const clockFreeze=JSON.parse(clockBytes);
assert.equal(id.hash(fs.readFileSync(path.join(prior,'freeze.json'))),V2_FREEZE);
for(const f of clockFreeze.runtimes.x86.source.files){const b=fs.readFileSync(path.join(clock,'consumer',f.path));assert.equal(b.length,f.bytes);assert.equal(id.hash(b),f.sha256,f.path);let copied=b;if(f.path==='main.cpp')copied=Buffer.from(b.toString('utf8').replaceAll('0.3.0-preview.11',VERSION).replaceAll('frozen preview.11','frozen preview.15'));write('consumer/'+f.path,copied);}
assert.equal(id.hash(fs.readFileSync(path.join(root,'consumer/web/index.html'))),'e1101d9496199d09d48c0d78425e5c6cb291e9b19a0cee3c6e5a25c97a8f1537');
assert.match(fs.readFileSync(path.join(root,'consumer/main.cpp'),'utf8'),/const auto sampled = renewal_probe::sample_clock\(state_\);/);
const adapt=text=>text.replaceAll('a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550',SOURCE).replaceAll('0.3.0-preview.11',VERSION).replaceAll('bec71cef4135087b76ccb368db8544950c1806b8','141d721910738fdcc0292532eabc7c29da2464d2').replaceAll('7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d',CLOCK_FREEZE).replaceAll('preview11-real-renewal-runs-v2-20260926','preview15-real-renewal-runs-20260927').replace(/\b711\b/g,'717');
for(const name of ['Prepare.cjs','Build-Web.mjs','Build.ps1','Run.ps1','Verify-Run.cjs','SelfTest-Verify-Run.cjs']){
 let text=adapt(fs.readFileSync(path.join(prior,name),'utf8'));
 if(name==='Prepare.cjs'){
  text=text.replace("const SDK = path.join(ROOT, 'inputs/source');","const SDK = path.join(ROOT, 'staging/source');");
  text=text.replace("const EVIDENCE = 'C:/SOUI-SDK-production-validation/preview11-session-renewal-20260926';","// Current installation provenance is produced by Build-Sdk.cjs.");
  const start=text.indexOf('function prepare() {'),end=text.indexOf('function attemptPath(value) {',start);assert(start>=0&&end>start);
  text=text.slice(0,start)+`function prepare() { return require('./Prepare-Inputs.cjs')({ROOT,SDK,INPUTS,SOURCE,VERSION,encode,hash,read,assert,write,walk,snapshot,verify,copyFiles,checkRecord,identityTool,checkSdkSource}); }\n`+text.slice(end);
  text=text.replace("'Verify-Run.cjs', 'Clone-V1.cjs'","'Verify-Run.cjs', 'Initialize.cjs', 'Prepare-Inputs.cjs', 'Build-Sdk.cjs'");
  text=text.replace("'configure', 'native-build', 'deploy', 'self-check'","'configure', 'native-build', 'clock-check', 'deploy', 'self-check'");
  text=text.replace("throw Error('v2 inputs are copied by Clone-V1.cjs, never rebuilt from the mutable SDK checkout')","prepare()");
 }
 if(name==='Build.ps1'){
  text=text.replace("'--target','InstalledRenewalProbe','--parallel','2'","'--target','InstalledRenewalProbe','SupervisorClockTests','--parallel','2'");
  text=text.replace("  Invoke-Step 'deploy'", "  Invoke-Step 'clock-check' (Join-Path $build 'Release\\SupervisorClockTests.exe') @() $build 30\n  Invoke-Step 'deploy'");
  text=text.replace('$ErrorActionPreference = \'Stop\'',`$ErrorActionPreference = 'Stop'\n$env:VcpkgEnabled='false';$env:VcpkgAutoLink='false';$env:VcpkgEnableManifest='false';$env:MSBUILDDISABLENODEREUSE='1'`);
 }
 if(name==='Run.ps1'){
  text=text.replaceAll('C:\\SOUI-SDK-development\\preview9-tray\\out\\*.exe','C:\\SOUI-SDK-development\\preview14-scheduler\\out\\*.exe');
  text=text.replace('RenewalStarter|InstalledRenewalProbe)', 'RenewalStarter|InstalledRenewalProbe|UpgradeLedger|MSBuild|cl|link|cdb)');
  text=text.replace("$_.ExecutablePath -like 'C:\\SOUI-SDK-development\\preview14-scheduler\\out\\*.exe'", "$_.ExecutablePath -like 'C:\\SOUI-SDK-development\\preview14-scheduler\\out\\*.exe' -or $_.ExecutablePath -like 'C:\\SOUI-SDK-source-validation\\*.exe'");
 }
 write(name,text);
}
write('Capture-Command.cjs',fs.readFileSync('C:/SOUI-SDK-production-validation/preview15-full-source-20260927/Capture-Command.cjs'));
write('provenance/clock-parent-freeze.json',clockBytes);write('provenance/protocol-parent-freeze.json',fs.readFileSync(path.join(prior,'freeze.json')));
write('provenance/parent-PROTOCOL.md',fs.readFileSync(path.join(prior,'PROTOCOL.md')));
const selected=[...new Set([...originalSource.files.map(f=>f.path),...manifest.files.map(f=>f.path)])].sort();
for(const name of selected)write('staging/source/'+name,fs.readFileSync(path.join(sourceRoot,name)));
const staged=path.join(root,'staging/source');assert.equal(id.source(staged).sha256,SOURCE);
const checked=cp.spawnSync(process.execPath,[path.join(staged,'tools/sdk/soui-sdk.cjs'),'verify-source','--package',pkg,'--source',staged],{windowsHide:true,encoding:'utf8',timeout:120000});
write('provenance/staged-sdk-verify.stdout.log',checked.stdout||'');write('provenance/staged-sdk-verify.stderr.log',checked.stderr||'');write('provenance/staged-sdk-verify.result.json',id.encoded({program:process.execPath,exitCode:checked.status,error:checked.error?.message||null,signal:checked.signal,guiExecuted:false}));assert.equal(checked.status,0);assert(!checked.error);
write('provenance/sdk-manifest.json',fs.readFileSync(path.join(pkg,'sdk-manifest.json')));
write('staging-input.json',id.encoded({schema:'soui.current-renewal-staging/v1',version:VERSION,sourceSha256:SOURCE,sourceFiles:originalSource.files.length,snapshot:id.snapshot(staged,id.walk(staged)),sdkManifestSha256:id.hash(fs.readFileSync(path.join(pkg,'sdk-manifest.json'))),parent:{preparationRoot:clock,freezeSha256:CLOCK_FREEZE},protocolParent:{preparationRoot:prior,freezeSha256:V2_FREEZE},consumer:id.snapshot(path.join(root,'consumer'),id.walk(path.join(root,'consumer'))),sdkCompiled:false,guiExecuted:false,realRenewalVerified:false,releaseReady:false}));
console.log(JSON.stringify({status:'current-sdk-and-original-animation-staged',version:VERSION,sourceFiles:originalSource.files.length,selectedFiles:selected.length,sdkCompiled:false,guiExecuted:false}));
