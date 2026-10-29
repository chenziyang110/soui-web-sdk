import {createHash} from 'node:crypto';
import {mkdir, readFile, rename, rm, writeFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const LIBRARIES = ['sprites', 'costumes', 'backdrops', 'sounds'];
const ASSET_NAME = /^[a-f0-9]{32}\.(svg|png|jpg|jpeg|wav|mp3|gif)$/;
const ASSET_ORIGIN = 'https://assets.scratch.mit.edu';
const CONCURRENCY = 10;
const MAX_BYTES = 64 * 1024 * 1024;
const digest = (algorithm, bytes) => createHash(algorithm).update(bytes).digest('hex');

function collectAssets(value, library, assets) {
  if (!value || typeof value !== 'object') return;
  if (!Array.isArray(value)) {
    const filename = typeof value.md5ext === 'string' ? value.md5ext :
      (typeof value.assetId === 'string' && typeof value.dataFormat === 'string' ?
        `${value.assetId}.${value.dataFormat}` : null);
    if (filename !== null) {
      if (!ASSET_NAME.test(filename)) throw new Error(`Invalid asset filename in ${library}: ${filename}`);
      if (!assets.has(filename)) assets.set(filename, new Set());
      assets.get(filename).add(library);
    }
  }
  for (const child of Object.values(value)) collectAssets(child, library, assets);
}

async function validCachedAsset(filename, expectedMD5) {
  try {
    const data = await readFile(filename);
    return data.length > 0 && data.length <= MAX_BYTES && digest('md5', data) === expectedMD5 ? data : null;
  } catch {
    // An unreadable, incomplete, or missing cache entry is fetched again.
    return null;
  }
}

async function fetchAsset(url, expectedMD5) {
  const response = await fetch(url, {redirect: 'error', signal: AbortSignal.timeout(45000)});
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  const announcedLength = Number(response.headers.get('content-length'));
  if (announcedLength > MAX_BYTES) throw new Error('Asset exceeds the 64 MiB limit');
  const chunks = [];
  let received = 0;
  for await (const chunk of response.body) {
    received += chunk.length;
    if (received > MAX_BYTES) throw new Error('Asset exceeds the 64 MiB limit');
    chunks.push(chunk);
  }
  const data = Buffer.concat(chunks, received);
  if (!data.length || digest('md5', data) !== expectedMD5) throw new Error('Asset MD5 does not match the library identity');
  return data;
}

/** Download all assets used by the pinned official Scratch libraries. */
export async function buildOfflineAssets(cacheRoot, packageRoot) {
  cacheRoot = path.resolve(cacheRoot);
  packageRoot = path.resolve(packageRoot);
  const assetsRoot = path.join(cacheRoot, 'assets');
  await mkdir(assetsRoot, {recursive: true});
  const packageInfo = JSON.parse(await readFile(path.join(packageRoot, 'package.json'), 'utf8'));
  const assets = new Map();
  const libraries = {};
  for (const library of LIBRARIES) {
    const source = await readFile(path.join(packageRoot, 'src/lib/libraries', `${library}.json`));
    const data = JSON.parse(source);
    if (!Array.isArray(data)) throw new Error(`Scratch ${library} library must be an array`);
    libraries[library] = {entries: data.length, sha256: digest('sha256', source)};
    collectAssets(data, library, assets);
  }

  const entries = [...assets.entries()].sort(([a], [b]) => a.localeCompare(b));
  const results = new Array(entries.length);
  let cursor = 0;
  let cached = 0;
  let downloaded = 0;
  let completed = 0;
  async function worker() {
    for (;;) {
      const index = cursor++;
      if (index >= entries.length) return;
      const [filename, usedBy] = entries[index];
      const expectedMD5 = filename.slice(0, 32);
      const target = path.join(assetsRoot, filename);
      const url = `${ASSET_ORIGIN}/internalapi/asset/${filename}/get/`;
      try {
        let data = await validCachedAsset(target, expectedMD5);
        if (data) cached++;
        else {
          let lastError;
          for (let attempt = 0; attempt <= 2; attempt++) {
            try {
              data = await fetchAsset(url, expectedMD5);
              const temporary = `${target}.${process.pid}.part`;
              try {
                await writeFile(temporary, data, {flag: 'w'});
                await rename(temporary, target);
              } finally {
                await rm(temporary, {force: true});
              }
              downloaded++;
              lastError = undefined;
              break;
            } catch (error) {
              lastError = error;
              if (attempt < 2) await new Promise(resolve => setTimeout(resolve, 500 * (attempt + 1)));
            }
          }
          if (!data || lastError) throw lastError || new Error('Asset download produced no data');
        }
        results[index] = {file: filename, bytes: data.length, md5: expectedMD5,
          sha256: digest('sha256', data), source: url, libraries: [...usedBy].sort()};
      } catch (error) {
        results[index] = {file: filename, source: url, libraries: [...usedBy].sort(),
          error: error instanceof Error ? error.message : String(error)};
      }
      completed++;
      if (completed % 100 === 0 || completed === entries.length)
        console.log(`Scratch offline assets: ${completed}/${entries.length} (${cached} cached, ${downloaded} downloaded)`);
    }
  }
  await Promise.all(Array.from({length: Math.min(CONCURRENCY, entries.length)}, worker));
  const failures = results.filter(result => result.error);
  const manifest = {
    schema: 'soui.scratch-offline-assets/v1',
    package: {name: packageInfo.name, version: packageInfo.version, license: packageInfo.license},
    status: failures.length ? 'incomplete' : 'complete',
    sources: {libraries: 'https://github.com/scratchfoundation/scratch-editor/tree/develop/packages/scratch-gui/src/lib/libraries',
      assetOrigin: ASSET_ORIGIN},
    licenseNote: 'Library metadata comes from the pinned Scratch package. Preserve its LICENSE and TRADEMARK. This manifest records asset origins and hashes; it does not assert a separate asset license or grant trademark rights.',
    libraries,
    counts: {uniqueAssets: entries.length, cached, downloaded, failed: failures.length,
      totalBytes: results.reduce((sum, result) => sum + (result.bytes || 0), 0)},
    assets: results
  };
  await writeFile(path.join(cacheRoot, 'asset-manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`);
  if (failures.length) throw new Error(`${failures.length} Scratch assets could not be verified; see asset-manifest.json`);
  return manifest;
}

if (process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url) {
  const defaultCache = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../.scratch-cache');
  const cacheRoot = process.argv[2] || defaultCache;
  buildOfflineAssets(cacheRoot, process.argv[3] || path.join(cacheRoot, 'package')).catch(error => {
    console.error(error instanceof Error ? error.message : String(error));
    process.exitCode = 1;
  });
}
