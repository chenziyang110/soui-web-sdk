'use strict';
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const identity = require('./delivery-identity.cjs');
const {isHistoricalNativeBackup} = require('./native-payload.cjs');
const {isVersion} = require('./sdk-version.cjs');
const verifiedInputs = new Map();
const sha = name => crypto.createHash('sha256').update(verifiedInputs.get(name) || payloadBytes(name, 'Native payload')).digest('hex');
const args = process.argv.slice(2);
if (args.length && (args.length !== 2 || args[0] !== '--kernel-provenance')) {
 throw new Error('Usage: Update-DependencyManifest.cjs [--kernel-provenance DELIVERY_RELATIVE_JSON]');
}
const historicalProvenance = 'third_party/manifests/kernel-build-preview14.json';
const provenanceSource = args.length ? args[1] : historicalProvenance;
function need(value, message) { if (!value) throw new Error(message); }
const reservedMetadata = ['third_party/manifests/dependencies.json', 'third_party/manifests/license-review.json'];
function reservedIdentity(relative) {
 const full = identity.safePath(root, relative);
 try { return fs.statSync(full, {bigint:true}); }
 catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}
const [outputIdentity, reviewIdentity] = reservedMetadata.map(reservedIdentity);
need(!outputIdentity || !reviewIdentity || outputIdentity.dev !== reviewIdentity.dev || outputIdentity.ino !== reviewIdentity.ino,
 'Metadata output cannot alias independent license review');
function metadataInput(relative, role) {
 need(!reservedMetadata.includes(relative.toLowerCase()), role+' cannot reference reserved metadata');
 const full = identity.safePath(root, relative);
 const input = fs.statSync(full, {bigint:true});
 need(input.isFile(), role+' must be a delivered regular file');
 for (const reserved of reservedMetadata) {
  const existing = reservedIdentity(reserved);
  need(!existing || input.dev !== existing.dev || input.ino !== existing.ino, role+' cannot alias reserved metadata');
 }
 return full;
}
function payloadBytes(relative, role) {
 return fs.readFileSync(metadataInput(relative, role));
}
const sdkVersion = payloadBytes('VERSION', 'SDK VERSION').toString('utf8').trim();
need(isVersion(sdkVersion), 'Invalid SDK VERSION');
need(provenanceSource.startsWith('third_party/manifests/') && provenanceSource.endsWith('.json'),
 'Kernel provenance must be a delivery-relative JSON under third_party/manifests');
const provenanceBytes = payloadBytes(provenanceSource, 'Kernel provenance');
const kernel = JSON.parse(provenanceBytes.toString('utf8').replace(/^\uFEFF/, ''));
if (!args.length) need(identity.hash(identity.normalize(provenanceBytes, 'lf')) ===
 '9ead5a63f1c0db3081992278c40a1f8dc68c1ae0790327b7c3f89aac3813ea30', 'Historical kernel provenance identity differs');
need(kernel?.schema === 'soui.kernel-import/v1' && kernel.releaseReady === false && isVersion(kernel.sdkVersion),
 'Invalid kernel import schema/version/release scope');
need(args.length ? provenanceSource.toLowerCase() !== historicalProvenance.toLowerCase() && kernel.sdkVersion === sdkVersion :
 kernel.sdkVersion === '0.3.0-preview.14', 'Kernel provenance does not match the selected current or historical version');
need(Array.isArray(kernel.imports) && kernel.imports.length === 2 &&
 new Set(kernel.imports.map(item => item?.architecture)).size === 2 &&
 kernel.imports.every(item => ['x86', 'x64'].includes(item?.architecture)),
 'Kernel imports must contain exactly one x86 and one x64 record');
for (const imported of kernel.imports) {
 const expectedPath = 'third_party/prebuilt/'+imported.architecture+'/miniblink/node.dll';
 need(imported.path === expectedPath && Number.isSafeInteger(imported.bytes) && imported.bytes > 0 &&
  /^[a-f0-9]{64}$/.test(imported.sha256 || '') && /^[a-f0-9]{64}$/.test(imported.sourceInputSha256 || '') &&
  typeof imported.buildEvidence === 'string' && imported.buildEvidence.length > 0,
  'Invalid kernel import identity: '+imported.architecture);
 const evidencePath = args.length ? metadataInput(imported.buildEvidence, 'Current kernel build evidence') :
  identity.safePath(root, imported.buildEvidence);
 const raw = payloadBytes(imported.path, 'Kernel DLL');
 need(raw.length === imported.bytes && identity.hash(raw) === imported.sha256,
  'Kernel DLL bytes/hash differ from provenance: '+imported.architecture);
 need(raw.length >= 64 && raw.readUInt16LE(0) === 0x5a4d, 'Kernel DLL lacks an MZ header');
 const pe = raw.readUInt32LE(0x3c);
 need(pe >= 64 && pe <= raw.length - 24 && raw.readUInt32LE(pe) === 0x00004550 &&
  raw.readUInt16LE(pe + 4) === (imported.architecture === 'x64' ? 0x8664 : 0x014c),
  'Kernel DLL PE/COFF header is incomplete or architecture differs: '+imported.architecture);
 const optionalStart = pe + 24;
 const optionalBytes = raw.readUInt16LE(pe + 20);
 const sectionCount = raw.readUInt16LE(pe + 6);
 const is64 = imported.architecture === 'x64';
 need(optionalBytes >= (is64 ? 112 : 96) && optionalBytes <= raw.length - optionalStart &&
  raw.readUInt16LE(optionalStart) === (is64 ? 0x20b : 0x10b),
  'Kernel DLL optional header is incomplete or architecture differs: '+imported.architecture);
 const sectionStart = optionalStart + optionalBytes;
 need(sectionCount > 0 && sectionCount * 40 <= raw.length - sectionStart,
  'Kernel DLL section table is empty or incomplete: '+imported.architecture);
 need((raw.readUInt16LE(pe + 22) & 0x2000) !== 0, 'Kernel PE is not marked as a DLL');
 verifiedInputs.set(imported.path, raw);
}
need(kernel.imports[0].sourceInputSha256 === kernel.imports[1].sourceInputSha256,
 'Kernel imports must bind the same source-input identity');
verifiedInputs.set(provenanceSource, provenanceBytes);
const definitions = [
 ['soui-source', 'third_party/manifests/provenance/soui/SOURCE-MANIFEST.json', 'SOUI 2.9.0.3 lineage', 'upstream source notices'],
 ['miniblink-source', 'third_party/manifests/provenance/preview14/third_party/sources/miniblink/SOURCE-MANIFEST.json', 'Miniblink49 VS2022 working snapshot', 'upstream and bundled dependency notices'],
 ['miniblink-encoding-patch', 'third_party/manifests/provenance/preview14/third_party/patches/miniblink/0007-mark-utf8-source-encoding.patch', 'preview.7 post-import patch', 'same terms as Miniblink source'],
 ['miniblink-scheduler-patch', 'third_party/manifests/provenance/preview14/third_party/patches/miniblink/0008-bound-ready-task-priority-wait.patch', 'preview.14 ready-task fairness', 'same terms as Miniblink source'],
 ['miniblink-build-provenance', provenanceSource, kernel.sdkVersion+' dual-architecture import', 'build identity record'],
 ['zlib-source', 'third_party/sources/zlib/source-manifest.json', '1.2.11', 'Zlib'],
 ['json', 'third_party/include/json/json.hpp', 'pinned single header', 'MIT'],
 ['tailwind-utility-css', 'demos/workbench/web/apps/react/utility.css', '3.4.17 generated utility subset', 'MIT; third_party/licenses/tailwindcss-3.4.17-LICENSE.txt'],
 ...['x86','x64'].flatMap(arch => [
   ['miniblink-'+arch, 'third_party/prebuilt/'+arch+'/miniblink/node.dll', ('imported binary '+kernel.sdkVersion), 'binary redistribution review incomplete'],
   ['zlib-runtime-'+arch, 'third_party/prebuilt/'+arch+'/runtime/vc/zlib1.dll', '1.3.1 app-local', 'Zlib']
 ]),
 ['web-sdk', 'sdk/web/package-lock.json', 'lockfile', 'resolved package licenses'],
 ['template-web', 'templates/desktop-app/web/package-lock.json', 'lockfile', 'resolved package licenses'],
 ['modern-template-web', 'templates/modern-app/web/package-lock.json', 'lockfile', 'resolved package licenses'],
 ['webview2-sdk', 'sdk/renderers/webview2/dependency.json', '1.0.4258.31 optional SDK', 'Microsoft SDK LICENSE and NOTICE; Runtime separate'],
 ['demo-web', 'demos/workbench/web/package-lock.json', 'lockfile', 'resolved package licenses']
];
const dependencies = definitions.map(([id, source, version, license]) => {
 const raw=verifiedInputs.get(source) || payloadBytes(source, 'Dependency source');
 const sourceNormalization=identity.normalization(source,raw);
 return {id,source,version,license,sourceNormalization,sha256:identity.hash(identity.normalize(raw,sourceNormalization))};
});
const nativeFiles = [];
function walk(relative) {
 for(const entry of fs.readdirSync(path.join(root, relative), {withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))) {
  const name = relative+'/'+entry.name;
  if(entry.isDirectory()) walk(name);
  else if(entry.isFile() && !isHistoricalNativeBackup(name)) nativeFiles.push({path:name, bytes:verifiedInputs.has(name) ? verifiedInputs.get(name).length : fs.statSync(path.join(root,name)).size, sha256:sha(name)});
 }
}
walk('third_party/prebuilt');
const manifest = {schema:'soui.dependencies/v2', sdkVersion,
 releaseReady:false, dependencies, nativeFiles,
 notes:['SOURCE-MANIFEST hashes identify exported source snapshots; build products are excluded from full-source packaging.',
 'Microsoft VC/UCRT redistributables are app-local binaries, with no source redistribution.',
 'Historical node.dll.before-gpu-lcd files remain in full source archives but are excluded from active native dependencies and SDK Core packages.',
 'Full per-file source delivery integrity is recorded separately in source-manifest.json.']};
fs.writeFileSync(path.join(root,'third_party/manifests/dependencies.json'),JSON.stringify(manifest,null,2)+'\n');
// Independent license-review.json is owned by review, never by metadata generation.
console.log(JSON.stringify({dependencies:dependencies.length,nativeFiles:nativeFiles.length}));
