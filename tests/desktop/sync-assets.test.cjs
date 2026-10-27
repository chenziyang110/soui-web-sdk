'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const { syncAssets } = require('../../tools/sync-desktop-assets.cjs');

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-sync-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const input = path.join(root, 'input'), output = path.join(root, 'web');
  fs.mkdirSync(input);
  fs.writeFileSync(path.join(input, 'index.html'), '<p>one</p>');
  return { root, input, output };
}
test('sync propagates deletion and empty directories; unchanged input is a no-op', t => {
  const { input, output } = fixture(t);
  fs.mkdirSync(path.join(input, 'empty'));
  fs.writeFileSync(path.join(input, 'obsolete.js'), 'old');
  assert.equal(syncAssets(input, output).changed, true);
  assert.equal(syncAssets(input, output).changed, false);
  fs.unlinkSync(path.join(input, 'obsolete.js'));
  fs.rmdirSync(path.join(input, 'empty'));
  fs.writeFileSync(path.join(input, 'index.html'), '<p>two</p>');
  assert.equal(syncAssets(input, output).changed, true);
  assert.deepEqual(fs.readdirSync(output), ['index.html']);
  assert.equal(fs.readFileSync(path.join(output, 'index.html'), 'utf8'), '<p>two</p>');
});
test('existing directories are never adopted', t => {
  const { input, output } = fixture(t);
  fs.mkdirSync(output);
  assert.throws(() => syncAssets(input, output), /Refusing to adopt/);
  assert.deepEqual(fs.readdirSync(output), []);
});
test('manual edits, extra files and extra directories prevent all deletion', t => {
  const { input, output } = fixture(t);
  syncAssets(input, output);
  const index = path.join(output, 'index.html');
  fs.writeFileSync(index, 'manual');
  assert.throws(() => syncAssets(input, output), /edits or extra/);
  assert.equal(fs.readFileSync(index, 'utf8'), 'manual');
  fs.copyFileSync(path.join(input, 'index.html'), index);
  fs.writeFileSync(path.join(output, 'userdata'), 'keep');
  assert.throws(() => syncAssets(input, output), /edits or extra/);
  fs.unlinkSync(path.join(output, 'userdata'));
  fs.mkdirSync(path.join(output, 'userdata'));
  assert.throws(() => syncAssets(input, output), /edits or extra/);
  assert.ok(fs.statSync(path.join(output, 'userdata')).isDirectory());
});
test('source identity, output overlap and concurrent lock are rejected', t => {
  const { root, input, output } = fixture(t);
  syncAssets(input, output);
  const other = path.join(root, 'other');
  fs.mkdirSync(other);
  fs.writeFileSync(path.join(other, 'index.html'), 'other');
  assert.throws(() => syncAssets(other, output), /source identity/);
  assert.throws(() => syncAssets(input, path.join(input, 'web')), /overlap/);
  fs.writeFileSync(`${output}.soui-assets.lock`, 'other invocation');
  assert.throws(() => syncAssets(input, output), /EEXIST/);
  assert.equal(fs.readFileSync(`${output}.soui-assets.lock`, 'utf8'), 'other invocation');
});
test('links in source, output and ancestors are rejected without touching targets', t => {
  const { root, input, output } = fixture(t);
  const link = path.join(input, 'link');
  try { fs.symlinkSync('index.html', link); }
  catch (error) { if (error.code === 'EPERM') return t.skip('Symlink creation unavailable'); throw error; }
  assert.throws(() => syncAssets(input, output), /links/);
  assert.equal(fs.existsSync(output), false);
  fs.unlinkSync(link);
  syncAssets(input, output);
  fs.symlinkSync(path.join(input, 'index.html'), path.join(output, 'extra'));
  assert.throws(() => syncAssets(input, output), /links/);
  const parentLink = path.join(root, 'alias');
  fs.symlinkSync(input, parentLink, 'dir');
  assert.throws(() => syncAssets(parentLink, path.join(root, 'new-web')), /ancestors/);
  assert.equal(fs.readFileSync(path.join(input, 'index.html'), 'utf8'), '<p>one</p>');
});

// Load the production module against a closed, in-memory fs implementation.
// Only reading its source uses real fs; no mock operation falls back to real I/O.
function mockSync({ fault, previous = true, unchanged = false, cli = false } = {}) {
  const p = path.win32;
  const names = { volume: 'C:\\', root: 'C:\\fixture', input: 'C:\\fixture\\input',
    output: 'C:\\fixture\\web', workspace: 'C:\\fixture\\.web.soui-tmp-MOCK00' };
  names.manifest = `${names.output}.soui-assets.json`;
  names.lock = `${names.output}.soui-assets.lock`;
  names.fresh = p.join(names.workspace, 'new');
  names.freshManifest = p.join(names.workspace, 'new.json');
  names.backup = p.join(names.workspace, 'previous');
  names.backupManifest = p.join(names.workspace, 'previous.json');
  const entries = new Map(), handles = new Map(), calls = [];
  const key = file => p.normalize(file);
  const put = (file, value) => entries.set(key(file), value);
  const file = value => ({ type: 'file', data: Buffer.from(value) });
  for (const dir of [names.volume, names.root, names.input]) put(dir, { type: 'dir' });
  put(p.join(names.input, 'index.html'), file('<p>new</p>'));
  const digest = value => crypto.createHash('sha256').update(value).digest('hex');
  if (previous) {
    const data = Buffer.from(unchanged ? '<p>new</p>' : '<p>old</p>');
    put(names.output, { type: 'dir' });
    put(p.join(names.output, 'index.html'), file(data));
    put(names.manifest, file(JSON.stringify({ schema: 'soui.desktop.managed-assets/1',
      sourceIdentity: digest(names.input), inventory: {
        files: [{ path: 'index.html', size: data.length, sha256: digest(data) }], directories: []
      } })));
  }
  let descriptor = 1, matches = 0, injected = 0, faultIndex = -1, injectedSyscall;
  const native = (code, syscall) => Object.assign(new Error('private-path-sentinel native message'), {
    code, syscall, errno: -4048, path: 'C:\\private-path-sentinel\\source',
    dest: 'C:\\private-path-sentinel\\destination', cause: new Error('private-path-sentinel cause'),
    stack: 'private-path-sentinel original stack', syncPhase: 'attacker-phase', extra: 'private-path-sentinel'
  });
  const entry = name => {
    const value = entries.get(key(name));
    if (!value) throw native('ENOENT', 'lstat');
    return value;
  };
  const parent = name => assert.equal(entry(p.dirname(name)).type, 'dir');
  const mock = {};
  const define = (name, syscall, action) => {
    mock[name] = (...args) => {
      calls.push({ name, args });
      if (fault && name === fault.method && fault.match(args, names) && ++matches === (fault.at || 1)) {
        ++injected;
        faultIndex = calls.length - 1;
        injectedSyscall = syscall;
        throw fault.error || native('EPERM', syscall);
      }
      return action(...args);
    };
  };
  define('lstatSync', 'lstat', name => {
    const value = entry(name);
    return { isDirectory: () => value.type === 'dir', isFile: () => value.type === 'file',
      isSymbolicLink: () => false, nlink: 1, size: value.data?.length || 0 };
  });
  define('realpathSync', 'realpath', name => { entry(name); return key(name); });
  define('readdirSync', 'scandir', name => {
    assert.equal(entry(name).type, 'dir');
    return [...entries.keys()].filter(item => item !== key(name) && p.dirname(item) === key(name)).map(item => p.basename(item));
  });
  define('openSync', 'open', (name, flags) => {
    if (flags === 'wx') {
      parent(name);
      if (entries.has(key(name))) throw native('EEXIST', 'open');
      put(name, file(''));
    }
    const value = entry(name);
    assert.equal(value.type, 'file');
    const fd = descriptor++;
    handles.set(fd, { value, position: 0 });
    return fd;
  });
  define('readSync', 'read', (fd, buffer, offset, length, position) => {
    const handle = handles.get(fd); assert.ok(handle);
    const start = position === null ? handle.position : position;
    const count = Math.min(length, Math.max(0, handle.value.data.length - start));
    handle.value.data.copy(buffer, offset, start, start + count);
    if (position === null) handle.position += count;
    return count;
  });
  define('writeSync', 'write', (fd, buffer, offset, length, position) => {
    const handle = handles.get(fd); assert.ok(handle);
    const start = position === null ? handle.position : position;
    const data = Buffer.alloc(Math.max(handle.value.data.length, start + length));
    handle.value.data.copy(data); buffer.copy(data, start, offset, offset + length);
    handle.value.data = data;
    if (position === null) handle.position += length;
    return length;
  });
  define('closeSync', 'close', fd => assert.equal(handles.delete(fd), true));
  define('mkdtempSync', 'mkdtemp', prefix => {
    assert.equal(`${prefix}MOCK00`, names.workspace);
    parent(names.workspace); put(names.workspace, { type: 'dir' }); return names.workspace;
  });
  define('mkdirSync', 'mkdir', name => {
    parent(name); assert.equal(entries.has(key(name)), false); put(name, { type: 'dir' });
  });
  define('writeFileSync', 'write', (name, data, options) => {
    parent(name); assert.equal(options.flag, 'wx'); assert.equal(entries.has(key(name)), false); put(name, file(data));
  });
  define('renameSync', 'rename', (from, to) => {
    entry(from); parent(to); assert.equal(entries.has(key(to)), false);
    const moved = [...entries].filter(([name]) => name === key(from) || name.startsWith(`${key(from)}\\`));
    for (const [name] of moved) entries.delete(name);
    for (const [name, value] of moved) put(`${key(to)}${name.slice(key(from).length)}`, value);
  });
  define('unlinkSync', 'unlink', name => { assert.equal(entry(name).type, 'file'); entries.delete(key(name)); });
  define('rmdirSync', 'rmdir', name => {
    assert.equal(entry(name).type, 'dir');
    assert.equal([...entries.keys()].some(item => item.startsWith(`${key(name)}\\`)), false);
    entries.delete(key(name));
  });
  const module = { exports: {} }, output = [], errors = [];
  const request = name => {
    if (name === 'node:fs') return mock;
    if (name === 'node:path') return p;
    if (name === 'node:crypto') return crypto;
    throw new Error(`Unexpected dependency: ${name}`);
  };
  request.main = cli ? module : {};
  const process = { argv: ['node', 'sync-desktop-assets.cjs', '--input', names.input, '--output', names.output] };
  const context = { module, require: request, Buffer, process,
    console: { log: value => output.push(value), error: value => errors.push(value) } };
  const source = fs.readFileSync(require.resolve('../../tools/sync-desktop-assets.cjs'), 'utf8');
  vm.runInNewContext(source, context, { filename: 'sync-desktop-assets.cjs' });
  return { run: () => module.exports.syncAssets(names.input, names.output), names, entries, calls,
    handles, output, errors, process, injected: () => injected, faultIndex: () => faultIndex,
    injectedSyscall: () => injectedSyscall };
}
function caught(action) {
  let error;
  try { action(); } catch (failure) { error = failure; }
  assert.ok(error, 'expected production code to throw');
  return error;
}
const diagnosticPoints = [
  ['validate-source', 'lstatSync', ([name], n) => name === n.input],
  ['validate-output', 'realpathSync', ([name], n) => name === n.root],
  ['acquire-lock', 'openSync', ([name], n) => name === n.lock],
  ['inspect-output', 'lstatSync', ([name], n) => name === n.output],
  ['read-ownership', 'openSync', ([name], n) => name === n.manifest],
  ['inspect-previous', 'readdirSync', ([name], n) => name === n.output],
  ['inspect-source', 'readdirSync', ([name], n) => name === n.input],
  ['create-workspace', 'mkdtempSync', () => true],
  ['create-tree', 'mkdirSync', ([name], n) => name === n.fresh],
  ['copy-assets', 'openSync', ([name], n) => name === path.win32.join(n.fresh, 'index.html')],
  ['verify-tree', 'readdirSync', ([name], n) => name === n.fresh],
  ['write-manifest', 'writeFileSync', ([name], n) => name === n.freshManifest],
  ['verify-previous', 'readdirSync', ([name], n) => name === n.output, 2],
  ['backup-tree', 'renameSync', ([name], n) => name === n.output],
  ['backup-manifest', 'renameSync', ([name], n) => name === n.manifest],
  ['publish-tree', 'renameSync', ([name], n) => name === n.fresh],
  ['publish-manifest', 'renameSync', ([name], n) => name === n.freshManifest],
  ['remove-backup-tree', 'unlinkSync', ([name], n) => name === path.win32.join(n.backup, 'index.html')],
  ['remove-backup-manifest', 'unlinkSync', ([name], n) => name === n.backupManifest],
  ['remove-workspace', 'rmdirSync', ([name], n) => name === n.workspace],
  ['release-lock', 'unlinkSync', ([name], n) => name === n.lock],
];

test('diagnostics retain safe fields at every sync phase without real filesystem operations', async t => {
  for (const [index, [phase, method, match, at]] of diagnosticPoints.entries()) await t.test(phase, () => {
    const f = mockSync({ fault: { method, match, at } });
    const error = caught(f.run);
    assert.equal(f.injected(), 1);
    assert.equal(error.syncPhase, phase);
    assert.equal(error.code, 'EPERM');
    assert.equal(error.errno, -4048);
    assert.equal(error.syscall, f.injectedSyscall());
    const retained = index >= 8 && phase !== 'release-lock';
    assert.equal(error.message, phase === 'release-lock' ?
      'Could not remove owned synchronization lock; inspect it before retrying.' :
      `Filesystem operation failed (EPERM).${retained ? ' Generated workspace .web.soui-tmp-MOCK00 retained; stop builds and inspect it for manual recovery.' : ''}`);
    for (const field of ['path', 'dest', 'cause', 'extra']) assert.equal(Object.hasOwn(error, field), false);
    assert.doesNotMatch(error.stack, /private-path-sentinel/);
    assert.doesNotMatch(JSON.stringify({ message: error.message, error }), /private-path-sentinel|attacker-phase/);
    assert.equal(f.entries.get(path.win32.join(f.names.input, 'index.html')).data.toString(), '<p>new</p>');
    if (index < 13) assert.equal(f.entries.get(path.win32.join(f.names.output, 'index.html')).data.toString(), '<p>old</p>');
    if (retained) assert.equal(f.entries.has(f.names.workspace), true);
    if (phase !== 'release-lock') assert.equal(f.entries.has(f.names.lock), false);
    assert.equal(f.handles.size, 0);
    assert.ok(f.calls.slice(f.faultIndex() + 1).every(call => call.name === 'closeSync' ||
      (call.name === 'unlinkSync' && call.args[0] === f.names.lock)), 'failure must not retry or continue publishing');
  });
});

test('diagnostics preserve validation and CLI messages, exit codes and cleanup semantics', () => {
  const existing = mockSync({ previous: false });
  existing.entries.set(existing.names.output, { type: 'dir' });
  const validation = caught(existing.run);
  assert.equal(validation.message, 'Refusing to adopt an existing output or orphan ownership manifest; choose a new output.');
  assert.equal(validation.syncPhase, 'inspect-output');
  assert.equal(Object.hasOwn(validation, 'code'), false);
  const point = diagnosticPoints.find(([phase]) => phase === 'publish-tree');
  const [, method, match] = point;
  const cli = mockSync({ cli: true, fault: { method, match } });
  assert.equal(cli.process.exitCode, 1);
  assert.deepEqual(cli.output, []);
  assert.deepEqual(cli.errors, ['Desktop asset sync failed: Filesystem operation failed (EPERM). Generated workspace .web.soui-tmp-MOCK00 retained; stop builds and inspect it for manual recovery.']);
  assert.equal(cli.entries.has(cli.names.output), false);
  assert.equal(cli.entries.has(cli.names.backup), true);
  assert.equal(cli.entries.has(cli.names.backupManifest), true);
  assert.equal(cli.entries.has(cli.names.fresh), true);
  assert.equal(cli.entries.has(cli.names.lock), false);
  const success = mockSync({ cli: true });
  assert.deepEqual(success.output, ['Desktop assets synchronized (1 files).']);
  assert.deepEqual(success.errors, []);
  assert.equal(success.process.exitCode, undefined);
  assert.equal(success.entries.has(success.names.workspace), false);
  const unchanged = mockSync({ cli: true, unchanged: true });
  assert.deepEqual(unchanged.output, ['Desktop assets unchanged (1 files).']);
  assert.equal(unchanged.entries.has(unchanged.names.lock), false);
});

test('diagnostics distinguish published output from workspace cleanup failure', () => {
  const [, method, match] = diagnosticPoints.find(([phase]) => phase === 'remove-workspace');
  const f = mockSync({ fault: { method, match } });
  assert.equal(caught(f.run).syncPhase, 'remove-workspace');
  assert.equal(f.entries.get(path.win32.join(f.names.output, 'index.html')).data.toString(), '<p>new</p>');
  assert.equal(f.entries.has(f.names.manifest), true);
  assert.equal(f.entries.has(f.names.workspace), true);
  assert.equal(f.entries.has(f.names.backup), false);
});

test('diagnostics drop untrusted metadata and never evaluate accessors', () => {
  const [, method, match] = diagnosticPoints.find(([phase]) => phase === 'publish-tree');
  for (const fields of [
    { code: 'C:\\private-path-sentinel', syscall: 'rename C:\\private-path-sentinel', errno: 'private-path-sentinel' },
    { code: { toString() { throw new Error('private-path-sentinel'); } }, syscall: {}, errno: NaN },
    { code: 'UNRECOGNIZED', syscall: 'unknown', errno: Infinity },
    { code: 'EPERM', syscall: 'rename', errno: 2147483648 },
    { code: 'EPERM', syscall: 'rename', errno: -2147483649 },
    { code: 'EPERM', syscall: 'rename', errno: 1.5 },
  ]) {
    const native = Object.assign(new Error('private-path-sentinel'), fields, {
      path: 'private-path-sentinel', dest: 'private-path-sentinel', cause: 'private-path-sentinel',
      syncPhase: 'private-path-sentinel', stack: 'private-path-sentinel'
    });
    const f = mockSync({ fault: { method, match, error: native } });
    const error = caught(f.run);
    assert.equal(error.syncPhase, 'publish-tree');
    assert.equal(Object.hasOwn(error, 'code'), fields.code === 'EPERM');
    assert.equal(Object.hasOwn(error, 'syscall'), fields.syscall === 'rename');
    if (fields.code === 'EPERM') assert.equal(error.code, 'EPERM');
    if (fields.syscall === 'rename') assert.equal(error.syscall, 'rename');
    assert.equal(Object.hasOwn(error, 'errno'), false);
    assert.doesNotMatch(JSON.stringify({ message: error.message, error }) + error.stack, /private-path-sentinel|UNRECOGNIZED/);
  }
  let accessed = 0;
  const native = new Error('private-path-sentinel');
  for (const name of ['code', 'syscall', 'errno']) Object.defineProperty(native, name, {
    get() { ++accessed; throw new Error('private-path-sentinel'); }
  });
  const f = mockSync({ fault: { method, match, error: native } });
  assert.equal(caught(f.run).syncPhase, 'publish-tree');
  assert.equal(accessed, 0);
  const proxy = new Proxy({}, { getOwnPropertyDescriptor() { throw new Error('private-path-sentinel'); } });
  const trapped = mockSync({ fault: { method, match, error: proxy } });
  assert.equal(caught(trapped.run).syncPhase, 'publish-tree');
  const inherited = Object.create({ code: 'EPERM', syscall: 'rename', errno: -4048,
    message: 'private-path-sentinel', cause: 'private-path-sentinel' });
  const inheritedError = caught(mockSync({ fault: { method, match, error: inherited } }).run);
  assert.deepEqual(Object.keys(inheritedError), ['syncPhase']);
  assert.doesNotMatch(inheritedError.message, /private-path-sentinel/);
  for (const errno of [-2147483648, 0, 2147483647]) {
    const native = Object.assign(new Error('private-path-sentinel'), { code: 'EPERM', syscall: 'rename', errno });
    const error = caught(mockSync({ fault: { method, match, error: native } }).run);
    assert.equal(error.errno, errno);
  }
});
