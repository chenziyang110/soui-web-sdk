'use strict';
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict'), cp = require('node:child_process');
const root = __dirname, repo = 'C:/SOUI-SDK-development/preview14-scheduler', deps = path.join(repo, 'out/dependencies/x64');
const animated = 'C:/SOUI-SDK-production-validation/preview14-renewal-animation-candidate-x64-20260926';
const id = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
const terminal = id.readJson(path.join(root, 'dependency-build-result.json'));
assert.equal(terminal.exitCode, 0); assert.equal(terminal.sourceUnchanged, true);
const input = id.readJson(path.join(root, 'dependency-build-input.json'));
assert.equal(id.snapshot(repo, input.input.files.map(f => f.path), true).sha256, input.input.sha256);
const commandBase = path.join(root, 'command-logs/dependencies-x64-attempt1'), command = id.readJson(path.join(commandBase, 'result.json'));
assert.equal(command.exitCode, 0); assert.equal(command.timedOut, false); assert.equal(command.error, null);
for (const stream of ['stdout', 'stderr']) { const b = fs.readFileSync(path.join(commandBase, stream + '.log')); assert.equal(b.length, command[stream].bytes); assert.equal(id.hash(b), command[stream].sha256); }
const build = id.readJson(path.join(deps, 'build-All.json'));
assert.equal(build.architecture, 'x64'); assert.equal(build.component, 'All'); assert.equal(build.status, 'built');
assert.equal(build.artifacts.length, 6);
const artifacts = build.artifacts.map(f => {
  const b = fs.readFileSync(f.path); assert.equal(id.hash(b), f.sha256.toLowerCase());
  assert.equal(b.readUInt16LE(0), 0x5a4d); const pe = b.readUInt32LE(0x3c); assert.equal(b.readUInt32LE(pe), 0x00004550); assert.equal(b.readUInt16LE(pe + 4), 0x8664);
  return { kind: f.kind, path: f.path, bytes: b.length, sha256: id.hash(b), peMachine: '0x8664' };
});
const logFiles = fs.readdirSync(deps).filter(n => n.endsWith('.log'));
assert.equal(logFiles.length, 18);
const decode = b => b[0] === 255 && b[1] === 254 ? b.toString('utf16le') : b.toString('utf8');
const warningCounts = {};
for (const name of logFiles) {
  const log = decode(fs.readFileSync(path.join(deps, name)));
  assert(!/warning C48(?:19|28)/.test(log), 'Source encoding warning: ' + name);
  for (const match of log.matchAll(/warning (C\d+|LNK\d+|MSB\d+)/g)) warningCounts[match[1]] = (warningCounts[match[1]] || 0) + 1;
}
const filesUnderBuild = id.walk(deps), commands = filesUnderBuild.filter(n => /(?:CL|link|Lib)\.command\.1\.tlog$/i.test(n));
assert(commands.some(n => /miniblink.*link\.command/i.test(n)));
assert(commands.some(n => /miniblink.*CL\.command/i.test(n)));
for (const name of commands) assert(!/[\\/]vcpkg[\\/]/i.test(decode(fs.readFileSync(path.join(deps, name)))), 'Ambient vcpkg path: ' + name);
const fixed = id.readJson(path.join(repo, 'third_party/manifests/dependencies.json'));
for (const file of fixed.nativeFiles) { const b = fs.readFileSync(path.join(repo, file.path)); assert.equal(b.length, file.bytes); assert.equal(id.hash(b), file.sha256); }
const animationPending = true;
const review = {
  reviewedAt: new Date().toISOString(), sourceCommit: cp.execFileSync('git', ['-C', repo, 'rev-parse', 'HEAD'], { encoding: 'utf8', windowsHide: true }).trim(),
  architecture: 'x64', component: 'All', originalBuildExitCode: command.exitCode,
  startedAt: command.startedAt, finishedAt: command.finishedAt, sourceInputSha256: input.input.sha256,
  sourceFiles: input.input.files.length, sourceUnchanged: true, artifacts,
  buildLogCount: logFiles.length, compilerLinkCommandFiles: commands.length, ambientVcpkgPathsFound: false,
  encodingWarningsFound: false, warningCounts, warningsRiskReviewComplete: false,
  fixedPrebuiltUnchanged: true, animationPending,
  applicationSmokeVerified: false, actualAnimationVerified: false, currentSdkPackageValidated: false, productionPass: false
};
fs.writeFileSync(path.join(root, 'dependency-review.json'), id.encoded(review), { flag: 'wx' });
const target = path.join(repo, 'verification/production-validation/scheduler-fairness-x64-rebuild-2026-09-27'); assert(!fs.existsSync(target)); const files = [];
function copy(original, relative) {
  const b = fs.readFileSync(original), dest = path.join(target, relative); fs.mkdirSync(path.dirname(dest), { recursive: true }); fs.writeFileSync(dest, b, { flag: 'wx' });
  files.push({ path: relative, original, bytes: b.length, sha256: id.hash(b) });
}
for (const name of ['Prepare-Archive.cjs', 'Archive-Dependencies.cjs', 'Start-Dependencies.cjs', 'Capture-Dependencies.cjs', 'Capture-Command.cjs', 'dependency-build-input.json', 'dependency-build-running.json', 'dependency-build-result.json', 'dependency-review.json']) copy(path.join(root, name), 'raw/driver/' + name);
for (const name of id.walk(commandBase)) copy(path.join(commandBase, name), 'raw/driver/command/' + name);
for (const name of ['build-All.json', ...logFiles, ...commands, ...filesUnderBuild.filter(n => /gpu-[^/]+\/(build-gpu\.cmd|gpu-sources\.rsp)$/.test(n))]) copy(path.join(deps, name), 'raw/build/' + name);
for (const name of id.walk(animated, '', (_, leaf) => ['runtimes', 'runtime'].includes(leaf))) copy(path.join(animated, name), 'raw/animated/' + name);
files.sort((a, b) => a.path.localeCompare(b.path));
fs.writeFileSync(path.join(target, 'raw-manifest.json'), id.encoded({ schema: 'soui.raw-evidence/v1', files }), { flag: 'wx' });
console.log(id.encoded({ files: files.length, bytes: files.reduce((sum, f) => sum + f.bytes, 0), review }));
