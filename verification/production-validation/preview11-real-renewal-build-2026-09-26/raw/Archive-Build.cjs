'use strict';
// Only npm audit (read-only dependency advisory lookup) or immutable evidence
// archival. Never launches a GUI, compiles, installs, or edits frozen inputs.
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto'), cp = require('node:child_process');
const root = __dirname;
const outputRoot = 'C:/SOUI-SDK-development/preview9-tray/verification/production-validation/preview11-real-renewal-build-2026-09-26';
const expectedFreeze = '7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d';
const expectedSource = 'a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550';
const successAttempts = ['x86-attempt4', 'x64-attempt1'];
const attempts = ['x86-attempt1', 'x86-attempt2', 'x86-attempt3', ...successAttempts];
const auditRoot = path.join(root, 'audit-build-20260926');
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const json = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const assert = (condition, message) => { if (!condition) throw Error(message); };
function writeNew(file, bytes) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, bytes, { flag: 'wx' }); }
function writeJson(file, value) { writeNew(file, JSON.stringify(value, null, 2) + '\n'); }
function walk(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const file = path.join(directory, entry.name);
    assert(!fs.lstatSync(file).isSymbolicLink(), 'Linked evidence input: ' + file);
    return entry.isDirectory() ? walk(file) : (assert(entry.isFile(), 'Non-file input'), [file]);
  });
}
function sameFile(file, expected) {
  const bytes = fs.readFileSync(file);
  assert(bytes.length === expected.bytes && sha(bytes) === expected.sha256, 'Frozen file differs: ' + file);
}
function verifyToolchain(attempt) {
  const dir = path.join(root, 'attempts', attempt), descriptor = json(path.join(dir, 'toolchain-input.json'));
  const files = walk(path.join(dir, 'toolchain')).map(file => path.relative(path.join(dir, 'toolchain'), file).replaceAll('\\', '/')).sort();
  assert(JSON.stringify(files) === JSON.stringify(descriptor.files.map(file => file.path).sort()), 'Toolchain path set differs');
  for (const file of descriptor.files) sameFile(path.join(dir, 'toolchain', file.path), file);
  return descriptor.sha256;
}
function freeze() {
  const file = path.join(root, 'freeze.json'), bytes = fs.readFileSync(file);
  assert(sha(bytes) === expectedFreeze, 'Final freeze identity differs');
  const value = JSON.parse(bytes);
  assert(value.status === 'compiled-inputs-frozen' && value.sourceInputSha256 === expectedSource, 'Freeze source/status differs');
  for (const attempt of successAttempts) {
    const arch = attempt.startsWith('x86') ? 'x86' : 'x64', entry = value.runtimes[arch];
    assert(entry.buildId === attempt, 'Wrong successful attempt in freeze');
    assert(sha(fs.readFileSync(entry.buildRecordPath)) === entry.buildRecordSha256, 'Frozen build receipt differs');
  }
  return value;
}
function audit() {
  freeze();
  assert(!fs.existsSync(auditRoot), 'Refuse to overwrite audit evidence');
  fs.mkdirSync(auditRoot);
  const results = [];
  for (const attempt of successAttempts) {
    const dir = path.join(root, 'attempts', attempt), pending = json(path.join(dir, 'pending.json'));
    const toolchainSha256 = verifyToolchain(attempt);
    for (const key of ['node', 'npmCli']) sameFile(pending.tools[key].path, pending.tools[key]);
    const program = pending.tools.node.path, args = [pending.tools.npmCli.path, 'audit', '--json'];
    const workingDirectory = path.join(dir, 'toolchain'), startedAt = new Date().toISOString();
    const result = cp.spawnSync(program, args, { cwd: workingDirectory, windowsHide: true, encoding: null,
      timeout: 180000, maxBuffer: 16 * 1024 * 1024 });
    const finishedAt = new Date().toISOString();
    const stdout = result.stdout || Buffer.alloc(0), stderr = result.stderr || Buffer.alloc(0);
    writeNew(path.join(auditRoot, attempt + '.stdout.json'), stdout);
    writeNew(path.join(auditRoot, attempt + '.stderr.log'), stderr);
    let vulnerabilities = null, parseError = null, inputsUnchanged = false;
    try {
      const parsed = JSON.parse(stdout.toString('utf8'));
      vulnerabilities = parsed.metadata?.vulnerabilities || null;
    } catch (error) { parseError = error.message; }
    let inputError = null;
    try { inputsUnchanged = verifyToolchain(attempt) === toolchainSha256; } catch (error) { inputError = error.message; }
    const record = { schema: 'soui.real-renewal-audit/v1', attempt, program, arguments: args, workingDirectory,
      processId: result.pid ?? null, startedAt, finishedAt, exitCode: result.status, signal: result.signal,
      timedOut: result.error?.code === 'ETIMEDOUT', timeoutMs: 180000,
      error: result.error ? { name: result.error.name, code: result.error.code || null, message: result.error.message } : null,
      stdoutBytes: stdout.length, stdoutSha256: sha(stdout), stderrBytes: stderr.length, stderrSha256: sha(stderr),
      toolchainSha256, inputsUnchanged, inputError, parseError, vulnerabilities,
      advisoryLookupSucceeded: result.status === 0 && !result.error && vulnerabilities?.total === 0 && inputsUnchanged,
      guiExecuted: false, realRenewalVerified: false, releaseReady: false };
    writeJson(path.join(auditRoot, attempt + '.result.json'), record);
    results.push(record);
    console.log(JSON.stringify({ attempt, exitCode: record.exitCode, error: record.error, vulnerabilities, inputsUnchanged }));
  }
  writeJson(path.join(auditRoot, 'summary.json'), { schema: 'soui.real-renewal-audit-summary/v1',
    results: results.map(r => ({ attempt: r.attempt, exitCode: r.exitCode, advisoryLookupSucceeded: r.advisoryLookupSucceeded,
      vulnerabilities: r.vulnerabilities, inputsUnchanged: r.inputsUnchanged })), releaseReady: false });
  if (results.some(r => !r.advisoryLookupSucceeded)) process.exitCode = 1;
}
function selection() {
  const entries = [];
  function add(relative, target = relative) {
    const source = path.join(root, relative);
    assert(fs.statSync(source).isFile(), 'Archive input is not a file: ' + relative);
    entries.push({ source, originalPath: relative.replaceAll('\\', '/'), target: target.replaceAll('\\', '/') });
  }
  function tree(relative) {
    for (const file of walk(path.join(root, relative))) add(path.relative(root, file));
  }
  for (const name of ['PROTOCOL.md', 'Prepare.cjs', 'Build-Web.mjs', 'Build.ps1', 'Run.ps1', 'Verify-Run.cjs',
    'SelfTest-Verify-Run.cjs', 'Archive-Build.cjs', 'Archive-Preparation.cjs', 'prepared.json', 'freeze.json',
    'preparation-verify.result.json', 'preparation-verify.stdout.log', 'preparation-verify.stderr.log', 'independent-build-review.json']) add(name);
  tree('consumer'); tree('audit-build-20260926');
  for (const attempt of attempts) {
    const relative = 'attempts/' + attempt, dir = path.join(root, relative);
    for (const name of ['pending.json', 'toolchain-input.json', 'web-build.json', 'runtime-input.json', 'build.json',
      'web-typecheck.tsconfig.json', 'vite-index-before-classic.html']) if (fs.existsSync(path.join(dir, name))) add(relative + '/' + name);
    for (const folder of ['logs', 'consumer-source', 'drivers', 'protocol']) if (fs.existsSync(path.join(dir, folder))) tree(relative + '/' + folder);
    for (const name of ['package.json', 'package-lock.json']) if (fs.existsSync(path.join(dir, 'toolchain', name))) add(relative + '/toolchain/' + name);
    if (fs.existsSync(path.join(dir, 'native-build'))) {
      for (const file of walk(path.join(dir, 'native-build'))) {
        const name = path.basename(file);
        if (name === 'CMakeCache.txt' || name === 'CMakeConfigureLog.yaml' || /\.command\.[0-9]+\.tlog$/i.test(name) ||
          /\.(?:rsp|vcxproj|props)$/i.test(name)) add(path.relative(root, file));
      }
    }
  }
  const selfTest = 'self-test-only/2026-09-26T06-29-51-056Z-49140/self-test-result.json';
  const self = json(path.join(root, selfTest));
  assert(self.testCount === 88 && self.passed === 88 && self.failed === 0 && self.syntheticOnly === true &&
    self.actualGuiExecuted === false && self.actualClockRenewalVerified === false, 'Frozen synthetic self-test result differs');
  add(selfTest, 'synthetic-self-test-88.json');
  // All historical synthetic reports remain external; include report metadata
  // to preserve prior test counts without importing simulated 8h event streams.
  for (const file of walk(path.join(root, 'self-test-only')).filter(file => path.basename(file) === 'self-test-result.json')) {
    if (path.resolve(file) !== path.resolve(path.join(root, selfTest))) add(path.relative(root, file));
  }
  const seen = new Set();
  for (const entry of entries) {
    assert(!seen.has(entry.target.toLowerCase()), 'Duplicate archive target'); seen.add(entry.target.toLowerCase());
    assert(!/(^|\/)(?:node_modules|runtime|web-output)(\/|$)/.test(entry.target) &&
      !/\.(exe|dll|lib|obj|pdb|ilk|idb)$/i.test(entry.target), 'Binary/full input selected for archive');
  }
  return entries.sort((a, b) => a.target < b.target ? -1 : a.target > b.target ? 1 : 0);
}
function stepSummary(attempt) {
  const dir = path.join(root, 'attempts', attempt, 'logs');
  return fs.readdirSync(dir).filter(n => n.endsWith('.result.json')).sort().map(name => {
    const value = json(path.join(dir, name));
    return { step: name.slice(0, -12), exitCode: value.exitCode, timedOut: value.timedOut };
  });
}
function archive() {
  freeze();
  assert(!fs.existsSync(outputRoot), 'Refuse to overwrite archive directory');
  const reviewPath = path.join(root, 'independent-build-review.json');
  assert(sha(fs.readFileSync(reviewPath)) === 'e1d206d35a397c39568419146f64419a4426d27dba22c851e67a2b8833174110',
    'Independent review identity differs');
  const review = json(reviewPath);
  assert(review.status === 'build-and-no-gui-self-check-evidence-verified' && review.freeze.sha256 === expectedFreeze,
    'Independent review status/freeze differs');
  const entries = selection(), successes = {};
  for (const attempt of successAttempts) {
    const steps = stepSummary(attempt);
    assert(steps.length === 13 && steps.every(step => step.exitCode === 0 && step.timedOut === false), 'Successful attempt raw step records differ');
    successes[attempt] = steps;
  }
  const failures = Object.fromEntries(attempts.slice(0, 3).map(a => [a, stepSummary(a).filter(s => s.exitCode !== 0 || s.timedOut)]));
  assert(Object.values(failures).every(f => f.length > 0), 'Historical failed attempt is missing its nonzero raw result');
  // Snapshot every source before writing any archive, then prove the bytes did
  // not change while being copied. Existing evidence is never rewritten.
  const inputs = entries.map(e => { const bytes = fs.readFileSync(e.source); return { ...e, bytes }; });
  fs.mkdirSync(outputRoot, { recursive: false });
  const manifest = [];
  for (const input of inputs) {
    const target = path.join(outputRoot, 'raw', input.target);
    writeNew(target, input.bytes);
    assert(fs.readFileSync(target).equals(input.bytes) && fs.readFileSync(input.source).equals(input.bytes), 'Archive/source bytes changed');
    manifest.push({ path: 'raw/' + input.target, originalPath: input.originalPath, bytes: input.bytes.length, sha256: sha(input.bytes) });
  }
  const audits = json(path.join(auditRoot, 'summary.json'));
  const metadata = { schema: 'soui.real-renewal-build-archive/v1', createdAt: new Date().toISOString(),
    sourceInputSha256: expectedSource, freezeSha256: expectedFreeze, files: manifest,
    successfulAttempts: successes, failedAttempts: failures, audits: audits.results,
    compiled: true, closedReproducibleBuildVerified: false, cleanMachineVerified: false,
    guiExecuted: false, realRenewalVerified: false, resourceGate: false, benchmarkValid: false, releaseReady: false,
    externalLinkInputLimitation: 'Host vcpkg integration injected F:/GitHub/vcpkg/installed/<arch>-windows library paths and *.lib references; these inputs are not frozen.' };
  writeJson(path.join(outputRoot, 'raw-manifest.json'), metadata);
  const readme = [
    '# preview.11 同文档续期夹具：独立构建证据',
    '',
    '本目录只归档外部夹具的编译、严格 TypeScript 检查、无 GUI 自检与分析器合成测试。未执行本夹具 GUI、35 分钟或 8 小时真实续期；不代表 Workbench V02、资源、性能或生产验收通过。',
    '',
    '- 冻结 SDK 输入：' + expectedSource + '（711 项规范化源码输入；本轮未修改 SDK）。',
    '- 最终 freeze.json SHA-256：' + expectedFreeze + '。',
    '- 成功：x86-attempt4、x64-attempt1；每个 attempt 的 13 项原始步骤记录均 exitCode=0、timedOut=false，包含严格 TS 检查和原生 --self-check。',
    '- 未保存两次最外层 Build.ps1 退出的独立原始记录；外层工具当时报告 0。这里仅据已保存的 13 项步骤记录作结论，不补造外层 raw 退出证据。',
    '- 分析器合成自检：88/88；syntheticOnly=true、actualGuiExecuted=false、actualClockRenewalVerified=false。模拟时钟与模拟 NDJSON 不能证明真实时长。',
    '',
    '## 保留的失败及实际修正',
    '',
    '| 原始 attempt | 原始失败 | 后续实际修正 |',
    '| --- | --- | --- |',
    '| x86-attempt1 | 严格 TypeScript 检查中 task kind 仍为 unknown | 明确收窄 kind 后再赋给字符串观察字段 |',
    '| x86-attempt2 | legacy 产物清单的路径键不匹配 | 使用实际清单的精确相对键，保留原失败输出 |',
    '| x86-attempt3 | C++ JSON 字符串辅助函数参与 ADL 冲突，COM 声明缺失 | 改用 json_quoted 避免冲突，并显式包含 objbase.h |',
    '',
    '失败 attempts 的源码、驱动、参数、步骤退出码及 stdout/stderr 原字节均保留；成功重试没有覆盖失败。',
    '',
    '## 未冻结的宿主链接输入',
    '',
    '两架构真实 link.command.1.tlog 显示宿主 vcpkg 集成引入 F:\\GitHub\\vcpkg\\installed\\<arch>-windows\\lib 的 LIBPATH 和 *.lib 引用。这些输入没有进入本次冻结集合。因此本证据不能称为封闭、可复现的构建，也不能称为干净机器验证通过。已归档链接/编译命令 tlog、CMakeCache、相关项目文件与可找到的响应文件，原始编码不变。',
    '',
    '## 审计与复核',
    '',
    'npm audit --json 使用两次成功 attempt 各自已锁定的 toolchain 目录以及记录中的 Node/npm 入口；保存原字节 stdout/stderr、实际退出码、参数、时刻及运行前后工具链散列。未执行 audit fix。当前结果：',
    ...audits.results.map(r => '- ' + r.attempt + '：exitCode=' + JSON.stringify(r.exitCode) +
      '，vulnerabilities=' + JSON.stringify(r.vulnerabilities) + '，inputsUnchanged=' + r.inputsUnchanged + '。'),
    '',
    '独立复核原文见 raw/independent-build-review.json；raw-manifest.json 给出每份原始文件的路径、长度和 SHA-256。归档器拒绝覆盖，复制后同时复核源文件与目标文件字节。',
    '',
    '本归档不收录 EXE/DLL/LIB、node_modules、完整 SDK 输入或运行载荷副本。其身份保留在冻结清单和构建记录中；这些省略的载荷仍在外部原始目录，不能仅靠本归档重现运行。',
    '',
    'releaseReady、realRenewalVerified、guiExecuted、resourceGate、benchmarkValid、closedReproducibleBuildVerified 和 cleanMachineVerified 均为 false。',
    ''
  ].join('\n');
  writeNew(path.join(outputRoot, 'README.md'), readme);
  const manifestBytes = fs.readFileSync(path.join(outputRoot, 'raw-manifest.json'));
  console.log(JSON.stringify({ outputRoot, rawFiles: manifest.length, totalFiles: manifest.length + 2,
    rawBytes: manifest.reduce((sum, f) => sum + f.bytes, 0), manifestSha256: sha(manifestBytes),
    freezeSha256: expectedFreeze, auditResults: audits.results, guiExecuted: false, realRenewalVerified: false, releaseReady: false }, null, 2));
}
try {
  const command = process.argv[2];
  assert(process.argv.length === 3 && ['audit', 'archive'].includes(command), 'Usage: node Archive-Build.cjs audit|archive');
  if (command === 'audit') audit(); else archive();
} catch (error) { console.error(error.stack || error.message); process.exitCode = 1; }

