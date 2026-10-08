'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const root = 'C:/SOUI-SDK-development/preview9-tray';
const suite = __dirname;
const id = require(path.join(root, 'tools/internal/delivery-identity.cjs'));
const build = require(path.join(root, 'tools/internal/build-evidence.cjs'));
const read = name => id.readJson(path.join(suite, name));
const frozen = read('source-inputs.json');
assert.equal(frozen.sha256, id.source(root).sha256);
const version = build.version(root);
assert.equal(version, '0.3.0-preview.10');
const variants = [];
for (const arch of ['x86', 'x64']) {
  const relative = `${arch}-attempt1`;
  const output = path.join(suite, relative);
  const full = build.verifyBuild(root, arch, 'Release');
  const sdk = build.verifyBuild(root, arch, 'Release', true);
  assert.equal(full.source.sha256, frozen.sha256);
  assert.equal(sdk.source.sha256, frozen.sha256);
  for (const step of ['full-build','sdk-build','no-desktop-tests','install','consumer-configure','consumer-build']) {
    assert.equal(read(`${relative}/${step}.result.json`).exitCode, 0, `${arch} ${step}`);
  }
  const xml = fs.readFileSync(path.join(output, 'tests.xml'), 'utf8');
  assert.match(xml, /tests="8"/);
  assert.match(xml, /failures="0"/);
  assert.match(xml, /disabled="0"/);
  const consumerRelative = 'consumer-build/bin/Release/SouiTrayConsumer.exe';
  const binary = fs.readFileSync(path.join(output, consumerRelative));
  const pe = binary.readUInt32LE(0x3c);
  assert.equal(binary.toString('binary', pe, pe + 4), 'PE\0\0');
  const machine = binary.readUInt16LE(pe + 4);
  assert.equal(machine, arch === 'x86' ? 0x14c : 0x8664);
  const argument = read(`${relative}/consumer-argument.result.json`);
  assert.equal(argument.exitCode, 2); assert.equal(argument.timedOut, false);
  assert.equal(argument.sha256, id.hash(binary));
  const installed = path.join(output, 'sdk-install');
  const consumerSource = path.join(output, 'consumer-source');
  const testsRoot = path.join(root, `out/${arch}/tests/native/Release`);
  const testNames = ['soui_sdk_contract_tests.exe','soui_task_service_tests.exe','soui_bridge_budget_tests.exe',
    'soui_task_store_capacity_tests.exe','soui_tray_service_tests.exe','soui_menu_service_tests.exe','soui_global_shortcut_tests.exe'];
  variants.push({architecture: arch, configuration: 'Release', sourceSha256: frozen.sha256,
    fullBuildId: full.buildId, sdkBuildId: sdk.buildId, testsPassed: 8,
    installedSdk: id.snapshot(installed, id.walk(installed)),
    consumerSource: id.snapshot(consumerSource, id.walk(consumerSource)),
    consumer: {peMachine: machine.toString(16), ...id.snapshot(output, [consumerRelative])},
    nativeTests: id.snapshot(testsRoot, testNames),
    applications: Object.fromEntries(Object.entries(full.payloads).map(([target, payload]) =>
      [target, {sha256: payload.sha256, bytes: payload.bytes, files: payload.files.length}]))});
}
for (const result of read('packaging-tests.result.json')) assert.equal(result.exitCode, 0);
assert.equal(read('web-tests.result.json').exitCode, 0);
const audits = read('audit-full-results.json');
assert.equal(audits.length, 5);
for (const result of audits) {
  assert.equal(result.exitCode, 0);
  const audit = read(`audit-full-${result.package.replaceAll('/', '-')}.json`);
  assert.equal(audit.metadata.vulnerabilities.total, 0);
}
assert.equal(read('sdk-pack.result.json').exitCode, 1);
assert.equal(read('sdk-pack-attempt2.result.json').exitCode, 0);
assert.equal(read('sdk-verification.result.json').integrityExitCode, 0);
assert.equal(read('sdk-verification.result.json').sourceMatchExitCode, 0);
const pack = read('sdk-verify.json');
assert.equal(pack.version, version); assert.equal(pack.files, 417);
const summary = {schema:'soui.residency-preflight/v1', createdAt:new Date().toISOString(), version,
  sourceSha256:frozen.sha256, sourceFiles:frozen.files.length, host:read('host-os.json'), architectures:variants,
  webTestsPassed:35, packageTestsPassed:14, deliveryIdentityTestsPassed:7, fullAudits:5,
  sdkPackage:{files:pack.files, integrityVerified:true, sourceMatches:true, firstAttemptExitCode:1,
    failure:'EPERM during staging directory rename; new destination retry passed, root cause unconfirmed'},
  desktopGate:read('desktop-gate.json'), desktopAndGuestExecuted:false,
  testScope:'Seven SDK tests use no UI; Demo task adapter test does not create a business window or WebView but initializes COM STA.',
  pending:['real menu and keyboard input','Explorer restart','Workbench resident smoke and modal-close safety',
    'Win7 SP1 x86','V02 IME, resize and scrolling concurrent with tasks','Debug matrix',
    'full source relocation','isolated long stability','Electron value comparison','production approval'],
  sourceDeliveryVerified:false, benchmarkValid:false, releaseReady:false};
fs.writeFileSync(path.join(suite, 'summary.json'), id.encoded(summary));
console.log(JSON.stringify({version, sourceSha256:frozen.sha256, architectures:variants.map(v=>v.architecture),
  testsPerArchitecture:8, installedFiles:variants.map(v=>v.installedSdk.files.length), sdkPackageFiles:417, desktop:false}));
