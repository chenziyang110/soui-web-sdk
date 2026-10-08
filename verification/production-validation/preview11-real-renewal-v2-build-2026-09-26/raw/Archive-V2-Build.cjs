'use strict';
// Read-only npm advisory lookup or byte-preserving evidence archival only.
// Never builds, installs, launches the native fixture/GUI, or changes frozen input.
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto'), cp = require('node:child_process');
const root = __dirname;
const destination = 'C:/SOUI-SDK-development/preview9-tray/verification/production-validation/preview11-real-renewal-v2-build-2026-09-26';
const freezeSha = '80d19d4e2008a505bbb8a9293849f3756d4bf07ad7838ad38aa5bb054ac0ee3a';
const sourceSha = 'a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550';
const parentFreezeSha = '7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d';
const attempts = ['x86-attempt1', 'x64-attempt1'];
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const encode = v => JSON.stringify(v, null, 2) + '\n';
const assert = (condition, message) => { if (!condition) throw Error(message); };
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const local = relative => path.join(root, relative);
function writeNew(file, content) { fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, content, {flag: 'wx'}); }
function writeJson(file, value) { writeNew(file, encode(value)); }
function walk(directory) {
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
    const file = path.join(directory, entry.name); assert(!fs.lstatSync(file).isSymbolicLink(), 'Linked input');
    return entry.isDirectory() ? walk(file) : (assert(entry.isFile(), 'Non-file input'), [file]);
  });
}
function descriptor(file) { const b = fs.readFileSync(file); return {bytes: b.length, sha256: sha(b)}; }
function same(file, expected) { const actual = descriptor(file); assert(actual.bytes === expected.bytes && actual.sha256 === expected.sha256, 'File differs: ' + file); }
function toolchain(attempt) {
  const base = local('attempts/' + attempt + '/toolchain'), expected = read(local('attempts/' + attempt + '/toolchain-input.json'));
  assert(sha(encode(expected.files)) === expected.sha256, 'Toolchain manifest hash differs');
  const names = walk(base).map(file => path.relative(base, file).replaceAll('\\', '/')).sort();
  assert(JSON.stringify(names) === JSON.stringify(expected.files.map(f => f.path)), 'Toolchain file set differs');
  for (const file of expected.files) same(path.join(base, file.path), file);
  return expected;
}
function frozen() {
  const b = fs.readFileSync(local('freeze.json')); assert(sha(b) === freezeSha, 'Compiled freeze changed');
  const value = JSON.parse(b);
  assert(value.status === 'compiled-inputs-frozen' && value.fixtureRevision === 2 && value.sourceInputSha256 === sourceSha &&
    value.parent.freezeSha256 === parentFreezeSha, 'Wrong freeze lineage/status');
  for (const arch of ['x86', 'x64']) {
    const entry = value.runtimes[arch]; assert(entry.buildId === arch + '-attempt1', 'Unexpected attempt');
    assert(sha(fs.readFileSync(entry.buildRecordPath)) === entry.buildRecordSha256, 'Frozen build record changed');
  }
  return value;
}
function audit() {
  frozen(); const auditDir = local('audit-build-20260926');
  assert(!fs.existsSync(auditDir), 'Refuse repeated audit/overwrite');
  const before = Object.fromEntries(attempts.map(a => [a, toolchain(a)]));
  for (const name of ['package.json', 'package-lock.json']) {
    const a = fs.readFileSync(local('attempts/x86-attempt1/toolchain/' + name));
    const b = fs.readFileSync(local('attempts/x64-attempt1/toolchain/' + name));
    assert(a.equals(b), 'Architecture toolchain lock/package differ');
  }
  const pending = read(local('attempts/x86-attempt1/pending.json'));
  for (const key of ['node', 'npmCli']) same(pending.tools[key].path, pending.tools[key]);
  fs.mkdirSync(auditDir);
  const program = pending.tools.node.path, args = [pending.tools.npmCli.path, 'audit', '--json'];
  const cwd = local('attempts/x86-attempt1/toolchain'), startedAt = new Date().toISOString();
  const result = cp.spawnSync(program, args, {cwd, windowsHide: true, encoding: null, timeout: 45000, maxBuffer: 16 * 1024 * 1024});
  const finishedAt = new Date().toISOString(), stdout = result.stdout || Buffer.alloc(0), stderr = result.stderr || Buffer.alloc(0);
  writeNew(path.join(auditDir, 'stdout.json'), stdout); writeNew(path.join(auditDir, 'stderr.log'), stderr);
  let vulnerabilities = null, parseError = null, inputError = null;
  try { vulnerabilities = JSON.parse(stdout.toString('utf8')).metadata?.vulnerabilities ?? null; } catch (error) { parseError = error.message; }
  const after = {};
  try { for (const a of attempts) after[a] = toolchain(a).sha256; } catch (error) { inputError = error.message; }
  const inputsUnchanged = !inputError && attempts.every(a => after[a] === before[a].sha256);
  const record = {schema: 'soui.real-renewal-v2-audit/v1', scope: 'one audit of the byte-identical x86/x64 package and lock',
    program, arguments: args, workingDirectory: cwd, startedAt, finishedAt, pid: result.pid ?? null,
    exitCode: result.status, signal: result.signal, timedOut: result.error?.code === 'ETIMEDOUT', timeoutMs: 45000,
    error: result.error ? {code: result.error.code, message: result.error.message} : null, parseError, vulnerabilities,
    stdout: {bytes: stdout.length, sha256: sha(stdout)}, stderr: {bytes: stderr.length, sha256: sha(stderr)},
    samePackageAndLock: true, package: descriptor(path.join(cwd, 'package.json')), lock: descriptor(path.join(cwd, 'package-lock.json')),
    toolchainBefore: Object.fromEntries(attempts.map(a => [a, before[a].sha256])), toolchainAfter: after, inputsUnchanged, inputError,
    advisoryLookupSucceeded: result.status === 0 && !result.error && vulnerabilities?.total === 0 && inputsUnchanged,
    realRenewalVerified: false, releaseReady: false};
  writeJson(path.join(auditDir, 'result.json'), record);
  console.log(encode({exitCode: record.exitCode, vulnerabilities, inputsUnchanged, advisoryLookupSucceeded: record.advisoryLookupSucceeded}));
  if (!record.advisoryLookupSucceeded) process.exitCode = 1;
}
function captured(name) {
  const base = local('command-logs/' + name), result = read(path.join(base, 'result.json'));
  same(path.join(base, 'stdout.log'), result.stdout); same(path.join(base, 'stderr.log'), result.stderr);
  assert(result.exitCode === 0 && result.timedOut === false && !result.error, 'Outer captured command did not pass: ' + name);
  return result;
}
function status() {
  const freeze = frozen(), archs = {};
  for (const attempt of attempts) {
    const arch = attempt.slice(0, 3), base = local('attempts/' + attempt);
    const steps = fs.readdirSync(path.join(base, 'logs')).filter(n => n.endsWith('.result.json')).sort().map(name => {
      const value = read(path.join(base, 'logs', name));
      assert(value.exitCode === 0 && value.timedOut === false && !value.error, 'Build step failed: ' + name);
      return {name: name.slice(0, -12), exitCode: value.exitCode, timedOut: value.timedOut};
    });
    assert(steps.length === 13, 'Expected all 13 steps including finalize');
    const self = read(path.join(base, 'logs/self-check.stdout.log'));
    assert(self.status === 'self-check-passed' && self.architecture === arch && self.fixtureRevision === 2 &&
      self.records === 8 && self.phaseCases === 45 && self.guiExecuted === false && self.realRenewalVerified === false, 'Native self-check differs');
    const outer = captured(arch + '-build-attempt1');
    assert(outer.args.includes(arch) && outer.args.includes(attempt), 'Outer command architecture/attempt differs');
    archs[arch] = {attempt, steps, selfCheck: self, outerExitCode: outer.exitCode, buildRecordSha256: freeze.runtimes[arch].buildRecordSha256};
  }
  for (const name of ['prepare-verify-attempt2', 'analyzer-self-test-attempt1', 'freeze-attempt1']) captured(name);
  assert(!fs.existsSync(local('command-logs/prepare-verify-attempt1/result.json')), 'Historical missing child result unexpectedly replaced');
  const wrapperFailure = read(local('command-logs/prepare-verify-attempt1/wrapper-failure-observation.json'));
  const results = walk(local('self-test-only')).filter(file => path.basename(file) === 'self-test-result.json');
  assert(results.length === 1, 'Unexpected synthetic suites');
  const synthetic = read(results[0]);
  assert(synthetic.testCount === 106 && synthetic.passed === 106 && synthetic.failed === 0 && synthetic.syntheticOnly === true &&
    synthetic.actualGuiExecuted === false && synthetic.actualClockRenewalVerified === false, 'Synthetic verifier result differs');
  const auditRecord = read(local('audit-build-20260926/result.json'));
  same(local('audit-build-20260926/stdout.json'), auditRecord.stdout); same(local('audit-build-20260926/stderr.log'), auditRecord.stderr);
  const reviewFile = local('independent-build-review.json'), reviewBytes = fs.readFileSync(reviewFile);
  const review = JSON.parse(reviewBytes);
  assert(sha(reviewBytes) === '68283b8fc95e634a75b2111ebbab6e5c22ac554b8a30ee3ae774e9d22e852f74' &&
    review.status === 'build-and-frozen-input-evidence-verified' && review.freeze.sha256 === freezeSha,
    'Independent review identity/status/freeze differs');
  const value = {schema: 'soui.real-renewal-v2-build-execution/v1', createdAt: new Date().toISOString(), version: '0.3.0-preview.11',
    fixtureRevision: 2, status: 'compiled-and-no-gui-checks-complete-real-desktop-pending', sourceInputSha256: sourceSha,
    freezeSha256: freezeSha, parent: freeze.parent, architectures: archs,
    synthetic: {file: path.relative(root, results[0]).replaceAll('\\', '/'), testCount: 106, passed: 106, failed: 0, syntheticOnly: true},
    captureFailure: {path: 'command-logs/prepare-verify-attempt1', childExitCode: null, observation: wrapperFailure,
      explanation: 'The first capture wrapper attempted fsync on a read-only descriptor and failed with EPERM before persisting the child result. Its child exit is not asserted. The retained r+ fix and a distinct second capture have raw numeric exit 0.'},
    audit: {resultPath: 'audit-build-20260926/result.json', exitCode: auditRecord.exitCode,
      vulnerabilities: auditRecord.vulnerabilities, inputsUnchanged: auditRecord.inputsUnchanged,
      advisoryLookupSucceeded: auditRecord.advisoryLookupSucceeded,
      failureReason: read(local('audit-build-20260926/stdout.json')).message ?? null},
    independentReview: {path: 'independent-build-review.json', bytes: reviewBytes.length, sha256: sha(reviewBytes)},
    compiled: true, nativeSelfCheckExecuted: true, syntheticSelfTestExecuted: true,
    v2GuiExecuted: false, harnessSmokeVerified: false, realRenewalVerified: false, resourceGate: false,
    benchmarkValid: false, releaseReady: false,
    limitations: ['v2 GUI/real-clock renewal not executed', 'Host-global vcpkg adds unfrozen library search paths and libraries; closed reproducibility/clean-machine validation is not established', 'Original v1 real x86 smoke remains failed and preserved']};
  writeJson(local('execution-status.json'), value); return value;
}
function selection() {
  const names = new Set();
  const add = relative => { assert(fs.statSync(local(relative)).isFile(), 'Missing archive input: ' + relative); names.add(relative.replaceAll('\\', '/')); };
  const tree = relative => { for (const file of walk(local(relative))) add(path.relative(root, file)); };
  for (const name of ['README.md', 'PROTOCOL.md', 'Prepare.cjs', 'Build-Web.mjs', 'Build.ps1', 'Run.ps1', 'Verify-Run.cjs',
    'SelfTest-Verify-Run.cjs', 'Capture-Command.cjs', 'Clone-V1.cjs', 'Archive-V2-Build.cjs', 'clone-provenance.json',
    'prepared.json', 'preparation-status.json', 'freeze.json', 'independent-static-review.json', 'independent-build-review.json', 'execution-status.json']) add(name);
  for (const folder of ['consumer', 'review-history', 'capture-wrapper-history', 'command-logs', 'audit-build-20260926',
    'provenance/parent-drivers', 'provenance/parent-protocol', 'provenance/failed-x86-smoke']) tree(folder);
  add('provenance/parent-prepared.json');
  // Parent 3.4MB freeze is already preserved in v1 repository evidence and its
  // exact identity is bound by prepared/clone/execution records; avoid duplication.
  for (const name of ['package.json', 'package-lock.json']) add('toolchain/' + name);
  for (const attempt of attempts) {
    const base = 'attempts/' + attempt;
    for (const name of ['pending.json', 'toolchain-input.json', 'web-build.json', 'runtime-input.json', 'build.json',
      'web-typecheck.tsconfig.json', 'vite-index-before-classic.html']) add(base + '/' + name);
    for (const folder of ['logs', 'consumer-source', 'drivers', 'protocol']) tree(base + '/' + folder);
    for (const name of ['package.json', 'package-lock.json']) add(base + '/toolchain/' + name);
    for (const file of walk(local(base + '/native-build'))) {
      const name = path.basename(file);
      if (name === 'CMakeCache.txt' || name === 'CMakeConfigureLog.yaml' || /\.(command|read)\.[0-9]+\.tlog$/i.test(name) || /\.(rsp|vcxproj|props)$/i.test(name)) add(path.relative(root, file));
    }
  }
  for (const file of walk(local('self-test-only'))) if (path.basename(file) === 'self-test-result.json') add(path.relative(root, file));
  for (const name of names) assert(!/(^|\/)(node_modules|runtime|web-output|inputs)(\/|$)/.test(name) && !/\.(exe|dll|lib|obj|pdb|zip)$/i.test(name), 'Binary or repeated SDK input selected');
  return [...names].sort();
}
function runnerEvidence() {
  const base = 'C:/SOUI-SDK-production-validation/runner-binding-self-test-only/revision2-parent-binding-20260926-attempt1';
  const summaryBytes = fs.readFileSync(path.join(base, 'summary.json'));
  assert(sha(summaryBytes) === '996d11a8745a593b6c6038ce14437355fa0f0fc81f1592f465cbd122bd8ddb0f', 'Runner checker summary changed');
  const summary = JSON.parse(summaryBytes);
  assert(summary.status === 'passed-non-gui-checker-only' && summary.caseCount === 8 && summary.passedCases === 8 &&
    summary.actualInputPositives === 2 && summary.syntheticNegativeRejections === 6 && summary.freezeSha256 === freezeSha &&
    summary.guiExecuted === false && summary.nativeExecuted === false && summary.networkExecuted === false &&
    summary.productionIdentitiesUnchanged === true, 'Runner checker scope/status differs');
  assert(sha(fs.readFileSync(local('Run.ps1'))) === summary.runSha256, 'Runner source differs from checker extraction');
  assert(sha(fs.readFileSync(path.join(base, 'check-inputs.cjs'))) === summary.checkerSha256, 'Extracted checker changed');
  const extraction = read(path.join(base, 'extraction.json')), production = fs.readFileSync(local('Run.ps1'));
  assert(sha(production.subarray(extraction.byteStart, extraction.byteEndExclusive)) === summary.checkerSha256, 'Actual embedded checker bytes differ');
  const names = ['summary.json', 'extraction.json', 'check-inputs.cjs', 'production-Run.ps1', 'Test-RunnerBinding.cjs'];
  for (const item of summary.productionIdentitiesBefore) same(item.path, item);
  assert(JSON.stringify(summary.productionIdentitiesBefore) === JSON.stringify(summary.productionIdentitiesAfter), 'Runner test production identities changed');
  for (const item of summary.cases) {
    const prefix = 'cases/' + item.name, record = read(path.join(base, prefix, 'result.json'));
    assert(record.passed === true && record.exitCode === item.exitCode && record.guiExecuted === false &&
      record.nativeExecuted === false && !record.error && !record.validationError && record.productionCheckerSha256 === summary.checkerSha256,
      'Runner case record differs');
    same(path.join(base, prefix, 'stdout.log'), record.stdout); same(path.join(base, prefix, 'stderr.log'), record.stderr);
    names.push(prefix + '/result.json', prefix + '/stdout.log', prefix + '/stderr.log');
    if (record.inputKind === 'actual-v2-frozen-inputs') {
      assert(record.exitCode === 0 && record.expectedRejection === null, 'Real-input checker positive failed');
      names.push(prefix + '/output/identity-before.json');
    } else {
      assert(record.inputKind === 'synthetic-mutated-freeze-or-build-record' && record.exitCode === 1 &&
        typeof record.expectedRejection === 'string' && fs.readFileSync(path.join(base, prefix, 'stderr.log'), 'utf8').includes(record.expectedRejection),
        'Synthetic checker negative was not rejected by the expected gate');
    }
  }
  return names.map(name => ({path: 'runner-binding-checks/' + name, source: path.join(base, name), ...descriptor(path.join(base, name))}));
}
function archive() {
  assert(!fs.existsSync(destination), 'Refuse existing archive directory');
  const state = fs.existsSync(local('execution-status.json')) ? read(local('execution-status.json')) : status();
  frozen(); assert(state.freezeSha256 === freezeSha && state.v2GuiExecuted === false, 'Execution summary differs');
  same(local('independent-build-review.json'), state.independentReview);
  const names = selection(), items = [...names.map(name => ({path: name, source: local(name), ...descriptor(local(name))})),
    ...runnerEvidence()].sort((a,b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
  fs.mkdirSync(destination, {recursive: false});
  for (const item of items) {
    const to = path.join(destination, 'raw', item.path); fs.mkdirSync(path.dirname(to), {recursive: true});
    fs.copyFileSync(item.source, to, fs.constants.COPYFILE_EXCL); same(to, item); same(item.source, item);
  }
  for (const item of items) same(item.source, item);
  const manifest = {schema: 'soui.production-evidence-raw-archive/v1', archivedAt: new Date().toISOString(), sourceRoot: root,
    sourceInputSha256: sourceSha, freezeSha256: freezeSha, files: items.map(item => ({path: item.path, sourcePath: item.source,
      archivedPath: 'raw/' + item.path, bytes: item.bytes, sha256: item.sha256})),
    fileCount: items.length, bytes: items.reduce((n, item) => n + item.bytes, 0), originalBytesPreserved: true,
    exclusions: ['Runtime binaries/Web output, node_modules, 937 immutable SDK inputs, and synthetic 8h event streams are not duplicated',
      'Parent compiled freeze is preserved in the v1 build archive; exact hash is bound in this archive'], realRenewalVerified: false, releaseReady: false};
  writeJson(path.join(destination, 'raw-manifest.json'), manifest);
  const text = `# preview.11 续期夹具 v2：编译与无 GUI 验证证据\n\n本目录归档外部 v2 的真实 x86/x64 构建：每架构 **13 个步骤均原始退出 0**，外层 Build 也由 Capture-Command 保存完整 stdout/stderr 和数值退出码 0。两架构 native self-check 均确认 records=8、fixtureRevision=2、phaseCases=45。分析器 **106 项合成用例通过**，只证明解析器测试行为。\n\n**v2 GUI 尚未执行**；没有短 smoke、35 分钟、8 小时、Win7、资源、性能或生产通过结论。原 v1 实际 x86 smoke 退出 1、ready/任务为 0 的失败仍原样保留，v2 编译不抹去它。\n\n## 失败与修正原样保留\n\n第一轮 prepare-verify 的 capture 包装器对只读文件描述符 fsync，发生 EPERM，未写出 child result；保留该目录的原始 stdout/stderr、wrapper-failure-observation 和旧 Capture 源码。**不能从 stdout 倒推出第一轮 child 退出码 0。** 随后将描述符改为 r+ 并保留错误记录逻辑，独立 prepare-verify-attempt2 才有实际数值退出码 0。\n\nreview-history 保存 runner/Prepare 绑定修订前源码；最终两架构 build/protocol/driver manifests 绑定实际使用的版本。历史 preparation-status 和 frozen PROTOCOL 中的“当时未编译”描述是原准备时点记录；当前执行状态以 raw/execution-status.json 和相应 raw receipt 为准，不改历史冻结文件。\n\n## 审计与限制\n\n先核对两架构 package.json/package-lock.json 原字节一致，并完整检查两个安装工具链，随后仅在 x86 相同锁输入下执行一次 npm audit --json。实际退出码 ${state.audit.exitCode}，漏洞总数 ${state.audit.vulnerabilities?.total ?? '不可用'}；两架构工具链前后未变 ${state.audit.inputsUnchanged}。完整参数、时间、原字节 stdout/stderr 与退出码在 raw/audit-build-20260926。审计结果不代表生产或许可审核。\n\n实际链接仍受到宿主全局 vcpkg 集成影响，F:\\GitHub\\vcpkg\\installed\\<arch>-windows\\lib 路径和附加库未冻结。两架构 link.command.1.tlog、工程与 CMake 配置已归档；**不能声称封闭可复现或干净机通过**。\n\n## 文件身份\n\n- v2 freeze SHA-256：\`${freezeSha}\`。\n- SDK 源码输入 SHA-256：\`${sourceSha}\`，版本 0.3.0-preview.11，SDK 内容未变。\n- 父 v1 freeze SHA-256：\`${parentFreezeSha}\`；完整父冻结位于先前 preview11-real-renewal-build-2026-09-26 编译归档，当前保留身份与必要父记录。\n- raw/ 包含最终夹具源码（含两个 header）、脚本/协议、冻结与准备清单、历史修订、每架构所有步骤（包括 finalize）和外层捕获原日志、CMake/link 命令、106 项合成报告及独立评审。\n- raw-manifest.json 对每个复制文件记录长度和 SHA-256；Archive-V2-Build.cjs 拒绝覆盖、逐项复制后再次核对源与目标原字节。未复制二进制、node_modules、完整 SDK 输入或模拟长测大流。\n\n后续必须通过桌面互斥与外部 QEMU 门禁，串行执行 v2 真实 smoke，再视结果安排实际 35 分钟/8 小时验证。所有 realRenewalVerified/resourceGate/benchmarkValid/releaseReady 保持 false。\n`;
  const auditFailureNote = state.audit.advisoryLookupSucceeded ? '' :
    '\n本轮依赖审计未通过：npm 服务在 TLS 连接建立前断开，原始退出码为 1，漏洞统计不可用。没有重试，也没有把网络失败计为 0 漏洞或审计通过。\n';
  const runnerNote = '\n生产 Run.ps1 内嵌 checker 的独立检查为 8/8：两个正例只读检查实际 v2 冻结输入，六个负例使用重算散列的合成篡改记录并被预期门禁拒绝。raw/runner-binding-checks 保留精确抽取脚本、身份、各例原退出码/日志；未执行原生应用或 GUI，不能作为 app smoke 或续期通过。\n';
  writeNew(path.join(destination, 'README.md'), text + auditFailureNote + runnerNote);
  console.log(encode({archivePath: destination, rawFileCount: items.length, totalFileCount: items.length + 2,
    rawBytes: manifest.bytes, rawManifestSha256: sha(fs.readFileSync(path.join(destination, 'raw-manifest.json'))), freezeSha256: freezeSha}));
}
assert(process.argv.length === 3 && ['audit', 'status', 'archive'].includes(process.argv[2]), 'Usage: node Archive-V2-Build.cjs audit|status|archive');
if (process.argv[2] === 'audit') audit(); else if (process.argv[2] === 'status') status(); else archive();
