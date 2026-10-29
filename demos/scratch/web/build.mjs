import {cp, mkdir, readFile, writeFile, stat, rm} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {buildOfflineAssets} from './offline-assets.mjs';
import {distInventory} from '../upstream/provenance.mjs';

const root = path.dirname(fileURLToPath(import.meta.url));
const cache = path.resolve(root, '../.scratch-cache');
const output = path.join(root, 'dist');
const lock = JSON.parse(await readFile(path.join(root, 'vendor-lock.json'), 'utf8'));
const buildArgs = process.argv.slice(2);
if (buildArgs.length && (buildArgs.length !== 2 || buildArgs[0] !== '--source-vendor'))
  throw new Error('Usage: node build.mjs [--source-vendor SOURCE_BUILD_OUTPUT]');
const sourceVendor = buildArgs.length ? path.resolve(buildArgs[1]) : null;
let sourceRecord = null;
if (sourceVendor) {
  sourceRecord = JSON.parse(await readFile(path.join(sourceVendor,'source-build.json'),'utf8'));
  const digest = bytes => createHash('sha256').update(bytes).digest('hex');
  if (sourceRecord.schema !== 'soui.scratch-source-build/v1' ||
      sourceRecord.sourceCommit !== lock.source.split('/').at(-1) ||
      sourceRecord.referenceArtifactVersion !== lock.version ||
      sourceRecord.sourceLockSha256 !== 'cabc070103553a7fe01332190b884c0dd8bc5517a64c13dc967f8afc92efa1ed' ||
      sourceRecord.patchDriverSha256 !== digest(await readFile(path.join(root,'../upstream/build.mjs'))) ||
      sourceRecord.reducerPatchSha256 !== digest((await readFile(path.join(root,'../upstream/targets-reducer.js'),'utf8')).replace(/\r\n/g,'\n')) ||
      sourceRecord.bundleSha256 !== digest(await readFile(path.join(sourceVendor,'dist/scratch-gui-standalone.js'))) ||
      JSON.stringify(sourceRecord.distFiles) !== JSON.stringify(await distInventory(path.join(sourceVendor,'dist'))))
    throw new Error('Scratch source candidate provenance or dist inventory mismatch.');
}
await mkdir(cache, {recursive: true});
const archive = path.join(cache, 'scratch-scratch-gui-standalone-15.3.0.tgz');
try { await stat(archive); } catch {
  const response = await fetch(lock.url);
  if (!response.ok) throw new Error(`Scratch download failed: ${response.status}`);
  await writeFile(archive, new Uint8Array(await response.arrayBuffer()));
}
const integrity = 'sha512-' + createHash('sha512').update(await readFile(archive)).digest('base64');
if (integrity !== lock.integrity) throw new Error('Scratch archive integrity mismatch; remove the cache archive and retry.');
const unpacked = path.join(cache, 'package');
if (!await stat(path.join(unpacked, 'dist/scratch-gui-standalone.js')).catch(() => null)) {
  const extracted = spawnSync('tar', ['-xf', archive, '-C', cache], {stdio: 'inherit'});
  if (extracted.status !== 0) throw new Error('Could not extract Scratch package.');
}
// This directory is generated exclusively by this script; never remove source/cache folders.
if (path.dirname(output) !== root || path.basename(output) !== 'dist') throw new Error('Invalid generated output path.');
await rm(output, {recursive:true, force:true});
await mkdir(output, {recursive: true});
await cp(sourceVendor ? path.join(sourceVendor,'dist') : path.join(unpacked, 'dist'), output, {
  recursive: true,
  filter: source => !source.endsWith('.map') && !source.endsWith('scratch-gui.js') && !source.endsWith('scratch-gui.js.LICENSE.txt')
});
await buildOfflineAssets(cache, unpacked);
await cp(path.join(cache, 'assets'), path.join(output, 'assets'), {recursive: true});
for (const file of ['index.html', 'app.css', 'app.js', 'lessons.js', 'performance-scenes.js', 'performance-meter.js'])
  await cp(path.join(root, file), path.join(output, file));
if (sourceRecord) {
  if (!/^\d+\.\d+\.\d+(?:[-+][\w.-]+)?$/.test(sourceRecord.upstreamSourceVersion))
    throw new Error('Invalid source vendor version identity.');
  const htmlPath = path.join(output,'index.html');
  const html = await readFile(htmlPath,'utf8');
  const anchor = '<html lang="zh-CN">';
  if (html.split(anchor).length !== 2) throw new Error('Scratch HTML identity anchor changed.');
  await writeFile(htmlPath,html.replace(anchor,`<html lang="zh-CN" data-scratch-source-commit="${sourceRecord.sourceCommit}" data-scratch-source-version="${sourceRecord.upstreamSourceVersion}">`));
}
// Build from this delivery's public client, retaining SDK session and renewal semantics.
const sdk = path.resolve(root, '../../../sdk/web');
const compiler = path.join(sdk, 'node_modules/typescript/bin/tsc');
const compile = spawnSync(process.execPath, [compiler, '-p', path.join(sdk, 'tsconfig.json')], {stdio: 'inherit'});
if (compile.status !== 0) throw new Error('Public SOUI Web client compilation failed; run npm --prefix sdk/web ci.');
await cp(path.join(sdk, 'dist'), path.join(output, 'soui'), {recursive: true});
await mkdir(path.join(output, 'scratch-legal'), {recursive: true});
for (const file of ['LICENSE', 'TRADEMARK', 'package.json'])
  await cp(path.join(unpacked, file), path.join(output, 'scratch-legal', `scratch-${file}`));
await cp(path.join(root, 'vendor-lock.json'), path.join(output, 'scratch-legal/vendor-lock.json'));
if (sourceRecord) await writeFile(path.join(output,'scratch-legal/source-build.json'),JSON.stringify({
  schema:'soui.scratch-runtime-source/v1',sourceCommit:sourceRecord.sourceCommit,
  sourceLockSha256:sourceRecord.sourceLockSha256,patches:sourceRecord.patches,
  bundleSha256:sourceRecord.bundleSha256,upstreamSourceVersion:sourceRecord.upstreamSourceVersion,
  referenceArtifactVersion:sourceRecord.referenceArtifactVersion,
  fullSourceRecordSha256:createHash('sha256').update(await readFile(path.join(sourceVendor,'source-build.json'))).digest('hex')
},null,2)+'\n');
await cp(path.join(cache, 'asset-manifest.json'), path.join(output, 'scratch-legal/asset-manifest.json'));
await writeFile(path.join(output, 'scratch-legal/README.txt'),
  'Local SOUI SDK Scratch demonstration. Scratch GUI is AGPL-3.0-only.\n' +
  'Scratch marks belong to Scratch Foundation; no endorsement is implied.\n' +
  'Reference package/source: ' + lock.url + '\n' + lock.source + '\n' +
  'GUI and bundled dependency notices: *.LICENSE.txt.\n' +
  (sourceRecord ?
    'Source candidate: ' + sourceRecord.upstreamSourceVersion + ' at ' + sourceRecord.sourceCommit + '.\n' +
    'Corresponding patched sources, dependency lock and build record are delivered separately in the SourceVendor output.\n' :
    'Original package sources are retained in the build cache at .scratch-cache/package/src; provide corresponding sources with redistribution.\n'));
console.log(`Scratch ${lock.version} offline web built: ${output}`);
