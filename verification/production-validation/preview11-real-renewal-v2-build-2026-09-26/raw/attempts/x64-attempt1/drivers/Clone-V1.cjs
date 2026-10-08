'use strict';
// File copies and SHA-256 checks only; no process, compiler, package manager or GUI.
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const ROOT = __dirname;
const OLD = 'C:/SOUI-SDK-production-validation/preview11-real-renewal-preparation-20260926';
const RUN = 'C:/SOUI-SDK-production-validation/preview11-real-renewal-runs-20260926/x86-smoke-attempt1';
const EXPECTED_FREEZE = '7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d';
const hash = b => crypto.createHash('sha256').update(b).digest('hex');
const encode = v => JSON.stringify(v, null, 2) + '\n';
const check = (test, message) => { if (!test) throw Error(message); };
const read = p => JSON.parse(fs.readFileSync(p, 'utf8'));
const copied = [];
function copy(from, relative, expected) {
  check(!fs.lstatSync(from).isSymbolicLink() && fs.statSync(from).isFile(), 'Not a regular source');
  const bytes = fs.readFileSync(from), actual = {bytes: bytes.length, sha256: hash(bytes)};
  if (expected) check(actual.bytes === expected.bytes && actual.sha256 === expected.sha256, 'Parent identity differs: ' + from);
  const to = path.join(ROOT, relative);
  fs.mkdirSync(path.dirname(to), {recursive: true});
  fs.writeFileSync(to, bytes, {flag: 'wx'});
  check(fs.readFileSync(to).equals(bytes), 'Copy differs');
  copied.push({path: relative, source: from, ...actual});
}
function copyManifest(from, to, manifest) {
  check(hash(encode(manifest.files)) === manifest.sha256, 'Manifest hash differs');
  for (const item of manifest.files) {
    check(!path.isAbsolute(item.path) && !item.path.includes('..') && !item.path.includes('\\'), 'Unsafe parent path');
    copy(path.join(from, item.path), to + '/' + item.path, item);
  }
}
check(!fs.existsSync(path.join(ROOT, 'prepared.json')) && !fs.existsSync(path.join(ROOT, 'inputs')) &&
  !fs.existsSync(path.join(ROOT, 'attempts')) && !fs.existsSync(path.join(ROOT, 'freeze.json')), 'Existing v2 state must be preserved');
const freezeFile = path.join(OLD, 'freeze.json'), freezeBytes = fs.readFileSync(freezeFile);
check(hash(freezeBytes) === EXPECTED_FREEZE, 'Parent freeze identity differs');
const freeze = JSON.parse(freezeBytes), base = read(path.join(OLD, 'prepared.json'));
check(base.inputs.files.length === 937 && base.inputs.sha256 === freeze.inputs.sha256, 'Wrong frozen inputs');
copyManifest(path.join(OLD, 'inputs'), 'inputs', base.inputs);
copyManifest(path.join(OLD, 'toolchain'), 'toolchain', base.toolchainSeed);
const runtime = freeze.runtimes.x86, buildBytes = fs.readFileSync(runtime.buildRecordPath);
check(hash(buildBytes) === runtime.buildRecordSha256, 'Parent build identity differs');
const build = JSON.parse(buildBytes);
copyManifest(freeze.consumer.x86.root, 'consumer', build.consumerSource);
copyManifest(path.join(path.dirname(runtime.buildRecordPath), 'drivers'), 'provenance/parent-drivers', build.drivers);
for (const item of build.drivers.files) copy(path.join(OLD, item.path), item.path, item);
for (const item of build.protocol.files) {
  copy(path.join(OLD, item.path), 'provenance/parent-protocol/' + item.path, item);
  if (!fs.existsSync(path.join(ROOT, item.path))) copy(path.join(OLD, item.path), item.path, item);
}
copy(freezeFile, 'provenance/parent-freeze.json');
copy(path.join(OLD, 'prepared.json'), 'provenance/parent-prepared.json');
for (const name of ['native.ndjson', 'supervisor.json', 'diagnosis.json', 'verification.result.json',
    'verification.stdout.log', 'verification.stderr.log', 'diagnostics-after/source-navigation.log'])
  copy(path.join(RUN, name), 'provenance/failed-x86-smoke/' + name);
for (const item of copied) {
  const bytes = fs.readFileSync(item.source);
  check(bytes.length === item.bytes && hash(bytes) === item.sha256, 'Original changed during copy');
}
const parent = {preparationRoot: OLD, freezeSha256: EXPECTED_FREEZE, failureRunRoot: RUN,
  failureReportSha256: hash(fs.readFileSync(path.join(RUN, 'native.ndjson'))),
  failureSupervisorSha256: hash(fs.readFileSync(path.join(RUN, 'supervisor.json')))};
fs.writeFileSync(path.join(ROOT, 'prepared.json'), encode({...base, preparedAt: new Date().toISOString(),
  fixtureRevision: 2, sourceRoot: path.join(ROOT, 'inputs/source'), parent,
  compiled: false, guiExecuted: false, realRenewalVerified: false, releaseReady: false}), {flag: 'wx'});
fs.writeFileSync(path.join(ROOT, 'clone-provenance.json'), encode({schema: 'soui.renewal-fixture-v2-clone/v1',
  createdAt: new Date().toISOString(), parent, inputFiles: 937, inputSha256: base.inputs.sha256,
  files: copied, originalsUnchanged: true, compiled: false, guiExecuted: false, realRenewalVerified: false,
  note: 'Copied baseline hashes precede the explicitly reviewed v2 source/driver edits; no attempts, node_modules or new freeze copied.'}), {flag: 'wx'});
console.log(encode({status: 'v2-baseline-copied', inputFiles: 937, inputSha256: base.inputs.sha256,
  originalsUnchanged: true, compiled: false, guiExecuted: false}));
