import {createHash} from 'node:crypto';
import {readdir, lstat, readFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {resolveRendererProfile,verifyPatchRecords,verifyPatchedSources,verifySourcePaths,verifyShippedRecipe} from './renderer-profiles.mjs';

const here=path.dirname(fileURLToPath(import.meta.url));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const lf=text=>text.replace(/\r\n/g,'\n');
const canonical=value=>JSON.stringify(value,(_,item)=>item && !Array.isArray(item) && typeof item==='object' ?
  Object.fromEntries(Object.keys(item).sort().map(key=>[key,item[key]])):item);
function rootAttributes(html) {
  // The generated entry has an explicit root after its whitespace, comments
  // and optional doctype. Do not accept an HTML-looking string inside a
  // comment/script, or an implicit browser root with no identity attributes.
  let remaining=html.replace(/^\uFEFF/,'').trimStart(),doctypeSeen=false;
  for(;;) {
    if(remaining.startsWith('<!--')) {
      const end=remaining.indexOf('-->');
      if(end<0)return new Map();
      remaining=remaining.slice(end+3).trimStart();continue;
    }
    const doctype=/^<!doctype\s+html\b[^>]*>/i.exec(remaining);
    if(!doctype || doctypeSeen)break;
    doctypeSeen=true;remaining=remaining.slice(doctype[0].length).trimStart();
  }
  const root=/^<html(?=\s|>)[^>]*>/i.exec(remaining),attributes=new Map();
  if(root)for(const item of root[0].matchAll(/\s+([\w:.-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/g)) {
    const name=item[1].toLowerCase(),values=attributes.get(name) ?? [];
    values.push(item[2] ?? item[3] ?? item[4]);attributes.set(name,values);
  }
  return attributes;
}

// Shared by package verification before and after copying the runtime. This
// does not replace verifySourceVendor: its identity must already be verified.
export async function verifyRuntimeSourceSelection(resources,sourceIdentity) {
  let record;
  try {record=JSON.parse(await readFile(path.join(resources,'scratch-legal/source-build.json'),'utf8'));}
  catch(error) {if(error.code!=='ENOENT')throw error;}
  if(!sourceIdentity) {
    if(record)throw new Error('Source-built runtime requires its explicit SourceVendor; stock packaging cannot adopt it.');
    return;
  }
  const sourceRecord=sourceIdentity.record;
  if(!record || record.schema!=='soui.scratch-runtime-source/v1' ||
      typeof sourceRecord.rendererProfile!=='string' ||
      record.rendererProfile!==resolveRendererProfile(sourceRecord.rendererProfile) ||
      canonical(record.patches)!==canonical(sourceRecord.patches) ||
      record.fullSourceRecordSha256!==sourceIdentity.recordSha256 ||
      record.sourceCommit!==sourceRecord.sourceCommit || record.sourceLockSha256!==sourceRecord.sourceLockSha256 ||
      record.upstreamSourceVersion!==sourceRecord.upstreamSourceVersion ||
      record.referenceArtifactVersion!==sourceRecord.referenceArtifactVersion ||
      record.bundleSha256!==sourceRecord.bundleSha256 ||
      sha(await readFile(path.join(resources,'scratch-gui-standalone.js')))!==sourceRecord.bundleSha256)
    throw new Error('Packaged runtime does not match the selected source vendor/profile; rebuild with the same SourceVendor.');
  const html=await readFile(path.join(resources,'index.html'),'utf8');
  const attributes=rootAttributes(html);
  for(const [name,value] of [['data-scratch-source-commit',sourceRecord.sourceCommit],
    ['data-scratch-source-version',sourceRecord.upstreamSourceVersion],['data-scratch-renderer-profile',sourceRecord.rendererProfile]]) {
    const values=attributes.get(name);
    if(!values || values.length!==1 || values[0]!==value)
      throw new Error('Packaged HTML does not identify the selected source vendor/profile.');
  }
}

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
  // Reject a renamed/mixed profile before hashing a complete source/dist tree.
  // The subsequent checks still verify every actual source and recipe byte.
  verifyPatchRecords(rendererProfile,record.patches,sha(reducer));
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
