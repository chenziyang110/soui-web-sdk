'use strict';
// JSON-compatible YAML needs no downloaded parser or validation dependency.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
let spawnAttempts = 0;
const savedSpawn = cp.spawnSync;
cp.spawnSync = () => { spawnAttempts++; throw new Error('Static planning must never spawn'); };
const { buildPlan, MATRIX, parseArgs, WEBVIEW2, isolatedEnvironment } = require('./platform-build.cjs');
const file = path.resolve(__dirname, '../../.github/workflows/platform-build.yml');
const workflow = JSON.parse(fs.readFileSync(file, 'utf8'));
const expression = text => '$' + '{{ ' + text + ' }}';
const pushBranch = 'codex/portable-runtime-platform-matrix-20261020';

function checkWorkflow(w) {
  assert.deepEqual(Object.keys(w.on).sort(), ['push', 'workflow_dispatch']);
  assert.deepEqual(w.on.push, { branches: [pushBranch] });
  assert.deepEqual(w.on.workflow_dispatch, {});
  assert.deepEqual(w.permissions, { contents: 'read' });
  assert.equal(w.concurrency['cancel-in-progress'], true);
  assert.equal(w.concurrency.group, 'soui-platform-' + expression('github.workflow') + '-' + expression('github.ref'));
  assert.deepEqual(Object.keys(w.jobs), ['native-build']);
  const job = w.jobs['native-build'];
  assert.equal(job.if, expression('github.event.repository.private == false'));
  assert.equal(job['runs-on'], expression('matrix.runner'));
  assert.equal(job['timeout-minutes'], 45);
  assert.equal(job.strategy['max-parallel'], 2);
  assert.equal(job.strategy['fail-fast'], false);
  assert.equal(job.permissions, undefined);
  assert.deepEqual(job.strategy.matrix.include.map(r => [r.runner, r.platform, r.arch]), MATRIX);
  const actions = job.steps.filter(s => s.uses);
  assert.deepEqual(actions.map(s => s.uses), [
    'actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1',
    'actions/setup-node@949feb2413d6458794dcd2491c4babbbce0c15c1'
  ]);
  assert.equal(actions[0].with['persist-credentials'], false);
  assert.equal(actions[0].with.ref, expression('github.sha'));
  assert.equal(actions[1].with['node-version'], '24.19.0');
  assert.equal(actions[1].with.architecture, expression('matrix.arch'));
  assert.equal(actions[1].with['package-manager-cache'], false);
  const text = JSON.stringify(w);
  assert(!/secrets[.]|pull_request_target|upload-artifact|actions\/cache|self-hosted/.test(text));
  assert(job.steps.some(s => s.run === 'node tools/ci/validate-platform-workflow.cjs'));
  const apt = job.steps.find(s => s.env?.UBUNTU_ARCHIVE);
  assert.equal(apt.if, expression("matrix.platform == 'linux'"));
  for (const pinned of ['libwebkit2gtk-4.1-dev=2.52.6-0ubuntu0.24.04.1',
    'libgtk-3-dev=3.24.41-4ubuntu1.3', 'pkg-config=1.8.1-2build1']) assert(apt.run.includes(pinned));
  assert(apt.run.includes('Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg'));
  assert(!/allow-unauthenticated|trusted=yes|Verify-Peer=false|allow-downgrades/.test(apt.run));
}
function checkPlan(plan) {
  const native = plan.platform !== 'windows';
  for (const property of ['desktopApplicationExecutionPlanned', 'nativeRuntimeVerified', 'releaseReady', 'artifactsUploaded'])
    assert.equal(plan[property], false);
  assert.equal(plan.runtimeNoGuiTestsPlanned, native);
  const runs = plan.steps.filter(s => s.tool === 'ctest');
  assert.equal(runs.length, native ? 2 : 0);
  if (native) assert.deepEqual(runs.map(s => s.args[s.args.indexOf('-R') + 1]),
    ['^sdk[.]portable[.]primitives$', '^ci[.]runtime-installed$']);
  for (const run of runs) {
    assert(run.args.includes('--no-tests=error'));
    assert(run.args.includes('--fixture-exclude-any'));
    assert.equal(run.args[run.args.indexOf('--timeout') + 1], '30');
  }
  for (const step of plan.steps) {
    assert(['cmake', 'ctest', process.execPath].includes(step.tool));
    assert(step.timeout > 0 && step.timeout <= 600000);
    if (step.tool === process.execPath) assert(/[/](create|stage|verify|archive)-desktop-app[.]cjs$/.test(step.args[0]));
    if (step.tool === 'cmake') assert(!step.args.includes('-P'));
  }
  assert(plan.steps.find(s => s.name === 'sdk-configure').args.includes('-DSOUI_BUILD_TESTS=' + (native ? 'ON' : 'OFF')));
  assert(plan.steps.find(s => s.name === 'installed-consumers-configure').args.includes('-DCI_RUN_RUNTIME_TEST=' + (native ? 'ON' : 'OFF')));
  assert(plan.steps.find(s => s.name === 'installed-consumers-build').args.includes('ci_installed_headers'));
  assert(plan.steps.some(s => s.name === 'expanded-verify'));
  assert(plan.steps.every(s => !s.args.includes('ALL_BUILD')));
}
// Configuration model only, not evidence that GitHub has dispatched a run.
function selectedMatrix(w, event) {
  checkWorkflow(w);
  if (event.repositoryPrivate !== false) return [];
  const automatic = event.name === 'push' && event.ref === 'refs/heads/' + pushBranch;
  const manual = event.name === 'workflow_dispatch' && event.workflowOnDefaultBranch === true;
  return automatic || manual ? w.jobs['native-build'].strategy.matrix.include : [];
}
checkWorkflow(workflow);
const exactPush = { name: 'push', ref: 'refs/heads/' + pushBranch, repositoryPrivate: false };
assert.equal(selectedMatrix(workflow, { ...exactPush, workflowOnDefaultBranch: false }).length, 6);
assert.equal(selectedMatrix(workflow, { ...exactPush, workflowOnDefaultBranch: true }).length, 6);
for (const event of [
  { ...exactPush, ref: 'refs/heads/main' },
  { ...exactPush, ref: exactPush.ref + '-other' },
  { ...exactPush, ref: 'refs/tags/' + pushBranch },
  { ...exactPush, name: 'pull_request' },
  { ...exactPush, repositoryPrivate: true },
  { ...exactPush, name: 'workflow_dispatch', workflowOnDefaultBranch: false }
]) assert.equal(selectedMatrix(workflow, event).length, 0);
assert.equal(selectedMatrix(workflow, { ...exactPush, name: 'workflow_dispatch', workflowOnDefaultBranch: true }).length, 6);
const plans = MATRIX.map(([runner, platform, arch]) => {
  const plan = buildPlan({ platform, arch, root: path.resolve(__dirname, '../..'),
    work: path.resolve(require('node:os').tmpdir(), 'soui-static-plan', runner) });
  checkPlan(plan);
  return { runner, platform, arch, steps: plan.steps.length,
    ctestSteps: plan.steps.filter(s => s.tool === 'ctest').length, applicationExecution: false };
});
const copy = () => JSON.parse(JSON.stringify(workflow));
let negativeControls = 0;
for (const mutate of [
  w => { w.permissions.contents = 'write'; },
  w => { w.on.pull_request_target = {}; },
  w => { w.on.pull_request = {}; },
  w => { w.on.push.branches[0] = 'main'; },
  w => { w.on.push.branches.push('codex/**'); },
  w => { w.on.push.tags = ['*']; },
  w => { w.jobs['native-build'].steps[0].with.ref = expression('github.event.pull_request.head.sha || github.sha'); },
  w => { w.jobs['native-build'].strategy.matrix.include[0].runner = 'self-hosted'; },
  w => { w.jobs['native-build']['timeout-minutes'] = 360; },
  w => { w.jobs['native-build'].steps[0].with['persist-credentials'] = true; }
]) {
  const bad = copy(); mutate(bad); assert.throws(() => checkWorkflow(bad)); negativeControls++;
}
const badWindows = buildPlan({ platform: 'windows', arch: 'arm64', work: path.resolve(require('node:os').tmpdir(), 'soui-static-negative') });
badWindows.steps.push({ name: 'unexpected-test', tool: 'ctest', args: [], timeout: 1000 });
assert.throws(() => checkPlan(badWindows)); negativeControls++;
assert.throws(() => buildPlan({ platform: 'linux', arch: 'x86', work: path.resolve('unused') })); negativeControls++;
assert.throws(() => parseArgs(['--plan', '--platform', 'linux', '--platform', 'windows'])); negativeControls++;
const ambientPaths = ['CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'OBJC_INCLUDE_PATH',
  'INCLUDE', 'LIB', 'LIBPATH', 'LIBRARY_PATH', 'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR',
  'CMAKE_PREFIX_PATH', 'VCPKG_ROOT', 'NODE_OPTIONS', 'cpath'];
const injected = Object.fromEntries(ambientPaths.map(key => [key, '/unreviewed/source-fallback']));
const parentEnv = { ...injected, PATH: '/native/tools', SDKROOT: '/native/sdk' };
const parentBefore = { ...parentEnv };
const isolated = isolatedEnvironment(parentEnv, { VcpkgEnabled: 'false' });
for (const key of ambientPaths) assert(!Object.hasOwn(isolated, key));
assert.equal(isolated.PATH, '/native/tools');
assert.equal(isolated.SDKROOT, '/native/sdk');
assert.equal(isolated.VcpkgEnabled, 'false');
assert.equal(isolated.MSBUILDDISABLENODEREUSE, '1');
assert.deepEqual(parentEnv, parentBefore);
assert.equal(spawnAttempts, 0);
assert.match(WEBVIEW2.sha256, /^[a-f0-9]{64}$/);
cp.spawnSync = savedSpawn;
console.log(JSON.stringify({ result: 'passed', staticOnly: true, processesSpawnedByPlanning: spawnAttempts,
  workflowParsedAs: 'JSON-compatible YAML', matrix: plans, negativeControls,
  triggerModel: { modelOnly: true, automaticEvent: 'push', exactBranch: pushBranch,
    matchingPushWorkflowRuns: 1, matchingPushMatrixJobs: 6, otherBranchMatrixJobs: 0,
    pullRequestMatrixJobs: 0, privatePushMatrixJobs: 0, dispatchWithoutDefaultWorkflowMatrixJobs: 0 },
  ambientSearchPathsRejected: ambientPaths.length,
  projectConfigureBuildRuntimeExecuted: false }, null, 2));
