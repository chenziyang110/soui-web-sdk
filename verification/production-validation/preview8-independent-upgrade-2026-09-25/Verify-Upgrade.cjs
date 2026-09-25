'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const root = __dirname;
const read = name => JSON.parse(fs.readFileSync(path.join(root, name), 'utf8'));
const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const assert = (condition, message) => { if (!condition) throw new Error(message); };
const report = read('result.json');
assert(report.schema === 'soui.upgrade-trial/v2' && report.passed === true && report.releaseReady === false,
  'Trial result is not a passed preview report');
assert(report.oldVersion === '0.3.0-preview.7' && report.newVersion === '0.3.0-preview.8', 'Wrong version pair');
const expected = [
  ['old-initial', report.oldVersion], ['new-upgraded', report.newVersion],
  ['old-rolled-back', report.oldVersion], ['new-restored', report.newVersion],
];
assert(Array.isArray(report.stages) && report.stages.length === expected.length, 'Wrong stage count');
assert(Object.keys(report.businessFiles).length === 2, 'Wrong business file count');
for (const [name, hash] of Object.entries(report.businessFiles)) {
  assert(name.startsWith('app/') && !name.includes('..') && /^[a-f0-9]{64}$/i.test(hash), 'Unsafe business path/hash');
  assert(digest(path.join(root, 'business-source', name)) === hash.toLowerCase(), `Business source changed: ${name}`);
}

const buildIds = new Set();
for (let index = 0; index < expected.length; index++) {
  const [stageName, version] = expected[index];
  const stage = report.stages[index];
  const lock = read(`${stageName}/sdk-lock.json`);
  assert(stage.stage === stageName && stage.sdkVersion === version && stage.businessFilesPreserved === true,
    `Stage summary failed: ${stageName}`);
  assert(lock.schema === 'soui.sdk-lock/v1' && lock.manifest.version === version &&
    lock.manifest.releaseReady === false, `SDK lock failed: ${stageName}`);
  assert(Array.isArray(stage.architectures) && stage.architectures.length === 2 &&
    stage.architectures[0].architecture === 'x86' && stage.architectures[1].architecture === 'x64',
  `Architecture matrix failed: ${stageName}`);
  for (const archResult of stage.architectures) {
    const arch = archResult.architecture;
    const folder = `${stageName}/${arch}`;
    const build = read(`${folder}/build-Release.json`);
    const smoke = read(`${folder}/smoke-Release.json`);
    const log = fs.readFileSync(path.join(root, folder, 'smoke.log'), 'utf8');
    const exe = build.payloads?.UpgradeProbe?.files?.find(file => file.path === 'UpgradeProbe.exe');
    assert(build.schema === 'soui.source-build/v2' && build.version === version &&
      build.architecture === arch && build.configuration === 'Release' && build.nativeBuild === 'passed' &&
      build.buildId === archResult.buildId && build.source.sha256 === archResult.sourceSha256 &&
      build.releaseReady === false && exe?.sha256 === archResult.exeSha256.toLowerCase(),
    `Build identity failed: ${folder}`);
    assert(smoke.schema === 'soui.native-smoke/v2' && smoke.passed === true && smoke.releaseReady === false &&
      smoke.version === version && smoke.architecture === arch && smoke.buildId === build.buildId &&
      smoke.sourceSha256 === build.source.sha256 &&
      JSON.stringify(smoke.payloads) === JSON.stringify(build.payloads),
    `Smoke payload identity failed: ${folder}`);
    assert(smoke.runs.length === 1 && smoke.runs[0].target === 'UpgradeProbe' &&
      smoke.runs[0].scenario === 'default' && smoke.runs[0].passed === true &&
      smoke.runs[0].exitCode === 0 && smoke.runs[0].timedOut === false &&
      smoke.runs[0].log === log && log.includes('business-marker=customer-state-2026') &&
      log.includes('application-ready') && log.includes('host-close-complete'),
    `Business startup/close failed: ${folder}`);
    assert(!buildIds.has(build.buildId), `Build ID reused: ${folder}`);
    buildIds.add(build.buildId);
  }
}

const files = [];
function walk(dir, relative = '') {
  for (const entry of fs.readdirSync(dir, {withFileTypes: true})) {
    if (!relative && entry.name === 'verification.json') continue;
    const name = relative ? `${relative}/${entry.name}` : entry.name;
    const file = path.join(dir, entry.name);
    assert(!entry.isSymbolicLink(), `Linked evidence: ${name}`);
    if (entry.isDirectory()) walk(file, name);
    else {
      assert(entry.isFile(), `Unknown evidence entry: ${name}`);
      files.push({path: name, bytes: fs.statSync(file).size, sha256: digest(file)});
    }
  }
}
walk(root);
files.sort((a, b) => a.path.localeCompare(b.path, 'en'));
console.log(JSON.stringify({schema: 'soui.upgrade-trial-verification/v2', passed: true,
  versions: [report.oldVersion, report.newVersion], stages: expected.map(([name]) => name),
  architectures: ['x86', 'x64'], distinctBuildIds: buildIds.size, files, releaseReady: false}, null, 2));
