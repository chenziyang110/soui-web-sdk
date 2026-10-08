'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function insist(condition, message) {
  if (!condition) throw new Error(message);
}
function sha(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}
function json(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}
function files(root) {
  const result = [];
  const walk = (directory, prefix = '') => {
    for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
      const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
      if (entry.isSymbolicLink()) throw new Error(`Linked evidence: ${relative}`);
      if (entry.isDirectory()) walk(path.join(directory, entry.name), relative);
      else if (entry.isFile()) result.push(relative);
      else throw new Error(`Unknown evidence entry: ${relative}`);
    }
  };
  walk(root);
  return result.sort();
}
function resultFile(directory) {
  const candidates = fs.readdirSync(directory).filter(name => name.endsWith('.json') && !name.endsWith('-host.json'));
  insist(candidates.length === 1, `Expected one smoke result in ${directory}`);
  return path.join(directory, candidates[0]);
}
function verifyResult(file, host) {
  const result = json(file);
  insist(result.schemaVersion === 1 && result.status === 'measured' && result.scene === 'S01' &&
    result.hostKind === host && result.fixture?.fixtureId === 'electron-smoothness-10k-v1' &&
    result.fixture?.rowCount === 10000 && result.sceneChecks?.completed === true &&
    Number.isFinite(result.timing?.durationMs) && result.timing.durationMs > 0 &&
    Array.isArray(result.js?.eventLog) && Array.isArray(result.js?.rafIntervalsMs),
  `Invalid ${host} result`);
  return {file: path.basename(file), bytes: fs.statSync(file).size, sha256: sha(file),
    scene: result.scene, hostKind: host};
}
function verify(root) {
  const shared = path.join(root, 'shared-page');
  const souiPage = path.join(root, 'soui-smoke-page');
  const pageHash = sha(path.join(shared, 'benchmark.html'));
  insist(sha(path.join(shared, 'index.html')) === pageHash &&
    sha(path.join(souiPage, 'benchmark.html')) === pageHash &&
    sha(path.join(souiPage, 'index.html')) !== pageHash,
  'Shared and probe-only pages were not separated');
  for (const relative of files(path.join(shared, 'assets'))) {
    insist(sha(path.join(shared, 'assets', relative)) === sha(path.join(souiPage, 'assets', relative)),
      `Shared asset changed for SOUI: ${relative}`);
  }
  const electronDir = path.join(root, 'electron-run1');
  const hostFiles = fs.readdirSync(electronDir).filter(name => name.endsWith('-host.json'));
  insist(hostFiles.length === 1, 'Electron host metadata is missing');
  const host = json(path.join(electronDir, hostFiles[0]));
  insist(host.schema === 'soui.benchmark-electron-run/v1' && host.pageSha256 === pageHash &&
    host.versions?.electron === '44.4.4' && host.security?.sandbox === true &&
    host.security?.contextIsolation === true && host.security?.nodeIntegration === false &&
    host.security?.webSecurity === true, 'Electron host identity or isolation differs');
  const results = [
    verifyResult(resultFile(path.join(root, 'soui-x64-run5')), 'soui-miniblink'),
    verifyResult(resultFile(path.join(root, 'soui-x86-run1')), 'soui-miniblink'),
    verifyResult(resultFile(electronDir), 'electron'),
  ];
  insist(fs.readFileSync(path.join(root, 'source-navigation.log'), 'utf8').includes('allowed=1') &&
    fs.readFileSync(path.join(root, 'source-navigation-rejected.log'), 'utf8').includes('allowed=0'),
  'Navigation guard observations are missing');
  return {schema: 'soui.benchmark-host-smoke/v1', passed: true, smokeOnly: true,
    benchmarkValid: false, sharedPageSha256: pageHash, electronVersion: host.versions.electron,
    results, files: files(root).map(relative => ({path: relative, bytes: fs.statSync(path.join(root, relative)).size,
      sha256: sha(path.join(root, relative))}))};
}

try {
  if (process.argv.length !== 3) throw new Error('Usage: node tests/acceptance/verify-benchmark-smoke.cjs ARCHIVE_DIRECTORY');
  console.log(JSON.stringify(verify(path.resolve(process.argv[2])), null, 2));
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
