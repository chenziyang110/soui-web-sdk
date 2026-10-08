'use strict';
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const root = __dirname, repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const parent = 'C:/SOUI-SDK-production-validation/preview11-renewal-supervisor-race-20260926';
const buildRoot = 'C:/SOUI-SDK-production-validation/preview14-scheduler-dependencies-x64-20260926';
const id = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
const write = (relative, bytes) => { const dest = path.join(root, relative); fs.mkdirSync(path.dirname(dest), { recursive: true }); fs.writeFileSync(dest, bytes, { flag: 'wx' }); };
assert(!fs.existsSync(path.join(root, 'freeze.json')));
const buildResult = id.readJson(path.join(buildRoot, 'dependency-build-result.json'));
assert.equal(buildResult.exitCode, 0); assert.equal(buildResult.sourceUnchanged, true);
const capture = id.readJson(path.join(buildRoot, 'command-logs/dependencies-x64-attempt1/result.json'));
assert.equal(capture.exitCode, 0); assert.equal(capture.timedOut, false); assert.equal(capture.error, null);
const input = id.readJson(path.join(buildRoot, 'dependency-build-input.json'));
assert.equal(input.architecture, 'x64'); assert.equal(input.component, 'All');
assert.equal(input.input.sha256, '012bfc5c482f59dc14452a76cc13e7536662950e218ada4e888ad475a8d7d652');
assert.equal(id.snapshot(repo, input.input.files.map(x => x.path), true).sha256, input.input.sha256);
const reportFile = path.join(repo, 'out/dependencies/x64/build-All.json'), report = id.readJson(reportFile);
assert.equal(report.architecture, 'x64'); assert.equal(report.component, 'All'); assert.equal(report.status, 'built');
const built = report.artifacts.filter(a => a.kind === 'miniblink'); assert.equal(built.length, 1);
const dllPath = path.resolve(built[0].path);
assert.equal(dllPath.toLowerCase(), path.resolve(repo, 'out/dependencies/x64/miniblink/bin/miniblink.dll').toLowerCase());
const dll = fs.readFileSync(dllPath); assert.equal(id.hash(dll), built[0].sha256.toLowerCase());
assert.equal(dll.readUInt16LE(0), 0x5a4d); const pe = dll.readUInt32LE(0x3c);
assert.equal(dll.readUInt32LE(pe), 0x00004550); assert.equal(dll.readUInt16LE(pe + 4), 0x8664);
const oldRaw = fs.readFileSync(path.join(parent, 'freeze.json'));
assert.equal(id.hash(oldRaw), 'b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef');
const base = JSON.parse(oldRaw), old = base.runtimes.x64;
for (const f of base.controls) { const b = fs.readFileSync(path.join(parent, f.path)); assert.equal(b.length, f.bytes); assert.equal(id.hash(b), f.sha256); }
assert.deepEqual(id.snapshot(old.runtime, id.walk(old.runtime)).files, old.payload.files);
assert.notEqual(id.hash(dll), old.payload.files.find(f => f.path === 'node.dll').sha256);
for (const f of old.payload.files) write('runtimes/x64/' + f.path, f.path === 'node.dll' ? dll : fs.readFileSync(path.join(old.runtime, f.path)));
const runtime = path.join(root, 'runtimes/x64'), payload = id.snapshot(runtime, id.walk(runtime));
assert.equal(payload.files.length, 70);
assert.deepEqual(payload.files.filter(f => f.sha256 !== old.payload.files.find(p => p.path === f.path).sha256).map(f => f.path), ['node.dll']);
const html = fs.readFileSync(path.join(runtime, 'resources/index.html'), 'utf8');
assert.equal(html.split('animation:turn 1s linear infinite;').length, 2);
let run = fs.readFileSync(path.join(parent, 'Run.cjs'), 'utf8');
function replace(from, to) { assert.equal(run.split(from).length, 2, from); run = run.replace(from, to); }
replace("assert(['x86','x64'].includes(arch));", "assert.equal(arch,'x64');");
replace("assert.equal(f.schema,'soui.renewal-supervisor-clock/v1');", "assert.equal(f.schema,'soui.renewal-animated-kernel-candidate/v1');");
const start = run.indexOf(' const htmlFile='), end = run.indexOf(' const second=gate();');
assert(start > 0 && end > start);
run = run.slice(0, start) + " const payload=snapshot(runtime);write(path.join(run,'payload-before.json'),encode(payload));\n" + run.slice(end);
assert.equal(run.split('animationOffDiagnosticOnly:true').length, 4);
run = run.replaceAll('animationOffDiagnosticOnly:true', 'animatedKernelCandidate:true');
replace('qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests', 'qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb');
assert(!run.includes('animation:none;')); assert(run.includes("'--duration-ms','30000'"));
write('Run.cjs', run);
write('Capture-Command.cjs', fs.readFileSync(path.join(parent, 'Capture-Command.cjs')));
write('inputs/build-All.json', fs.readFileSync(reportFile));
for (const name of ['dependency-build-input.json', 'dependency-build-result.json']) write('inputs/' + name, fs.readFileSync(path.join(buildRoot, name)));
for (const name of ['stdout.log', 'stderr.log', 'result.json']) write('inputs/command/' + name, fs.readFileSync(path.join(buildRoot, 'command-logs/dependencies-x64-attempt1', name)));
const controls = ['Prepare.cjs', 'Run.cjs', 'Review.cjs', 'Capture-Command.cjs', ...id.walk(path.join(root, 'inputs')).map(n => 'inputs/' + n)].map(file => {
  const b = fs.readFileSync(path.join(root, file)); return { path: file, bytes: b.length, sha256: id.hash(b) };
});
const freeze = {
  schema: 'soui.renewal-animated-kernel-candidate/v1', parentFreezeSha256: id.hash(oldRaw),
  consumerSdkVersion: base.version, supervisorClockRevision: 1,
  kernelSourceCandidate: 'scheduler-fairness-unpublished', kernelBuildSourceSha256: input.input.sha256,
  runtimeDifferenceFromParent: ['node.dll'], runtimes: { x64: { runtime, payload } }, controls,
  nativeDurationMs: 30000, originalAnimationUnchanged: true, debuggerAttached: false,
  currentSdkPackageValidated: false, realRenewalVerified: false, productionPass: false
};
write('freeze.json', id.encoded(freeze));
console.log(id.encoded({ freezeSha256: id.hash(fs.readFileSync(path.join(root, 'freeze.json'))), kernelSha256: id.hash(dll), files: payload.files.length, guiExecuted: false }));
