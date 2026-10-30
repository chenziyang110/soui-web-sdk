'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const sdk = require('../../tools/sdk/soui-sdk.cjs');
const devtools = require('../../tools/internal/devtools-package.cjs');

const repository = path.resolve(__dirname, '../..');
const cmakeModule = path.join(repository, 'cmake/SouiDevTools.cmake');
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encoded = value => JSON.stringify(value, null, 2) + '\n';
const cmakePath = value => value.replace(/\\/g, '/');
const powershellQuote = value => "'" + value.replace(/'/g, "''") + "'";

function fixture(t) {
  const owner = fs.realpathSync(os.tmpdir());
  const directory = fs.realpathSync(fs.mkdtempSync(path.join(owner, '.soui-devtools-sdk-test-')));
  t.after(() => {
    assert.equal(path.dirname(directory), owner);
    assert.ok(path.basename(directory).startsWith('.soui-devtools-sdk-test-'));
    fs.rmSync(directory, {recursive: true, force: true});
  });
  function write(relative, bytes) {
    const file = path.join(directory, relative);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, bytes);
    return file;
  }
  function manifest(version, files) {
    return {
      schema: 'soui.source-sdk/v1', id: 'soui-desktop-sdk', version,
      channel: version.includes('-') ? version.split('-')[1].split('.')[0] : 'stable',
      releaseReady: false, files,
    };
  }
  function select(version, {applicationVersion = '7.4.2', lockVersion = version, header} = {}) {
    write('application/VERSION', applicationVersion + '\n');
    const bytes = header === undefined
      ? '#pragma once\n#define SOUI_SDK_VERSION_STRING "' + version + '"\n' : header;
    const headerPath = write('application/sdk/include/soui/version.h', bytes);
    if (lockVersion !== null) {
      const lockManifest = manifest(lockVersion, [{
        path: 'sdk/include/soui/version.h', bytes: Buffer.byteLength(bytes), sha256: sha256(bytes),
      }]);
      write('application/sdk-lock.json', encoded({
        schema: 'soui.sdk-lock/v1', manifestSha256: sha256(encoded(lockManifest)), manifest: lockManifest,
      }));
    }
    return {root: path.join(directory, 'application'), headerPath};
  }
  function resolve(root) {
    const output = path.join(directory, 'selected.txt');
    const script = write('select.cmake', [
      'cmake_minimum_required(VERSION 3.24)',
      `include("${cmakePath(cmakeModule)}")`,
      `_soui_devtools_sdk_version("${cmakePath(root)}" selected_sdk_version)`,
      'get_property(dependencies DIRECTORY PROPERTY CMAKE_CONFIGURE_DEPENDS)',
      `file(WRITE "${cmakePath(output)}" "\${selected_sdk_version}\n\${dependencies}")`, '',
    ].join('\n'));
    const result = cp.spawnSync('cmake', ['-P', script], {
      encoding: 'utf8', windowsHide: true, timeout: 15000,
    });
    return {result, output};
  }
  return {directory, write, manifest, select, resolve};
}

function selected(f, root) {
  const {result, output} = f.resolve(root);
  assert.equal(result.status, 0, result.error?.message || result.stderr || result.stdout);
  const [version, dependencies] = fs.readFileSync(output, 'utf8').split(/\r?\n/);
  return {version, dependencies: dependencies.split(';')};
}

async function prepareAndDeploy(f, sdkVersion) {
  // Only synthetic bytes are inspected. This fixture never loads a kernel.
  const kernel = Buffer.alloc(512);
  kernel.write('MZ', 0, 'ascii');
  kernel.writeUInt32LE(0x80, 0x3c);
  kernel.write('PE\0\0', 0x80, 'ascii');
  kernel.writeUInt16LE(0x8664, 0x84);
  const frontendRoot = path.join(f.directory, 'devtools-input');
  const input = (relative, bytes) => f.write('devtools-input/' + relative, bytes);
  const entry = relative => {
    const bytes = fs.readFileSync(path.join(frontendRoot, relative));
    return {path: relative, bytes: bytes.length, sha256: sha256(bytes)};
  };
  input('front_end/inspector.html', '<!doctype html><title>Synthetic fixture</title>\n');
  input('kernel/node.dll', kernel);
  input('licenses/LICENSE.txt', 'Synthetic test fixture only.\n');
  const manifestBytes = encoded([entry('front_end/inspector.html')]);
  input('approved-manifest.json', manifestBytes);
  const kernelSha256 = sha256(kernel), manifestSha256 = sha256(manifestBytes);
  const approvalBytes = encoded({
    schema: 'soui.devtools-package-approval/v1', backend: 'miniblink',
    kernel: {path: 'kernel/node.dll', sha256: kernelSha256, version: 'synthetic-1'},
    frontend: {manifest: 'approved-manifest.json', sha256: manifestSha256, version: 'synthetic-1'},
    licenses: [entry('licenses/LICENSE.txt')],
  });
  input('package-approval.json', approvalBytes);
  const pins = {
    kernelSha256, manifestSha256, approvalSha256: sha256(approvalBytes), architecture: 'x64', sdkVersion,
  };
  const preparedRoot = path.join(f.directory, 'prepared');
  await devtools.prepare({
    ...pins, frontendRoot, kernelRelative: 'kernel/node.dll',
    approvalRelative: 'package-approval.json', output: preparedRoot,
  });
  const applicationOutput = path.join(f.directory, 'runtime');
  f.write('runtime/node.dll', kernel);
  await devtools.deploy({...pins, preparedRoot, applicationOutput});
  return {
    prepared: JSON.parse(fs.readFileSync(path.join(preparedRoot, 'package/package.json'), 'utf8')),
    delivered: JSON.parse(fs.readFileSync(path.join(applicationOutput, 'devtools/package/package.json'), 'utf8')),
  };
}

test('DevTools metadata uses the complete installed SDK version after the application changes VERSION', async t => {
  const f = fixture(t), sdkVersion = '0.6.0-preview.1';
  const {root, headerPath} = f.select(sdkVersion, {applicationVersion: '7.4.2'});
  const selection = selected(f, root);
  assert.equal(selection.version, sdkVersion);
  assert.deepEqual(selection.dependencies, [cmakePath(headerPath), cmakePath(path.join(root, 'sdk-lock.json'))]);
  const metadata = await prepareAndDeploy(f, selection.version);
  assert.equal(metadata.prepared.sdk_version, sdkVersion);
  assert.equal(metadata.delivered.sdk_version, sdkVersion);
  assert.equal(fs.readFileSync(path.join(root, 'VERSION'), 'utf8'), '7.4.2\n');
});

test('an SDK source tree without an installed lock retains stable and rc version strings', t => {
  for (const version of ['0.6.0', '0.6.0-rc.2']) {
    const f = fixture(t), {root, headerPath} = f.select(version, {lockVersion: null});
    const selection = selected(f, root);
    assert.equal(selection.version, version);
    assert.deepEqual(selection.dependencies, [cmakePath(headerPath)]);
  }
});

test('SDK header and installed lock conflicts fail before package metadata can be selected', t => {
  for (const options of [
    {lockVersion: '0.5.0'},
    {header: '#define SOUI_SDK_VERSION_STRING "0.6.0-preview.1"\n#define SOUI_SDK_VERSION_STRING "0.6.0-preview.1"\n'},
    {header: '#define SOUI_SDK_VERSION_STRING "invalid"\n'},
    {header: '#pragma once\n'},
  ]) {
    const f = fixture(t), {root} = f.select('0.6.0-preview.1', options);
    const {result, output} = f.resolve(root);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /SDK.*(version|VERSION_STRING)|SDK must declare/);
    assert.equal(fs.existsSync(output), false);
  }
});

test('New-Project rejects a verified SDK from a different template delivery before creating output',
  {skip: process.platform !== 'win32'}, t => {
    const f = fixture(t);
    const generatorVersion = fs.readFileSync(path.join(repository, 'VERSION'), 'utf8').trim();
    const sdkVersion = generatorVersion === '0.0.0' ? '0.0.1' : '0.0.0';
    const bytes = 'Synthetic source SDK package.\n';
    const packageRoot = path.join(f.directory, 'selected-package');
    f.write('selected-package/payload/sdk/fixture.txt', bytes);
    f.write('selected-package/sdk-manifest.json', encoded(f.manifest(sdkVersion, [{
      path: 'sdk/fixture.txt', bytes: Buffer.byteLength(bytes), sha256: sha256(bytes),
    }])));
    assert.equal(sdk.readPackage(packageRoot).version, sdkVersion, 'The input must pass real package validation');
    const environment = {...process.env};
    const pathKey = Object.keys(environment).find(key => key.toLowerCase() === 'path') || 'PATH';
    environment[pathKey] = path.dirname(process.execPath) + path.delimiter + (environment[pathKey] || '');
    for (const profile of ['compatibility', 'modern']) {
      const output = path.join(f.directory, profile + '-project');
      const script = "$ErrorActionPreference='Stop'\ntry {\n& " +
        powershellQuote(path.join(repository, 'tools/New-Project.ps1')) +
        ' -Name SelectionFixture -OutputRoot ' + powershellQuote(output) +
        ' -SdkPackage ' + powershellQuote(packageRoot) + ' -Profile ' + powershellQuote(profile) +
        '\n} catch {[Console]::Error.WriteLine($_.Exception.Message); exit 1}\n';
      const result = cp.spawnSync('powershell.exe', [
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand',
        Buffer.from(script, 'utf16le').toString('base64'),
      ], {
        encoding: 'utf8', windowsHide: true, timeout: 30000,
        env: environment,
      });
      assert.notEqual(result.status, 0);
      assert.match(result.stderr, /Selected SDK version[\s\S]*does not match template\/generator version/);
      assert.ok(result.stderr.includes(sdkVersion));
      assert.ok(result.stderr.includes(generatorVersion));
      assert.equal(fs.existsSync(output), false, 'A mismatch must not leave a partially generated project');
    }
  });
