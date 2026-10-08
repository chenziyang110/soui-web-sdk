'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const id = require('./delivery-identity.cjs');

function replace(source, application) {
  source = id.safePath(source);
  application = id.safePath(application);
  const destination = id.safePath(application, 'resources');
  const names = id.walk(source);
  if (!names.includes('index.html')) throw new Error('Web build missing index.html');
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
    if (id.snapshot(source, names).sha256 !== id.snapshot(stage, names).sha256) throw new Error('Web source changed during deployment');
    if (fs.existsSync(destination)) { fs.renameSync(destination, backup); moved = true; }
    try { fs.renameSync(stage, destination); }
    catch (error) {
      if (moved) fs.renameSync(backup, destination);
      throw error;
    }
    if (moved) fs.rmSync(id.safePath(application, path.basename(backup)), {recursive: true});
  } finally {
    if (fs.existsSync(stage)) fs.rmSync(id.safePath(application, path.basename(stage)), {recursive: true});
  }
  return {files: names.length};
}
module.exports = {replace};
if (require.main === module) {
  try {
    if (process.argv.length !== 4) throw new Error('Usage: deploy-resources.cjs WEB_ROOT APP_OUTPUT');
    console.log(JSON.stringify(replace(process.argv[2], process.argv[3])));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
