'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const root = 'C:/SOUI-SDK-development/preview9-tray';
const suite = __dirname;
const id = require(path.join(root, 'tools/internal/delivery-identity.cjs'));
const build = require(path.join(root, 'tools/internal/build-evidence.cjs'));
const packaging = require(path.join(root, 'tools/sdk/soui-sdk.cjs'));
const read = name => id.readJson(path.join(suite, name));
const log = name => fs.readFileSync(path.join(suite, name), 'utf8').replace(/^\uFEFF/, '');
const exactNames = (actual, expected, label) => {
  assert.equal(new Set(actual).size, actual.length, `${label}: duplicate names`);
  assert.deepEqual([...actual].sort(), [...expected].sort(), `${label}: exact names`);
};
const expectedTests = ['sdk.native.contract','sdk.native.task-service','sdk.native.bridge-budget',
  'sdk.native.task-store-capacity','sdk.native.tray-service','sdk.native.menu_service',
  'sdk.native.global_shortcut','demo.native.public-task'];
function attributes(text) {
  const values = Object.create(null);
  const remainder = text.replace(/([\w.-]+)="([^"<>]*)"/g, (_, name, value) => {
    assert.equal(Object.hasOwn(values, name), false, `duplicate XML attribute ${name}`);
    values[name] = value;
    return '';
  });
  assert.equal(remainder.trim(), '', 'unexpected XML attribute syntax');
  return values;
}
// Deliberately accepts only the CTest JUnit structure emitted by this frozen run.
function nativeResults(xml) {
  const suiteMatch = /^\s*<\?xml[^?]*\?>\s*<testsuite\s+([^>]*)>([\s\S]*)<\/testsuite>\s*$/.exec(xml);
  assert.ok(suiteMatch, 'one CTest testsuite required');
  const suiteAttributes = attributes(suiteMatch[1]);
  for (const [key, value] of Object.entries({tests:'8', failures:'0', disabled:'0', skipped:'0'}))
    assert.equal(suiteAttributes[key], value, `CTest ${key}`);
  assert.doesNotMatch(xml, /<(?:failure|error|skipped)\b/);
  const cases = [];
  const remainder = suiteMatch[2].replace(/<testcase\s+([^>]*)>([\s\S]*?)<\/testcase>/g, (_, attrs, body) => {
    const test = attributes(attrs);
    assert.equal(test.status, 'run', `${test.name}: not run`);
    assert.equal(test.classname, test.name);
    const output = /^\s*<properties\s*\/>\s*<system-out>([^<]*)<\/system-out>\s*$/.exec(body);
    assert.ok(output, `${test.name}: unexpected case result`);
    cases.push({name:test.name, output:output[1]});
    return '';
  });
  assert.equal(remainder.trim(), '', 'unparsed CTest cases');
  exactNames(cases.map(test => test.name), expectedTests, 'CTest');
  const groups = {};
  for (const [name, prefix, count] of [['sdk.native.tray-service','tray_service',14],
    ['sdk.native.menu_service','menu_service',15], ['sdk.native.global_shortcut','global_shortcut',9]]) {
    const output = cases.find(test => test.name === name).output.trim();
    const match = new RegExp(`^${prefix}: (\\d+) no-desktop contract groups passed \\(fake platform; .+ claim\\)$`).exec(output);
    assert.ok(match, `${name}: missing group result`);
    groups[prefix] = Number(match[1]);
    assert.equal(groups[prefix], count, name);
  }
  return {count:cases.length, names:cases.map(test => test.name), groups};
}
function nodeResults(text, expected) {
  const counts = {};
  for (const key of ['tests','pass','fail','cancelled','skipped','todo']) {
    const matches = [...text.matchAll(new RegExp(`^ℹ ${key} (\\d+)\\r?$`, 'gm'))];
    assert.equal(matches.length, 1, `one Node ${key} summary required`);
    counts[key] = Number(matches[0][1]);
    assert.equal(counts[key], ['tests','pass'].includes(key) ? expected : 0, `Node ${key}`);
  }
  return counts.pass;
}
function packageResults(text) {
  const matches = [...text.matchAll(/^SDK package smoke: (\d+)\/(\d+) passed \([^\r\n]*\)\.\r?$/gm)];
  assert.equal(matches.length, 1, 'one package smoke result required');
  assert.equal(Number(matches[0][1]), 14);
  assert.equal(Number(matches[0][2]), 14);
  return Number(matches[0][1]);
}
const fileMatches = (actual, expected) => {
  const a = fs.readFileSync(actual), b = fs.readFileSync(expected);
  assert.equal(a.length, b.length, actual);
  assert.equal(id.hash(a), id.hash(b), actual);
};
const normalizedPath = value => value.replaceAll('\\', '/').toLowerCase();
function installedFiles(installed, arch) {
  const sdkOutput = path.join(root, `out/sdk-${arch}`);
  const sources = new Map();
  for (const name of id.walk(path.join(root, 'sdk/include')))
    sources.set(`include/${name}`, path.join(root, 'sdk/include', name));
  for (const name of id.walk(path.join(root, 'third_party/licenses')))
    sources.set(`share/SouiSdk/licenses/${name}`, path.join(root, 'third_party/licenses', name));
  sources.set('share/SouiSdk/THIRD_PARTY_NOTICES.md', path.join(root, 'THIRD_PARTY_NOTICES.md'));
  sources.set('lib/Release/soui_sdk.lib', path.join(sdkOutput, 'Release/soui_sdk.lib'));
  for (const name of ['SouiSdkConfig.cmake','SouiSdkConfigVersion.cmake'])
    sources.set(`lib/cmake/SouiSdk/${name}`, path.join(sdkOutput, name));
  const exportRoot = path.join(sdkOutput, 'CMakeFiles/Export');
  for (const name of ['SouiSdkTargets.cmake','SouiSdkTargets-release.cmake']) {
    const candidates = id.walk(exportRoot).filter(file => path.basename(file) === name);
    assert.equal(candidates.length, 1, `unique export ${name}`);
    sources.set(`lib/cmake/SouiSdk/${name}`, path.join(exportRoot, candidates[0]));
  }
  const names = id.walk(installed);
  assert.equal(names.length, 35, 'installed SDK file count');
  exactNames(names, [...sources.keys()], 'installed SDK');
  for (const [name, source] of sources) fileMatches(path.join(installed, name), source);
  return id.snapshot(installed, names);
}
if (process.argv[2] === '--self-test') {
  assert.equal(process.argv.length, 3);
  const xml = log('x86-attempt1/tests.xml');
  nativeResults(xml);
  for (const invalid of [xml.replace('skipped="0"','skipped="1"'), xml.replace('status="run"','status="notrun"'),
    xml.replace('sdk.native.contract','sdk.native.unknown'), xml.replace('15 no-desktop','14 no-desktop'),
    xml.replace('</testsuite>', '<testcase name="extra" status="run"></testcase></testsuite>')])
    assert.throws(() => nativeResults(invalid));
  assert.throws(() => exactNames(['a','a'], ['a','b'], 'duplicate'));
  assert.throws(() => exactNames([], ['a'], 'missing'));
  assert.equal(nodeResults(log('web-tests.log'), 35), 35);
  assert.throws(() => nodeResults(log('web-tests.log').replace('ℹ skipped 0','ℹ skipped 1'), 35));
  assert.throws(() => nodeResults(log('web-tests.log') + '\nℹ tests 35\n', 35));
  assert.throws(() => nodeResults(log('delivery-identity.log'), 35));
  assert.equal(packageResults(log('sdk-package-smoke.log')), 14);
  assert.throws(() => packageResults(log('sdk-package-smoke.log').replace('14/14','13/14')));
  console.log('Collector parser checks passed: 11 negative cases; no evidence files changed.');
  process.exit(0);
}
assert.equal(process.argv.length, 2, 'collector takes no arguments except --self-test');
const frozen = read('source-inputs.json');
assert.deepEqual(frozen, id.source(root), 'frozen source file identities');
const version = build.version(root);
assert.equal(version, '0.3.0-preview.10');
const variants = [];
for (const arch of ['x86', 'x64']) {
  const relative = `${arch}-attempt1`;
  const output = path.join(suite, relative);
  const full = build.verifyBuild(root, arch, 'Release');
  const sdk = build.verifyBuild(root, arch, 'Release', true);
  assert.deepEqual(read(`${relative}/full-build.json`), full, `${arch}: archived full build receipt must match live buildId and payloads`);
  assert.deepEqual(read(`${relative}/sdk-build.json`), sdk, `${arch}: archived SDK receipt must match live buildId and library`);
  assert.equal(full.source.sha256, frozen.sha256);
  assert.equal(sdk.source.sha256, frozen.sha256);
  for (const step of ['full-build','sdk-build','no-desktop-tests','install','consumer-configure','consumer-build']) {
    assert.equal(read(`${relative}/${step}.result.json`).exitCode, 0, `${arch} ${step}`);
  }
  const tests = nativeResults(log(`${relative}/tests.xml`));
  const consumerRelative = 'consumer-build/bin/Release/SouiTrayConsumer.exe';
  const binary = fs.readFileSync(path.join(output, consumerRelative));
  const pe = binary.readUInt32LE(0x3c);
  assert.equal(binary.toString('binary', pe, pe + 4), 'PE\0\0');
  const machine = binary.readUInt16LE(pe + 4);
  assert.equal(machine, arch === 'x86' ? 0x14c : 0x8664);
  const argument = read(`${relative}/consumer-argument.result.json`);
  assert.equal(argument.exitCode, 2); assert.equal(argument.timedOut, false);
  assert.deepEqual(argument.arguments, ['--invalid']);
  assert.equal(argument.sha256, id.hash(binary));
  assert.equal(log(`${relative}/consumer-argument.stdout.log`), '');
  assert.equal(log(`${relative}/consumer-argument.stderr.log`).trim(), 'Expected exactly --smoke-test or --interactive');
  const installed = path.join(output, 'sdk-install');
  const consumerSource = path.join(output, 'consumer-source');
  assert.deepEqual(id.snapshot(consumerSource, id.walk(consumerSource)),
    id.snapshot(path.join(root, 'tests/tray-consumer'), id.walk(path.join(root, 'tests/tray-consumer'))),
    `${arch}: copied consumer source`);
  const cache = log(`${relative}/consumer-build/CMakeCache.txt`);
  for (const [key, value] of [['CMAKE_HOME_DIRECTORY',consumerSource],
    ['SouiSdk_DIR',path.join(installed, 'lib/cmake/SouiSdk')], ['CMAKE_GENERATOR_PLATFORM',arch === 'x86' ? 'Win32' : 'x64']]) {
    const matches = [...cache.matchAll(new RegExp(`^${key}:[^=]+=([^\\r\\n]*)`, 'gm'))];
    assert.equal(matches.length, 1, key);
    assert.equal(normalizedPath(matches[0][1]), normalizedPath(value), key);
  }
  const project = normalizedPath(log(`${relative}/consumer-build/SouiTrayConsumer.vcxproj`));
  assert.ok(project.includes(normalizedPath(path.join(installed, 'lib/Release/soui_sdk.lib'))), 'external installed library link');
  const testsRoot = path.join(root, `out/${arch}/tests/native/Release`);
  const testNames = ['soui_sdk_contract_tests.exe','soui_task_service_tests.exe','soui_bridge_budget_tests.exe',
    'soui_task_store_capacity_tests.exe','soui_tray_service_tests.exe','soui_menu_service_tests.exe','soui_global_shortcut_tests.exe'];
  variants.push({architecture: arch, configuration: 'Release', sourceSha256: frozen.sha256,
    fullBuildId: full.buildId, sdkBuildId: sdk.buildId, testsPassed: tests.count,
    tests: tests.names, nativeContractGroups: tests.groups,
    installedSdk: installedFiles(installed, arch),
    consumerSource: id.snapshot(consumerSource, id.walk(consumerSource)),
    consumer: {peMachine: machine.toString(16), ...id.snapshot(output, [consumerRelative])},
    nativeTests: id.snapshot(testsRoot, testNames),
    applications: Object.fromEntries(Object.entries(full.payloads).map(([target, payload]) =>
      [target, {sha256: payload.sha256, bytes: payload.bytes, files: payload.files.length}]))});
}
const packageRuns = read('packaging-tests.result.json');
exactNames(packageRuns.map(result => result.name), ['sdk-package-smoke','delivery-identity'], 'packaging results');
for (const result of packageRuns) assert.equal(result.exitCode, 0);
assert.equal(read('web-tests.result.json').exitCode, 0);
assert.deepEqual(read('web-tests.result.json').arguments,
  ['--test','tests/web/sdk-client.test.mjs','tests/web/native-resolvers.test.mjs']);
const webTestsPassed = nodeResults(log('web-tests.log'), 35);
const packageTestsPassed = packageResults(log('sdk-package-smoke.log'));
const deliveryIdentityTestsPassed = nodeResults(log('delivery-identity.log'), 7);
const audits = read('audit-full-results.json');
exactNames(audits.map(result => result.package), ['sdk/web','templates/desktop-app/web','demos/workbench/web',
  'tests/acceptance','benchmarks/hosts/electron'], 'audit results');
for (const result of audits) {
  assert.equal(result.exitCode, 0);
  assert.deepEqual(result.arguments, ['--prefix',result.package,'audit','--json']);
  const audit = read(`audit-full-${result.package.replaceAll('/', '-')}.json`);
  assert.equal(audit.metadata.vulnerabilities.total, 0);
}
assert.equal(read('sdk-pack.result.json').exitCode, 1);
assert.equal(read('sdk-pack-attempt2.result.json').exitCode, 0);
assert.equal(read('sdk-verification.result.json').integrityExitCode, 0);
assert.equal(read('sdk-verification.result.json').sourceMatchExitCode, 0);
const pack = read('sdk-verify.json');
assert.equal(pack.version, version); assert.equal(pack.files, 417);
assert.equal(pack.status, 'verified'); assert.equal(pack.releaseReady, false);
assert.deepEqual(read('sdk-verify-source.json'), {status:'source-matches-package', version});
assert.match(log('sdk-pack.log'), /EPERM.*rename/);
const packed = read('sdk-pack-attempt2.log');
assert.equal(packed.status, 'packed'); assert.equal(packed.version, version);
assert.equal(packed.files, 417); assert.equal(packed.releaseReady, false);
const packageDirectory = path.join(suite, 'sdk-package-attempt2');
assert.equal(normalizedPath(packed.output), normalizedPath(packageDirectory));
const manifest = packaging.readPackage(packageDirectory); // Validates every payload byte/hash.
assert.equal(manifest.version, version); assert.equal(manifest.files.length, pack.files);
const packageNames = manifest.files.map(file => file.path);
exactNames(id.walk(path.join(packageDirectory, 'payload')), packageNames, 'SDK payload');
const sourceNames = [...packaging.managedRoots.flatMap(name => id.walk(root, name,
  (_, leaf) => ['node_modules','dist','out','.git','.vs'].includes(leaf))), ...packaging.managedFiles];
exactNames(sourceNames, packageNames, 'SDK source set');
for (const file of manifest.files) {
  const bytes = id.normalize(fs.readFileSync(id.safePath(root, file.path)), file.sourceNormalization || 'none');
  assert.equal(bytes.length, file.bytes, file.path);
  assert.equal(id.hash(bytes), file.sha256, file.path);
}
const desktopGate = read('desktop-gate.json');
assert.equal(desktopGate.desktopExecuted, false);
const summary = {schema:'soui.residency-preflight/v1', createdAt:new Date().toISOString(), version,
  sourceSha256:frozen.sha256, sourceFiles:frozen.files.length, host:read('host-os.json'), architectures:variants,
  webTestsPassed, packageTestsPassed, deliveryIdentityTestsPassed, fullAudits:audits.length,
  sdkPackage:{files:pack.files, integrityVerified:true, sourceMatches:true, firstAttemptExitCode:1,
    failure:'EPERM during staging directory rename; new destination retry passed, root cause unconfirmed'},
  desktopGate, desktopAndGuestExecuted:false,
  collectorValidation:'Exact test/result sets and parsed log counts; archived/live build receipts; physical SDK package/source and installation byte identities.',
  testScope:'Seven SDK tests use no UI; Demo task adapter test does not create a business window or WebView but initializes COM STA.',
  pending:['real menu and keyboard input','Explorer restart','Workbench resident smoke and modal-close safety',
    'Win7 SP1 x86','V02 IME, resize and scrolling concurrent with tasks','Debug matrix',
    'full source relocation','isolated long stability','Electron value comparison','production approval'],
  sourceDeliveryVerified:false, benchmarkValid:false, releaseReady:false};
fs.writeFileSync(path.join(suite, 'summary.json'), id.encoded(summary));
console.log(JSON.stringify({version, sourceSha256:frozen.sha256, architectures:variants.map(v=>v.architecture),
  testsPerArchitecture:variants.map(v=>v.testsPassed), installedFiles:variants.map(v=>v.installedSdk.files.length),
  sdkPackageFiles:manifest.files.length, webTestsPassed, packageTestsPassed, deliveryIdentityTestsPassed, desktop:false}));
