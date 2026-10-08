import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
import {spawnSync} from 'node:child_process';
import preparation from './Prepare.cjs';

const {ROOT, INPUTS, SOURCE, encode, hash, read, assert, snapshot, verify, write, checkAttempt, attemptPath} = preparation;
const attempt = attemptPath(process.argv[2]);
assert(process.argv.length === 3, 'Build-Web.mjs expects one attempt name');
const pending = checkAttempt(attempt);
const toolchain = path.join(attempt, 'toolchain');
const toolchainInput = read(path.join(attempt, 'toolchain-input.json'));
verify(toolchain, toolchainInput);
const source = path.join(attempt, 'consumer-source/web');
const output = path.join(attempt, 'web-output');
const sdk = path.join(INPUTS, 'web-sdk');
assert(!fs.existsSync(output), 'Web output exists; preserve failure and start another attempt');
const requireTool = createRequire(path.join(toolchain, 'package.json'));
const typecheckTool = requireTool.resolve('typescript/bin/tsc');
const typecheckConfig = path.join(attempt, 'web-typecheck.tsconfig.json');
write(typecheckConfig, {compilerOptions: {target: 'ES2015', module: 'ESNext', moduleResolution: 'Bundler',
  lib: ['ES2015', 'DOM'], strict: true, noEmit: true, baseUrl: attempt,
  paths: {'@soui-sdk': [path.join(sdk, 'dist/index.d.ts')]}}, files: [path.join(source, 'main.ts')]});
const typecheckStart = new Date().toISOString();
const typecheck = spawnSync(process.execPath, [typecheckTool, '--project', typecheckConfig],
  {cwd: source, encoding: 'utf8', windowsHide: true, timeout: 120000, maxBuffer: 8 * 1024 * 1024});
const typecheckLog = path.join(attempt, 'logs/web-typecheck');
fs.writeFileSync(typecheckLog + '.stdout.log', typecheck.stdout || '', {flag: 'wx'});
fs.writeFileSync(typecheckLog + '.stderr.log', typecheck.stderr || '', {flag: 'wx'});
write(typecheckLog + '.result.json', {program: process.execPath, arguments: [typecheckTool, '--project', typecheckConfig],
  startedAt: typecheckStart, finishedAt: new Date().toISOString(), exitCode: typecheck.status,
  signal: typecheck.signal, timedOut: typecheck.error?.code === 'ETIMEDOUT', error: typecheck.error ? String(typecheck.error) : null});
assert(!typecheck.error && typecheck.status === 0, 'Strict public SDK consumer typecheck failed');
function esmEntry(name) {
  const packageRoot = path.join(toolchain, 'node_modules', ...name.split('/'));
  const packageInfo = read(path.join(packageRoot, 'package.json'));
  const imported = packageInfo.exports?.['.']?.import;
  const entry = typeof imported === 'string' ? imported : imported?.default || packageInfo.module;
  assert(typeof entry === 'string' && entry.startsWith('./') && !entry.includes('..'), 'Missing package ESM entry: ' + name);
  return pathToFileURL(path.join(packageRoot, entry)).href;
}
const {build} = await import(esmEntry('vite'));
const {default: legacy} = await import(esmEntry('@vitejs/plugin-legacy'));
// Resolve Vite's virtual polyfill module against the isolated installed tools.
process.chdir(toolchain);
await build({configFile: false, root: source, base: './',
  resolve: {alias: {'@soui-sdk': path.join(sdk, 'dist/index.js')}},
  plugins: [legacy({targets: ['Chrome 49'], renderLegacyChunks: true})],
  build: {outDir: output, emptyOutDir: false, manifest: true, cssTarget: 'chrome49', sourcemap: false}});
const manifest = read(path.join(output, '.vite/manifest.json'));
const application = manifest['index-legacy.html'];
const polyfills = manifest['vite/legacy-polyfills-legacy'];
assert(application?.file && polyfills?.file, 'Missing Chrome 49 legacy manifest entries');
function asset(value) {
  assert(typeof value === 'string' && /^[a-zA-Z0-9_./-]+$/.test(value) && !value.startsWith('/') &&
    value.split('/').every(part => part && part !== '.' && part !== '..'), 'Unsafe manifest asset');
  assert(fs.statSync(path.join(output, value)).isFile(), 'Missing manifest asset');
  return './' + value;
}
const styleTags = (manifest['index.html']?.css || []).map(file => '<link rel="stylesheet" href="' + asset(file) + '">').join('\n');
const prelude = '<script>(function(){try{var D=window.DOMException;new D(1,2);}catch(e){var P=D&&D.prototype;var S=function(message,name){this.message=String(message||"");this.name=String(name||"Error");};if(P)S.prototype=P;try{window.DOMException=S;}catch(ignore){}}})();</script>';
const scripts = prelude + '\n<script src="' + asset(polyfills.file) + '"></script>\n<script>System.import(' +
  JSON.stringify(asset(application.file)) + ').catch(function(error){window.__souiBootstrapError=String(error&&error.message||error);var s=document.getElementById("status");if(s)s.textContent="页面启动失败："+window.__souiBootstrapError;});</script>';
// Keep this fixture's status/input/details nodes and inline styles. The SDK's
// generic classic-entry helper intentionally replaces the body with #app.
const originalHtml = fs.readFileSync(path.join(source, 'index.html'), 'utf8');
const moduleTag = /<script\s+type="module"\s+src="\/main\.ts"\s*><\/script>/g;
assert((originalHtml.match(moduleTag) || []).length === 1, 'Expected one fixed fixture entry');
const classic = originalHtml.replace(moduleTag, scripts).replace('</head>', styleTags + '\n</head>');
assert(!/type\s*=\s*["']module["']/i.test(classic) && /id="status"/.test(classic) && /id="details"/.test(classic), 'Invalid classic fixture HTML');
fs.copyFileSync(path.join(output, 'index.html'), path.join(attempt, 'vite-index-before-classic.html'), fs.constants.COPYFILE_EXCL);
fs.writeFileSync(path.join(output, 'index.html'), classic, 'utf8');
checkAttempt(attempt);
verify(toolchain, toolchainInput);
write(path.join(attempt, 'web-build.json'), {schema: 'soui.real-renewal-web/v1', finishedAt: new Date().toISOString(),
  sourceSha256: SOURCE, consumerSourceSha256: pending.consumerSource.sha256,
  webSdkSha256: snapshot(sdk).sha256, toolchainSha256: toolchainInput.sha256,
  typecheck: {passed: true, typescript: read(requireTool.resolve('typescript/package.json')).version,
    configSha256: hash(fs.readFileSync(typecheckConfig))}, classicEntry: true, target: 'Chrome 49',
  output: snapshot(output), node: process.version, guiExecuted: false, realRenewalVerified: false});
console.log(encode({status: 'web-built', output, classicEntry: true, guiExecuted: false}));
