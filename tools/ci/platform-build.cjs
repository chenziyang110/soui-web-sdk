'use strict';
// --plan is static: no spawning, downloading, configuring, building or test execution.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const ROOT = path.resolve(__dirname, '../..');
const MATRIX = Object.freeze([
  ['windows-2022', 'windows', 'x64'], ['windows-11-arm', 'windows', 'arm64'],
  ['ubuntu-24.04', 'linux', 'x64'], ['ubuntu-24.04-arm', 'linux', 'arm64'],
  ['macos-15-intel', 'macos', 'x64'], ['macos-15', 'macos', 'arm64']
]);
const WEBVIEW2 = Object.freeze({
  version: '1.0.4258.31',
  url: 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4258.31/microsoft.web.webview2.1.0.4258.31.nupkg',
  sha256: '56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9',
  bytes: 9801922
});
const fwd = value => value.replaceAll('\\', '/');
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
function isolatedEnvironment(parent, properties) {
  const env = { ...parent };
  // SDK headers/libraries must not be supplied by ambient compiler search paths.
  // Native system packages/frameworks still use the installed toolchain defaults.
  const inherited = /^(CMAKE_|VCPKG_|PKG_CONFIG(?:_|$)|CC$|CXX$|CFLAGS$|CXXFLAGS$|LDFLAGS$|CL$|_CL_$|LINK$|_LINK_$|CPATH$|CPLUS_INCLUDE_PATH$|C_INCLUDE_PATH$|OBJC_INCLUDE_PATH$|INCLUDE$|LIB$|LIBPATH$|LIBRARY_PATH$|NODE_OPTIONS$|NODE_PATH$)/i;
  for (const key of Object.keys(env)) if (inherited.test(key)) delete env[key];
  return Object.assign(env, properties, { MSBUILDDISABLENODEREUSE: '1' });
}

function buildPlan({ platform, arch, root = ROOT, work, generatorArgs = [] }) {
  if (!MATRIX.some(row => row[1] === platform && row[2] === arch))
    throw new Error('Unsupported native matrix target');
  if (!work || !path.isAbsolute(work)) throw new Error('Require an absolute fresh output directory');
  const p = (...parts) => fwd(path.join(work, ...parts));
  const windows = platform === 'windows', nativeRuntime = !windows, prefix = p('i');
  const common = [
    '-DCMAKE_BUILD_TYPE=Release', ...generatorArgs,
    '-DCMAKE_TOOLCHAIN_FILE=', '-DCMAKE_MODULE_PATH=', '-DCMAKE_PROJECT_INCLUDE=',
    '-DCMAKE_PROJECT_INCLUDE_BEFORE=', '-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=',
    '-DCMAKE_USER_MAKE_RULES_OVERRIDE=', '-DCMAKE_C_COMPILER_LAUNCHER=',
    '-DCMAKE_CXX_COMPILER_LAUNCHER=', '-DCMAKE_C_LINKER_LAUNCHER=', '-DCMAKE_CXX_LINKER_LAUNCHER=',
    '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF'
  ];
  if (platform === 'macos') common.push('-DCMAKE_OSX_ARCHITECTURES=' + (arch === 'x64' ? 'x86_64' : 'arm64'),
    '-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0');
  const properties = {
    UserRootDir: p('iso/user'), MSBuildUserExtensionsPath: p('iso/msbuild'),
    VCLibPackagePath: p('iso/no-vcpkg'), VcpkgRoot: p('iso/vcpkg'),
    VcpkgEnabled: 'false', VcpkgEnableManifest: 'false', EnableVcpkgArtifactsIntegration: 'false',
    ImportDirectoryBuildProps: 'false', ImportDirectoryBuildTargets: 'false',
    ImportUserLocationsByWildcardBeforeMicrosoftCommonProps: 'false',
    ImportUserLocationsByWildcardAfterMicrosoftCommonProps: 'false',
    ImportUserLocationsByWildcardBeforeMicrosoftCommonTargets: 'false',
    ImportUserLocationsByWildcardAfterMicrosoftCommonTargets: 'false'
  };
  if (windows) common.push('-DCMAKE_VS_GLOBALS=' + Object.entries(properties).map(([k,v]) => k + '=' + v).join(';'));
  const steps = [];
  const add = (name, tool, args, timeout = 180000, cwd = work) => steps.push({ name, tool, args, timeout, cwd });
  const configure = (name, source, dest, flags) =>
    add(name, 'cmake', ['-S', fwd(source), '-B', dest, ...common, ...flags]);
  const build = (name, dir, targets) => add(name, 'cmake', [
    '--build', dir, '--config', 'Release', '--target', ...targets, '--parallel', '2',
    ...(windows ? ['--', '/m:2', '/nodeReuse:false', '/noAutoResponse',
      ...Object.entries(properties).map(([k,v]) => '/p:' + k + '=' + v)] : [])
  ], 600000);
  configure('sdk-configure', root, p('b'), [
    '-DSOUI_BUILD_PORTABLE=ON', '-DSOUI_BUILD_DESKTOP=ON',
    '-DSOUI_BUILD_TESTS=' + (nativeRuntime ? 'ON' : 'OFF'),
    ...['DESKTOP_EXAMPLE', 'TEMPLATE', 'DEMOS', 'WEBVIEW2', 'UIA', 'VENDOR_TESTS',
      'AUTO_HOST', 'INPUT_UI_BENCHMARKS'].map(n => '-DSOUI_BUILD_' + n + '=OFF'),
    '-DSOUI_DESKTOP_STARTUP_TRACE=OFF', '-DCMAKE_INSTALL_PREFIX=' + prefix,
    ...(windows ? ['-DSOUI_WEBVIEW2_SDK_ROOT=' + p('webview2')] : [])
  ]);
  build('sdk-build', p('b'), ['soui_desktop']);
  if (nativeRuntime) {
    build('runtime-primitives-build', p('b'), ['soui_runtime_primitives']);
    add('runtime-primitives-test', 'ctest', ['--test-dir', p('b'), '-C', 'Release',
      '-R', '^sdk[.]portable[.]primitives$', '--fixture-exclude-any', '.*',
      '--no-tests=error', '--output-on-failure', '--timeout', '30'], 45000);
  }
  add('sdk-install', 'cmake', ['--install', p('b'), '--config', 'Release', '--prefix', prefix], 120000);
  configure('installed-consumers-configure', path.join(root, 'tests/ci/installed-consumer'), p('c'), [
    '-DSDK_PREFIX=' + prefix, '-DCMAKE_PREFIX_PATH=' + prefix,
    '-DSouiRuntime_DIR=' + prefix + '/lib/cmake/SouiRuntime',
    '-DSouiDesktop_DIR=' + prefix + '/lib/cmake/SouiDesktop',
    '-DCI_RUN_RUNTIME_TEST=' + (nativeRuntime ? 'ON' : 'OFF')
  ]);
  build('installed-consumers-build', p('c'), ['ci_runtime_consumer', 'ci_desktop_consumer', 'ci_installed_headers']);
  if (nativeRuntime) add('runtime-installed-test', 'ctest', ['--test-dir', p('c'), '-C', 'Release',
    '-R', '^ci[.]runtime-installed$', '--fixture-exclude-any', '.*',
    '--no-tests=error', '--output-on-failure', '--timeout', '30'], 45000);
  const tools = prefix + '/share/SouiDesktop/tools/';
  add('template-generate', process.execPath, [tools + 'create-desktop-app.cjs',
    '--output', p('app'), '--name', 'CiDesktopCheck', '--identifier', 'org.soui.ci', '--version', '0.1.0'], 60000);
  configure('template-configure', p('app'), p('ab'), [
    '-DCMAKE_PREFIX_PATH=' + prefix, '-DSouiDesktop_DIR=' + prefix + '/lib/cmake/SouiDesktop',
    '-DSouiRuntime_DIR=' + prefix + '/lib/cmake/SouiRuntime', '-DSOUI_APP_STARTUP_TRACE=OFF'
  ]);
  build('template-link', p('ab'), ['CiDesktopCheck']);
  const binary = windows ? p('ab/Release/CiDesktopCheck.exe') : p('ab/CiDesktopCheck');
  const legal = ['--sdk-license', prefix + '/share/SouiRuntime/LICENSE.md',
    '--runtime-notices', prefix + '/share/SouiRuntime/THIRD_PARTY_NOTICES.md'];
  if (windows) legal.push('--webview2-license', prefix + '/share/SouiDesktop/webview2/LICENSE.txt',
    '--webview2-notices', prefix + '/share/SouiDesktop/webview2/NOTICE.txt');
  add('application-stage', process.execPath, [tools + 'stage-desktop-app.cjs',
    '--binary', binary, '--assets', p('app/web'), '--output', p('stage'),
    '--name', 'CiDesktopCheck', '--identifier', 'org.soui.ci', '--version', '0.1.0',
    '--platform', platform, '--arch', arch, ...legal], 60000);
  add('application-verify', process.execPath, [tools + 'verify-desktop-app.cjs',
    '--input', p('stage'), '--platform', platform, '--arch', arch], 60000);
  add('application-archive', process.execPath, [tools + 'archive-desktop-app.cjs',
    '--input', p('stage'), '--output', p('delivery'), '--platform', platform, '--arch', arch], 60000);
  const basename = 'CiDesktopCheck-0.1.0-' + platform + '-' + arch;
  add('archive-extract', 'cmake', ['-E', 'tar', 'xf', p('delivery', basename + '.zip')], 60000, p('expanded'));
  add('expanded-verify', process.execPath, [tools + 'verify-desktop-app.cjs',
    '--input', p('expanded', basename), '--platform', platform, '--arch', arch], 60000);
  return { platform, arch, prefix, work, properties, steps,
    runtimeNoGuiTestsPlanned: nativeRuntime, desktopApplicationExecutionPlanned: false,
    artifactsUploaded: false, releaseReady: false, nativeRuntimeVerified: false,
    webview2: windows ? WEBVIEW2 : null };
}

function capture(tool, args) {
  const r = spawnSync(tool, args, { encoding: 'utf8', timeout: 30000, windowsHide: true, maxBuffer: 4 * 1024 * 1024 });
  if (r.error || r.signal || !Number.isInteger(r.status) || r.status !== 0)
    throw new Error('Tool inventory failed: ' + tool + ' (' + (r.error?.code || r.signal || r.status) + ')');
  return r.stdout.trim();
}
function discoverGenerator(platform, arch) {
  if (platform !== 'windows') return ['-G', 'Unix Makefiles'];
  const vswhere = path.join(process.env['ProgramFiles(x86)'], 'Microsoft Visual Studio/Installer/vswhere.exe');
  const component = arch === 'arm64' ? 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' : 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64';
  const instances = JSON.parse(capture(vswhere, ['-latest', '-products', '*', '-requires', component, '-format', 'json', '-utf8']));
  if (instances.length !== 1) throw new Error('Required preinstalled MSVC target tools absent; no installation attempted');
  const generator = ({17: 'Visual Studio 17 2022', 18: 'Visual Studio 18 2026'})[Number(instances[0].installationVersion.split('.')[0])];
  const available = JSON.parse(capture('cmake', ['-E', 'capabilities'])).generators.map(g => g.name);
  if (!generator || !available.includes(generator)) throw new Error('Installed VS/CMake mismatch; no tool download or fallback');
  return ['-G', generator, '-A', arch === 'arm64' ? 'ARM64' : 'x64',
    '-DCMAKE_GENERATOR_INSTANCE=' + fwd(instances[0].installationPath)];
}
function assertPrefix(cacheFile, prefix) {
  const cache = fs.readFileSync(cacheFile, 'utf8');
  for (const name of ['SouiRuntime', 'SouiDesktop']) {
    const row = cache.split(/\r?\n/).find(line => line.startsWith(name + '_DIR:PATH='));
    if (!row || fs.realpathSync(row.slice(row.indexOf('=') + 1)) !== fs.realpathSync(path.join(prefix, 'lib/cmake', name)))
      throw new Error('Template package outside isolated installation: ' + name);
  }
}
function compareTrees(left, right, relative = '') {
  const files = root => fs.readdirSync(path.join(root, relative), { withFileTypes: true })
    .map(e => ({ name: e.name, directory: e.isDirectory(), file: e.isFile() })).sort((a,b) => a.name.localeCompare(b.name));
  const a = files(left), b = files(right);
  if (JSON.stringify(a) !== JSON.stringify(b)) throw new Error('Extracted file set differs: ' + relative);
  let count = 0;
  for (const entry of a) {
    const rel = path.join(relative, entry.name);
    if (entry.directory) count += compareTrees(left, right, rel);
    else if (!entry.file || hash(fs.readFileSync(path.join(left, rel))) !== hash(fs.readFileSync(path.join(right, rel))))
      throw new Error('Extracted bytes/type differ: ' + rel);
    else count++;
  }
  return count;
}
async function execute(plan) {
  const expectedOS = { windows: 'win32', linux: 'linux', macos: 'darwin' }[plan.platform];
  if (process.env.GITHUB_ACTIONS !== 'true' || process.platform !== expectedOS || process.arch !== plan.arch ||
      process.env.RUNNER_ARCH?.toLowerCase() !== plan.arch)
    throw new Error('Execution requires matching native GitHub runner; --plan is local/static only');
  if (process.env.GITHUB_REPOSITORY !== 'chenziyang110/soui-web-sdk')
    throw new Error('Only reviewed public repository may execute this workflow');
  fs.mkdirSync(plan.work);
  for (const value of Object.values(plan.properties)) if (value.includes('/iso/')) fs.mkdirSync(value, { recursive: true });
  const report = { schema: 'soui.ci.build-only/1', source: capture('git', ['rev-parse', 'HEAD']),
    runner: process.env.RUNNER_NAME, imageOS: process.env.ImageOS, imageVersion: process.env.ImageVersion,
    platform: plan.platform, architecture: plan.arch, node: process.version, osRelease: os.release(),
    cmake: capture('cmake', ['--version']), steps: [], outcome: 'running', runtimeNoGuiTestsPassed: false,
    desktopApplicationExecuted: false, releaseReady: false, nativeRuntimeVerified: false,
    authenticityVerified: false, signed: false, artifactsUploaded: false };
  const save = () => fs.writeFileSync(path.join(plan.work, 'result.json'), JSON.stringify(report, null, 2) + '\n');
  try {
    const env = isolatedEnvironment(process.env, plan.properties);
    if (plan.webview2) {
      const response = await fetch(WEBVIEW2.url, { redirect: 'error', signal: AbortSignal.timeout(60000) });
      if (!response.ok) throw new Error('Official WebView2 request failed: ' + response.status);
      const bytes = Buffer.from(await response.arrayBuffer());
      if (bytes.length !== WEBVIEW2.bytes || hash(bytes) !== WEBVIEW2.sha256) throw new Error('WebView2 identity mismatch');
      const packageFile = path.join(plan.work, 'webview2.nupkg');
      fs.writeFileSync(packageFile, bytes);
      fs.mkdirSync(path.join(plan.work, 'webview2'));
      const r = spawnSync('cmake', ['-E', 'tar', 'xf', packageFile],
        { cwd: path.join(plan.work, 'webview2'), env, stdio: 'inherit', timeout: 60000, windowsHide: true });
      if (r.error || r.signal || !Number.isInteger(r.status) || r.status !== 0) throw new Error('Data-only WebView2 extraction failed');
      report.webview2 = { ...WEBVIEW2, packageProgramsExecuted: false };
    }
    fs.mkdirSync(path.join(plan.work, 'expanded'));
    for (const step of plan.steps) {
      if (step.tool === 'ctest') {
        const selected = JSON.parse(capture('ctest', [...step.args, '--show-only=json-v1'])).tests;
        const expected = step.name === 'runtime-primitives-test'
          ? ['sdk.portable.primitives', path.join(plan.work, 'b/tests/portable/soui_runtime_primitives')]
          : ['ci.runtime-installed', path.join(plan.work, 'c/ci_runtime_consumer')];
        if (selected.length !== 1 || selected[0].name !== expected[0] ||
            selected[0].command?.length !== 1 || path.resolve(selected[0].command[0]) !== expected[1] ||
            selected[0].properties.some(p => p.name === 'FIXTURES_REQUIRED' && p.value?.length))
          throw new Error('Runtime CTest selection/command/fixture differs from reviewed scope');
        report.steps.push({ name: step.name + '-selection', tests: selected });
      }
      console.log('\n[' + step.name + '] ' + JSON.stringify([step.tool, ...step.args]));
      const startUTC = new Date().toISOString();
      const r = spawnSync(step.tool, step.args, { cwd: step.cwd, env, stdio: 'inherit', timeout: step.timeout, windowsHide: true });
      report.steps.push({ ...step, startUTC, endUTC: new Date().toISOString(),
        exitCode: r.status, signal: r.signal, errorCode: r.error?.code ?? null });
      save();
      if (r.error || r.signal || !Number.isInteger(r.status) || r.status !== 0) throw new Error('Stage failed: ' + step.name);
      if (step.name === 'template-configure') assertPrefix(path.join(plan.work, 'ab/CMakeCache.txt'), plan.prefix);
    }
    const basename = 'CiDesktopCheck-0.1.0-' + plan.platform + '-' + plan.arch;
    report.extractedFilesMatched = compareTrees(path.join(plan.work, 'stage'), path.join(plan.work, 'expanded', basename));
    const zip = fs.readFileSync(path.join(plan.work, 'delivery', basename + '.zip'));
    report.package = { name: basename + '.zip', bytes: zip.length, sha256: hash(zip) };
    const manifest = JSON.parse(fs.readFileSync(path.join(plan.work, 'stage/application-manifest.json'), 'utf8'));
    if (manifest.nativeRuntimeVerified !== false || manifest.signed !== false ||
        manifest.dependenciesBundled !== false || manifest.developmentOnly !== true)
      throw new Error('Application qualification unexpectedly changed');
    report.runtimeNoGuiTestsPassed = plan.runtimeNoGuiTestsPlanned;
    report.outcome = 'passed';
  } catch (error) {
    report.outcome = 'failed'; report.failure = error.message; throw error;
  } finally { save(); }
}
function parseArgs(argv) {
  if (argv.length === 1 && argv[0] === '--summary') return { summary: true };
  const result = {};
  for (let i = 0; i < argv.length; i++) {
    const flag = argv[i];
    if (flag === '--plan' && !result.plan) result.plan = true;
    else if (['--platform', '--arch', '--work'].includes(flag) && argv[i+1] && !argv[i+1].startsWith('--') && !Object.hasOwn(result, flag.slice(2)))
      result[flag.slice(2)] = argv[++i];
    else throw new Error('Unknown, duplicate or incomplete argument');
  }
  return result;
}
async function main(argv) {
  const options = parseArgs(argv);
  const temporary = process.env.RUNNER_TEMP || os.tmpdir();
  const work = path.resolve(options.work || path.join(options.plan ? temporary : fs.realpathSync(temporary), 'sc'));
  if (options.summary) {
    const file = path.join(work, 'result.json');
    const content = fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '{"outcome":"not started or failed before build; see logs"}';
    if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY,
      '### Platform build qualification\n\n' + content.split('\n').map(line => '    ' + line).join('\n') + '\n');
    console.log(content); return;
  }
  // Reject local execution before discovery; no local compiler/VS inventory is needed.
  if (!options.plan && process.env.GITHUB_ACTIONS !== 'true') throw new Error('Use --plan for local static inspection');
  const generatorArgs = options.plan
    ? ['-G', options.platform === 'windows' ? 'Visual Studio <detected 17/18>' : 'Unix Makefiles',
      ...(options.platform === 'windows' ? ['-A', options.arch === 'arm64' ? 'ARM64' : 'x64'] : [])]
    : discoverGenerator(options.platform, options.arch);
  const plan = buildPlan({ ...options, work, generatorArgs });
  if (options.plan) console.log(JSON.stringify(plan, null, 2));
  else await execute(plan);
}
module.exports = { buildPlan, MATRIX, WEBVIEW2, compareTrees, parseArgs, isolatedEnvironment };
if (require.main === module) main(process.argv.slice(2)).catch(error => { console.error(error.message); process.exitCode = 1; });
