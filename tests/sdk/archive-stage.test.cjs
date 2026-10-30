'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const {performance} = require('node:perf_hooks');
const id = require('../../tools/internal/delivery-identity.cjs');
const profiles = require('../../tools/internal/compression-profiles.cjs');
const archiveStage = require('../../tools/internal/archive-stage.cjs');
const deadlinePin = require('../../tools/internal/deadline-file-pin.cjs');
const originalSpawnSync = cp.spawnSync, originalToolIdentity = profiles.toolIdentity;

function fixture(t) {
  assert.equal(cp.spawnSync, originalSpawnSync, 'Previous fixture did not restore the native process boundary');
  assert.equal(profiles.toolIdentity, originalToolIdentity, 'Previous fixture did not restore the real tool identity reader');
  const parent = fs.realpathSync(os.tmpdir()), root = fs.mkdtempSync(path.join(parent, 'soui-extract-test-'));
  t.after(() => {
    assert.equal(path.dirname(root), parent);
    assert.ok(path.basename(root).startsWith('soui-extract-test-'));
    fs.rmSync(id.safePath(root), {recursive: true, force: true});
  });
  const archive = path.join(root, 'input.zip'); fs.writeFileSync(archive, 'inert archive');
  const contents = new Map([['data/.hidden', Buffer.from('dotfile')], ['data/中文 空格.txt', Buffer.from('exact bytes')]]);
  const files = [...contents].map(([name, bytes]) => ({path: name, bytes: bytes.length, sha256: id.hash(bytes)}));
  const pin = () => {const bytes = fs.readFileSync(archive); return {path: archive, bytes: bytes.length, sha256: id.hash(bytes)};};
  const options = {archive: pin(), files, destination: path.join(root, 'published'), sevenZipPath: path.join(root, '7z.exe'),
    powershellPath: path.join(root, 'powershell.exe'), timeoutMs: 120000};
  const members = () => files.map(row => ({path: row.path, bytes: row.bytes, attributes: 32}));
  const put = (stage, name, bytes) => {const full = id.safePath(stage, name); fs.mkdirSync(path.dirname(full), {recursive: true}); fs.writeFileSync(full, bytes);};
  const calls = [];
  let toolMock, processMock;
  // Install each property mock once. Re-mocking a mocked property records that
  // earlier mock as its original, so automatic restoration can leak it onward.
  const replaceToolIdentity = implementation => {
    if (toolMock) toolMock.mock.mockImplementation(implementation);
    else toolMock = t.mock.method(profiles, 'toolIdentity', implementation);
  };
  const native = (hook = () => undefined, listing = members) => {
    replaceToolIdentity((_file, limits) => {assert.ok(limits.deadline > 0); return {sha256: 'a'.repeat(64), runtimeFiles: []};});
    const implementation = (executable, args, settings) => {
      const role = executable === options.powershellPath ? 'members' : args[0];
      const call = {role, executable, args, settings}; calls.push(call);
      const override = hook(call);
      if (override) return override;
      if (role === 'members') return {status: 0, stdout: JSON.stringify({schema: 'soui.zip-members/v1', entries: listing()}), stderr: ''};
      if (role === 'x') for (const [name, bytes] of contents) put(args.find(a => a.startsWith('-o')).slice(2), name, bytes);
      return {status: 0, stdout: role + ' original stdout', stderr: role + ' original stderr'};
    };
    if (processMock) processMock.mock.mockImplementation(implementation);
    else processMock = t.mock.method(cp, 'spawnSync', implementation);
  };
  const noStage = () => assert.equal(fs.readdirSync(root).some(name => name.startsWith('.soui-extract-')), false);
  return {root, archive, pin, contents, files, options, members, native, replaceToolIdentity, calls, put, noStage};
}

test('verified staging publishes a complete exact file set and records actual native results', t => {
  const f = fixture(t); f.native(); const receipt = archiveStage.extract(f.options);
  assert.equal(receipt.schema, 'soui.archive-extraction/v1'); assert.equal(receipt.files, 2);
  assert.deepEqual(f.calls.map(c => c.role), ['members', 't', 'x']);
  for (const [name, bytes] of f.contents) assert.deepEqual(fs.readFileSync(path.join(f.options.destination, name)), bytes);
  assert.deepEqual(receipt.steps.map(row => row.exitCode), [0, 0, 0]); f.noStage();
});

for (const mode of ['empty', 'partial', 'extra', 'changed']) test('exit zero cannot publish ' + mode + ' extraction', t => {
  const f = fixture(t); f.native(({role, args}) => {
    if (role !== 'x') return;
    const stage = args.find(a => a.startsWith('-o')).slice(2);
    if (mode !== 'empty') f.put(stage, f.files[0].path, mode === 'changed' ? 'wrong bytes' : f.contents.get(f.files[0].path));
    if (mode === 'changed') f.put(stage, f.files[1].path, f.contents.get(f.files[1].path));
    if (mode === 'extra') {f.put(stage, f.files[1].path, f.contents.get(f.files[1].path)); f.put(stage, 'unlisted.txt', 'extra');}
    return {status: 0, stdout: 'empty or incomplete native success', stderr: ''};
  });
  assert.throws(() => archiveStage.extract(f.options), mode === 'changed' ? /changed/i : /closed set|missing/i);
  assert.equal(fs.existsSync(f.options.destination), false); f.noStage();
});

for (const status of [1, 2, null]) test('native failure ' + status + ' preserves streams and removes only its own partial stage', t => {
  const f = fixture(t), original = Object.assign(new Error('native launch failure'), {code: 'ETIMEDOUT'});
  f.native(({role, args}) => {
    if (role !== 'x') return;
    f.put(args.find(a => a.startsWith('-o')).slice(2), 'partial.txt', 'partial');
    return {status, signal: status === null ? 'SIGTERM' : null, error: status === null ? original : undefined,
      stdout: 'original native output', stderr: 'original native failure'};
  });
  assert.throws(() => archiveStage.extract(f.options), error => {
    if (status === null) assert.equal(error, original);
    const step = error.extraction.steps.at(-1); assert.equal(step.exitCode, status);
    assert.equal(step.stdout, 'original native output'); assert.equal(step.stderr, 'original native failure'); return true;
  });
  assert.equal(fs.existsSync(f.options.destination), false); f.noStage();
});

const unsafeMembers = [
  '../escape.txt', '/absolute.txt', 'C:/absolute.txt', 'data\\escape.txt', 'data/stream:ads',
  'data/NUL.txt', 'data/trailing.', 'data/a\nb.txt', 'data//empty.txt',
];
for (const name of unsafeMembers) test('preflight refuses unsafe archive member ' + JSON.stringify(name), t => {
  const f = fixture(t); f.native(undefined, () => [...f.members(), {path: name, bytes: 1, attributes: 32}]);
  assert.throws(() => archiveStage.extract(f.options), /path|member/i);
  assert.deepEqual(f.calls.map(c => c.role), ['members']); f.noStage();
});

for (const kind of ['case-collision', 'symbolic-link', 'reparse', 'directory-conflict', 'size-mismatch', 'unlisted-directory'])
  test('preflight rejects ' + kind + ' before extraction starts', t => {
    const f = fixture(t); f.native(undefined, () => {
      const rows = f.members();
      if (kind === 'case-collision') rows.push({...rows[0], path: rows[0].path.toUpperCase()});
      if (kind === 'symbolic-link') rows[0].attributes = (0xa1ff * 65536) >>> 0;
      if (kind === 'reparse') rows[0].attributes = 0x400;
      if (kind === 'directory-conflict') rows.push({path: rows[0].path + '/', bytes: 0, attributes: 16});
      if (kind === 'size-mismatch') rows[0].bytes++;
      if (kind === 'unlisted-directory') rows.push({path: 'unknown/', bytes: 0, attributes: 16});
      return rows;
    });
    assert.throws(() => archiveStage.extract(f.options)); assert.deepEqual(f.calls.map(c => c.role), ['members']); f.noStage();
  });

test('an archive identity change during metadata read blocks every extraction command', t => {
  const f = fixture(t); f.native(({role}) => {if (role === 'members') fs.appendFileSync(f.archive, 'changed');});
  assert.throws(() => archiveStage.extract(f.options), /archive.*changed/i);
  assert.deepEqual(f.calls.map(c => c.role), ['members']); f.noStage();
});

test('archive drift during extraction refuses publication after native success', t => {
  const f = fixture(t); f.native(({role}) => {if (role === 'x') fs.appendFileSync(f.archive, 'changed');});
  assert.throws(() => archiveStage.extract(f.options), /archive.*changed/i);
  assert.equal(fs.existsSync(f.options.destination), false); f.noStage();
});

test('archive tool runtime changes cannot authorize publication', t => {
  const f = fixture(t); f.native(); let observations = 0;
  f.replaceToolIdentity(() => ({sha256: 'a'.repeat(64), runtimeFiles: [{sha256: (++observations).toString().repeat(64)}]}));
  assert.throws(() => archiveStage.extract(f.options), /tool\/runtime identity changed/i);
  assert.equal(fs.existsSync(f.options.destination), false); f.noStage();
});

test('one aggregate deadline shrinks across native steps and never resets', t => {
  const f = fixture(t); let now = 1000; t.mock.method(performance, 'now', () => now);
  f.native(() => {now += 17;}); archiveStage.extract({...f.options, timeoutMs: 1000});
  assert.deepEqual(f.calls.map(c => c.settings.timeout), [1000, 983, 966]);
});

test('an exhausted deadline stops before starting another native step', t => {
  const f = fixture(t); let now = 1000; t.mock.method(performance, 'now', () => now);
  f.native(() => {now += 1001;});
  assert.throws(() => archiveStage.extract({...f.options, timeoutMs: 1000}), /deadline exceeded/);
  assert.deepEqual(f.calls.map(c => c.role), ['members']); f.noStage();
});

test('streamed archive hashing stops at the same deadline between tiny chunks and closes its descriptor', t => {
  const f = fixture(t); let now = 1000, reads = 0, closes = 0;
  const read = fs.readSync, close = fs.closeSync;
  t.mock.method(performance, 'now', () => now);
  t.mock.method(fs, 'readSync', (fd, buffer, offset, length, position) => {
    ++reads; const count = read(fd, buffer, offset, Math.min(length, 2), position); now += 101; return count;
  });
  t.mock.method(fs, 'closeSync', fd => {++closes; return close(fd);});
  assert.throws(() => deadlinePin.pin(f.archive, {deadline: 1150}), error => error.code === 'ETIMEDOUT');
  assert.equal(reads, 2); assert.equal(closes, 1);
});

test('a file changed between tiny hash chunks cannot produce a stable pin', t => {
  const f = fixture(t); const read = fs.readSync; let changed = false;
  t.mock.method(fs, 'readSync', (fd, buffer, offset, length, position) => {
    const count = read(fd, buffer, offset, Math.min(length, 2), position);
    if (!changed) {changed = true; fs.writeFileSync(f.archive, 'other archive'); fs.utimesSync(f.archive, 1000000000, 1000000001);}
    return count;
  });
  assert.throws(() => deadlinePin.pin(f.archive, {deadline: performance.now() + 10000}), /changed/i);
});

test('ancestor traversal stops before another lstat once its deadline expires', t => {
  const f = fixture(t); const stat = fs.lstatSync; let now = 1000, stats = 0;
  t.mock.method(performance, 'now', () => now);
  t.mock.method(fs, 'lstatSync', (...args) => {++stats; const value = stat(...args); now += 101; return value;});
  assert.throws(() => deadlinePin.pin(f.archive, {deadline: 1050}), error => error.code === 'ETIMEDOUT');
  assert.equal(stats, 1);
});

test('payload hash deadline expiry refuses publication after native success', t => {
  const f = fixture(t); let now = 1000, afterExtraction = false; const open = fs.openSync, read = fs.readSync, paths = new Map();
  t.mock.method(performance, 'now', () => now); f.native(({role}) => {if (role === 'x') afterExtraction = true;});
  t.mock.method(fs, 'openSync', (file, ...args) => {const fd = open(file, ...args); paths.set(fd, file); return fd;});
  t.mock.method(fs, 'readSync', (fd, ...args) => {
    const count = read(fd, ...args);
    if (afterExtraction && String(paths.get(fd)).includes('.soui-extract-')) now += 1001;
    return count;
  });
  assert.throws(() => archiveStage.extract({...f.options, timeoutMs: 1000}), error => {
    assert.equal(error.code, 'ETIMEDOUT'); assert.equal(error.extraction.steps.at(-1).exitCode, 0); return true;
  });
  assert.equal(fs.existsSync(f.options.destination), false); f.noStage();
});

test('optional tool identity deadline covers streamed executable and runtime hashing', t => {
  const f = fixture(t), runtime = path.join(f.root, '7z.dll'); fs.writeFileSync(f.options.sevenZipPath, 'tiny executable'); fs.writeFileSync(runtime, 'tiny runtime');
  let now = 1000, observations = 0; const open = fs.openSync, read = fs.readSync, paths = new Map();
  t.mock.method(performance, 'now', () => now);
  t.mock.method(fs, 'openSync', (file, ...args) => {const fd = open(file, ...args); paths.set(fd, file); return fd;});
  t.mock.method(fs, 'readSync', (fd, ...args) => {const count = read(fd, ...args); if (paths.get(fd) === runtime) now += 101; return count;});
  t.mock.method(cp, 'spawnSync', () => {++observations; return {status: 0, stdout: '7-Zip 25.01\nLibs:\n0 : 25.01 : 7z.dll\n', stderr: ''};});
  assert.throws(() => profiles.toolIdentity(f.options.sevenZipPath, {deadline: 1050}), error => error.code === 'ETIMEDOUT');
  assert.equal(observations, 1);
});

for (const status of [2, null]) test('optional tool identity failure preserves native status and streams ' + status, t => {
  const f = fixture(t), original = Object.assign(new Error('original identity launch failure'), {code: 'ETIMEDOUT'});
  fs.writeFileSync(f.options.sevenZipPath, 'tiny executable');
  t.mock.method(cp, 'spawnSync', () => ({status, signal: status === null ? 'SIGTERM' : null, error: status === null ? original : undefined,
    stdout: 'original identity stdout', stderr: 'original identity stderr'}));
  assert.throws(() => profiles.toolIdentity(f.options.sevenZipPath, {deadline: performance.now() + 10000}), error => {
    if (status === null) assert.equal(error, original);
    assert.equal(error.archiveToolObservation.exitCode, status); assert.equal(error.archiveToolObservation.stdout, 'original identity stdout');
    assert.equal(error.archiveToolObservation.stderr, 'original identity stderr'); return true;
  });
});

test('relative tool paths and invalid budgets fail before any native command', t => {
  const f = fixture(t); f.native();
  for (const timeoutMs of [0, -1, Infinity, NaN, 1200001]) assert.throws(() => archiveStage.extract({...f.options, timeoutMs}), /timeout/);
  assert.throws(() => archiveStage.extract({...f.options, sevenZipPath: './7z.exe'}), /absolute.*tool/i);
  assert.equal(f.calls.length, 0); f.noStage();
});

test('absolute Windows path aliases cannot bypass destination name validation', t => {
  const f = fixture(t); f.native();
  for (const name of ['published.', 'published ', 'NUL.txt', 'stream:ads']) {
    assert.throws(() => archiveStage.extract({...f.options, destination: path.join(f.root, name)}), /path/i);
  }
  assert.equal(f.calls.length, 0); f.noStage();
});

test('existing destinations and a publication race are never merged or overwritten', t => {
  const f = fixture(t); fs.mkdirSync(f.options.destination); fs.writeFileSync(path.join(f.options.destination, 'mine.txt'), 'owned');
  f.native(); assert.throws(() => archiveStage.extract(f.options), /destination.*exists/i); assert.equal(f.calls.length, 0);
  fs.rmSync(f.options.destination, {recursive: true});
  f.native(({role}) => {if (role === 'x') {fs.mkdirSync(f.options.destination); fs.writeFileSync(path.join(f.options.destination, 'mine.txt'), 'racing owner');}});
  assert.throws(() => archiveStage.extract(f.options), /destination.*exists/i);
  assert.equal(fs.readFileSync(path.join(f.options.destination, 'mine.txt'), 'utf8'), 'racing owner'); f.noStage();
});

test('cleanup denial is attached without replacing the original native error', t => {
  const f = fixture(t), native = Object.assign(new Error('first native error'), {code: 'EIO'}), remove = fs.rmSync;
  const cleanup = Object.assign(new Error('cleanup denied'), {code: 'EACCES'}); let stage;
  f.native(({role, args}) => {if (role === 'x') {stage = args.find(a => a.startsWith('-o')).slice(2); return {error: native, status: null, stdout: '', stderr: 'first'};}});
  t.mock.method(fs, 'rmSync', (target, ...args) => {if (target === stage) throw cleanup; return remove(target, ...args);});
  assert.throws(() => archiveStage.extract(f.options), error => {assert.equal(error, native); assert.equal(error.cleanupFailure.code, 'EACCES'); return true;});
  assert.equal(fs.existsSync(stage), true); assert.equal(fs.existsSync(f.options.destination), false);
});

test('extracted hard links cannot be published', t => {
  const f = fixture(t); const external = path.join(f.root, 'owned-external.txt'); fs.writeFileSync(external, 'owned');
  f.native(({role, args}) => {if (role === 'x') {fs.linkSync(external, path.join(args.find(a => a.startsWith('-o')).slice(2), 'linked')); return {status: 0, stdout: '', stderr: ''};}});
  assert.throws(() => archiveStage.extract(f.options), /plain|link/i); assert.equal(fs.readFileSync(external, 'utf8'), 'owned'); f.noStage();
});

test('directory links in extracted content cannot escape or delete external data', t => {
  const f = fixture(t), external = path.join(f.root, 'external'); fs.mkdirSync(external);
  fs.writeFileSync(path.join(external, 'mine.txt'), 'owned external data');
  f.native(({role, args}) => {
    if (role === 'x') {fs.symlinkSync(external, path.join(args.find(a => a.startsWith('-o')).slice(2), 'redirect'), process.platform === 'win32' ? 'junction' : 'dir');
      return {status: 0, stdout: '', stderr: ''};}
  });
  assert.throws(() => archiveStage.extract(f.options), /linked path/i);
  assert.equal(fs.readFileSync(path.join(external, 'mine.txt'), 'utf8'), 'owned external data'); f.noStage();
});

test('a linked destination ancestor is rejected before any native command', t => {
  const f = fixture(t), external = path.join(f.root, 'external'), link = path.join(f.root, 'redirect'); fs.mkdirSync(external);
  fs.symlinkSync(external, link, process.platform === 'win32' ? 'junction' : 'dir'); f.native();
  assert.throws(() => archiveStage.extract({...f.options, destination: path.join(link, 'published')}), /linked path/i);
  assert.equal(f.calls.length, 0); assert.deepEqual(fs.readdirSync(external), []); f.noStage();
});

test('CLI preserves a native extraction failure in diagnostics and returns nonzero', t => {
  const f = fixture(t), plan = path.join(f.root, 'plan.json'), preload = path.join(f.root, 'fake-native.cjs');
  fs.writeFileSync(plan, JSON.stringify({schema: 'soui.archive-extraction-plan/v1', ...f.options, powershellPath: undefined}));
  const profileModule = require.resolve('../../tools/internal/compression-profiles.cjs');
  fs.writeFileSync(preload, [
    `require(${JSON.stringify(profileModule)}).toolIdentity=()=>({sha256:'a'.repeat(64),runtimeFiles:[]});`,
    `require('node:child_process').spawnSync=(_exe,args)=>{`,
    `if(args.includes('-File'))return {status:0,stdout:${JSON.stringify(JSON.stringify({schema: 'soui.zip-members/v1', entries: f.members()}))},stderr:''};`,
    `if(args[0]==='x')return {status:2,stdout:'native original stdout',stderr:'native original stderr'};`,
    `return {status:0,stdout:'test ok',stderr:''};};`,
  ].join('\n'));
  const actual = cp.spawnSync(process.execPath, ['--require', preload, require.resolve('../../tools/internal/archive-stage.cjs'), plan],
    {encoding: 'utf8', windowsHide: true, timeout: 10000});
  assert.equal(actual.status, 1); assert.equal(actual.stdout, '');
  const line = actual.stderr.split(/\r?\n/).find(value => value.startsWith('Extraction diagnostics: '));
  const diagnostics = JSON.parse(line.slice('Extraction diagnostics: '.length));
  assert.equal(diagnostics.extraction.steps.at(-1).exitCode, 2);
  assert.equal(diagnostics.extraction.steps.at(-1).stderr, 'native original stderr');
  assert.equal(fs.existsSync(f.options.destination), false); f.noStage();
});

// Tiny stored ZIP records are test data, not a production ZIP reader/writer.
function storedZip(rows) {
  const local = [], central = []; let offset = 0;
  for (const row of rows) {
    const name = Buffer.from(row.path), data = Buffer.from(row.data || ''); let crc = 0xffffffff;
    for (const byte of data) {crc ^= byte; for (let n = 0; n < 8; ++n) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);}
    crc = (crc ^ 0xffffffff) >>> 0;
    const header = Buffer.alloc(30); header.writeUInt32LE(0x04034b50); header.writeUInt16LE(20, 4); header.writeUInt16LE(0x800, 6);
    header.writeUInt32LE(crc, 14); header.writeUInt32LE(data.length, 18); header.writeUInt32LE(data.length, 22); header.writeUInt16LE(name.length, 26);
    const directory = Buffer.alloc(46); directory.writeUInt32LE(0x02014b50); directory.writeUInt16LE(0x314, 4); directory.writeUInt16LE(20, 6);
    directory.writeUInt16LE(0x800, 8); directory.writeUInt32LE(crc, 16); directory.writeUInt32LE(data.length, 20); directory.writeUInt32LE(data.length, 24);
    directory.writeUInt16LE(name.length, 28); directory.writeUInt32LE(row.attributes === undefined ? 32 : row.attributes, 38); directory.writeUInt32LE(offset, 42);
    local.push(header, name, data); central.push(directory, name); offset += header.length + name.length + data.length;
  }
  const directory = Buffer.concat(central), end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50); end.writeUInt16LE(rows.length, 8);
  end.writeUInt16LE(rows.length, 10); end.writeUInt32LE(directory.length, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...local, directory, end]);
}
const sevenZipPath = path.join(process.env.ProgramFiles || 'C:/Program Files', '7-Zip/7z.exe');
const functional = process.platform === 'win32' && fs.existsSync(sevenZipPath);
test('PS5 metadata plus actual 7z preserves Chinese names and paths beyond 260 characters', {skip: !functional}, t => {
  const f = fixture(t), deep = '中文 空格/' + 'nested-segment/'.repeat(18) + '说明.txt';
  const rows = [{path: '.hidden', data: 'dot'}, {path: deep, data: 'long Unicode exact bytes'}];
  assert.ok(path.join(f.root, deep).length > 260); fs.writeFileSync(f.archive, storedZip(rows));
  const files = rows.map(row => ({path: row.path, bytes: Buffer.byteLength(row.data), sha256: id.hash(Buffer.from(row.data))}));
  const receipt = archiveStage.extract({...f.options, archive: f.pin(), files, sevenZipPath, powershellPath: undefined});
  assert.equal(receipt.files, 2); assert.deepEqual(receipt.steps.map(step => step.name), ['members', 'test', 'extract']);
  assert.deepEqual(receipt.steps.map(step => step.exitCode), [0, 0, 0]);
  assert.match(receipt.steps[0].executable.replaceAll('\\', '/'), /\/System32\/WindowsPowerShell\/v1\.0\/powershell\.exe$/i);
  assert.equal(fs.readFileSync(path.join(f.options.destination, deep), 'utf8'), rows[1].data); f.noStage();
});
for (const row of [{path: '../outside.txt', data: 'escape'}, {path: 'link', data: '../outside', attributes: (0xa1ff * 65536) >>> 0}])
  test('actual ZIP metadata refuses ' + row.path + ' before any native extraction', {skip: !functional}, t => {
    const f = fixture(t); fs.writeFileSync(f.archive, storedZip([row]));
    assert.throws(() => archiveStage.extract({...f.options, archive: f.pin(), sevenZipPath, powershellPath: undefined}), error => {
      assert.match(error.message, row.path.startsWith('../') ? /Invalid path:/ : /ZIP linked or special member refused/);
      assert.deepEqual(error.extraction.steps.map(s => s.name), ['members']);
      assert.equal(error.extraction.steps[0].exitCode, 0); return true;
    });
    assert.equal(fs.existsSync(f.options.destination), false); f.noStage();
  });
