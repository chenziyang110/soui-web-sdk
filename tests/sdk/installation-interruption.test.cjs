'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const installer = path.resolve(__dirname, '../../tools/sdk/soui-sdk.cjs');
const sdk = require(installer);
const worker = path.join(__dirname, 'installation-interruption-worker.cjs');
const root = path.resolve(__dirname, '../../out/sdk-recovery', 'attempt-' + crypto.randomUUID());
fs.mkdirSync(root, {recursive: true});
const write = (file, value) => { fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, value); };
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encoded = value => JSON.stringify(value, null, 2) + '\n';
const first = path.join(root, 'first'), second = path.join(root, 'second'), source = path.join(root, 'source');
for (const name of sdk.managedRoots) write(path.join(source, name, 'fixture.txt'), name + '\n');
for (const name of sdk.managedFiles) write(path.join(source, name), name + '\n');
write(path.join(source, 'sdk/shared.txt'), 'original bytes\r\n');
write(path.join(source, 'sdk/removed.txt'), 'old API');
sdk.pack(source, first, '0.1.0-preview.1');
write(path.join(source, 'sdk/shared.txt'), 'replacement bytes\n');
write(path.join(source, 'sdk/added.txt'), 'new API');
fs.unlinkSync(path.join(source, 'sdk/removed.txt'));
sdk.pack(source, second, '0.1.0-preview.2');
const business = Buffer.from('中文账本\r\nrevision=17\0\xff', 'utf8');
function project(name, initial = true) {
  const destination = path.join(root, name);
  write(path.join(destination, 'app/账本.dat'), business);
  if (initial) sdk.install(first, destination);
  return destination;
}
function cli(destination, command, packageRoot, tag) {
  const args = [installer, command, '--project', destination];
  if (packageRoot) args.push('--package', packageRoot);
  const result = cp.spawnSync(process.execPath, args, {encoding: 'utf8', windowsHide: true, timeout: 15000});
  const record = {args, status: result.status, signal: result.signal, error: result.error?.message,
    stdout: result.stdout, stderr: result.stderr};
  write(path.join(destination, '..', path.basename(destination) + '-' + tag + '.json'), encoded(record));
  return record;
}
function verified(destination, version) {
  assert.equal(sdk.readLock(destination).manifest.version, version);
  assert.deepEqual(fs.readFileSync(path.join(destination, 'app/账本.dat')), business);
}
async function stopped(destination, boundary, command = 'install', packageRoot = second, suffix = '') {
  const report = path.join(root, path.basename(destination) + '-' + command + '-' + boundary + suffix + '.boundary.json');
  const child = cp.spawn(process.execPath, [worker, installer, destination, packageRoot, command, boundary, report],
    {windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']});
  const output = [], errors = [];
  child.stdout.on('data', b => output.push(b)); child.stderr.on('data', b => errors.push(b));
  let ended = false;
  const closed = new Promise(resolve => child.on('close', (code, signal) => { ended = true; resolve({code, signal}); }));
  const deadline = Date.now() + 15000;
  while (!fs.existsSync(report) && !ended && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 10));
  if (!fs.existsSync(report)) {
    if (!ended) child.kill('SIGKILL');
    const result = await closed;
    write(report + '.failed.json', encoded({...result, stdout: Buffer.concat(output).toString(), stderr: Buffer.concat(errors).toString()}));
    assert.fail('Boundary not reached: ' + boundary + ' ' + Buffer.concat(errors));
  }
  assert.equal(JSON.parse(fs.readFileSync(report)).pid, child.pid);
  return {child, async terminate() {
    assert.equal(ended, false, 'Child must still be alive at termination');
    assert.equal(child.kill('SIGKILL'), true);
    const result = await closed;
    write(report + '.exit.json', encoded({...result, stdout: Buffer.concat(output).toString(), stderr: Buffer.concat(errors).toString()}));
    assert.ok(result.signal || result.code !== 0, 'Real termination must not become success');
  }};
}
function visibleFiles(destination) {
  const walk = (relative = '') => fs.readdirSync(path.join(destination, relative), {withFileTypes: true})
    .filter(e => !e.name.startsWith('.soui-sdk-')).flatMap(e => {
      const name = path.posix.join(relative, e.name);
      return e.isDirectory() ? walk(name) : [[name, hash(fs.readFileSync(path.join(destination, name)))]];
    });
  return walk().sort((a, b) => a[0].localeCompare(b[0]));
}

test('process termination and new-process recovery', {timeout: 180000}, async t => {
  for (const boundary of ['prepare', 'journal', 'replace', 'delete', 'add', 'commit', 'cleanup']) {
    await t.test(boundary, async () => {
      const destination = project('upgrade-' + boundary);
      const before = fs.readFileSync(path.join(destination, 'sdk-lock.json'));
      const running = await stopped(destination, boundary); await running.terminate();
      const result = cli(destination, 'recover', null, 'recover');
      assert.equal(result.status, 0, result.stderr);
      const committed = ['commit', 'cleanup'].includes(boundary);
      verified(destination, committed ? '0.1.0-preview.2' : '0.1.0-preview.1');
      if (!committed) assert.deepEqual(fs.readFileSync(path.join(destination, 'sdk-lock.json')), before);
      assert.equal(cli(destination, 'recover', null, 'recover-again').status, 0);
      assert.equal(cli(destination, 'install', second, 'continue').status, 0);
      verified(destination, '0.1.0-preview.2');
    });
  }
  await t.test('first install rolls back to no SDK, then install automatically recovers', async () => {
    const destination = project('initial', false);
    const running = await stopped(destination, 'replace'); await running.terminate();
    assert.equal(cli(destination, 'recover', null, 'recover').status, 0);
    assert.equal(fs.existsSync(path.join(destination, 'sdk-lock.json')), false);
    assert.equal(fs.existsSync(path.join(destination, 'sdk/shared.txt')), false);
    assert.deepEqual(fs.readFileSync(path.join(destination, 'app/账本.dat')), business);
    const next = await stopped(destination, 'replace', 'install', second, '-second'); await next.terminate();
    assert.equal(cli(destination, 'install', second, 'automatic').status, 0);
    verified(destination, '0.1.0-preview.2');
  });
  await t.test('rollback is restartable and a competing recovery is refused', async () => {
    const destination = project('recovery-interrupted');
    const running = await stopped(destination, 'add'); await running.terminate();
    const restoring = await stopped(destination, 'claim', 'recover');
    assert.match(cli(destination, 'recover', null, 'competing').stderr, /busy/);
    await restoring.terminate();
    const restoringAgain = await stopped(destination, 'replace', 'recover'); await restoringAgain.terminate();
    assert.equal(cli(destination, 'recover', null, 'restart').status, 0);
    verified(destination, '0.1.0-preview.1');
  });
  await t.test('active installer blocks verify, install and recover', async () => {
    const destination = project('active');
    const running = await stopped(destination, 'prepare');
    try {
      for (const command of ['verify-project', 'install', 'recover']) {
        const result = cli(destination, command, command === 'install' ? second : null, command);
        assert.equal(result.status, 1);
        assert.match(result.stderr, /busy|Pending/);
      }
    } finally { await running.terminate(); }
    assert.equal(cli(destination, 'recover', null, 'after-exit').status, 0);
    verified(destination, '0.1.0-preview.1');
  });
  for (const corruption of ['target', 'backup', 'new-backup', 'lock', 'journal']) {
    await t.test('reject ' + corruption + ' conflict without partial recovery', async () => {
      const destination = project('conflict-' + corruption);
      const running = await stopped(destination, 'add'); await running.terminate();
      if (corruption === 'target') write(path.join(destination, 'sdk/shared.txt'), 'local change to preserve');
      if (corruption === 'backup') write(path.join(destination, '.soui-sdk-transaction/old/sdk/shared.txt'), 'bad backup');
      if (corruption === 'new-backup') write(path.join(destination, '.soui-sdk-transaction/new/sdk/shared.txt'), 'bad candidate');
      if (corruption === 'lock') fs.appendFileSync(path.join(destination, 'sdk-lock.json'), ' ');
      if (corruption === 'journal') {
        const file = path.join(destination, '.soui-sdk-transaction/journal.json');
        const journal = JSON.parse(fs.readFileSync(file));
        const next = JSON.parse(journal.nextLock);
        next.manifest.files[0].path = 'app/账本.dat';
        next.manifestSha256 = hash(encoded(next.manifest));
        journal.nextLock = encoded(next); write(file, encoded(journal));
      }
      const before = visibleFiles(destination);
      const result = cli(destination, 'recover', null, 'rejected');
      assert.equal(result.status, 1, result.stdout);
      assert.deepEqual(visibleFiles(destination), before);
      assert.equal(fs.existsSync(path.join(destination, '.soui-sdk-transaction/journal.json')), true);
    });
  }
  await t.test('committed but modified payload is preserved, never rolled back', async () => {
    const destination = project('committed-conflict');
    const running = await stopped(destination, 'commit'); await running.terminate();
    write(path.join(destination, 'sdk/shared.txt'), 'modified after commit');
    const before = visibleFiles(destination);
    assert.equal(cli(destination, 'recover', null, 'rejected').status, 1);
    assert.deepEqual(visibleFiles(destination), before);
  });
  await t.test('old lock BOM and whitespace survive rollback byte for byte', async () => {
    const destination = project('lock-bytes');
    const file = path.join(destination, 'sdk-lock.json');
    const bytes = Buffer.from('\uFEFF' + fs.readFileSync(file, 'utf8') + ' \r\n'); write(file, bytes);
    const running = await stopped(destination, 'replace'); await running.terminate();
    assert.equal(cli(destination, 'recover', null, 'recover').status, 0);
    assert.deepEqual(fs.readFileSync(file), bytes);
    verified(destination, '0.1.0-preview.1');
  });
  await t.test('recovery refuses a junction without writing outside the project', async () => {
    const destination = project('linked-target');
    const running = await stopped(destination, 'add'); await running.terminate();
    const outside = path.join(root, 'outside'), sdkPath = path.join(destination, 'sdk');
    fs.mkdirSync(outside); write(path.join(outside, 'shared.txt'), 'external data');
    const saved = path.join(destination, 'saved-sdk');
    assert.equal(path.dirname(sdkPath), destination); assert.equal(path.dirname(saved), destination);
    fs.renameSync(sdkPath, saved);
    fs.symlinkSync(outside, sdkPath, process.platform === 'win32' ? 'junction' : 'dir');
    assert.match(cli(destination, 'recover', null, 'rejected').stderr, /Linked path/);
    assert.equal(fs.readFileSync(path.join(outside, 'shared.txt'), 'utf8'), 'external data');
    assert.equal(fs.readdirSync(outside).length, 1);
  });
  await t.test('old unjournaled transaction is preserved and rejected', () => {
    const destination = project('legacy');
    write(path.join(destination, '.sdk-install-old/old/sdk/shared.txt'), 'preserved');
    assert.match(cli(destination, 'recover', null, 'rejected').stderr, /Legacy/);
    assert.equal(fs.readFileSync(path.join(destination, '.sdk-install-old/old/sdk/shared.txt'), 'utf8'), 'preserved');
  });
  await t.test('upgrade rollback to older package also recovers', async () => {
    const destination = project('downgrade'); sdk.install(second, destination);
    const running = await stopped(destination, 'replace', 'install', first); await running.terminate();
    assert.equal(cli(destination, 'recover', null, 'recover').status, 0);
    verified(destination, '0.1.0-preview.2');
    assert.equal(cli(destination, 'install', first, 'retry').status, 0);
    verified(destination, '0.1.0-preview.1');
  });
  await t.test('file-directory transitions fail before installed bytes change', () => {
    const fixture = path.join(root, 'shape-source'), a = path.join(root, 'shape-a'), b = path.join(root, 'shape-b');
    for (const name of sdk.managedRoots) write(path.join(fixture, name, 'fixture.txt'), name);
    for (const name of sdk.managedFiles) write(path.join(fixture, name), name);
    write(path.join(fixture, 'sdk/branch'), 'old file'); sdk.pack(fixture, a, '0.1.0-preview.3');
    fs.unlinkSync(path.join(fixture, 'sdk/branch'));
    write(path.join(fixture, 'sdk/branch/child.txt'), 'new child'); sdk.pack(fixture, b, '0.1.0-preview.4');
    for (const [name, from, to] of [['forward', a, b], ['reverse', b, a]]) {
      const destination = project('shape-' + name, false); sdk.install(from, destination);
      const before = visibleFiles(destination);
      assert.throws(() => sdk.install(to, destination), /path shape/);
      assert.deepEqual(visibleFiles(destination), before);
      assert.equal(fs.existsSync(path.join(destination, '.soui-sdk-transaction')), false);
    }
  });
  if (process.platform === 'win32') await t.test('retirement retries transient denial and preserves permanent failures', () => {
    const destination = project('retire-denial');
    const rename = fs.renameSync;
    let attempts = 0;
    fs.renameSync = (from, to) => {
      if (path.basename(to).startsWith('.soui-sdk-retired-') && attempts++ < 2) {
        const error = new Error('transient retirement denial'); error.code = 'EPERM'; throw error;
      }
      return rename(from, to);
    };
    try { sdk.install(second, destination); } finally { fs.renameSync = rename; }
    assert.equal(attempts, 3); verified(destination, '0.1.0-preview.2');
    attempts = 0;
    fs.renameSync = (from, to) => {
      if (path.basename(to).startsWith('.soui-sdk-retired-')) {
        ++attempts; const error = new Error('permanent retirement denial'); error.code = 'EACCES'; throw error;
      }
      return rename(from, to);
    };
    try { assert.throws(() => sdk.install(first, destination), /recovery required/); }
    finally { fs.renameSync = rename; }
    assert.equal(attempts, 12); // Six bounded attempts, then six in error-path recovery.
    assert.equal(fs.existsSync(path.join(destination, '.soui-sdk-transaction/journal.json')), true);
    assert.deepEqual(fs.readFileSync(path.join(destination, 'app/账本.dat')), business);
  });
});
console.log('Raw interruption samples retained: ' + root);
