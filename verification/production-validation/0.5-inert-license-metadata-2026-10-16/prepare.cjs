'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',work=__dirname,id=require(root+'/tools/internal/delivery-identity.cjs');
for(const which of ['original','proposed']){
  const target=work+'/'+which;assert(!fs.existsSync(target));fs.mkdirSync(target);
  for(const name of ['THIRD_PARTY_NOTICES.md',...id.walk(root,'third_party/licenses'),
    'tools/internal/deploy-resources.cjs','tools/internal/delivery-identity.cjs','tests/sdk/legal-notice-materials.test.cjs']){
    const to=id.safePath(target,name);fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(id.safePath(root,name),to,fs.constants.COPYFILE_EXCL);
  }
}
const proposed=work+'/proposed',indexPath=proposed+'/third_party/licenses/npm/index.json';
const index=JSON.parse(fs.readFileSync(indexPath)),changes=[];
for(const entry of index.packages){
 const from=entry.declaration.path,to=from+'.txt';assert(from.endsWith('/upstream-package.json'));
 const source=id.safePath(proposed,from),target=id.safePath(proposed,to);assert(!fs.existsSync(target));
 assert.equal(id.hash(fs.readFileSync(source)),entry.declaration.sha256);
 fs.renameSync(source,target);assert.equal(id.hash(fs.readFileSync(target)),entry.declaration.sha256);
 entry.declaration.path=to;changes.push({from,to,bytes:entry.declaration.bytes,sha256:entry.declaration.sha256});
}
fs.writeFileSync(indexPath,JSON.stringify(index,null,2)+'\n');
fs.writeFileSync(work+'/RENAMES.json',JSON.stringify({changes,licenseTextChanged:false,runtimeValidatorChanged:false},null,2),{flag:'wx'});
for(const which of ['original','proposed']){
 const web=work+'/'+which+'/web';fs.mkdirSync(web);fs.writeFileSync(web+'/index.html','<div>local</div>');
 require(root+'/tools/internal/deploy-resources.cjs').replace(web,work+'/'+which+'/app',work+'/'+which);
}
console.log(JSON.stringify({declarations:changes.length,preservedBytes:true}));
