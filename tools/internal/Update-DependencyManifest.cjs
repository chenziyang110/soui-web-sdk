'use strict';
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const identity = require('./delivery-identity.cjs');
const sha = name => crypto.createHash('sha256').update(fs.readFileSync(path.join(root, name))).digest('hex');
const definitions = [
 ['soui-source', 'third_party/sources/soui/SOURCE-MANIFEST.json', 'SOUI 2.9.0.3 lineage', 'upstream source notices'],
 ['miniblink-source', 'third_party/sources/miniblink/SOURCE-MANIFEST.json', 'Miniblink49 VS2022 working snapshot', 'upstream and bundled dependency notices'],
 ['miniblink-encoding-patch', 'third_party/patches/miniblink/0007-mark-utf8-source-encoding.patch', 'preview.7 post-import patch', 'same terms as Miniblink source'],
 ['zlib-source', 'third_party/sources/zlib/source-manifest.json', '1.2.11', 'Zlib'],
 ['json', 'third_party/include/json/json.hpp', 'pinned single header', 'MIT'],
 ...['x86','x64'].flatMap(arch => [
   ['miniblink-'+arch, 'third_party/prebuilt/'+arch+'/miniblink/node.dll', 'rebuilt preview 2026-09-23', 'binary redistribution review incomplete'],
   ['zlib-runtime-'+arch, 'third_party/prebuilt/'+arch+'/runtime/vc/zlib1.dll', '1.2.11 app-local', 'Zlib']
 ]),
 ['web-sdk', 'sdk/web/package-lock.json', 'lockfile', 'resolved package licenses'],
 ['template-web', 'templates/desktop-app/web/package-lock.json', 'lockfile', 'resolved package licenses'],
 ['demo-web', 'demos/workbench/web/package-lock.json', 'lockfile', 'resolved package licenses']
];
const dependencies = definitions.map(([id, source, version, license]) => {
 const raw=fs.readFileSync(path.join(root,source));
 const sourceNormalization=identity.normalization(source,raw);
 return {id,source,version,license,sourceNormalization,sha256:identity.hash(identity.normalize(raw,sourceNormalization))};
});
const nativeFiles = [];
function walk(relative) {
 for(const entry of fs.readdirSync(path.join(root, relative), {withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))) {
  const name = relative+'/'+entry.name;
  if(entry.isDirectory()) walk(name);
  else if(entry.isFile()) nativeFiles.push({path:name, bytes:fs.statSync(path.join(root,name)).size, sha256:sha(name)});
 }
}
walk('third_party/prebuilt');
const manifest = {schema:'soui.dependencies/v2', sdkVersion:fs.readFileSync(path.join(root,'VERSION'),'utf8').trim(),
 releaseReady:false, dependencies, nativeFiles,
 notes:['SOURCE-MANIFEST hashes identify exported source snapshots; build products are excluded from full-source packaging.',
 'Microsoft VC/UCRT redistributables are app-local binaries, with no source redistribution.',
 'Full per-file source delivery integrity is recorded separately in source-manifest.json.']};
fs.writeFileSync(path.join(root,'third_party/manifests/dependencies.json'),JSON.stringify(manifest,null,2)+'\n');
const review = {schema:'soui.binary-license-review/v1',status:'unverified',releaseReady:false,
 scope:'Exact preview Miniblink binaries and their linked third-party configuration',
 binaries:dependencies.filter(x=>/^miniblink-x/.test(x.id)).map(x=>({architecture:x.id.slice(10),sha256:x.sha256})),
 evidence_refs:['third_party/sources/miniblink/SOURCE-MANIFEST.json','third_party/sources/miniblink/LOCAL-CHANGES.patch',
 'third_party/patches/miniblink/README.md','third_party/patches/miniblink/0007-mark-utf8-source-encoding.patch',
 'verification/production-validation/kernel-link-inputs-2026-09-25/observation.json',
 'verification/production-validation/kernel-notice-inventory-2026-09-25/README.md',
 'verification/production-validation/kernel-notice-recovery-2026-09-25/README.md'],
 required_evidence:['Applicable redistribution terms for complete binaries','Linked third-party notices and build configuration review']};
fs.writeFileSync(path.join(root,'third_party/manifests/license-review.json'),JSON.stringify(review,null,2)+'\n');
console.log(JSON.stringify({dependencies:dependencies.length,nativeFiles:nativeFiles.length}));
