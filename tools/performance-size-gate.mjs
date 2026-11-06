import {createReadStream} from 'node:fs';
import {lstat, readdir, readFile, realpath, writeFile} from 'node:fs/promises';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {zipInventory} from './internal/performance-zip-inventory.mjs';

const snapshotSchema = 'soui.performance-size-snapshot/v1';
const fail = message => { throw new Error(message); };
const canonical = value => JSON.stringify(Array.isArray(value) ? value.map(normalize) : normalize(value));
function normalize(value) {
  if (Array.isArray(value)) return value.map(normalize);
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(key => [key, normalize(value[key])]));
  return value;
}
function relativeName(value) {
  if (typeof value !== 'string' || !value || value.includes('\\') || value.includes(':') ||
      value.startsWith('/') || value.split('/').some(part => !part || part === '.' || part === '..') ||
      /[\x00-\x1f]/.test(value)) fail('Expected a safe package-relative path.');
  return value;
}
function safeBytes(value) {
  if (!Number.isSafeInteger(value) || value < 0) fail('Invalid file size.');
  return value;
}
function addBytes(a, b) { return safeBytes(a + safeBytes(b)); }
function isKernel(name, prefixes) { return prefixes.some(prefix => name === prefix || name.startsWith(prefix + '/')); }
async function fileIdentity(file) {
  const info = await lstat(file);
  if (!info.isFile() || info.isSymbolicLink()) fail('Expected a regular file: ' + file);
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(file)) hash.update(chunk);
  // Detect concurrent packaging/writes instead of binding a mixed candidate.
  const after = await lstat(file);
  if (info.size !== after.size || info.mtimeMs !== after.mtimeMs || info.ino !== after.ino)
    fail('File changed while recording: ' + file);
  return {bytes:safeBytes(info.size), sha256:hash.digest('hex')};
}
function validateProfile(profile) {
  for (const key of ['backend', 'architecture', 'configuration', 'toolchain', 'featureSet', 'resourceSet', 'packaging']) {
    if (typeof profile?.[key] !== 'string' || !profile[key]) fail('Missing frozen profile field: ' + key);
  }
  if (!Array.isArray(profile.requiredRuntimeDependencies) ||
      profile.requiredRuntimeDependencies.some(value => typeof value !== 'string' || !value) ||
      new Set(profile.requiredRuntimeDependencies).size !== profile.requiredRuntimeDependencies.length)
    fail('Explicit requiredRuntimeDependencies list is required.');
}
function validateSnapshot(snapshot) {
  if (snapshot.schema !== snapshotSchema) fail('Unsupported size snapshot schema.');
  validateProfile(snapshot.profile);
  if (!Array.isArray(snapshot.kernelPaths) || !snapshot.kernelPaths.length) fail('Kernel paths are required.');
  const prefixes = snapshot.kernelPaths.map(relativeName);
  if (new Set(prefixes).size !== prefixes.length) fail('Duplicate kernel paths.');
  if (!Array.isArray(snapshot.files) || !snapshot.files.length) fail('Runtime file inventory is empty.');
  const seen = new Set();
  let runtimeBytes = 0, kernelBytes = 0;
  for (const file of snapshot.files) {
    const name = relativeName(file.path), folded = name.toLowerCase();
    if (seen.has(folded)) fail('Duplicate runtime path: ' + name);
    seen.add(folded);
    if (!/^[a-f0-9]{64}$/.test(file.sha256)) fail('Invalid file identity: ' + name);
    runtimeBytes = addBytes(runtimeBytes, file.bytes);
    if (isKernel(name, prefixes)) kernelBytes = addBytes(kernelBytes, file.bytes);
  }
  for (const prefix of prefixes) if (!snapshot.files.some(file => isKernel(file.path, [prefix])))
    fail('Kernel inventory entry has no files: ' + prefix);
  if (!/^[a-f0-9]{64}$/.test(snapshot.archive?.sha256)) fail('Invalid archive identity.');
  const inventorySha256=createHash('sha256').update(canonical(snapshot.files)).digest('hex');
  if(snapshot.archive.inventorySha256!==inventorySha256) fail('Archive is not bound to the runtime inventory.');
  const totals = {archiveBytes:safeBytes(snapshot.archive.bytes), runtimeBytes, kernelBytes};
  if (canonical(totals) !== canonical(snapshot.totals)) fail('Inventory totals do not match file records.');
  return snapshot;
}

export async function recordSize(config, baseDirectory = process.cwd()) {
  if (config.schema !== 'soui.performance-size-config/v1') fail('Unsupported size config schema.');
  validateProfile(config.profile);
  const root = path.resolve(baseDirectory, config.runtimeRoot);
  const rootInfo = await lstat(root);
  if (!rootInfo.isDirectory() || rootInfo.isSymbolicLink()) fail('Runtime root must be a real directory.');
  const resolvedRoot = await realpath(root);
  const files = [];
  async function visit(directory, prefix = '') {
    for (const entry of (await readdir(directory, {withFileTypes:true})).sort((a,b) => a.name.localeCompare(b.name, 'en'))) {
      const name = relativeName(prefix + entry.name), absolute = path.join(directory, entry.name);
      const info = await lstat(absolute);
      if (info.isSymbolicLink()) fail('Links are not allowed in the measured runtime: ' + name);
      const resolved = await realpath(absolute);
      const within = path.relative(resolvedRoot, resolved);
      if (within === '..' || within.startsWith('..' + path.sep) || path.isAbsolute(within)) fail('Runtime path escaped inventory.');
      if (info.isDirectory()) await visit(absolute, name + '/');
      else if (info.isFile()) files.push({path:name, ...await fileIdentity(absolute)});
      else fail('Non-regular runtime entry: ' + name);
    }
  }
  await visit(root);
  const kernelPaths = config.kernelPaths?.map(relativeName);
  files.sort((a,b)=>a.path.localeCompare(b.path,'en'));
  const archivePath=path.resolve(baseDirectory,config.archive);
  const archive=await fileIdentity(archivePath);
  const zipped=await zipInventory(archivePath,config.archiveRoot ?? '',files);
  if(canonical(zipped)!==canonical(files)) fail('ZIP contents do not match the runtime inventory.');
  const archiveAfter=await fileIdentity(archivePath);
  if(canonical(archiveAfter)!==canonical(archive)) fail('Archive changed while binding its contents.');
  archive.inventorySha256=createHash('sha256').update(canonical(files)).digest('hex');
  const snapshot = {schema:snapshotSchema, profile:config.profile, buildIdentity:config.buildIdentity ?? null,
    kernelPaths, archive, files};
  snapshot.totals = {archiveBytes:snapshot.archive.bytes,
    runtimeBytes:files.reduce((sum,file) => addBytes(sum,file.bytes),0),
    kernelBytes:files.filter(file => isKernel(file.path,kernelPaths ?? [])).reduce((sum,file) => addBytes(sum,file.bytes),0)};
  return validateSnapshot(snapshot);
}

export function compareSizes(baseline, candidate) {
  validateSnapshot(baseline); validateSnapshot(candidate);
  if (canonical(baseline.profile) !== canonical(candidate.profile)) fail('Frozen profiles differ; sizes are not comparable.');
  if (canonical(baseline.kernelPaths) !== canonical(candidate.kernelPaths)) fail('Kernel inventory scope changed.');
  const previous = new Map(baseline.files.map(file => [file.path.toLowerCase(),file]));
  const current = new Map(candidate.files.map(file => [file.path.toLowerCase(),file]));
  const deltas = Object.fromEntries(Object.keys(baseline.totals).map(key => [key,candidate.totals[key]-baseline.totals[key]]));
  const added = candidate.files.filter(file => !previous.has(file.path.toLowerCase()));
  const removed = baseline.files.filter(file => !current.has(file.path.toLowerCase()));
  const changed = candidate.files.filter(file => previous.has(file.path.toLowerCase()) &&
    file.sha256 !== previous.get(file.path.toLowerCase()).sha256).map(file =>
    ({path:file.path,deltaBytes:file.bytes-previous.get(file.path.toLowerCase()).bytes}));
  const addedNativeDependencies = added.filter(file => /\.(dll|ocx|sys)$/i.test(file.path)).map(file => file.path);
  const reasons = Object.entries(deltas).filter(([,delta]) => delta > 0).map(([key]) => key + '-grew');
  if (addedNativeDependencies.length) reasons.push('new-native-runtime-file');
  // Resources cannot silently disappear merely to offset code growth.
  if (removed.length) reasons.push('runtime-files-removed-require-equivalence-review');
  return {schema:'soui.performance-size-comparison/v1',pass:reasons.length === 0,reasons,deltas,
    baseline:baseline.totals,candidate:candidate.totals,addedNativeDependencies,added:added.map(file => file.path),
    removed:removed.map(file => file.path),changed};
}

async function cli(args) {
  const [mode, first, second, output, ...extra] = args;
  if (extra.length || !first || !second || (mode !== 'snapshot' && mode !== 'compare') ||
      (mode === 'snapshot' && output)) fail('Usage: node tools/performance-size-gate.mjs snapshot CONFIG OUTPUT | compare BASELINE CANDIDATE [REPORT]');
  if (mode === 'snapshot') {
    const snapshot = await recordSize(JSON.parse(await readFile(first,'utf8')),path.dirname(path.resolve(first)));
    const destination = path.resolve(second);
    const config = JSON.parse(await readFile(first,'utf8'));
    const runtimeRoot = path.resolve(path.dirname(path.resolve(first)),config.runtimeRoot);
    const relative = path.relative(runtimeRoot,destination);
    if (!relative.startsWith('..' + path.sep) && relative !== '..' && !path.isAbsolute(relative))
      fail('Snapshot must be saved outside the runtime inventory.');
    await writeFile(destination,JSON.stringify(snapshot,null,2)+'\n',{flag:'wx'});
    console.log(JSON.stringify(snapshot.totals));
  } else {
    const report = compareSizes(JSON.parse(await readFile(first,'utf8')),JSON.parse(await readFile(second,'utf8')));
    if (output) await writeFile(output,JSON.stringify(report,null,2)+'\n',{flag:'wx'});
    console.log(JSON.stringify(report));
    if (!report.pass) process.exitCode = 1;
  }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  cli(process.argv.slice(2)).catch(error => {console.error(error.message);process.exitCode=1;});
