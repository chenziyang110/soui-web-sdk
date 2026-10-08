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

test('modern profile validation precedes recovery of an interrupted upgrade', {timeout: 30000}, async () => {
  const root = path.resolve(__dirname, '../../out/sdk-profile-recovery', 'attempt-' + crypto.randomUUID());
  const source = path.join(root, 'source'), legacy = path.join(root, 'legacy');
  const first = path.join(root, 'modern-first'), second = path.join(root, 'modern-second');
  const project = path.join(root, 'project');
  const write = (file, bytes) => { fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, bytes); };
  const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
  const snapshot = (directory, relative = '') => fs.readdirSync(path.join(directory, relative), {withFileTypes: true})
    .sort((a, b) => a.name.localeCompare(b.name)).flatMap(entry => {
      assert.equal(entry.isSymbolicLink(), false);
      const name = path.posix.join(relative, entry.name);
      return entry.isDirectory() ? snapshot(directory, name) : [[name, digest(fs.readFileSync(path.join(directory, name)))]];
    });
  for (const name of sdk.managedRoots) write(path.join(source, name, 'fixture.txt'), name + '\n');
  for (const name of sdk.managedFiles) write(path.join(source, name), name + '\n');
  write(path.join(source, 'sdk/shared.txt'), 'original modern bytes');
  sdk.pack(source, legacy, '0.3.0-preview.1');
  for (const name of ['sdk/include/soui/host/webview2_host.h', 'sdk/renderers/webview2/CMakeLists.txt',
    'tools/internal/Restore-WebView2.ps1']) write(path.join(source, name), 'modern component');
  sdk.pack(source, first, '0.4.0-preview.1');
  write(path.join(source, 'sdk/shared.txt'), 'replacement modern bytes');
  sdk.pack(source, second, '0.4.0-preview.2');
  const business = Buffer.from('customer-owned application bytes\r\n');
  write(path.join(project, 'project.json'), JSON.stringify({runtimeProfile: 'modern'}));
  write(path.join(project, 'app/business.dat'), business);
  sdk.install(first, project);
  const boundary = path.join(root, 'replace.boundary.json');
  const child = cp.spawn(process.execPath, [worker, installer, project, second, 'install', 'replace', boundary],
    {windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']});
  const output = [], errors = [];
  child.stdout.on('data', bytes => output.push(bytes));
  child.stderr.on('data', bytes => errors.push(bytes));
  let ended = false;
  const closed = new Promise(resolve => child.once('close', (code, signal) => { ended = true; resolve({code, signal}); }));
  try {
    const deadline = Date.now() + 10000;
    while (!fs.existsSync(boundary) && !ended && Date.now() < deadline)
      await new Promise(resolve => setTimeout(resolve, 10));
    assert.equal(fs.existsSync(boundary), true, Buffer.concat(errors).toString());
    assert.equal(JSON.parse(fs.readFileSync(boundary)).pid, child.pid);
  } finally {
    if (!ended) child.kill('SIGKILL');
    const result = await closed;
    write(path.join(root, 'termination.json'), JSON.stringify({...result,
      stdout: Buffer.concat(output).toString(), stderr: Buffer.concat(errors).toString()}, null, 2) + '\n');
    assert.ok(result.signal || result.code !== 0, 'The interruption must not be recorded as success');
  }
  const before = snapshot(project);
  assert.throws(() => sdk.install(legacy, project), /does not provide the modern profile/);
  assert.deepEqual(snapshot(project), before, 'An incompatible package must not even recover pending writes');
  assert.throws(() => sdk.readLock(project), /Pending SDK transaction/);
  sdk.install(second, project);
  assert.equal(sdk.readLock(project).manifest.version, '0.4.0-preview.2');
  assert.equal(fs.readFileSync(path.join(project, 'sdk/shared.txt'), 'utf8'), 'replacement modern bytes');
  assert.deepEqual(fs.readFileSync(path.join(project, 'app/business.dat')), business);
  assert.equal(fs.existsSync(path.join(project, '.soui-sdk-transaction')), false);
  console.log('Profile/recovery original records retained: ' + root);
});
