'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process'),zlib=require('node:zlib'),assert=require('node:assert/strict');
const base=__dirname,repo='F:/work/soui-issue96',root=path.join(base,'portable');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>{const b=fs.readFileSync(p);return {path:p,bytes:b.length,sha256:sha(b)};};
const read=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
const reports=[];
for(const arch of ['x64','x86']){
 const r=read(base+'/build-'+arch+'/RESULT.json');assert(r.natural&&r.sourceUnchanged&&r.vendorDefaultOff&&r.sdkTestsOn);
 const output=cp.spawnSync('C:/Program Files/CMake/bin/ctest.exe',['--test-dir',root+'/out/'+arch,'-C','Release','--show-only=json-v1'],{encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:8*1024*1024});
 assert.equal(output.status,0,output.stderr);fs.writeFileSync(base+'/registry-'+arch+'.json',output.stdout,{flag:'wx'});
 const registry=require(repo+'/tools/internal/vendor-tests.cjs').registry(output.stdout,'OFF');
 assert(registry.registeredNativeTests>100&&registry.registeredVendorTests===0);
 const build=read(root+'/out/'+arch+'/build-Release.json');assert.equal(build.nativeBuild,'passed');
 reports.push({architecture:arch,configuration:'Release',defaultAllBuilt:true,freshArchitectureBuild:true,
  vendorOverrideSupplied:false,vendorDefaultOff:true,sdkTestsOn:true,sourceUnchanged:true,
  registeredNativeTests:registry.registeredNativeTests,registeredVendorTests:0,nativeTestsExecutedInThisPortableBuild:false,
  nativeBuildId:build.buildId,sourceSha256:build.source.sha256,naturalProcessClosure:true,releaseReady:false});
}
const testLog=fs.readFileSync(base+'/vendor-tests.log','utf8');assert.match(testLog,/tests 21/);assert.match(testLog,/pass 21/);assert.match(testLog,/fail 0/);assert.match(testLog,/skipped 0/);
const dest=repo+'/verification/production-validation/issue-maintenance-2026-10-16';fs.mkdirSync(dest+'/raw',{recursive:true});
const sources=[__filename,base+'/PREPARED.json',base+'/execute.cjs',base+'/audit-vendor.cjs',base+'/VENDOR-AUDIT.json',base+'/audit-notices.py',base+'/NOTICE-AUDIT.json',base+'/vendor-tests.log',base+'/registry-x64.json',base+'/registry-x86.json'];
for(const arch of ['x64','x86']){
 const dir=base+'/build-'+arch;
 for(const name of fs.readdirSync(dir))if(fs.statSync(dir+'/'+name).isFile())sources.push(dir+'/'+name);
 for(const name of ['CMakeCache.txt','native-Release.log','build-Release.json'])sources.push(root+'/out/'+arch+'/'+name);
 for(const name of ['RESULT.json','INPUTS.json'])sources.push('C:/work/sm05-source30-pipeline-2/unit-'+arch+'-Release-source30-1/'+name);
 sources.push('C:/work/sm05-source30-pipeline-2/build-'+arch+'-Release-source30-1/INPUTS.json');
}
sources.push('C:/work/sm05-source30-application-packages-2/VERIFIED.json');
const rows=sources.map((source,i)=>{
 const raw=fs.readFileSync(source),compressed=raw.length>128*1024;
 const name=String(i).padStart(3,'0')+'-'+path.basename(source)+(compressed?'.gz':'');
 const output=compressed?zlib.gzipSync(raw):raw;
 fs.writeFileSync(dest+'/raw/'+name,output,{flag:'wx'});
 return {original:pin(source),archive:{path:'raw/'+name,bytes:output.length,sha256:sha(output)},encoding:compressed?'gzip':'identity'};
});
save(dest+'/INDEX.json',{schema:'soui.issue-maintenance-archive/v1',rows,externalBuildDirectoriesRetained:true,releaseReady:false});
const vendor=read(base+'/VENDOR-AUDIT.json'),notices=read(base+'/NOTICE-AUDIT.json');
save(dest+'/SUMMARY.json',{schema:'soui.issue-maintenance-summary/v1',version:'0.5.0',sourceCommit:'40f04eb1f63158c6489fd23509d66fb47f2f38c6',
 portableBuilds:reports,configurationTests:{passed:21,failed:0,skipped:0,actualCMakePolicyCases:7,syntheticRequiredFiles:true,privateKernelCompilationInPolicyFixtures:false},
 source30VendorEvidence:{exactCasesPerArchitecture:54,architectures:['x64','x86'],unchangedNativeInputFiles:vendor.comparedSourceFiles.length,originalLogsReused:true,newNativeTestExecution:false},
 applicationNotices:{exactReleaseZipHashes:true,archives:6,sourceNoticeFilesPerArchive:notices.sourceNotices.length,openSslSsLeayTextPresent:true,completeLinkedComponentInventory:false,redistributionTermsApproved:false},
 remaining:{messagePortFourArms:false,realShellResidency:false,win7Interaction:false,longRunSession:false,fullProductionAcceptance:false},releaseReady:false});
console.log(JSON.stringify({archivedFiles:rows.length,portableBuilds:reports,noticesPerArchive:notices.sourceNotices.length}));
