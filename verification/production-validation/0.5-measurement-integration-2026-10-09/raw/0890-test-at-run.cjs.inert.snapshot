'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const vm = require('node:vm');

const repository = path.resolve(__dirname, '../..');
const helperPath = path.join(repository, 'tools/internal/measurement-validation.cjs');
const helperSource = fs.readFileSync(helperPath, 'utf8');
const sdkCheck = 'cmake/SouiWindowsSdk.cmake';
const toolNames = ['OwnedProcessRunner', 'OwnedProcessAssignmentFixture', 'ProcessTreeSampler', 'InputTrace', 'NativeTraceIdentity'];
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function load(workspace, launches) {
  const module = {exports: {}};
  const localRequire = name => name === './owned-process.cjs' ? {
    launchOwned() {
      launches.count++;
      throw new Error('Native launch prohibited in pure preparation fixture');
    }
  } : require(name);
  vm.runInNewContext(helperSource, {module, require: localRequire,
    __dirname: path.join(workspace, 'tools/internal'), process, console, Buffer}, {filename: helperPath});
  return module.exports;
}

function fixture(t, architecture) {
  const temporaryRoot = fs.realpathSync(os.tmpdir());
  const workspace = fs.mkdtempSync(path.join(temporaryRoot, 'soui-measurement-source-'));
  const ownedPath = fs.realpathSync(workspace);
  t.after(() => {
    assert.equal(fs.realpathSync(workspace), ownedPath);
    assert.ok(ownedPath.startsWith(temporaryRoot + path.sep));
    fs.rmSync(workspace, {recursive: true, force: true});
  });
  const write = (name, bytes) => {
    const file = path.join(workspace, name);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, bytes);
  };
  // Real input bytes, synthetic bounded PE headers: none of these tools execute.
  const existing = load(repository, {count: 0}).fingerprint();
  for (const entry of existing.files) write(entry.path, fs.readFileSync(path.join(repository, entry.path)));
  write(sdkCheck, fs.readFileSync(path.join(repository, sdkCheck)));
  write('tools/internal/owned-process.cjs', '// pure fixture client\n');
  for (const name of ['process-tree-sampler', 'run-traces']) write(`tests/benchmark/${name}.test.cjs`, '// pure fixture test\n');
  const launches = {count: 0}, validation = load(workspace, launches);
  const directory = architecture === 'x64' ? 'out/benchmark-protocol' : 'out/benchmark-protocol-x86';
  const executables = toolNames.map(name => {
    const bytes = Buffer.alloc(128);
    bytes.writeUInt16LE(0x5a4d, 0);
    bytes.writeUInt32LE(64, 60);
    bytes.writeUInt32LE(0x4550, 64);
    bytes.writeUInt16LE(architecture === 'x64' ? 34404 : 332, 68);
    const filename = `${directory}/Release/${name}.exe`;
    write(filename, bytes);
    return {name, path: filename, bytes: bytes.length, sha256: hash(bytes), coff: architecture === 'x64' ? 34404 : 332};
  });
  write(`${directory}/measurement-tools.json`, JSON.stringify({schema: 'soui.measurement-tool-preparation/v1',
    architecture, configuration: 'Release', prepared: true, buildExitCode: 0,
    genericSupervisionPassed: null, source: validation.fingerprint(), executables}));
  return {workspace, write, validation, launches, sdkFile: path.join(workspace, sdkCheck)};
}

for (const architecture of ['x86', 'x64']) {
  test(`${architecture}: unchanged source and matching tool identities remain prepared`, t => {
    const f = fixture(t, architecture), prepared = f.validation.prepared(architecture);
    assert.equal(prepared.report.prepared, true);
    assert.equal(prepared.report.source.sha256, f.validation.fingerprint().sha256);
    assert.deepEqual(Object.keys(prepared.tools).sort(), [...toolNames].sort());
    assert.equal(f.launches.count, 0);
  });

  test(`${architecture}: changed Windows SDK selection input invalidates preparation`, t => {
    const f = fixture(t, architecture), before = f.validation.fingerprint().sha256;
    fs.appendFileSync(f.sdkFile, '\n# changed SDK selection fixture\n');
    assert.notEqual(f.validation.fingerprint().sha256, before, 'The actual top-level CMake SDK check is a build input');
    assert.throws(() => f.validation.prepared(architecture), /missing or stale/);
    assert.equal(f.launches.count, 0);
  });

  test(`${architecture}: missing Windows SDK selection input cannot reuse prepared tools`, t => {
    const f = fixture(t, architecture);
    fs.unlinkSync(f.sdkFile);
    assert.throws(() => f.validation.fingerprint(), /ENOENT/);
    assert.throws(() => f.validation.prepared(architecture), /ENOENT/);
    assert.equal(f.launches.count, 0);
  });

  test(`${architecture}: changed or missing SDK input rejects the native group before launch`, async t => {
    for (const mode of ['changed', 'missing']) {
      const f = fixture(t, architecture);
      if (mode === 'changed') fs.appendFileSync(f.sdkFile, '\n# changed SDK selection fixture\n');
      else fs.unlinkSync(f.sdkFile);
      await assert.rejects(f.validation.runGroup('native', architecture), /missing or stale|ENOENT/);
      assert.equal(f.launches.count, 0, `${mode} SDK input must fail before any owned subject is started`);
    }
  });
}
