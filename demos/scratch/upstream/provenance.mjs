import {createHash} from 'node:crypto';
import {readdir, lstat, readFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {resolveRendererProfile,verifyPatchedSources,verifySourcePaths,verifyShippedRecipe} from './renderer-profiles.mjs';

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
  if(JSON.stringify(members)!==JSON.stringify(['LICENSE','dist','package-lock.json','source-build.json','sources','recipe'].sort()))
    throw new Error('Source vendor output has unexpected or missing members.');
  for(const name of members) {
    const info=await lstat(path.join(root,name));
    if(info.isSymbolicLink() || (['dist','sources','recipe'].includes(name) ? !info.isDirectory() : !info.isFile()))
      throw new Error('Source vendor members must be ordinary files/directories.');
  }
  const bytes=await readFile(path.join(root,'source-build.json'));
  const record=JSON.parse(bytes);
  if(typeof record.rendererProfile!=='string')throw new Error('Source build must explicitly record its renderer profile.');
  const rendererProfile=resolveRendererProfile(record.rendererProfile);
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
      JSON.stringify(record.recipeFiles)!==JSON.stringify(await distInventory(path.join(root,'recipe'))) ||
      sha(lf(await readFile(path.join(root,'package-lock.json'),'utf8')))!==lockSha ||
      sha(lf(await readFile(path.join(root,'sources/package-lock.json'),'utf8')))!==lockSha ||
      record.upstreamSourceVersion!==JSON.parse(await readFile(path.join(root,'sources/package.json'),'utf8')).version ||
      !/^\d+\.\d+\.\d+(?:[-+][\w.-]+)?$/.test(record.upstreamSourceVersion))
    throw new Error('Scratch source candidate provenance, source closure or dist inventory mismatch.');
  if(record.sourceMemberPathsSha256!==verifySourcePaths(record.sourceFiles.map(file=>file.path),rendererProfile))
    throw new Error('Corresponding source path identity differs.');
  if(!(await readFile(path.join(root,'LICENSE'))).equals(await readFile(path.join(root,'sources/LICENSE'))))
    throw new Error('Root license differs from the corresponding source license.');
  await verifyPatchedSources(path.join(root,'sources'),rendererProfile,record.patches,sha(reducer));
  await verifyShippedRecipe(path.join(root,'recipe'),rendererProfile,record.recipeFiles);
  return {record,recordSha256:sha(bytes)};
}
