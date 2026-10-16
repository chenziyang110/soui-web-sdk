'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const packet=JSON.parse(fs.readFileSync(__dirname+'/PROPOSAL.json'));
e.checkPin(packet.currentSource);packet.originals.forEach(e.checkPin);packet.files.forEach(row=>{if(row.before)e.checkPin(row.before);e.checkPin(row.after);});
const material=__dirname+'/distribution',application=__dirname+'/application',web=__dirname+'/web';
for(const item of [material,application,web])assert(!fs.existsSync(item));
fs.mkdirSync(material);fs.mkdirSync(web);
fs.cpSync(root+'/third_party/licenses',material+'/third_party/licenses',{recursive:true,errorOnExist:true,force:false});
fs.copyFileSync(root+'/THIRD_PARTY_NOTICES.md',material+'/THIRD_PARTY_NOTICES.md');
for(const row of packet.files){const target=material+'/'+row.relative;fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(row.after.path,target);}
fs.writeFileSync(web+'/index.html','<!doctype html><title>Local material deployment</title>');
const result=require(root+'/tools/internal/deploy-resources.cjs').replace(web,application,material);
const index=JSON.parse(fs.readFileSync(material+'/third_party/licenses/kernel-source/index.json.txt'));
const verified=[];
for(const row of index.materials){
 for(const relative of row.retainedAt){
  const source=fs.readFileSync(root+'/'+row.source),copied=fs.readFileSync(application+'/resources/legal/'+relative);
  assert.deepEqual(copied,source);const pin=e.pin(application+'/resources/legal/'+relative);
  assert.equal(pin.bytes,row.bytes);assert.equal(pin.sha256,row.sha256);verified.push({source:row.source,...pin});
 }
}
e.checkPin(packet.currentSource);packet.originals.forEach(e.checkPin);
fs.writeFileSync(__dirname+'/DEPLOYMENT-VERIFIED.json',JSON.stringify({passed:true,scope:'Exact notice source bytes and real deployment copies, without native launch or binary legal approval',proposal:e.pin(__dirname+'/PROPOSAL.json'),helper:e.pin(__filename),result,verified,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({passed:true,sourceDocuments:index.materials.length,verifiedCopies:verified.length,deployedFiles:result.files}));
