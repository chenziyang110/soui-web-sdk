'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function read(root, name) {
  return JSON.parse(fs.readFileSync(path.join(root, name), 'utf8'));
}
function sha(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}
function requireEvidence(condition, message) {
  if (!condition) throw new Error(message);
}

function verify(root) {
  const report = read(root, 'result.json');
  requireEvidence(report.schema === 'soui.upgrade-trial/v1' && report.passed === true &&
    report.releaseReady === false && Array.isArray(report.stages) && report.stages.length === 4,
  'The four-stage trial did not pass');
  const expected = [
    ['old-initial', report.oldVersion], ['new-upgraded', report.newVersion],
    ['old-rolled-back', report.oldVersion], ['new-restored', report.newVersion],
  ];
  requireEvidence(report.oldVersion !== report.newVersion, 'Upgrade versions are identical');
  const builds = [];
  for (let i = 0; i < expected.length; i++) {
    const [name, version] = expected[i];
    const stage = report.stages[i];
    const build = read(root, `${name}/build-Release.json`);
    const smoke = read(root, `${name}/smoke-Release.json`);
    const lock = read(root, `${name}/sdk-lock.json`);
    const log = fs.readFileSync(path.join(root, name, 'smoke.log'), 'utf8');
    requireEvidence(stage.stage === name && stage.version === version && stage.smokePassed === true &&
      stage.businessFilesPreserved === true, `Stage summary failed: ${name}`);
    requireEvidence(lock.schema === 'soui.sdk-lock/v1' && lock.manifest.version === version &&
      lock.manifest.releaseReady === false, `SDK lock identity failed: ${name}`);
    requireEvidence(build.schema === 'soui.source-build/v2' && build.version === version &&
      build.architecture === 'x64' && build.configuration === 'Release' && build.nativeBuild === 'passed' &&
      build.buildId === stage.buildId && build.source.sha256 === stage.sourceSha256 &&
      build.releaseReady === false, `Build identity failed: ${name}`);
    requireEvidence(smoke.schema === 'soui.native-smoke/v2' && smoke.passed === true &&
      smoke.releaseReady === false && smoke.buildId === build.buildId &&
      smoke.sourceSha256 === build.source.sha256 && smoke.version === version &&
      JSON.stringify(smoke.payloads) === JSON.stringify(build.payloads),
    `Smoke payload identity failed: ${name}`);
    requireEvidence(smoke.runs.length === 1 && smoke.runs[0].target === 'ProductTrial' &&
      smoke.runs[0].scenario === 'default' && smoke.runs[0].passed === true &&
      smoke.runs[0].exitCode === 0 && smoke.runs[0].timedOut === false &&
      smoke.runs[0].log === log && log.includes('business-marker-preview4-upgrade') &&
      log.includes('application-ready') && log.includes('host-close-complete'),
    `Real business startup/close failed: ${name}`);
    builds.push(build.buildId);
  }
  requireEvidence(new Set(builds).size === 4, 'The four builds were not independent');
  for (const [name, hash] of Object.entries(report.businessFiles)) {
    requireEvidence(!path.isAbsolute(name) && !name.includes('\\') && !name.includes(':') &&
      !name.split('/').some(part => !part || part === '.' || part === '..') &&
      /^[a-f0-9]{64}$/.test(hash) && sha(path.join(root, 'business-source', name)) === hash,
    `Business source changed: ${name}`);
  }
  const files = [];
  const walk = (dir, relative = '') => {
    for (const entry of fs.readdirSync(dir, {withFileTypes: true})) {
      const name = relative ? `${relative}/${entry.name}` : entry.name;
      if (entry.isSymbolicLink()) throw new Error(`Linked evidence: ${name}`);
      if (entry.isDirectory()) walk(path.join(dir, entry.name), name);
      else if (entry.isFile()) files.push({path: name, bytes: fs.statSync(path.join(dir, entry.name)).size,
        sha256: sha(path.join(dir, entry.name))});
      else throw new Error(`Unknown evidence entry: ${name}`);
    }
  };
  walk(root);
  files.sort((a, b) => a.path.localeCompare(b.path, 'en'));
  return {schema: 'soui.upgrade-trial-verification/v1', passed: true, versions: [report.oldVersion, report.newVersion],
    stages: expected.map(([name, version], index) => ({name, version, buildId: builds[index]})), files};
}

try {
  if (process.argv.length !== 3) throw new Error('Usage: node tests/acceptance/verify-upgrade.cjs EVIDENCE_DIRECTORY');
  const result = verify(path.resolve(process.argv[2]));
  console.log(JSON.stringify(result, null, 2));
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
