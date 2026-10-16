'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',arch=process.argv[2],out=__dirname+'/kernel-'+arch+'-1';assert(['x64','x86'].includes(arch));
const r=JSON.parse(fs.readFileSync(out+'/export-comparison.json','utf8'));
const names=name=>[...fs.readFileSync(out+'/'+name+'-exports.txt','utf8').matchAll(/^  Name: (.*)$/gm)].map(m=>m[1].trim());
const before=names('old'),after=names('rebuilt'),header=fs.readFileSync(root+'/third_party/include/miniblink/wke.h','utf8');
const declared=[...new Set([...header.matchAll(/ITERATOR\d+\([^,]+,\s*((?:wke|js)\w+)\s*,/g)].map(m=>m[1]))];assert(declared.length>250);
const direct=new Set(['wkeInitialize','wkeInitializeEx','wkePrepareWorkerShutdown','wkeFinalizePrepared']);
for(const name of ['sdk/src/host/source_kernel_api.h','sdk/src/host/runtime_shutdown_api.h','sdk/src/host/soui_miniblink_host.cpp']){
 const text=fs.readFileSync(root+'/'+name,'utf8');
 for(const m of text.matchAll(/GetProcAddress\([^,]+,\s*"((?:wke|js)\w+)"/g))direct.add(m[1]);
 for(const m of text.matchAll(/BIND_SOURCE_API\(((?:wke|js)\w+)\)/g))direct.add(m[1]);
}
const oldPublic=before.filter(n=>/^(wke|js)/.test(n)),required=[...new Set([...declared,...direct,...oldPublic])].sort();
const missingRequired=required.filter(n=>!after.includes(n));
const classified=r.missing.map(name=>({name,category:/^ares_[a-z0-9_]+$/.test(name)?'static-c-ares':name.startsWith('?')&&(/basic_ios/.test(name)||/_Move_assign_unequal_alloc.*@v8@@/.test(name))?'compiler-STL-or-V8-template':'unexplained'}));
const result={architecture:arch,sourceIdentity:r.sourceIdentity,dll:r.artifact,headerFunctionCount:declared.length,oldPublicCount:oldPublic.length,
 required,missingRequired,allExportDifferencePreserved:out+'/export-comparison.json',classifiedMissing:classified,
 allOldExportsPreserved:r.missing.length===0,publicApiSymbolsPreserved:missingRequired.length===0,
 binaryAbiQualified:false,fullRuntimeQualified:false};
fs.writeFileSync(out+'/public-api-audit.json',JSON.stringify(result,null,2));
console.log(JSON.stringify({arch,declared:declared.length,oldPublic:oldPublic.length,required:required.length,missingRequired,missingCategories:classified.reduce((a,x)=>(a[x.category]=(a[x.category]||0)+1,a),{})}));
assert.equal(missingRequired.length,0);assert(classified.every(x=>x.category!=='unexplained'));
