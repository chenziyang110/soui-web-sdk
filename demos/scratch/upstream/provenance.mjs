import {createHash} from 'node:crypto';
import {readdir, lstat, readFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here=path.dirname(fileURLToPath(import.meta.url));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const lf=text=>text.replace(/\r\n/g,'\n');

export async function distInventory(root, relative='') {
  const files=[];
  for(const item of (await readdir(path.join(root,relative),{withFileTypes:true})).sort((a,b)=>a.name.localeCompare(b.name,'en'))) {
    const name=relative ? relative+'/'+item.name : item.name;
    const info=await lstat(path.join(root,name));
    if(info.isSymbolicLink()) throw new Error('Source vendor links are not allowed.');
    if(info.isDirectory()) files.push(...await distInventory(root,name));
    else if(info.isFile()) files.push({path:name,bytes:info.size,
      sha256:createHash('sha256').update(await readFile(path.join(root,name))).digest('hex')});
    else throw new Error('Unexpected source vendor entry.');
  }
  return files;
}

// Shared adoption boundary for the Web build and local package. A selected
// source build must carry every corresponding source plus its locked inputs;
// matching only the generated bundle is insufficient for reproducible delivery.
export async function verifySourceVendor(root) {
  const rootInfo=await lstat(root);
  if(!rootInfo.isDirectory() || rootInfo.isSymbolicLink())throw new Error('Source vendor must be an ordinary directory.');
  const members=(await readdir(root)).sort();
  if(JSON.stringify(members)!==JSON.stringify(['LICENSE','dist','package-lock.json','source-build.json','sources'].sort()))
    throw new Error('Source vendor output has unexpected or missing members.');
  for(const name of members)if((await lstat(path.join(root,name))).isSymbolicLink())throw new Error('Source vendor links are not allowed.');
  const bytes=await readFile(path.join(root,'source-build.json'));
  const record=JSON.parse(bytes);
  const lock=JSON.parse(await readFile(path.join(here,'../web/vendor-lock.json'),'utf8'));
  const lockSha='cabc070103553a7fe01332190b884c0dd8bc5517a64c13dc967f8afc92efa1ed';
  const reducer=lf(await readFile(path.join(here,'targets-reducer.js'),'utf8'));
  if(record.schema!=='soui.scratch-source-build/v1' ||
      record.sourceCommit!==lock.source.split('/').at(-1) || record.referenceArtifactVersion!==lock.version ||
      record.sourceLockSha256!==lockSha ||
      record.patchDriverSha256!==sha(await readFile(path.join(here,'build.mjs'))) ||
      record.provenanceModuleSha256!==sha(await readFile(fileURLToPath(import.meta.url))) ||
      record.reducerPatchSha256!==sha(reducer) ||
      record.bundleSha256!==sha(await readFile(path.join(root,'dist/scratch-gui-standalone.js'))) ||
      JSON.stringify(record.distFiles)!==JSON.stringify(await distInventory(path.join(root,'dist'))) ||
      JSON.stringify(record.sourceFiles)!==JSON.stringify(await distInventory(path.join(root,'sources'))) ||
      sha(lf(await readFile(path.join(root,'package-lock.json'),'utf8')))!==lockSha ||
      sha(lf(await readFile(path.join(root,'sources/package-lock.json'),'utf8')))!==lockSha ||
      record.upstreamSourceVersion!==JSON.parse(await readFile(path.join(root,'sources/package.json'),'utf8')).version ||
      !/^\d+\.\d+\.\d+(?:[-+][\w.-]+)?$/.test(record.upstreamSourceVersion))
    throw new Error('Scratch source candidate provenance, source closure or dist inventory mismatch.');
  const patches=record.patches;
  const reducerPath='packages/scratch-gui/src/reducers/targets.js';
  const runtimePath='packages/scratch-vm/src/engine/runtime.js';
  if(!Array.isArray(patches) || patches.length!==2 || patches[0].path!==reducerPath || patches[1].path!==runtimePath ||
      patches[0].beforeSha256!=='1f0c42994e269e0e297ebb381aafdbc2d1e1ced1bf83a31f7ae12a4e4ed3dadc' ||
      patches[1].beforeSha256!=='c316f4ec4cf0e4c868c585c411cf126a1494f967b5645ceddbc0e276db599ca4' ||
      patches[0].afterSha256!==sha(reducer) ||
      sha(lf(await readFile(path.join(root,'sources',reducerPath),'utf8')))!==patches[0].afterSha256 ||
      sha(lf(await readFile(path.join(root,'sources',runtimePath),'utf8')))!==patches[1].afterSha256)
    throw new Error('Scratch patched corresponding sources differ from the build record.');
  return {record,recordSha256:sha(bytes)};
}
