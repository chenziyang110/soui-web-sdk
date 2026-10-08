'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const source = 'C:\\SOUI-SDK-production-validation\\benchmark-realtime-gate-20260928';
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const batches = [
  {name: '', runs: [{name: 'runner-identity-gate-tests-attempt1', tests: 21,
    pass: 21, fail: 0, exitCode: 0}], capture: true},
  {name: 'second', runs: [{name: 'gate-coverage-tests-attempt2', tests: 22,
    pass: 22, fail: 0, exitCode: 0}]},
  {name: 'third', runs: [{name: 'chronology-tests-attempt3', tests: 7,
    pass: 7, fail: 0, exitCode: 0}]},
  {name: 'fourth', runs: [{name: 'preflight-chronology-tests-attempt4', tests: 8,
    pass: 8, fail: 0, exitCode: 0}]},
  {name: 'fifth', runs: [{name: 'combined-tests-attempt5', tests: 24,
    pass: 24, fail: 0, exitCode: 0}]},
  {name: 'sixth', runs: [
    {name: 'in-run-gate-red-attempt1', tests: 8, pass: 7, fail: 1,
      exitCode: 1, text: 'Missing expected exception'},
    {name: 'in-run-gate-green-attempt1', tests: 24, pass: 24, fail: 0, exitCode: 0},
  ]},
  {name: 'seventh', runs: [
    {name: 'qemu-family-red-attempt1', tests: 16, pass: 15, fail: 1,
      exitCode: 1, text: 'qemu-system-x86_64w.exe'},
    {name: 'qemu-family-green-attempt1', tests: 24, pass: 24, fail: 0, exitCode: 0},
  ]},
  {name: 'eighth', runs: [
    {name: 'runner-pid-red-attempt1', tests: 8, pass: 7, fail: 1,
      exitCode: 1, text: 'Missing expected exception'},
    {name: 'runner-pid-green-attempt1', tests: 24, pass: 24, fail: 0, exitCode: 0},
  ]},
  {name: 'thirteenth', version: 13, runs: [
    {name: 'preflight-freshness-red-attempt1', tests: 9, pass: 8, fail: 1,
      exitCode: 1, text: 'Missing expected exception'},
    {name: 'preflight-freshness-green-attempt1', tests: 26, pass: 26, fail: 0,
      exitCode: 0},
  ]},
  {name: 'fourteenth', version: 14, runs: [
    {name: 'run-one-cleanup-attempt1', tests: 2, pass: 0, fail: 2, exitCode: 1,
      text: 'verifiedDirectChild'},
    {name: 'run-one-cleanup-first-attempt2', tests: 1, pass: 0, fail: 1,
      exitCode: 1, text: 'expectedExecutable'},
    {name: 'run-one-cleanup-first-attempt3', tests: 2, pass: 1, fail: 1,
      exitCode: 1, text: 'run-one.cjs soui|electron'},
    {name: 'run-one-cleanup-green-attempt1', tests: 2, pass: 2, fail: 0,
      exitCode: 0},
    {name: 'combined-tests-attempt14', tests: 28, pass: 28, fail: 0,
      exitCode: 0},
  ]},
  {name: 'fifteenth', version: 15, runs: [
    {name: 'run-one-cleanup-green-attempt2', tests: 2, pass: 2, fail: 0,
      exitCode: 0},
  ]},
  {name: 'sixteenth', version: 16, runs: [
    {name: 'helper-name-red-attempt1', tests: 17, pass: 16, fail: 1,
      exitCode: 1, text: 'ProcessTreeSampler.exe'},
    {name: 'helper-name-green-attempt1', tests: 28, pass: 28, fail: 0,
      exitCode: 0},
  ]},
  {name: 'seventeenth', version: 17, runs: [
    {name: 'owned-infrastructure-red-attempt1', exitCode: 1,
      cliError: 'invalid regular expression'},
    {name: 'owned-infrastructure-red-attempt2', tests: 1, pass: 0, fail: 1,
      exitCode: 1, text: 'Expected values to be strictly deep-equal'},
    {name: 'owned-infrastructure-green-attempt1', tests: 1, pass: 1, fail: 0,
      exitCode: 0},
    {name: 'owned-infrastructure-full-attempt1', tests: 29, pass: 29, fail: 0,
      exitCode: 0},
  ]},
  {name: 'eighteenth', version: 18, runs: [
    {name: 'launch-time-identity-red-attempt1', tests: 2, pass: 1, fail: 1,
      exitCode: 1, text: 'launch-time identity'},
    {name: 'launch-time-identity-green-attempt1', tests: 2, pass: 2, fail: 0,
      exitCode: 0},
    {name: 'launch-time-identity-combined-attempt1', tests: 29, pass: 29, fail: 0,
      exitCode: 0},
  ]},
];

function manifestPath(batch, index) {
  return path.join(root, index === 0 ? 'raw-manifest.json' :
    `raw-manifest-attempt${batch.version || index + 1}.json`);
}

function selected(batch) {
  const files = batch.capture ? ['Capture-Command.cjs'] : [];
  for (const run of batch.runs) {
    const prefix = `command-logs/${run.name}/`;
    files.push(`${prefix}result.json`, `${prefix}stdout.log`, `${prefix}stderr.log`);
  }
  return files;
}

function readArchived(relative) {
  return fs.readFileSync(path.join(root, 'raw', relative));
}

function assertCount(stdout, metric, count) {
  const line = new RegExp(`(?:^|\\r?\\n)[^\\r\\n]*${metric} ${count}(?:\\r?\\n|$)`);
  assert.match(stdout, line);
}

function verify(batch, index) {
  const manifest = JSON.parse(fs.readFileSync(manifestPath(batch, index), 'utf8'));
  assert.equal(manifest.schema, `soui.benchmark-live-gate-test-archive/v${batch.version || index + 1}`);
  assert.equal(manifest.benchmarkValid, false);
  assert.equal(manifest.releaseReady, false);
  assert.deepEqual(manifest.files.map(file => file.path), selected(batch));
  for (const entry of manifest.files) {
    const archived = readArchived(entry.path);
    assert.equal(archived.length, entry.bytes, entry.path);
    assert.equal(digest(archived), entry.sha256, entry.path);
    const original = path.join(source, entry.path);
    if (fs.existsSync(original))
      assert(fs.readFileSync(original).equals(archived), entry.path);
  }
  for (const run of batch.runs) {
    const prefix = `command-logs/${run.name}/`;
    const record = JSON.parse(readArchived(`${prefix}result.json`).toString('utf8'));
    const stdout = readArchived(`${prefix}stdout.log`);
    const stderr = readArchived(`${prefix}stderr.log`);
    assert.equal(record.exitCode, run.exitCode);
    assert.equal(record.timedOut, false);
    assert.equal(record.error, null);
    assert.equal(record.stdout.sha256, digest(stdout));
    assert.equal(record.stderr.sha256, digest(stderr));
    if (run.cliError) {
      assert.equal(stdout.length, 0);
      assert(stderr.toString('utf8').includes(run.cliError));
    } else {
      assert.equal(stderr.length, 0);
      assertCount(stdout.toString('utf8'), 'tests', run.tests);
      assertCount(stdout.toString('utf8'), 'pass', run.pass);
      assertCount(stdout.toString('utf8'), 'fail', run.fail);
      if (run.text) assert(stdout.toString('utf8').includes(run.text));
    }
  }
  process.stdout.write(`verified batch ${batch.version || index + 1}: ${manifest.files.length} original files\n`);
}

function copy(batch, index) {
  const destination = manifestPath(batch, index);
  if (fs.existsSync(destination)) throw new Error('Archive already exists');
  const entries = [];
  for (const relative of selected(batch)) {
    const archived = path.join(root, 'raw', relative);
    if (fs.existsSync(archived)) throw new Error(`Archive destination exists: ${relative}`);
    fs.mkdirSync(path.dirname(archived), {recursive: true});
    fs.copyFileSync(path.join(source, relative), archived, fs.constants.COPYFILE_EXCL);
    const bytes = readArchived(relative);
    entries.push({path: relative, bytes: bytes.length, sha256: digest(bytes)});
  }
  fs.writeFileSync(destination, JSON.stringify({
    schema: `soui.benchmark-live-gate-test-archive/v${batch.version || index + 1}`,
    benchmarkValid: false, releaseReady: false, files: entries,
  }, null, 2) + '\n', {flag: 'wx'});
  verify(batch, index);
}

function recordExisting(batch, index) {
  if (![17, 18].includes(batch.version))
    throw new Error('This batch was not captured in place');
  const destination = manifestPath(batch, index);
  if (fs.existsSync(destination)) throw new Error('Archive already exists');
  const entries = selected(batch).map(relative => {
    const bytes = readArchived(relative);
    if (batch.version === 17)
      assert(fs.readFileSync(path.join(source, relative)).equals(bytes), relative);
    else {
      const record = JSON.parse(readArchived(relative.replace(/(?:result\.json|stdout\.log|stderr\.log)$/,
        'result.json')).toString('utf8'));
      assert.equal(record.cwd, path.join(root, 'raw'), relative);
    }
    return {path: relative, bytes: bytes.length, sha256: digest(bytes)};
  });
  fs.writeFileSync(destination, JSON.stringify({
    schema: `soui.benchmark-live-gate-test-archive/v${batch.version}`,
    benchmarkValid: false, releaseReady: false, files: entries,
  }, null, 2) + '\n', {flag: 'wx'});
  verify(batch, index);
}

const action = process.argv[2];
const index = batches.findIndex(batch => action === (batch.name ? `verify-${batch.name}` : 'verify') ||
  action === (batch.name ? `copy-${batch.name}` : 'copy') ||
  action === (batch.name ? `record-${batch.name}` : 'record'));
if (index < 0) throw new Error('Usage: node Archive.cjs copy|verify|record-BATCH');
if (action.startsWith('copy')) copy(batches[index], index);
else if (action.startsWith('record')) recordExisting(batches[index], index);
else verify(batches[index], index);
