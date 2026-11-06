import {createHash} from 'node:crypto';
import {readFile, writeFile, stat, mkdir, cp, lstat} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {distInventory} from './provenance.mjs';
import {resolveRendererProfile,assertRendererOriginals,loadRendererReplacements,applyRendererReplacements,
  expectedPatchRecords,verifyPatchedSources,rendererPatchDescriptors,verifySourcePaths,
  snapshotRecipeInputs,verifyRecipeInputsUnchanged} from './renderer-profiles.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const sourceCommit = 'b4b934aaf6641a539325a971249234f4a50a2f03';
const lockSha256 = 'cabc070103553a7fe01332190b884c0dd8bc5517a64c13dc967f8afc92efa1ed';
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const lf = source => source.replace(/\r\n/g,'\n');
const argv = process.argv.slice(2);
const options = new Map();
for (let i=0;i<argv.length;i+=2) {
  if (!['--source-root','--output','--npm-cli','--renderer-profile'].includes(argv[i]) || !argv[i+1] || options.has(argv[i]))
    throw new Error('Usage: node build.mjs --source-root CLEAN_PINNED_CHECKOUT --output NEW_DIRECTORY [--npm-cli NPM_CLI_JS] [--renderer-profile stock|local-hull-cache-v1|local-hull-cache-gui-shared-v1]');
  options.set(argv[i],argv[i+1]);
}
if (!options.has('--source-root') || !options.has('--output')) throw new Error('source-root and output are required.');
const rendererProfile=resolveRendererProfile(options.get('--renderer-profile') ?? 'stock');
const recipeInputs=await snapshotRecipeInputs(rendererProfile);
const root = path.resolve(options.get('--source-root'));
const output = path.resolve(options.get('--output'));
const relativeOutput = path.relative(root,output);
if (!relativeOutput.startsWith('..' + path.sep) && relativeOutput !== '..' && !path.isAbsolute(relativeOutput))
  throw new Error('Output must be outside the upstream checkout.');
try {await lstat(output);throw new Error('Output already exists; preserve previous evidence.');}
catch(error) {if(error.code!=='ENOENT') throw error;}
const run = (program,args,quiet=false) => {
  const result=spawnSync(program,args,{cwd:root,stdio:quiet?'pipe':'inherit',encoding:'utf8',
    env:{...process.env,NODE_ENV:'production',CI:'1'}});
  if(result.error || result.status!==0) throw new Error(`Upstream command failed: ${program} ${args.join(' ')}`);
  return result.stdout?.trim();
};
if(run('git',['rev-parse','HEAD'],true)!==sourceCommit) throw new Error('Unexpected upstream source commit.');
if(run('git',['status','--porcelain','--untracked-files=all'],true)) throw new Error('Use a clean upstream checkout, not an existing worktree with edits.');
if(sha(lf(await readFile(path.join(root,'package-lock.json'),'utf8')))!==lockSha256)
  throw new Error('Upstream dependency lock differs.');
const trackedSourceMembers=run('git',['ls-files','-z'],true).split('\0').filter(Boolean);
verifySourcePaths(trackedSourceMembers,'stock');
await assertRendererOriginals(root,rendererProfile);
await loadRendererReplacements(rendererProfile);
const guiPath='packages/scratch-gui/src/reducers/targets.js';
const runtimePath='packages/scratch-vm/src/engine/runtime.js';
const originals=new Map([
  [guiPath,'1f0c42994e269e0e297ebb381aafdbc2d1e1ced1bf83a31f7ae12a4e4ed3dadc'],
  [runtimePath,'c316f4ec4cf0e4c868c585c411cf126a1494f967b5645ceddbc0e276db599ca4']
]);
const sourceTexts=new Map();
for(const [file,digest] of originals) {
  const text=lf(await readFile(path.join(root,file),'utf8'));
  if(sha(text)!==digest) throw new Error('Upstream patch source mismatch: '+file);
  sourceTexts.set(file,text);
}
const patchedReducer=lf(await readFile(path.join(here,'targets-reducer.js'),'utf8'));
function once(text,before,after) {
  if(text.split(before).length!==2) throw new Error('Upstream patch anchor must match exactly once.');
  return text.replace(before,after);
}
let runtime=sourceTexts.get(runtimePath);
runtime=once(runtime,'        this.profiler = null;\n\n        const newCloudDataManager',
  '        this.profiler = null;\n        this.souiPerformanceCounters = null;\n\n        const newCloudDataManager');
runtime=once(runtime,'    _step () {\n','    _step () {\n        if (this.souiPerformanceCounters) ++this.souiPerformanceCounters.steps;\n');
runtime=once(runtime,'            this.renderer.draw();\n',
  '            this.renderer.draw();\n            if (this.souiPerformanceCounters) ++this.souiPerformanceCounters.draws;\n');
const patches=expectedPatchRecords(rendererProfile,sha(patchedReducer));
if(sha(runtime)!==patches[1].afterSha256)throw new Error('Runtime counter recipe differs from its registered identity.');
await writeFile(path.join(root,guiPath),patchedReducer);
await writeFile(path.join(root,runtimePath),runtime);
await applyRendererReplacements(root,rendererProfile);
const npmCli=path.resolve(options.get('--npm-cli') ?? path.join(path.dirname(process.execPath),'node_modules/npm/bin/npm-cli.js'));
await stat(npmCli); // Explicit executable JS, no cmd/shell interpolation on Windows.
run(process.execPath,[npmCli,'ci','--include=dev','--no-audit','--no-fund']);
// Avoid GUI build:dev's POSIX-only `BUILD_TYPE=dev` on Windows. Build the
// locked dependency workspaces, then invoke the standalone production target
// through the existing cross-env dependency without a command-string shell.
for(const workspace of ['@scratch/task-herder','@scratch/scratch-storage','@scratch/scratch-svg-renderer',
  '@scratch/scratch-paint','@scratch/scratch-render','@scratch/scratch-vm'])
  run(process.execPath,[npmCli,'run','build','--workspace',workspace]);
run(process.execPath,[npmCli,'exec','--workspace','@scratch/scratch-gui','--','cross-env',
  'NODE_ENV=production','BUILD_TYPE=dist-standalone','webpack']);
// Build/prepare scripts must not silently change our recorded patch inputs.
await verifyPatchedSources(root,rendererProfile,patches,sha(patchedReducer));
if(sha(lf(await readFile(path.join(root,'package-lock.json'),'utf8')))!==lockSha256)
  throw new Error('Build changed the upstream dependency lock.');
const dist=path.join(root,'packages/scratch-gui/dist');
const bundle=await readFile(path.join(dist,'scratch-gui-standalone.js'));
await mkdir(output);
await cp(dist,path.join(output,'dist'),{recursive:true});
await cp(path.join(root,'LICENSE'),path.join(output,'LICENSE'));
await cp(path.join(root,'package-lock.json'),path.join(output,'package-lock.json'));
// Copy every tracked source/config, including the patched versions. Selecting
// by Git membership avoids guessing that any folder named 'build' is generated
// and accidentally omitting corresponding source needed for reproduction.
const sourceMembers=[...trackedSourceMembers,...rendererPatchDescriptors(rendererProfile)
  .filter(patch=>patch.beforeSha256===null).map(patch=>patch.path)];
const sourceMemberPathsSha256=verifySourcePaths(sourceMembers,rendererProfile);
for(const relative of sourceMembers) {
  if(path.isAbsolute(relative) || relative.split('/').some(part=>part==='..') || relative.includes('\\'))
    throw new Error('Unsafe upstream source member.');
  const source=path.join(root,relative),destination=path.join(output,'sources',relative);
  const info=await lstat(source);
  if(!info.isFile() || info.isSymbolicLink()) throw new Error('Unsupported upstream source member: '+relative);
  await mkdir(path.dirname(destination),{recursive:true});
  await cp(source,destination);
}
await verifyRecipeInputsUnchanged(recipeInputs);
for(const input of recipeInputs) {
  const destination=path.join(output,'recipe',input.path);
  await mkdir(path.dirname(destination),{recursive:true});
  await writeFile(destination,input.bytes,{flag:'wx'});
}
await writeFile(path.join(output,'source-build.json'),JSON.stringify({schema:'soui.scratch-source-build/v1',
  sourceCommit,sourceLockSha256:lockSha256,rendererProfile,patches,sourceMemberPathsSha256,
  patchDriverSha256:sha(await readFile(fileURLToPath(import.meta.url))),
  provenanceModuleSha256:sha(await readFile(path.join(here,'provenance.mjs'))),
  reducerPatchSha256:sha(patchedReducer),bundleSha256:sha(bundle),
  distFiles:await distInventory(path.join(output,'dist')),
  sourceFiles:await distInventory(path.join(output,'sources')),
  recipeFiles:await distInventory(path.join(output,'recipe')),
  // The published 15.3.0 artifact uses this release commit whose source package
  // versions remain 15.2.0. Preserve that distinction rather than relabel it.
  upstreamSourceVersion:JSON.parse(await readFile(path.join(root,'package.json'),'utf8')).version,
  referenceArtifactVersion:'15.3.0',node:process.version},null,2)+'\n');
console.log('Built patched upstream source: '+output);
