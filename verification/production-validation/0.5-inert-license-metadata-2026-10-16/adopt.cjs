'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=fs.realpathSync('C:/work/sm05-release-050'),id=require(root+'/tools/internal/delivery-identity.cjs');
const record=JSON.parse(fs.readFileSync(__dirname+'/RENAMES.json'));
assert(JSON.parse(fs.readFileSync(__dirname+'/NATIVE3-VERIFIED.json')).passed);
assert.deepEqual(fs.readFileSync(root+'/third_party/licenses/npm/index.json'),fs.readFileSync(__dirname+'/original/third_party/licenses/npm/index.json'));
fs.copyFileSync(root+'/source-manifest.json',__dirname+'/source23-manifest.json',fs.constants.COPYFILE_EXCL);
for(const change of record.changes){
 const source=id.safePath(root,change.from),target=id.safePath(root,change.to);
 assert(source.startsWith(root+path.sep)&&target.startsWith(root+path.sep));
 assert(!fs.existsSync(target));assert.equal(id.hash(fs.readFileSync(source)),change.sha256);
}
for(const change of record.changes){
 const source=id.safePath(root,change.from),target=id.safePath(root,change.to);fs.renameSync(source,target);
 assert.equal(id.hash(fs.readFileSync(target)),change.sha256);
}
fs.copyFileSync(__dirname+'/proposed/third_party/licenses/npm/index.json',root+'/third_party/licenses/npm/index.json');
fs.writeFileSync(__dirname+'/ADOPTED.json',JSON.stringify({adopted:true,changes:record.changes,hostValidationChanged:false,licenseTextChanged:false},null,2),{flag:'wx'});
console.log(JSON.stringify({adopted:true,declarations:record.changes.length}));
