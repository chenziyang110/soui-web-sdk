import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash, randomUUID} from 'node:crypto';
import {spawnSync} from 'node:child_process';

const demo = path.dirname(fileURLToPath(import.meta.url));
const sdk = path.resolve(demo, '../..');
const marker = '.scratch-resources.json';
const limits = Object.freeze({fileBytes: 32 * 1024 * 1024, totalBytes: 256 * 1024 * 1024, files: 4096, directories: 4096});
const args = process.argv.slice(2);
const locked = args[0] === '--locked';
if (locked) args.shift();
if (args.length !== 2) throw new Error('usage: node deploy.mjs WEB_DIST APPLICATION_OUTPUT');
const web = path.resolve(args[0]);
const application = path.resolve(args[1]);
if (web !== path.resolve(demo, 'web/dist')) throw new Error('Scratch deployment accepts only this demo web/dist.');
const relativeApp = path.relative(sdk, application).replaceAll('\\', '/');
if (!/^out\/scratch-(x86|x64)(-package)?\/bin\/(Debug|Release)\/SouiScratch$/.test(relativeApp))
  throw new Error('Scratch deployment output must be this SDK out/scratch-ARCH/bin/CONFIG/SouiScratch.');

async function ordinaryParents(full) {
  let cursor = path.resolve(full);
  for (;;) {
    const info = await fs.lstat(cursor);
    if (!info.isDirectory() || info.isSymbolicLink()) throw new Error(`Directory link refused: ${cursor}`);
    const parent = path.dirname(cursor);
    if (parent === cursor) return;
    cursor = parent;
  }
}
await ordinaryParents(web);
await ordinaryParents(application);
const executable = path.join(application, 'SouiScratch.exe');
const exeInfo = await fs.lstat(executable);
if (!exeInfo.isFile() || exeInfo.isSymbolicLink() || exeInfo.nlink !== 1)
  throw new Error('Scratch executable must be an ordinary unlinked file.');

// Hold an exclusive Windows file handle on the built EXE throughout publication.
// A running image makes this fail; while held, a new process cannot load it.
if (!locked) {
  if (process.platform !== 'win32') throw new Error('Scratch native deployment requires Windows.');
  const guard = `
$ErrorActionPreference='Stop'
$lease=$null
try {
  $lease=[IO.File]::Open($env:SOUI_SCRATCH_DEPLOY_EXE,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  & $env:SOUI_SCRATCH_DEPLOY_NODE $env:SOUI_SCRATCH_DEPLOY_SCRIPT --locked $env:SOUI_SCRATCH_DEPLOY_WEB $env:SOUI_SCRATCH_DEPLOY_APP
  $result=$LASTEXITCODE
} catch {
  [Console]::Error.WriteLine('Cannot deploy Scratch resources; close this output application first. '+$_.Exception.Message)
  $result=1
} finally {
  if($null-ne$lease){$lease.Dispose()}
}
exit $result
`;
  const result = spawnSync(path.join(process.env.SystemRoot || 'C:/Windows', 'System32/WindowsPowerShell/v1.0/powershell.exe'),
    ['-NoProfile', '-NonInteractive', '-EncodedCommand', Buffer.from(guard, 'utf16le').toString('base64')], {
      stdio: 'inherit', windowsHide: true,
      env: {...process.env, SOUI_SCRATCH_DEPLOY_EXE: executable, SOUI_SCRATCH_DEPLOY_NODE: process.execPath,
        SOUI_SCRATCH_DEPLOY_SCRIPT: fileURLToPath(import.meta.url), SOUI_SCRATCH_DEPLOY_WEB: web,
        SOUI_SCRATCH_DEPLOY_APP: application, SOUI_SCRATCH_DEPLOY_GUARDED: '1'}
    });
  if (result.error) throw result.error;
  process.exit(result.status ?? 1);
}
if (process.env.SOUI_SCRATCH_DEPLOY_GUARDED !== '1') throw new Error('Scratch deployment lock is required.');

function segment(name) {
  if (!name || name === '.' || name === '..' || name.length > 128 || /[<>:"/\\|?*\x00-\x1f]/.test(name) ||
      /[. ]$/.test(name) || /^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)/i.test(name))
    throw new Error('Unsafe Scratch resource path segment.');
}
let total = 0, directories = 0;
const entries = [];
async function walk(root, prefix = '', relative = '') {
  const full = path.join(root, relative);
  const info = await fs.lstat(full);
  if (!info.isDirectory() || info.isSymbolicLink()) throw new Error(`Resource directory link refused: ${full}`);
  if (++directories > limits.directories) throw new Error('Scratch directory budget exceeded.');
  for (const name of (await fs.readdir(full)).sort()) {
    segment(name);
    const local = relative ? `${relative}/${name}` : name;
    if (local.split('/').length > 32 || local.length > 1024) throw new Error('Scratch resource path budget exceeded.');
    const input = path.join(root, local);
    const stat = await fs.lstat(input);
    if (stat.isSymbolicLink()) throw new Error(`Resource link refused: ${input}`);
    if (stat.isDirectory()) { await walk(root, prefix, local); continue; }
    if (!stat.isFile() || stat.nlink !== 1 || stat.size > limits.fileBytes) throw new Error(`Invalid or oversized Scratch resource: ${input}`);
    const target = prefix ? `${prefix}/${local}` : local;
    if (target === marker || (!prefix && /^legal(?:\/|$)/i.test(local))) throw new Error('Scratch web output owns a reserved deployment path.');
    total += stat.size;
    if (total > limits.totalBytes || entries.length >= limits.files - 1) throw new Error('Scratch resource budget exceeded.');
    entries.push({input, path: target, bytes: stat.size});
  }
}
await walk(web);
if (!entries.some(entry => entry.path === 'index.html')) throw new Error('Scratch Web entry is missing.');
await ordinaryParents(path.join(sdk, 'third_party/licenses'));
await walk(path.join(sdk, 'third_party/licenses'), 'legal/third_party/licenses');
for (const name of ['LICENSE.md', 'THIRD_PARTY_NOTICES.md']) {
  const input = path.join(sdk, name), info = await fs.lstat(input);
  if (!info.isFile() || info.isSymbolicLink() || info.nlink !== 1 || info.size > limits.fileBytes) throw new Error('SDK notice is invalid.');
  entries.push({input, path: `legal/${name}`, bytes: info.size}); total += info.size;
}
if (entries.length >= limits.files || total > limits.totalBytes) throw new Error('Scratch combined resource budget exceeded.');

const suffix = randomUUID();
const stage = path.join(application, `.scratch-resources-new-${suffix}`);
const backup = path.join(application, `.scratch-resources-old-${suffix}`);
const target = path.join(application, 'resources');
async function removeOwnedScratchDirectory(full) {
  // Only exact task-created sibling staging names are eligible for recursion.
  const resolved = path.resolve(full);
  if (path.dirname(resolved) !== application || !/^\.scratch-resources-(new|old)-[0-9a-f-]{36}$/.test(path.basename(resolved)))
    throw new Error('Refusing cleanup outside the Scratch staging directory.');
  const info = await fs.lstat(resolved).catch(error => error.code === 'ENOENT' ? null : Promise.reject(error));
  if (!info) return;
  if (!info.isDirectory() || info.isSymbolicLink()) throw new Error('Scratch cleanup directory changed identity.');
  await fs.rm(resolved, {recursive: true});
}
let moved = false, published = false;
await fs.mkdir(stage);
try {
  const manifestFiles = [];
  for (const entry of entries) {
    const before = await fs.lstat(entry.input);
    if (!before.isFile() || before.isSymbolicLink() || before.nlink !== 1 || before.size !== entry.bytes) throw new Error('Scratch input changed before copy.');
    const data = await fs.readFile(entry.input);
    if (data.length !== entry.bytes) throw new Error('Scratch input changed during copy.');
    const sha256 = createHash('sha256').update(data).digest('hex');
    const destination = path.join(stage, entry.path);
    await fs.mkdir(path.dirname(destination), {recursive: true});
    await fs.writeFile(destination, data, {flag: 'wx'});
    const after = await fs.lstat(entry.input);
    if (after.ino !== before.ino || after.dev !== before.dev || after.size !== before.size || after.mtimeMs !== before.mtimeMs ||
        createHash('sha256').update(await fs.readFile(entry.input)).digest('hex') !== sha256)
      throw new Error('Scratch input changed during deployment.');
    manifestFiles.push({path: entry.path, bytes: data.length, sha256});
  }
  const manifest = JSON.stringify({schema: 'soui.scratch-demo-resources/v1',
    sdkVersion: (await fs.readFile(path.join(sdk, 'VERSION'), 'utf8')).trim(),
    profile: 'modern-scratch-demo', limits, totalBytes: total, files: manifestFiles}, null, 2) + '\n';
  if (Buffer.byteLength(manifest) > limits.fileBytes || total + Buffer.byteLength(manifest) > limits.totalBytes)
    throw new Error('Scratch manifest exceeds the resource budget.');
  await fs.writeFile(path.join(stage, marker), manifest, {flag: 'wx'});
  const existing = await fs.lstat(target).catch(error => error.code === 'ENOENT' ? null : Promise.reject(error));
  if (existing) {
    if (!existing.isDirectory() || existing.isSymbolicLink()) throw new Error('Existing Scratch resources are not an ordinary directory.');
    const previous = JSON.parse(await fs.readFile(path.join(target, marker), 'utf8'));
    if (previous.schema !== 'soui.scratch-demo-resources/v1') throw new Error('Existing resources are not owned by the Scratch demo deployer.');
    await fs.rename(target, backup); moved = true;
  }
  await fs.rename(stage, target); published = true;
  if (moved) await removeOwnedScratchDirectory(backup);
  console.log(`Scratch demo resources deployed: ${manifestFiles.length} files, ${total} bytes; manifest ${marker}`);
} catch (error) {
  if (!published && moved) await fs.rename(backup, target);
  throw error;
} finally {
  if (!published) await removeOwnedScratchDirectory(stage);
}
