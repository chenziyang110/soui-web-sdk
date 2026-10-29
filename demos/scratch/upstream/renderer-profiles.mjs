import {createHash} from 'node:crypto';
import {readFile, lstat, writeFile, mkdir} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here=path.dirname(fileURLToPath(import.meta.url));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const lf=text=>text.replace(/\r\n/g,'\n');
const canonical=value=>JSON.stringify(value,(_,item)=>item && !Array.isArray(item) && typeof item==='object' ?
  Object.fromEntries(Object.keys(item).sort().map(key=>[key,item[key]])):item);
const rendererDefinitions=[
  {path:'packages/scratch-render/src/RenderWebGL.js',beforeSha256:'f31804dba07683b38677d5be88afe8ee6f35906ae9ce763afd9d19bede23c7a2',
    afterSha256:'6d05af5925e2b0aa8e76adf131a703c6a3a30f83452e7012d5211265dae37731',asset:'RenderWebGL.js'},
  {path:'packages/scratch-render/src/Silhouette.js',beforeSha256:'101f6bd3292b3ca6c9013535fc6e46ab7152203817863ea8444fd6b9d03948cf',
    afterSha256:'9ba6f39d38b25cbfb8686f0ca93926e72f48152f0f2d2b4cc950972048b05b71',asset:'Silhouette.js'},
  {path:'packages/scratch-render/src/LocalHullCache.js',beforeSha256:null,
    afterSha256:'119d906641725593a9d47bd8a9dcb73970820a8e331e21af518b1be5f9d27695',asset:'LocalHullCache.js'}
];
export function resolveRendererProfile(value='stock') {
  if(!['stock','local-hull-cache-v1'].includes(value))throw new Error('Unknown Scratch renderer profile.');
  return value;
}
export function rendererPatchDescriptors(profile='stock') {
  return resolveRendererProfile(profile)==='stock' ? [] : rendererDefinitions.map(({asset,...record})=>({...record}));
}
export function expectedPatchRecords(profile,reducerSha256) {
  if(!/^[a-f0-9]{64}$/.test(reducerSha256))throw new Error('Invalid reducer identity.');
  return [
    {path:'packages/scratch-gui/src/reducers/targets.js',beforeSha256:'1f0c42994e269e0e297ebb381aafdbc2d1e1ced1bf83a31f7ae12a4e4ed3dadc',afterSha256:reducerSha256},
    {path:'packages/scratch-vm/src/engine/runtime.js',beforeSha256:'c316f4ec4cf0e4c868c585c411cf126a1494f967b5645ceddbc0e276db599ca4',
      afterSha256:'c5ada9b1ed82bafc0a2ee301ff2c3096d20fe71e1db0cc6b912d70a775516230'},
    ...rendererPatchDescriptors(profile)
  ];
}
export async function assertRendererOriginals(root,profile) {
  for(const patch of rendererPatchDescriptors(profile)) {
    const filename=path.join(root,patch.path);
    if(patch.beforeSha256===null) {
      try {await lstat(filename);throw new Error('New renderer source already exists: '+patch.path);}
      catch(error) {if(error.code!=='ENOENT')throw error;}
    } else {
      const info=await lstat(filename);
      if(!info.isFile() || info.isSymbolicLink() || sha(lf(await readFile(filename,'utf8')))!==patch.beforeSha256)
        throw new Error('Upstream renderer source mismatch: '+patch.path);
    }
  }
}
export async function loadRendererReplacements(profile) {
  resolveRendererProfile(profile);
  if(profile==='stock')return [];
  const result=[];
  for(const definition of rendererDefinitions) {
    const text=lf(await readFile(path.join(here,'renderer/local-hull-cache-v1',definition.asset),'utf8'));
    if(sha(text)!==definition.afterSha256)throw new Error('Fixed renderer recipe asset differs: '+definition.asset);
    const {asset,...record}=definition;
    result.push({...record,text});
  }
  return result;
}
export async function applyRendererReplacements(root,profile) {
  await assertRendererOriginals(root,profile);
  for(const {path:relative,text,beforeSha256} of await loadRendererReplacements(profile)) {
    const filename=path.join(root,relative);
    await mkdir(path.dirname(filename),{recursive:true});
    await writeFile(filename,text,beforeSha256===null ? {flag:'wx'} : undefined);
  }
}
export async function verifyPatchedSources(root,profile,patches,reducerSha256) {
  const expected=expectedPatchRecords(profile,reducerSha256);
  if(canonical(patches)!==canonical(expected))throw new Error('Source patches are not the exact registered profile closure.');
  for(const patch of expected) if(sha(lf(await readFile(path.join(root,patch.path),'utf8')))!==patch.afterSha256)
    throw new Error('Patched corresponding source differs: '+patch.path);
  // The stock source profile must retain the original renderer, not merely
  // omit its changes from the patch list while refreshing a self-made manifest.
  if(profile==='stock')await assertRendererOriginals(root,'local-hull-cache-v1');
}
export function sourcePathIdentity(profile) {
  return resolveRendererProfile(profile)==='stock' ?
    {count:3411,sha256:'480c5e797f25e688a95ff6e59d468a86f0ff91308b4b99e95df02aa1cb8358bb'} :
    {count:3412,sha256:'c95460534ab6b48a79ac6b784cdee202f4f267e5aad3893d0b01d789634ebbf8'};
}
export function verifySourcePaths(paths,profile) {
  const expected=sourcePathIdentity(profile),sorted=[...paths].sort();
  if(sorted.length!==expected.count || new Set(sorted).size!==sorted.length || sha(JSON.stringify(sorted))!==expected.sha256)
    throw new Error('Corresponding source member closure differs from the pinned release/profile.');
  return expected.sha256;
}
export function recipeSourceMembers(profile) {
  const names=['LICENSE.md','demos/scratch/upstream/build.mjs','demos/scratch/upstream/provenance.mjs',
    'demos/scratch/upstream/renderer-profiles.mjs','demos/scratch/upstream/README.md',
    'demos/scratch/upstream/targets-reducer.js','demos/scratch/web/vendor-lock.json'];
  if(resolveRendererProfile(profile)==='local-hull-cache-v1')
    names.push(...rendererDefinitions.map(item=>'demos/scratch/upstream/renderer/local-hull-cache-v1/'+item.asset));
  return names.sort().map(name=>({path:name,source:path.resolve(here,'../../..',name)}));
}
export async function snapshotRecipeInputs(profile) {
  const result=[];
  for(const member of recipeSourceMembers(profile)) {
    const info=await lstat(member.source);
    if(!info.isFile() || info.isSymbolicLink())throw new Error('Recipe input must be an ordinary file.');
    result.push({...member,bytes:await readFile(member.source)});
  }
  return result;
}
export async function verifyRecipeInputsUnchanged(inputs) {
  for(const input of inputs) if(!(await readFile(input.source)).equals(input.bytes))
    throw new Error('Recipe changed during source build: '+input.path);
}
export async function verifyShippedRecipe(root,profile,inventory) {
  const expected=recipeSourceMembers(profile);
  if(canonical(inventory.map(item=>item.path).sort())!==canonical(expected.map(item=>item.path).sort()))
    throw new Error('Shipped recipe member closure differs.');
  for(const item of expected) {
    const current=await readFile(item.source),shipped=await readFile(path.join(root,item.path));
    if(!current.equals(shipped))throw new Error('Shipped recipe source differs: '+item.path);
  }
  await loadRendererReplacements(profile);
}
