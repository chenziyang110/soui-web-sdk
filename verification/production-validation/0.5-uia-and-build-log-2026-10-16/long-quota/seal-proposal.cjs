'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',folder=__dirname,proposal=folder+'/proposal';
const pin=file=>{const raw=fs.readFileSync(file);return {path:path.resolve(file),bytes:raw.length,sha256:crypto.createHash('sha256').update(raw).digest('hex')};};
const files=fs.readdirSync(proposal,{recursive:true}).filter(name=>fs.statSync(path.join(proposal,name)).isFile()).sort();
const changes=[];for(const name of files){const current=path.join(root,name),proposed=path.join(proposal,name),after=pin(proposed),before=fs.existsSync(current)?pin(current):null;
 if(before&&before.sha256===after.sha256)continue;
 if(before){const backup=path.join(folder,'before',name);fs.mkdirSync(path.dirname(backup),{recursive:true});if(!fs.existsSync(backup))fs.copyFileSync(current,backup,fs.constants.COPYFILE_EXCL);assert.equal(pin(backup).sha256,before.sha256);}
 changes.push({relative:name.replaceAll('\\','/'),before,after});
}
const tests=[['coverage-green.txt',28],['binding-green.txt',71],['cache-final.txt',12],['resource-final.txt',43]];
for(const [file,count] of tests){const text=fs.readFileSync(folder+'/'+file,'utf8');assert(text.includes('tests '+count)&&text.includes('pass '+count)&&text.includes('fail 0'));}
const legacy=JSON.parse(fs.readFileSync(folder+'/legacy-final.json'));assert.equal(legacy.passed,106);assert.equal(legacy.failed,0);
const sealed=JSON.parse(fs.readFileSync('C:/work/sm05-source24-pipeline-2/SEALED.json'));
assert.equal(pin(root+'/source-manifest.json').sha256,sealed.sourceManifest.sha256);
fs.writeFileSync(folder+'/PROPOSAL-v2.json',JSON.stringify({schema:'soui.private-long-quota-proposal/1',supersedes:'PROPOSAL.json',proposedAt:new Date().toISOString(),adopted:false,candidateUnchanged:true,source24Seal:sealed.sourceManifest,
 changes,tests:[...tests.map(([file,count])=>({count,result:pin(folder+'/'+file)})),{count:106,result:pin(folder+'/legacy-final.json')}],
 pureRegressionCount:260,actualLongRun:false,fullCacheBudget:'not-evaluated',releaseReady:false},null,2),{flag:'wx'});
console.log(JSON.stringify({changedFiles:changes.length,pureRegressionCount:260,adopted:false,candidateUnchanged:true}));
