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
const guiProfile='local-hull-cache-gui-shared-v1';
const guiDefinitions=[
  {path:'packages/scratch-gui/src/containers/sprite-selector-item.jsx',
    beforeSha256:'991850e8290374988e65ed5eb3d99dd419cbf1ecae4de965c864deea9bad8e09',
    afterSha256:'1a1b4a2c6929be2eb62fe5ef10b07a2a45d2ad7edbd936913447e038f3edb26d',
    directory:'gui/shared-sprite-subscriptions-v1',asset:'sprite-selector-item.jsx'},
  {path:'packages/scratch-gui/src/components/sprite-selector/sprite-list.jsx',
    beforeSha256:'fc3800b42fddeb2ed1aadd0c621ddc6f6162e56d28a4746771aad69f1b653a39',
    afterSha256:'a6b4e21732360fdda329d0bb86fa73585d41bdbe90b5d217a1f3b7323a9fd96b',
    directory:'gui/shared-sprite-subscriptions-v1',asset:'sprite-list.jsx'},
  {path:'packages/scratch-gui/src/lib/sprite-selector-store-context.jsx',beforeSha256:null,
    afterSha256:'06066f16a1c7355b06d4e036f880228247b308a4ba8172a3c843860f0bcbb716',
    directory:'gui/shared-sprite-subscriptions-v1',asset:'sprite-selector-store-context.jsx'}
];
export function resolveRendererProfile(value='stock') {
  if(!['stock','local-hull-cache-v1',guiProfile].includes(value))throw new Error('Unknown Scratch renderer profile.');
  return value;
}
function definitionsForProfile(profile) {
  const selected=resolveRendererProfile(profile);
  return selected==='stock' ? [] : selected===guiProfile ? [...rendererDefinitions,...guiDefinitions] : rendererDefinitions;
}
const assetPath=definition=>path.posix.join(definition.directory ?? 'renderer/local-hull-cache-v1',definition.asset);
export function rendererPatchDescriptors(profile='stock') {
  return definitionsForProfile(profile).map(({asset,directory,...record})=>({...record}));
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
async function assertOriginalDefinitions(root,definitions) {
  for(const patch of definitions) {
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
export async function assertRendererOriginals(root,profile) {
  await assertOriginalDefinitions(root,definitionsForProfile(profile));
  // Older profiles must retain these upstream GUI sources. Otherwise a
  // self-consistent inventory could conceal the new GUI patch under an old name.
  if(profile!==guiProfile)await assertOriginalDefinitions(root,guiDefinitions);
}
export async function loadRendererReplacements(profile) {
  resolveRendererProfile(profile);
  if(profile==='stock')return [];
  const result=[];
  for(const definition of definitionsForProfile(profile)) {
    const text=lf(await readFile(path.join(here,assetPath(definition)),'utf8'));
    if(sha(text)!==definition.afterSha256)throw new Error('Fixed renderer recipe asset differs: '+definition.asset);
    const {asset,directory,...record}=definition;
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
export function verifyPatchRecords(profile,patches,reducerSha256) {
  const expected=expectedPatchRecords(profile,reducerSha256);
  if(canonical(patches)!==canonical(expected))throw new Error('Source patches are not the exact registered profile closure.');
  return expected;
}
export async function verifyPatchedSources(root,profile,patches,reducerSha256) {
  const expected=verifyPatchRecords(profile,patches,reducerSha256);
  for(const patch of expected) if(sha(lf(await readFile(path.join(root,patch.path),'utf8')))!==patch.afterSha256)
    throw new Error('Patched corresponding source differs: '+patch.path);
  // The stock source profile must retain the original renderer, not merely
  // omit its changes from the patch list while refreshing a self-made manifest.
  if(profile==='stock')await assertRendererOriginals(root,'local-hull-cache-v1');
  else if(profile!==guiProfile)await assertOriginalDefinitions(root,guiDefinitions);
}
export function sourcePathIdentity(profile) {
  const selected=resolveRendererProfile(profile);
  if(selected===guiProfile)return {count:3413,sha256:'541c6e1309389b3a9229cdc41bcfe84785c112b4a976ab2e953478983160e0e4'};
  return selected==='stock' ?
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
  names.push(...definitionsForProfile(profile).map(item=>'demos/scratch/upstream/'+assetPath(item)));
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
