'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),vm=require('node:vm');
const api=require('../../tools/internal/release-verification.cjs');
const root=path.resolve(__dirname,'../..');
function fixture(t){
 const parent=fs.realpathSync(os.tmpdir()),legacy=fs.mkdtempSync(path.join(parent,'soui-release-stock-'));
 t.after(()=>{
   const resolved=fs.realpathSync(legacy);
   assert.equal(path.dirname(resolved),parent);assert(path.basename(resolved).startsWith('soui-release-stock-'));
   fs.rmSync(resolved,{recursive:true,force:true});
 });
 const paths=Object.fromEntries(['LlvmRoot','MsvcRoot','WindowsSdkRoot','NinjaExe','WindowsCmdExe','PowerShellExe'].map(k=>[k,path.join(root,'test-tools',k)]));
 return {schema:'soui.release-requested-profile/v2',profile:'LLVM22MSVC142',toolsRoot:root,legacyToolsRoot:legacy,paths,qualificationPassed:false,releaseReady:false};
}
test('explicit legacy stock permits source-owned LLVM validation without switching its tool output',t=>{
 const p=fixture(t);assert.equal(api.validateStockOwners(p),p);
 const run={nativeToolchain:{requested:p}};
 for(const phase of ['fresh','baseline','upgrade','rollback','reupgrade','relocation'])
   assert.equal(api.releaseToolsRoot(run,phase),['baseline','rollback'].includes(phase)?p.legacyToolsRoot:root);
 assert.equal(api.releaseToolsRoot({},'fresh'),root);
 assert.throws(()=>api.releaseToolsRoot(run,'unknown'));
});
test('legacy v1 retains its original source-owned MSVC contract',t=>{
 const p=fixture(t);p.schema='soui.release-requested-profile/v1';
 assert.throws(()=>api.validateReleaseProfile(p));
 [p.toolsRoot,p.legacyToolsRoot]=[p.legacyToolsRoot,p.toolsRoot];
 assert.doesNotThrow(()=>api.validateStockOwners(p));
});
test('stock selection rejects missing roots, the same owner and linked source aliases',t=>{
 const p=fixture(t),link=path.join(p.legacyToolsRoot,'linked');
 fs.symlinkSync(root,link,'junction');
 for(const legacy of [root,root.toUpperCase(),'relative',link,path.join(p.legacyToolsRoot,'missing')])
   assert.throws(()=>api.validateStockOwners({...p,legacyToolsRoot:legacy}));
 const absent={...p};delete absent.legacyToolsRoot;assert.throws(()=>api.validateReleaseProfile(absent));
});
test('new stock ownership cannot introduce another compiler profile or claim qualification',t=>{
 const p=fixture(t);
 for(const mutate of [v=>v.schema='soui.release-requested-profile/v3',v=>v.profile='MSVC143',v=>v.releaseReady=true,
   v=>v.qualificationPassed=true,v=>v.legacyToolsRoot='C:/bad;owner',v=>v.toolsRoot='C:/bad;owner',v=>v.paths.NinjaExe='C:/bad;input',v=>delete v.paths.MsvcRoot]){
   const value=structuredClone(p);mutate(value);assert.throws(()=>api.validateReleaseProfile(value));
 }
});
test('collector carries its explicit profile into the release report boundary',()=>{
 // Execute the actual integration call with DTO substitutes; this does not certify a native build.
 const source=fs.readFileSync(path.join(root,'tools/Collect-Verification.cjs'),'utf8');
 const expression=source.match(/releaseVerification\.validateReports\([\s\S]*?\);/)[0];
 for(const nativeToolchain of ['MSVC143','LLVM22MSVC142']){
   let received;
   vm.runInNewContext(expression,{nativeToolchain,version:'0.5.0',root,generated:{},fresh:{},relocated:{},relocationSmoke:{},currentPackage:{},
     identity:{source:()=>({sha256:'source'}),hash:()=> 'package',encoded:()=>Buffer.alloc(0)},
     releaseVerification:{validateReports:(_reports,expected)=>{received=expected;}}});
   assert.equal(received.nativeToolchain,nativeToolchain);
 }
});
test('generated action documentation is preserved business input across release phases',t=>{
 const p=fixture(t),project=p.legacyToolsRoot;
 fs.mkdirSync(path.join(project,'app'));fs.writeFileSync(path.join(project,'app/main.cpp'),'business');
 fs.mkdirSync(path.join(project,'docs/generated'),{recursive:true});
 const doc=path.join(project,'docs/generated/actions.md');fs.writeFileSync(doc,'generated action contract');
 const before=api.businessSnapshot(project);assert(before.files.some(row=>row.path==='docs/generated/actions.md'));
 fs.writeFileSync(doc,'changed action contract');assert.notEqual(api.businessSnapshot(project).sha256,before.sha256);
});

// Execute the installed-SDK function from the actual PowerShell entry with a
// command recorder. The recorder never starts a compiler, SDK or desktop actor.
const releaseChildProcess=require('node:child_process'),releaseCrypto=require('node:crypto');
const releaseQuote=value=>"'"+value.replaceAll("'","''")+"'";
function recordedInstalledSdk(t,{architecture='x64',profile='LLVM22MSVC142',fresh=false,fail='',missingReceipt=false,entry=path.join(root,'tools/Verify-Release.ps1')}={}){
 const outputParent=path.join(root,'out');fs.mkdirSync(outputParent,{recursive:true});
 const output=fs.mkdtempSync(path.join(outputParent,'release-observer-routing-'));
 t.after(()=>{const actual=fs.realpathSync(output);assert.equal(path.dirname(actual),fs.realpathSync(outputParent));assert(path.basename(actual).startsWith('release-observer-routing-'));fs.rmSync(actual,{recursive:true,force:true});});
 const script=String.raw`
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile(${releaseQuote(entry)},[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Release entry has parser errors'}
$functions=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-eq'Build-InstalledSdk'},$true))
if($functions.Count-ne1){throw 'Installed SDK implementation function missing'}
$calls=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.CommandAst]-and$node.GetCommandName()-eq'Build-InstalledSdk'},$true))
if($calls.Count-ne1){throw 'Release entry must call the tested installed SDK function exactly once'}
. ([scriptblock]::Create($functions[0].Extent.Text))
$root=${releaseQuote(root)};$output=${releaseQuote(output)};$DevkitRoot=Join-Path $output 'selected llvm stock'
$node='C:/fixture/node.exe';$cmake='C:/fixture/cmake.exe';$powershell='C:/fixture/legacy powershell.exe';$PowerShellExe='C:/fixture/pwsh.exe'
$sdkArchitecture=${releaseQuote(architecture)};$NativeToolchain=${releaseQuote(profile)};$FreshConfigure=${fresh?'$true':'$false'}
$llvmPaths=[ordered]@{LlvmRoot='C:/fixture/llvm';MsvcRoot='C:/fixture/msvc';WindowsSdkRoot='C:/fixture/windows sdk';NinjaExe='C:/fixture/ninja.exe';WindowsCmdExe='C:/fixture/cmd.exe';PowerShellExe=$PowerShellExe}
$script:recorded=[Collections.Generic.List[object]]::new()
function Invoke-Checked([string]$Executable,[string[]]$Arguments,[string]$Log){
 $stage=if($Log.EndsWith('llvm-host-stock-build.log')){'host-stock'}elseif($Log.EndsWith('installed-sdk-host-observer.log')){'prepare'}else{'build'}
 $script:recorded.Add([ordered]@{executable=$Executable;arguments=@($Arguments);log=$Log;stage=$stage})
 if($stage-eq${releaseQuote(fail)}){throw ('Recorded '+$stage+' failure')}
 if($stage-eq'prepare'-and-not${missingReceipt?'$true':'$false'}){
  $destination=Join-Path $output 'installed-sdk-host-observer';[IO.Directory]::CreateDirectory($destination)|Out-Null
  [IO.File]::WriteAllText((Join-Path $destination 'prepared.json'),('synthetic route receipt only'+[char]10),[Text.UTF8Encoding]::new($false))
 }
}
$passed=$false;$failure=$null
try{Build-InstalledSdk;$passed=$true}catch{$failure=$_.Exception.Message}
[Console]::Out.WriteLine((ConvertTo-Json -InputObject ([ordered]@{passed=$passed;calls=@($script:recorded.ToArray());failure=$failure}) -Depth 10 -Compress))
if(-not$passed){exit 1}
`;
 const environment={...process.env};for(const key of Object.keys(environment))if(key.toUpperCase()==='PSMODULEPATH')delete environment[key];
 const result=releaseChildProcess.spawnSync('powershell.exe',['-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',Buffer.from(script,'utf16le').toString('base64')],{env:environment,encoding:'utf8',windowsHide:true,timeout:30000});
 assert.equal(result.signal,null,result.stderr);assert(!result.error,result.error?.message);assert(result.stdout.trim(),result.stderr);
 return {status:result.status,output,...JSON.parse(result.stdout.trim())};
}
function releaseArgumentValue(args,key){const index=args.indexOf(key);assert(index>=0,key+' missing');assert.equal(args.lastIndexOf(key),index,key+' repeated');return args[index+1];}
test('release installed SDK prepares its own x64 observer and passes the exact actual receipt pin',t=>{
 const result=recordedInstalledSdk(t);assert.equal(result.status,0,result.failure);assert.deepEqual(result.calls.map(row=>row.stage),['prepare','build']);
 const [prepare,build]=result.calls,stock=path.join(result.output,'selected llvm stock'),folder=path.join(result.output,'installed-sdk-host-observer');
 assert.deepEqual(prepare.arguments,[path.join(root,'tools/internal/host64-build-debug-observer.cjs'),'prepare',root,path.join(stock,'out/benchmark-protocol/llvm-profile-request-Release.json'),stock,'C:/fixture/cmake.exe',folder]);
 assert.equal(prepare.executable,'C:/fixture/node.exe');assert.equal(build.executable,'C:/fixture/node.exe');
 assert.equal(releaseArgumentValue(build.arguments,'--root'),root);assert.equal(releaseArgumentValue(build.arguments,'--devkit-root'),stock);assert.equal(releaseArgumentValue(build.arguments,'--architecture'),'x64');
 assert.equal(releaseArgumentValue(build.arguments,'--host64-observer-prepared'),path.join(folder,'prepared.json'));
 assert.equal(releaseArgumentValue(build.arguments,'--host64-observer-sha256'),releaseCrypto.createHash('sha256').update('synthetic route receipt only\n').digest('hex'));
 assert.equal(build.arguments.filter(flag=>flag==='--host64-build-observer').length,1);assert(build.arguments.includes('--sdk-only'));assert(build.arguments.includes('--install'));assert(!build.arguments.includes('--fresh-configure'));
});
test('x86 release prepares the selected stock x64 host tools before the SDK observer and keeps all explicit inputs',t=>{
 const result=recordedInstalledSdk(t,{architecture:'x86',fresh:true});assert.equal(result.status,0,result.failure);assert.deepEqual(result.calls.map(row=>row.stage),['host-stock','prepare','build']);
 const [stock,prepare,build]=result.calls;assert.equal(stock.executable,'C:/fixture/pwsh.exe');assert.equal(releaseArgumentValue(stock.arguments,'-Architecture'),'x64');assert.equal(releaseArgumentValue(stock.arguments,'-NativeToolchain'),'LLVM22MSVC142');assert(stock.arguments.includes('-FreshConfigure'));
 assert.equal(releaseArgumentValue(stock.arguments,'-File'),path.join(result.output,'selected llvm stock/tools/internal/Build-ValidationTools.ps1'));
 const inputs={LlvmRoot:'C:/fixture/llvm',MsvcRoot:'C:/fixture/msvc',WindowsSdkRoot:'C:/fixture/windows sdk',NinjaExe:'C:/fixture/ninja.exe',WindowsCmdExe:'C:/fixture/cmd.exe',PowerShellExe:'C:/fixture/pwsh.exe'};
 const flags={LlvmRoot:'--llvm-root',MsvcRoot:'--msvc-root',WindowsSdkRoot:'--windows-sdk-root',NinjaExe:'--ninja-exe',WindowsCmdExe:'--windows-cmd-exe',PowerShellExe:'--powershell-exe'};
 for(const [name,value]of Object.entries(inputs)){assert.equal(releaseArgumentValue(stock.arguments,'-'+name),value);assert.equal(releaseArgumentValue(build.arguments,flags[name]),value);}
 assert.equal(prepare.arguments[3],path.join(result.output,'selected llvm stock/out/benchmark-protocol/llvm-profile-request-Release.json'));assert.equal(releaseArgumentValue(build.arguments,'--architecture'),'x86');assert(build.arguments.includes('--fresh-configure'));
});
test('host stock failure, observer preparation failure and missing actual receipt all stop before the SDK build',t=>{
 for(const options of [{architecture:'x86',fail:'host-stock'},{fail:'prepare'},{missingReceipt:true}]){
  const result=recordedInstalledSdk(t,options);assert.equal(result.status,1);assert.equal(result.passed,false);assert(!result.calls.some(row=>row.stage==='build'));assert(result.failure);
 }
});
test('historical MSVC release SDK behavior keeps its original profile without LLVM observer commands',t=>{
 for(const fresh of [false,true]){
  const result=recordedInstalledSdk(t,{profile:'MSVC143',fresh});assert.equal(result.status,0,result.failure);assert.equal(result.calls.length,1);
  const [build]=result.calls;assert.equal(build.executable,'C:/fixture/legacy powershell.exe');assert.equal(releaseArgumentValue(build.arguments,'-File'),path.join(root,'tools/Build.ps1'));assert(build.arguments.includes('-SdkOnly'));assert(build.arguments.includes('-Install'));assert.equal(build.arguments.includes('-FreshConfigure'),fresh);assert(!build.arguments.some(value=>value.includes('host64')));
 }
});
