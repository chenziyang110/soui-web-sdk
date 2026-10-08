'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const commandRoot = 'C:\\SOUI-SDK-production-validation\\preview15-v02-interaction-20260927\\command-logs';
const commands = ['v8-final-source-create-attempt1', 'v8-final-source-verify-attempt1'];
const names = ['result.json', 'stdout.log', 'stderr.log', 'external-gates.ndjson'];
const manifestPath = path.join(root, 'raw-manifest.json');
const files = commands.flatMap(command => names.map(name => ({
  original: path.join(commandRoot, command, name),
  archived: path.join('raw', command, name),
})));
const sha256 = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));

function verify() {
  const manifest = readJson(manifestPath);
  assert.equal(manifest.schema, 'soui.win7-v8-source-manifest-archive/v1');
  assert.equal(manifest.files.length, files.length);
  for (let i = 0; i < files.length; i++) {
    const record = manifest.files[i];
    const item = files[i];
    assert.equal(record.path, item.archived.replaceAll('\\', '/'));
    assert.equal(record.original, item.original);
    const archived = path.resolve(root, record.path);
    assert(archived.startsWith(path.join(root, 'raw') + path.sep));
    assert.equal(fs.statSync(archived).size, record.bytes);
    assert.equal(sha256(archived), record.sha256);
    if (fs.existsSync(item.original)) assert.equal(sha256(item.original), record.sha256);
  }
  for (const command of commands) {
    const dir = path.join(root, 'raw', command);
    const result = readJson(path.join(dir, 'result.json'));
    const gates = fs.readFileSync(path.join(dir, 'external-gates.ndjson'), 'utf8')
      .trim().split(/\r?\n/).map(JSON.parse);
    const output = readJson(path.join(dir, 'stdout.log'));
    assert.equal(result.exitCode, 0);
    assert.equal(result.timedOut, false);
    assert.equal(result.interruptedByExternal, null);
    assert.equal(result.gateError, null);
    assert.equal(result.error, null);
    assert(gates.length > 0);
    assert(gates.every(gate => !gate.error && Array.isArray(gate.processes) && gate.processes.length === 0));
    assert.equal(output.releaseReady, false);
    assert.equal(output.files, 37149);
    assert.equal(output.status, command.includes('-create-') ? 'create' : 'verify');
  }
  process.stdout.write(`verified ${files.length} raw files; create and verify exited 0 with empty load gates\n`);
}

function copy() {
  assert(!fs.existsSync(manifestPath), 'Archive exists; refusing overwrite');
  for (const item of files) {
    assert(fs.existsSync(item.original), `Missing original: ${item.original}`);
    assert(!fs.existsSync(path.join(root, item.archived)), `Archive destination exists: ${item.archived}`);
  }
  const entries = files.map(item => {
    const dest = path.join(root, item.archived);
    fs.mkdirSync(path.dirname(dest), {recursive: true});
    fs.copyFileSync(item.original, dest, fs.constants.COPYFILE_EXCL);
    return {path: item.archived.replaceAll('\\', '/'), original: item.original,
      bytes: fs.statSync(dest).size, sha256: sha256(dest)};
  });
  fs.writeFileSync(manifestPath, JSON.stringify({
    schema: 'soui.win7-v8-source-manifest-archive/v1',
    sourceDeliveryVerified: false, releaseReady: false, files: entries,
  }, null, 2) + '\n', {flag: 'wx'});
  verify();
}
if (process.argv[2] === 'copy') copy();
else if (process.argv[2] === 'verify') verify();
else throw new Error('Usage: node Archive.cjs copy|verify');
