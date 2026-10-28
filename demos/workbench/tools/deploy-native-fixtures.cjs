'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');

// These nine native scan samples deliberately exercise Unicode names.
const sampleFiles = [
  'fileinsight-fixture/B.TXT', 'fileinsight-fixture/LICENSE',
  'fileinsight-fixture/a.txt', 'fileinsight-fixture/image.bin',
  'fileinsight-fixture/nested/报告.txt',
  'fileinsight-fixture-alt/EMPTY', 'fileinsight-fixture-alt/alpha.md',
  'fileinsight-fixture-alt/data.json', 'fileinsight-fixture-alt/nested/配置.ini',
].sort();
const sampleDirectories = [
  'fileinsight-fixture', 'fileinsight-fixture/nested',
  'fileinsight-fixture-alt', 'fileinsight-fixture-alt/nested',
].sort();
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const ownerSchema = 'soui.workbench-native-fixture-owner/v1';

function plainPath(value) {
  const absolute = path.resolve(value);
  for (let p = absolute; ; p = path.dirname(p)) {
    if (fs.existsSync(p)) assert.ok(!fs.lstatSync(p).isSymbolicLink(), 'Reparse path rejected');
    if (path.dirname(p) === p) break;
  }
  return absolute;
}

function snapshot(root, selectedOnly = false) {
  const files = [], directories = [];
  function walk(relative) {
    assert.ok(sampleDirectories.includes(relative), 'Unexpected native fixture directory');
    const directory = plainPath(path.join(root, relative));
    assert.ok(fs.lstatSync(directory).isDirectory());
    directories.push(relative);
    for (const name of fs.readdirSync(directory).sort()) {
      const relativeFile = relative + '/' + name, file = plainPath(path.join(root, relativeFile));
      const status = fs.lstatSync(file);
      if (status.isDirectory()) { walk(relativeFile); continue; }
      assert.ok(sampleFiles.includes(relativeFile), 'Unexpected native fixture file');
      assert.ok(status.isFile() && status.nlink === 1 && status.size <= 65536, 'Plain bounded sample required');
      const bytes = fs.readFileSync(file);
      assert.equal(bytes.length, status.size);
      files.push({path: relativeFile, bytes: bytes.length, sha256: digest(bytes)});
    }
  }
  if (!selectedOnly) assert.deepEqual(fs.readdirSync(root).sort(), ['fileinsight-fixture', 'fileinsight-fixture-alt'].sort());
  walk('fileinsight-fixture'); walk('fileinsight-fixture-alt');
  files.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
  assert.deepEqual(files.map(row => row.path), sampleFiles);
  assert.deepEqual(directories.sort(), sampleDirectories);
  return {files, directories};
}

function deploy(source, application) {
  source = plainPath(source); application = plainPath(application);
  assert.ok(fs.statSync(source).isDirectory() && fs.statSync(application).isDirectory());
  const destination = path.join(application, 'fixture-data');
  const relative = path.relative(source, destination);
  assert.ok(path.isAbsolute(relative) || relative === '..' || relative.startsWith('..' + path.sep), 'Output inside fixture source rejected');
  const expected = snapshot(source, true);
  const ownerPath = path.join(application, '.soui-workbench-native-fixtures.json');
  let previous = null, oldOwnerBytes = null;
  if (fs.existsSync(destination)) {
    previous = snapshot(plainPath(destination));
    const status = fs.lstatSync(plainPath(ownerPath));
    assert.ok(status.isFile() && status.nlink === 1 && status.size <= 8192, 'Native fixture ownership required');
    oldOwnerBytes = fs.readFileSync(ownerPath);
    const owner = JSON.parse(oldOwnerBytes.toString('utf8'));
    assert.equal(owner.schema, ownerSchema);
    assert.deepEqual(owner.snapshot, previous, 'Existing native fixture identity differs');
    if (JSON.stringify(previous) === JSON.stringify(expected)) {
      return {root: destination, files: expected.files.length, bytes: expected.files.reduce((sum, row) => sum + row.bytes, 0), reused: true};
    }
  } else {
    assert.ok(!fs.existsSync(ownerPath), 'Orphan native fixture ownership record');
  }
  const nonce = crypto.randomUUID();
  const stage = path.join(application, '.fixture-data-new-' + nonce);
  const stagedOwner = path.join(application, '.fixture-data-owner-new-' + nonce);
  const backup = path.join(application, '.fixture-data-old-' + nonce);
  const backupOwner = path.join(application, '.fixture-data-owner-old-' + nonce);
  fs.mkdirSync(stage);
  // Failure retains the fresh stage for inspection; an existing payload is never replaced.
  for (const row of expected.files) {
    const bytes = fs.readFileSync(plainPath(path.join(source, row.path)));
    assert.equal(digest(bytes), row.sha256, 'Source sample changed');
    const target = path.join(stage, row.path);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.writeFileSync(target, bytes, {flag: 'wx'});
  }
  assert.deepEqual(snapshot(stage), expected);
  assert.deepEqual(snapshot(source, true), expected, 'Source fixture set changed');
  const ownerBytes = Buffer.from(JSON.stringify({schema: ownerSchema, snapshot: expected}, null, 2) + '\n');
  fs.writeFileSync(stagedOwner, ownerBytes, {flag: 'wx'});
  let movedOld = false, movedOwner = false, published = false;
  try {
    if (previous) {
      assert.deepEqual(snapshot(plainPath(destination)), previous, 'Owned native output changed');
      assert.deepEqual(fs.readFileSync(plainPath(ownerPath)), oldOwnerBytes, 'Owner record changed');
      fs.renameSync(destination, backup); movedOld = true;
      fs.renameSync(ownerPath, backupOwner); movedOwner = true;
    } else assert.ok(!fs.existsSync(destination), 'Native fixture destination appeared');
    fs.renameSync(stage, destination); published = true;
    assert.ok(!fs.existsSync(ownerPath), 'Native fixture ownership destination appeared');
    fs.renameSync(stagedOwner, ownerPath);
  } catch (error) {
    if (published) {
      assert.ok(!fs.existsSync(stage), 'Retained stage destination appeared');
      fs.renameSync(destination, stage);
    }
    if (movedOld) {
      assert.ok(!fs.existsSync(destination), 'Rollback data destination appeared');
      fs.renameSync(backup, destination);
    }
    if (movedOwner) {
      assert.ok(!fs.existsSync(ownerPath), 'Rollback ownership destination appeared');
      fs.renameSync(backupOwner, ownerPath);
    }
    throw error;
  }
  assert.deepEqual(snapshot(destination), expected);
  if (previous) {
    // Only the exact owned nine-file backup is removed; no recursive deletion.
    assert.equal(path.dirname(path.resolve(backup)), application);
    assert.deepEqual(snapshot(plainPath(backup)), previous);
    assert.deepEqual(fs.readFileSync(plainPath(backupOwner)), oldOwnerBytes);
    for (const row of previous.files) fs.unlinkSync(plainPath(path.join(backup, row.path)));
    for (const directory of [...previous.directories].sort((a, b) => b.length - a.length)) fs.rmdirSync(path.join(backup, directory));
    fs.rmdirSync(backup); fs.unlinkSync(backupOwner);
  }
  return {root: destination, files: expected.files.length, bytes: expected.files.reduce((sum, row) => sum + row.bytes, 0), reused: false};
}

function verify(source, application) {
  source = plainPath(source); application = plainPath(application);
  assert.ok(fs.statSync(source).isDirectory() && fs.statSync(application).isDirectory());
  const expected = snapshot(source, true), destination = plainPath(path.join(application, 'fixture-data'));
  assert.deepEqual(snapshot(destination), expected, 'Native fixture source/output identity differs');
  const ownerPath = plainPath(path.join(application, '.soui-workbench-native-fixtures.json'));
  const status = fs.lstatSync(ownerPath);
  assert.ok(status.isFile() && status.nlink === 1 && status.size <= 8192, 'Plain bounded native fixture ownership required');
  const ownerBytes = fs.readFileSync(ownerPath);
  assert.equal(ownerBytes.length, status.size);
  const owner = JSON.parse(ownerBytes.toString('utf8'));
  assert.equal(owner.schema, ownerSchema);
  assert.deepEqual(owner.snapshot, expected, 'Native fixture ownership snapshot differs');
  assert.deepEqual(snapshot(source, true), expected, 'Native fixture source changed during verification');
  assert.deepEqual(snapshot(destination), expected, 'Native fixture output changed during verification');
  const finalStatus = fs.lstatSync(plainPath(ownerPath));
  assert.ok(finalStatus.isFile() && finalStatus.nlink === 1 && finalStatus.size === ownerBytes.length, 'Native fixture owner changed');
  assert.deepEqual(fs.readFileSync(ownerPath), ownerBytes, 'Native fixture owner bytes changed');
  return [...expected.files.map(row => ({...row, path: 'fixture-data/' + row.path})),
    {path: '.soui-workbench-native-fixtures.json', bytes: ownerBytes.length, sha256: digest(ownerBytes)}]
    .sort((left, right) => left.path < right.path ? -1 : left.path > right.path ? 1 : 0);
}

module.exports = {deploy, verify};
if (require.main === module) {
  const args = process.argv.slice(2);
  assert.equal(args.length, 4);
  assert.equal(args[0], '--source'); assert.equal(args[2], '--application');
  console.log(JSON.stringify(deploy(args[1], args[3])));
}
