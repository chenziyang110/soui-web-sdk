'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const repo='F:/work/soui-issue96',released='C:/work/sm05-release-050';
const id=require(repo+'/tools/internal/delivery-identity.cjs');
const manifest=require(repo+'/tools/internal/vendor-test-names.json');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const pin=p=>({path:p,bytes:fs.statSync(p).size,sha256:hash(p)});
const archive=repo+'/verification/production-validation/0.5-source30-release-2026-10-16/scoped-results';
const index=JSON.parse(fs.readFileSync(archive+'/INDEX.json','utf8'));
const inputs=[...id.walk(repo,'tests/native'),...id.walk(repo,'cmake'),
 ...require(repo+'/tools/internal/vendor-tests.cjs').requiredSourcePaths(repo).map(x=>path.relative(repo,x.path).replaceAll('\\','/')),
 'CMakeLists.txt','tools/internal/vendor-test-names.json','tools/internal/vendor-tests.cjs','tools/Test.ps1'];
const compared=inputs.map(relative=>{
 const currentFile=path.join(repo,relative),oldFile=path.join(released,relative),current=pin(currentFile),old=pin(oldFile);
 const a=fs.readFileSync(currentFile),b=fs.readFileSync(oldFile);
 const normalization=relative.startsWith('third_party/sources/miniblink/')?'none':id.normalization(relative,a);
 const normalizedHash=bytes=>crypto.createHash('sha256').update(id.normalize(bytes,normalization)).digest('hex');
 assert.equal(normalizedHash(a),normalizedHash(b),relative);
 return {path:relative,normalization,sha256:normalizedHash(a),currentRawSha256:current.sha256,releasedRawSha256:old.sha256};
});
const rows=[];
for(const architecture of ['x64','x86']){
 const folder='C:/work/sm05-source30-pipeline-2/unit-'+architecture+'-Release-source30-1';
 const result=JSON.parse(fs.readFileSync(folder+'/RESULT.json','utf8'));
 assert(result.natural&&result.stable&&result.result.passed&&result.result.exitCode===0&&result.result.closureConfirmed);
 assert.equal(result.terminal.activeProcesses,0);assert.equal(result.terminal.terminatedOwnedJob,false);
 const entry=index.rows.find(x=>path.resolve(x.original.path)===path.resolve(result.ctest.stdout.path));assert(entry);
 const log=path.join(archive,entry.archive.path),p=pin(log);assert.equal(p.sha256,entry.archive.sha256);assert.equal(p.bytes,entry.archive.bytes);
 assert.equal(hash(result.ctest.stdout.path),p.sha256);assert.equal(result.ctest.stdout.sha256,p.sha256);
 const text=fs.readFileSync(log,'utf8');
 const passed=[...text.matchAll(/^\s*\d+\/208 Test\s+#\d+: (kernel\.native\.\S+)\s+\.{2,}\s+Passed\s+/gm)].map(x=>x[1]).sort();
 assert.deepEqual(passed,manifest.names);assert.equal(new Set(passed).size,54);
 assert.match(text,/100% tests passed, 0 tests failed out of 208/);
 const runInput=JSON.parse(fs.readFileSync(folder+'/INPUTS.json','utf8'));assert(runInput.args.includes('-RequireVendorTests'));
 const buildInput='C:/work/sm05-source30-pipeline-2/build-'+architecture+'-Release-source30-1/INPUTS.json';
 assert(JSON.parse(fs.readFileSync(buildInput,'utf8')).args.includes('--with-vendor-tests'));
 rows.push({architecture,configuration:'Release',passedVendorCases:passed,exact54Names:true,scope:'Re-read original Source30 execution; no new native execution claimed',stdout:{path:path.relative(repo,log).replaceAll('\\','/'),bytes:p.bytes,sha256:p.sha256},result:pin(folder+'/RESULT.json'),unitInput:pin(folder+'/INPUTS.json'),buildInput:pin(buildInput),closureConfirmed:true});
}
const report={schema:'soui.issue76-existing-vendor-evidence/v1',version:'0.5.0',comparedSourceFiles:compared,rows,releaseReady:false};
fs.writeFileSync(__dirname+'/VENDOR-AUDIT.json',JSON.stringify(report,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({architectures:rows.map(x=>x.architecture),exactCasesPerArchitecture:54,unchangedNativeInputFiles:compared.length,releaseReady:false}));
