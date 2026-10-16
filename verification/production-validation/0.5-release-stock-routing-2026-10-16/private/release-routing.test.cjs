'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),Module=require('node:module');
const test=require('node:test'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',mode=process.env.RELEASE_ROUTING_INPUT||'original';
assert(['original','proposed'].includes(mode));
const filename=path.join(root,'tools/internal/release-verification.cjs');
const m=new Module(filename,module);m.filename=filename;m.paths=Module._nodeModulePaths(path.dirname(filename));
m._compile(fs.readFileSync(path.join(__dirname,mode,'tools/internal/release-verification.cjs'),'utf8'),filename);
const api=m.exports,modern=path.resolve(root),legacy=path.resolve('C:/work/sm05-release-stock21-1/source');
const paths=Object.fromEntries(['LlvmRoot','MsvcRoot','WindowsSdkRoot','NinjaExe','WindowsCmdExe','PowerShellExe'].map(k=>[k,'C:/tools/'+k]));
const profile=(schema='v2')=>({schema:'soui.release-requested-profile/'+schema,profile:'LLVM22MSVC142',toolsRoot:modern,legacyToolsRoot:legacy,paths,qualificationPassed:false,releaseReady:false});

test('explicit external legacy stock permits current source to retain its LLVM stock',()=>{
 assert.doesNotThrow(()=>api.validateReleaseProfile(profile()));
 assert.equal(api.validateStockOwners(profile()).legacyToolsRoot,legacy);
});
test('original v1 ownership remains unchanged and cannot accept a v2-shaped override',()=>{
 const p={...profile('v1'),toolsRoot:legacy,legacyToolsRoot:modern};
 assert.doesNotThrow(()=>api.validateReleaseProfile(p));
 assert.throws(()=>api.validateReleaseProfile(profile('v1')));
});
test('all six phase choices keep baseline and rollback on the selected legacy stock',()=>{
 const run={nativeToolchain:{requested:profile()}};
 for(const phase of ['fresh','baseline','upgrade','rollback','reupgrade','relocation'])
   assert.equal(api.releaseToolsRoot(run,phase),['baseline','rollback'].includes(phase)?legacy:modern);
 assert.equal(api.releaseToolsRoot({},'fresh'),modern);
 assert.throws(()=>api.releaseToolsRoot(run,'unknown'));
});
test('same owner, unsafe paths, missing explicit roots, unsupported profile and inflated qualification are rejected',()=>{
 for(const mutate of [p=>p.legacyToolsRoot=p.toolsRoot,p=>p.legacyToolsRoot='relative',p=>delete p.legacyToolsRoot,
   p=>p.schema='soui.release-requested-profile/v3',p=>p.profile='MSVC143',p=>p.releaseReady=true,
   p=>p.qualificationPassed=true,p=>p.paths.NinjaExe='C:/bad;input',p=>delete p.paths.MsvcRoot]){
   const p=profile();mutate(p);assert.throws(()=>api.validateReleaseProfile(p));
 }
});
test('physical owner validation runs before tool builds and rejects an absent source directory',()=>{
 assert.throws(()=>api.validateStockOwners({...profile(),legacyToolsRoot:'C:/work/this-release-stock-does-not-exist-729489'}));
 const source=fs.readFileSync(path.join(__dirname,mode,'tools/Verify-Release.ps1'),'utf8');
 const validation=source.indexOf("Invoke-Json @('profile',$profileFile)");
 assert(validation>=0&&validation<source.indexOf("(Join-Path $legacyToolsRoot 'tools/internal/Build-ValidationTools.ps1')"));
});
test('collector passes its explicit compiler selection into release aggregate validation',()=>{
 // Run the exact real call expression with boundary data; this is not runtime or compiler evidence.
 const source=fs.readFileSync(path.join(__dirname,mode,'tools/Collect-Verification.cjs'),'utf8');
 const expression=source.match(/releaseVerification\.validateReports\([\s\S]*?\);/)[0];
 for(const nativeToolchain of ['MSVC143','LLVM22MSVC142']){
   let observed;
   vm.runInNewContext(expression,{nativeToolchain,version:'0.5.0',root,generated:{},fresh:{},relocated:{},relocationSmoke:{},currentPackage:{},
     identity:{source:()=>({sha256:'source'}),hash:()=> 'package',encoded:()=>Buffer.alloc(0)},releaseVerification:{validateReports:(_reports,expected)=>{observed=expected;}}});
   assert.equal(observed.nativeToolchain,nativeToolchain);
 }
});
