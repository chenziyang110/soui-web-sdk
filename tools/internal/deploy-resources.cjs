'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const id = require('./delivery-identity.cjs');

function publishStagedDirectory(stage, destination) {
  const delay = new Int32Array(new SharedArrayBuffer(4));
  for (let attempt = 0; ; ++attempt) {
    try {
      fs.renameSync(stage, destination);
      return;
    } catch (error) {
      // Use the SDK installer's bounded Windows publication policy. Keep
      // persistent errors and destination conflicts on the rollback path.
      if (process.platform !== 'win32' || !['EPERM', 'EACCES'].includes(error.code) ||
          attempt >= 5 || !fs.existsSync(stage) || fs.existsSync(destination)) throw error;
      Atomics.wait(delay, 0, 0, 30 * (attempt + 1));
    }
  }
}

function replace(source, application, distribution) {
  source = id.safePath(source);
  application = id.safePath(application);
  distribution = id.safePath(distribution);
  const destination = id.safePath(application, 'resources');
  const names = id.walk(source);
  if (!names.includes('index.html')) throw new Error('Web build missing index.html');
  if (names.some(name => name.startsWith('legal/'))) throw new Error('Web build owns reserved legal path');
  const requiredNotices = ['LICENSE.md', 'THIRD_PARTY_NOTICES.md'];
  for (const name of requiredNotices) {
    if (!fs.statSync(id.safePath(distribution, name)).isFile()) throw new Error('SDK legal materials missing: ' + name);
  }
  const licenses = id.walk(distribution, 'third_party/licenses');
  if (!licenses.length) throw new Error('SDK legal materials missing');
  const legal = [...requiredNotices, ...licenses];
  const suffix = crypto.randomUUID();
  const stage = id.safePath(application, `.resources-new-${suffix}`);
  const backup = id.safePath(application, `.resources-old-${suffix}`);
  fs.mkdirSync(stage, {recursive: true});
  let moved = false;
  try {
    for (const name of names) {
      const target = id.safePath(stage, name);
      fs.mkdirSync(path.dirname(target), {recursive: true});
      fs.copyFileSync(id.safePath(source, name), target);
    }
    for (const name of legal) {
      const target = id.safePath(stage, `legal/${name}`);
      fs.mkdirSync(path.dirname(target), {recursive: true});
      fs.copyFileSync(id.safePath(distribution, name), target);
    }
    if (id.snapshot(source, names).sha256 !== id.snapshot(stage, names).sha256) throw new Error('Web source changed during deployment');
    for (const name of legal) {
      if (id.hash(fs.readFileSync(id.safePath(distribution, name))) !==
          id.hash(fs.readFileSync(id.safePath(stage, `legal/${name}`)))) throw new Error('SDK legal material changed during deployment');
    }
    if (fs.existsSync(destination)) { fs.renameSync(destination, backup); moved = true; }
    try { publishStagedDirectory(stage, destination); }
    catch (error) {
      if (moved) fs.renameSync(backup, destination);
      throw error;
    }
    if (moved) fs.rmSync(id.safePath(application, path.basename(backup)), {recursive: true});
  } finally {
    if (fs.existsSync(stage)) fs.rmSync(id.safePath(application, path.basename(stage)), {recursive: true});
  }
  return {files: names.length + legal.length};
}
module.exports = {replace};
if (require.main === module) {
  try {
    if (process.argv.length !== 5) throw new Error('Usage: deploy-resources.cjs WEB_ROOT APP_OUTPUT SDK_ROOT');
    console.log(JSON.stringify(replace(process.argv[2], process.argv[3], process.argv[4])));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
