'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs'),id=require(root+'/tools/internal/delivery-identity.cjs');
const reviewPath=root+'/third_party/manifests/license-review.json',importsPath=root+'/third_party/manifests/kernel-build-050.json';
const before=e.pin(reviewPath),importsPin=e.pin(importsPath),sbomPin=e.pin(root+'/verification/sbom-native.cdx.json');
const prior=JSON.parse(fs.readFileSync(reviewPath)),imports=JSON.parse(fs.readFileSync(importsPath)),sbom=JSON.parse(fs.readFileSync(sbomPin.path));
assert.equal(prior.status,'unverified');assert.equal(prior.releaseReady,false);assert.equal(imports.sdkVersion,'0.5.0');
assert.equal(imports.imports.length,2);assert.deepEqual(imports.imports.map(x=>x.architecture).sort(),['x64','x86']);
const sources=[],binaries=[];
for(const row of imports.imports){
 assert.equal(row.path,'third_party/prebuilt/'+row.architecture+'/miniblink/node.dll');
 const pin=e.pin(id.safePath(root,row.path));assert.equal(pin.bytes,row.bytes);assert.equal(pin.sha256,row.sha256);sources.push(pin);
 const component=sbom.components.find(x=>x.name===row.path);assert(component);assert(component.hashes.some(x=>x.alg==='SHA-256'&&x.content===pin.sha256));
 for(const relative of [row.buildEvidence,row.evidenceIndex])sources.push(e.pin(id.safePath(root,relative)));
 binaries.push({architecture:row.architecture,path:row.path,bytes:row.bytes,sha256:row.sha256,sourceInputSha256:row.sourceInputSha256,
  buildEvidence:row.buildEvidence,evidenceIndex:row.evidenceIndex});
}
const references=['third_party/manifests/kernel-build-050.json','third_party/manifests/kernel-cache-source-050.json',
 'third_party/patches/miniblink/0012-retained-resource-byte-budget.patch','verification/sbom-native.cdx.json','third_party/licenses/kernel-source/index.json.txt',
 ...imports.imports.flatMap(x=>[x.buildEvidence,x.evidenceIndex])];
references.forEach(relative=>sources.push(e.pin(id.safePath(root,relative))));
const proposal={schema:prior.schema,status:'unverified',releaseReady:false,sdkVersion:'0.5.0',
 scope:'Exact current 0.5.0 candidate Miniblink binaries; complete redistribution review remains pending',binaries,
 evidence_refs:references,required_evidence:prior.required_evidence,
 limitations:['Binary identity and traceable link-input evidence are not legal approval.',
  'Static archives listed by the SBOM do not prove every archive member was retained.',
  'Final runtime package notice completeness and applicable redistribution terms remain to be reviewed.']};
fs.mkdirSync(__dirname+'/proposal/third_party/manifests',{recursive:true});
fs.copyFileSync(reviewPath,__dirname+'/before-license-review.json',fs.constants.COPYFILE_EXCL);
const output=__dirname+'/proposal/third_party/manifests/license-review.json';fs.writeFileSync(output,JSON.stringify(proposal,null,2)+'\n',{flag:'wx'});
[before,importsPin,sbomPin,...sources].forEach(e.checkPin);
fs.writeFileSync(__dirname+'/PROPOSAL.json',JSON.stringify({before,after:e.pin(output),inputs:[importsPin,sbomPin,...sources],
 identityVerified:true,actualLegalReviewCompleted:false,adopted:false,currentCandidateUnchanged:true,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({prepared:true,binaries:binaries.map(x=>({architecture:x.architecture,sha256:x.sha256})),actualLegalReviewCompleted:false,adopted:false}));
