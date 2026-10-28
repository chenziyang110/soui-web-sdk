'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root='F:/work/soui-issue96',sdk=require(root+'/tools/sdk/soui-sdk.cjs');
const output=path.join(__dirname,'license-delivery-2');fs.mkdirSync(output);
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const previous='C:/work/sm05-source30-post-2/sdk-package-0.5.0',next=path.join(output,'sdk-0.5.1-preview.1'),project=path.join(output,'business');
const oldPin=hash(previous+'/sdk-manifest.json');
sdk.pack(root,next,'0.5.1-preview.1');const manifest=sdk.readPackage(next);
assert(manifest.files.some(f=>f.path==='LICENSE.md'));assert.equal(manifest.releaseReady,false);
fs.mkdirSync(project);fs.writeFileSync(project+'/business.txt','Keep business data unchanged.\n');const businessHash=hash(project+'/business.txt');
const operations=[];
for(const pkg of [previous,next,previous,next]){
 const result=sdk.install(pkg,project),lock=sdk.readLock(project);
 assert.equal(hash(project+'/business.txt'),businessHash);
 const licensePresent=fs.existsSync(project+'/LICENSE.md');
 assert.equal(licensePresent,lock.manifest.version==='0.5.1-preview.1');
 if(licensePresent)assert.equal(hash(project+'/LICENSE.md'),hash(root+'/LICENSE.md'));
 operations.push({version:lock.manifest.version,result,licensePresent,businessUnchanged:true});
}
assert.equal(hash(previous+'/sdk-manifest.json'),oldPin);
const outputRecord={schema:'soui.first-party-license-delivery/v1',version:manifest.version,packageManifestSha256:hash(next+'/sdk-manifest.json'),previousManifestSha256:oldPin,licenseSha256:hash(root+'/LICENSE.md'),files:manifest.files.length,operations,published:false,productionQualificationComplete:false,releaseReady:false};
fs.writeFileSync(output+'/RESULT.json',JSON.stringify(outputRecord,null,2)+'\n');console.log(JSON.stringify(outputRecord));
