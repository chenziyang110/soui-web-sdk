'use strict';
// This collector reads frozen evidence and may write only suite/summary.json.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = 'C:/SOUI-SDK-development/preview9-tray';
const suite = __dirname;
const id = require(path.join(root, 'tools/internal/delivery-identity.cjs'));
const build = require(path.join(root, 'tools/internal/build-evidence.cjs'));
const consumed = new Map();
const normalizedPath = value => value.replaceAll('\\', '/').toLowerCase();
function bytesAt(file) {
  const bytes = fs.readFileSync(file), hash = id.hash(bytes);
  if (consumed.has(file)) assert.equal(consumed.get(file), hash, `evidence changed while reading: ${file}`);
  consumed.set(file, hash);
  return bytes;
}
function decodeText(bytes) {
  const utf16 = bytes.length>=2 && bytes[0]===0xff && bytes[1]===0xfe;
  if (utf16) assert.equal(bytes.length%2,0,'complete UTF-16LE log');
  return new TextDecoder(utf16?'utf-16le':'utf-8',{fatal:true}).decode(bytes).replace(/^\uFEFF/,'').replace(/\r\n/g,'\n');
}
const log = name => decodeText(bytesAt(id.safePath(suite, name)));
const read = name => JSON.parse(log(name));
function unchanged() {
  for (const [file, sha256] of consumed) assert.equal(id.hash(fs.readFileSync(file)), sha256, `read-only evidence changed: ${file}`);
}
function exactNames(actual, expected, label) {
  assert.equal(new Set(actual).size, actual.length, `${label}: duplicate names`);
  assert.deepEqual([...actual].sort(), [...expected].sort(), `${label}: exact names`);
}
const expectedOutputs = {
  'sdk.native.contract':'Native SDK contract checks passed',
  'sdk.native.session-renewal':'session_renewal: 14 no-desktop contract groups passed (fake clock; no real-duration or GUI claim)',
  'demo.native.session-renewal':'Demo renewal ownership and revoke-before-admission contracts passed; no GUI or media engine executed',
  'sdk.native.task-service':'Public native task service: progress, events, cancellation, capacity and session scope passed',
  'sdk.native.bridge-budget':'Native bridge budget: limits, generations, concurrency and lease cleanup passed',
  'sdk.native.task-store-capacity':'Native task store capacity and scanner authorization: admission, rotation, ownership, cancellation, and revocation passed',
  'sdk.native.tray-service':'tray_service: 14 no-desktop contract groups passed (fake platform; no Explorer/GUI claim)',
  'sdk.native.menu_service':'menu_service: 15 no-desktop contract groups passed (fake platform; no GUI claim)',
  'sdk.native.global_shortcut':'global_shortcut: 9 no-desktop contract groups passed (fake platform; no actual key/GUI claim)',
  'demo.native.public-task':''
};
const expectedTests = Object.keys(expectedOutputs);
const webFiles = ['tests/web/sdk-client.test.mjs', 'tests/web/native-resolvers.test.mjs', 'tests/web/session-renewal.test.mjs'];
function sourceTestNames(files, count) {
  const names = files.flatMap(name => [...bytesAt(id.safePath(root, name)).toString('utf8').matchAll(/^test\('([^'\r\n]+)'\s*,/gm)].map(match => match[1]));
  assert.equal(names.length, count, 'frozen top-level test declarations');
  assert.equal(new Set(names).size, names.length, 'unique declared test names');
  return names;
}
function attributes(text) {
  const values = Object.create(null);
  const remainder = text.replace(/([\w.-]+)="([^"<>]*)"/g, (_, name, value) => {
    assert.equal(Object.hasOwn(values, name), false, `duplicate XML attribute ${name}`);
    values[name] = value;
    return '';
  });
  assert.equal(remainder.trim(), '', 'unexpected XML attribute syntax');
  return values;
}
// Accept only this CTest JUnit format; unparsed content is a validation error.
function nativeResults(xml) {
  const match = /^\s*<\?xml version="1\.0" encoding="UTF-8"\?>\s*<testsuite\s+([^>]*)>([\s\S]*)<\/testsuite>\s*$/.exec(xml);
  assert.ok(match, 'one CTest testsuite required');
  const attrs = attributes(match[1]);
  exactNames(Object.keys(attrs), ['name','tests','failures','disabled','skipped','hostname','time','timestamp'], 'CTest attributes');
  for (const [name, value] of Object.entries({tests:'10', failures:'0', disabled:'0', skipped:'0'})) assert.equal(attrs[name], value, name);
  assert.doesNotMatch(xml, /<(?:failure|error|skipped)\b/);
  const cases = [];
  const rest = match[2].replace(/<testcase\s+([^>]*)>([\s\S]*?)<\/testcase>/g, (_, text, body) => {
    const item = attributes(text);
    exactNames(Object.keys(item), ['name','classname','time','status'], 'CTest case attributes');
    assert.equal(item.status, 'run', `${item.name}: status`);
    assert.equal(item.classname, item.name);
    assert.match(item.time, /^\d+(?:\.\d+)?$/);
    const output = /^\s*<properties\s*\/>\s*<system-out>([^<]*)<\/system-out>\s*$/.exec(body);
    assert.ok(output, `${item.name}: complete case output required`);
    assert.ok(Object.hasOwn(expectedOutputs, item.name), `unexpected CTest ${item.name}`);
    assert.equal(output[1].trim(), expectedOutputs[item.name], `${item.name}: output`);
    cases.push(item.name);
    return '';
  });
  assert.equal(rest.trim(), '', 'unparsed CTest cases');
  exactNames(cases, expectedTests, 'CTest names');
  return {count:cases.length, names:cases, groups:{session_renewal:14, tray_service:14, menu_service:15, global_shortcut:9}};
}
// A flat Node TAP run must identify each source-declared test exactly once.
// Summary counts alone would accept missing tests, duplicates or skipped cases.
function tapResults(text, names) {
  // These are non-result diagnostics emitted by the identity fixtures' Git.
  const lines = text.trimEnd().split('\n').filter(line=>!/^# warning: in the working copy of '[^'\n]+', LF will be replaced by CRLF the next time Git touches it$/.test(line));
  assert.equal(lines.shift(), 'TAP version 13');
  const actual = [];
  while (lines[0]?.startsWith('# Subtest: ')) {
    const name = lines.shift().slice('# Subtest: '.length);
    actual.push(name);
    assert.equal(lines.shift(), `ok ${actual.length} - ${name}`, 'TAP executed successful case without directives');
    assert.equal(lines.shift(), '  ---');
    assert.match(lines.shift() || '', /^  duration_ms: \d+(?:\.\d+)?$/);
    assert.equal(lines.shift(), "  type: 'test'");
    assert.equal(lines.shift(), '  ...');
  }
  exactNames(actual, names, 'TAP names');
  assert.equal(lines.shift(), `1..${names.length}`, 'one complete TAP plan');
  for (const [name, count] of Object.entries({tests:names.length,suites:0,pass:names.length,fail:0,cancelled:0,skipped:0,todo:0}))
    assert.equal(lines.shift(), `# ${name} ${count}`, `TAP ${name}`);
  assert.match(lines.shift() || '', /^# duration_ms \d+(?:\.\d+)?$/);
  assert.equal(lines.length, 0, 'no appended/duplicate TAP result');
  return {count:actual.length, names:actual};
}
function packageResults(text) {
  assert.equal(text.trim(), 'SDK package smoke: 14/14 passed (ownership, integrity, upgrade, rollback, IO recovery and paths).');
  return 14;
}
function commandResult(record, program, args, label) {
  assert.equal(record.exitCode, 0, `${label}: raw exit code`);
  assert.equal(normalizedPath(record.program), normalizedPath(program), `${label}: program`);
  assert.deepEqual(record.arguments, args, `${label}: arguments`);
  assert.ok(Number.isFinite(Date.parse(record.startedAt)) && Number.isFinite(Date.parse(record.finishedAt)), `${label}: timestamps`);
  assert.ok(Date.parse(record.finishedAt) >= Date.parse(record.startedAt), `${label}: ordered timestamps`);
  if (Object.hasOwn(record, 'timedOut')) assert.equal(record.timedOut, false, `${label}: timed out`);
}
function fileMatches(actual, expected) {
  const a = bytesAt(actual), b = bytesAt(expected);
  assert.equal(a.length, b.length, actual);
  assert.equal(id.hash(a), id.hash(b), actual);
}
function installedFiles(installed, arch) {
  const sdkOutput = path.join(root, `out/sdk-${arch}`), sources = new Map();
  for (const name of id.walk(path.join(root, 'sdk/include'))) sources.set(`include/${name}`, path.join(root, 'sdk/include', name));
  for (const name of id.walk(path.join(root, 'third_party/licenses'))) sources.set(`share/SouiSdk/licenses/${name}`, path.join(root, 'third_party/licenses', name));
  sources.set('share/SouiSdk/THIRD_PARTY_NOTICES.md', path.join(root, 'THIRD_PARTY_NOTICES.md'));
  sources.set('lib/Release/soui_sdk.lib', path.join(sdkOutput, 'Release/soui_sdk.lib'));
  for (const name of ['SouiSdkConfig.cmake','SouiSdkConfigVersion.cmake']) sources.set(`lib/cmake/SouiSdk/${name}`, path.join(sdkOutput, name));
  const exportRoot = path.join(sdkOutput, 'CMakeFiles/Export');
  for (const name of ['SouiSdkTargets.cmake','SouiSdkTargets-release.cmake']) {
    const candidates = id.walk(exportRoot).filter(file => path.basename(file) === name);
    assert.equal(candidates.length, 1, `unique generated export ${name}`);
    sources.set(`lib/cmake/SouiSdk/${name}`, path.join(exportRoot, candidates[0]));
  }
  const names = id.walk(installed);
  assert.equal(names.length, 35, 'installed SDK file count');
  exactNames(names, [...sources.keys()], 'installed SDK paths');
  for (const [name, source] of sources) fileMatches(path.join(installed, name), source);
  return id.snapshot(installed, names);
}
function peMachine(binary, expected) {
  assert.ok(binary.length >= 64, 'complete DOS header');
  assert.equal(binary.toString('ascii', 0, 2), 'MZ');
  const offset = binary.readUInt32LE(0x3c);
  assert.ok(offset >= 64 && offset + 24 <= binary.length, 'bounded PE header');
  assert.equal(binary.toString('binary', offset, offset + 4), 'PE\0\0');
  const machine = binary.readUInt16LE(offset + 4);
  assert.equal(machine, expected, 'consumer PE architecture');
  return machine.toString(16).padStart(4, '0');
}
function verifyArchitecture(arch, frozen) {
  const relative = `${arch}-attempt1`, output = path.join(suite, relative);
  const full = build.verifyBuild(root, arch, 'Release'), sdk = build.verifyBuild(root, arch, 'Release', true);
  assert.deepEqual(read(`${relative}/full-build-build-Release.json`), full, `${arch}: archived/live full build identity`);
  assert.deepEqual(read(`${relative}/sdk-build-build-Release.json`), sdk, `${arch}: archived/live SDK build identity`);
  assert.deepEqual(full.source, frozen); assert.deepEqual(sdk.source, frozen);
  const installed = path.join(output, 'sdk-install'), consumerSource = path.join(output, 'consumer-source');
  const consumerBuild = path.join(output, 'consumer-build'), platform = arch === 'x86' ? 'Win32' : 'x64';
  const executable = path.join(consumerBuild, 'Release/soui_session_consumer.exe');
  const pattern = '^(sdk.native.(contract|session-renewal|task-service|bridge-budget|task-store-capacity|tray-service|menu_service|global_shortcut)|demo.native.(public-task|session-renewal))$';
  const commands = {
    'full-build':['powershell.exe',['-NoProfile','-File','tools/Build.ps1','-Architecture',arch,'-Configuration','Release']],
    'sdk-build':['powershell.exe',['-NoProfile','-File','tools/Build.ps1','-SdkOnly','-Architecture',arch,'-Configuration','Release']],
    'no-desktop-tests':['ctest.exe',['--test-dir',`out/${arch}`,'-C','Release','--output-on-failure','-V','--timeout','35','-R',pattern,'--output-junit',path.join(output,'tests.xml')]],
    'install':['cmake.exe',['--install',`out/sdk-${arch}`,'--config','Release','--prefix',installed]],
    'consumer-configure':['cmake.exe',['-S',consumerSource,'-B',consumerBuild,'-G','Visual Studio 17 2022','-A',platform,'-DCMAKE_SYSTEM_VERSION=10.0.19041.0',`-DSouiSdk_DIR=${installed}/lib/cmake/SouiSdk`]],
    'consumer-build':['cmake.exe',['--build',consumerBuild,'--config','Release','--parallel','2']],
    'consumer-test':[executable,[]]
  };
  for (const [name, [program,args]] of Object.entries(commands)) commandResult(read(`${relative}/${name}.result.json`),program,args,`${arch} ${name}`);
  const tests = nativeResults(log(`${relative}/tests.xml`));
  const consumerRelative = 'consumer-build/Release/soui_session_consumer.exe';
  const machine = peMachine(bytesAt(executable), arch === 'x86' ? 0x14c : 0x8664);
  assert.equal(log(`${relative}/consumer-test.log`).trim(),
    'Installed public SDK: 20 simulated renewals, retained result/subscription, one invocation, revocation passed. No real-time or desktop claim.');
  assert.deepEqual(id.snapshot(consumerSource,id.walk(consumerSource)),id.snapshot(path.join(root,'tests/session-consumer'),id.walk(path.join(root,'tests/session-consumer'))),`${arch}: copied consumer source`);
  const cache = log(`${relative}/consumer-build/CMakeCache.txt`);
  for (const [key,value] of [['CMAKE_HOME_DIRECTORY',consumerSource],['SouiSdk_DIR',path.join(installed,'lib/cmake/SouiSdk')],['CMAKE_GENERATOR_PLATFORM',platform]]) {
    const matches = [...cache.matchAll(new RegExp(`^${key}:[^=]+=([^\\n]*)`, 'gm'))];
    assert.equal(matches.length,1,key); assert.equal(normalizedPath(matches[0][1]),normalizedPath(value),key);
  }
  const project = log(`${relative}/consumer-build/soui_session_consumer.vcxproj`);
  const blocks = [...project.matchAll(/<ItemDefinitionGroup Condition="([^"<>]*)">([\s\S]*?)<\/ItemDefinitionGroup>/g)].filter(match=>match[1].includes(`'Release|${platform}'`));
  assert.equal(blocks.length,1,'one Release consumer compile/link definition');
  const dependencies = [...blocks[0][2].matchAll(/<AdditionalDependencies>([^<>]*)<\/AdditionalDependencies>/g)];
  assert.equal(dependencies.length,1,'one consumer link dependencies set');
  const libraries = dependencies[0][1].split(';').map(normalizedPath);
  assert.deepEqual(libraries.filter(name=>name.includes('/')||name.includes(':')),[normalizedPath(path.join(installed,'lib/Release/soui_sdk.lib'))],'only installed SDK library path');
  assert.ok(libraries.includes('bcrypt.lib'),'renewal system entropy dependency exported');
  const includeGroups = [...blocks[0][2].matchAll(/<AdditionalIncludeDirectories>([^<>]*)<\/AdditionalIncludeDirectories>/g)];
  assert.ok(includeGroups.length>0);
  for (const match of includeGroups) assert.deepEqual(match[1].split(';').map(normalizedPath),[normalizedPath(path.join(installed,'include')),'%(additionalincludedirectories)']);
  assert.ok(!normalizedPath(project).includes(normalizedPath(root)),'consumer project must not reference source checkout');
  const nativeNames = ['soui_sdk_contract_tests.exe','soui_session_renewal_tests.exe','soui_task_service_tests.exe','soui_bridge_budget_tests.exe','soui_task_store_capacity_tests.exe','soui_tray_service_tests.exe','soui_menu_service_tests.exe','soui_global_shortcut_tests.exe','soui_demo_session_renewal_tests.exe'];
  return {architecture:arch,configuration:'Release',sourceSha256:frozen.sha256,fullBuildId:full.buildId,sdkBuildId:sdk.buildId,
    testsPassed:tests.count,tests:tests.names,nativeContractGroups:tests.groups,installedSdk:installedFiles(installed,arch),
    consumerSource:id.snapshot(consumerSource,id.walk(consumerSource)),consumer:{peMachine:machine,simulatedRenewals:20,invocations:1,...id.snapshot(output,[consumerRelative])},
    nativeTests:id.snapshot(path.join(root,`out/${arch}/tests/native/Release`),nativeNames),
    applications:Object.fromEntries(Object.entries(full.payloads).map(([target,payload])=>[target,{sha256:payload.sha256,bytes:payload.bytes,files:payload.files.length}]))};
}
function verifySdkPackage() {
  const packaging = require(path.join(root,'tools/sdk/soui-sdk.cjs'));
  const packageDirectory = path.join(suite,'sdk-package-attempt1'), version = '0.3.0-preview.11';
  const commands = {
    'sdk-pack-attempt1':['tools/sdk/soui-sdk.cjs','pack','--source',path.resolve(root),'--output',packageDirectory,'--version',version],
    'sdk-verify':['tools/sdk/soui-sdk.cjs','verify','--package',packageDirectory],
    'sdk-verify-source':['tools/sdk/soui-sdk.cjs','verify-source','--package',packageDirectory,'--source',path.resolve(root)]
  };
  for (const [name,args] of Object.entries(commands)) {
    const result = read(`${name}.result.json`);
    commandResult(result,'node',args,name);assert.equal(result.rawFile,`${name}.log`);
  }
  const packed = read('sdk-pack-attempt1.log'), verified = read('sdk-verify.log');
  assert.deepEqual(packed,{status:'packed',version,files:418,output:packageDirectory,releaseReady:false});
  assert.deepEqual(verified,{status:'verified',version,files:418,releaseReady:false});
  assert.deepEqual(read('sdk-verify-source.log'),{status:'source-matches-package',version});
  const manifest = packaging.readPackage(packageDirectory); // Re-read every actual payload byte/hash.
  assert.equal(manifest.version,version);assert.equal(manifest.files.length,verified.files);
  fileMatches(path.join(packageDirectory,'sdk-manifest.json'),path.join(packageDirectory,'sdk-manifest.json'));
  const packageNames = manifest.files.map(file=>file.path);
  exactNames(id.walk(path.join(packageDirectory,'payload')),packageNames,'SDK package payload paths');
  const sourceNames = [...packaging.managedRoots.flatMap(name=>id.walk(root,name,(_,leaf)=>['node_modules','dist','out','.git','.vs'].includes(leaf))),...packaging.managedFiles];
  exactNames(sourceNames,packageNames,'SDK current managed source paths');
  for (const file of manifest.files) {
    const payload = bytesAt(id.safePath(path.join(packageDirectory,'payload'),file.path));
    const source = id.normalize(bytesAt(id.safePath(root,file.path)),file.sourceNormalization||'none');
    for (const actual of [payload,source]) {
      assert.equal(actual.length,file.bytes,file.path);assert.equal(id.hash(actual),file.sha256,file.path);
    }
  }
  return {files:manifest.files.length,manifestSha256:id.hash(bytesAt(path.join(packageDirectory,'sdk-manifest.json'))),
    integrityVerified:true,sourceMatches:true,firstAttemptExitCode:0,releaseReady:false};
}
function selfTest() {
  const xml = log('x86-attempt1/tests.xml'); nativeResults(xml);
  let negativeCases = 0;
  const reject = action => {assert.throws(action);++negativeCases;};
  assert.equal(decodeText(Buffer.from('\ufefftest\r\n','utf16le')),'test\n');
  reject(()=>decodeText(Buffer.from([0xff,0xfe,0x61])));reject(()=>decodeText(Buffer.from([0xc3,0x28])));
  const firstCase = xml.match(/<testcase\s[^>]*>[\s\S]*?<\/testcase>/)[0];
  for (const invalid of [xml.replace('skipped="0"','skipped="1"'),xml.replace('failures="0"','failures="1"'),
    xml.replace('status="run"','status="notrun"'),xml.replace('tests="10"','tests="9"'),
    xml.replace('sdk.native.contract','sdk.native.unknown'),xml.replace('session_renewal: 14','session_renewal: 13'),
    xml.replace(firstCase,''),xml.replace('</testsuite>',firstCase+'</testsuite>'),
    xml.replace('status="run"','status="run" status="run"'),xml.replace('<properties/>','<skipped/>'),
    xml.replace('</system-out>','</system-out><failure/>'),xml.slice(0,-20)]) reject(()=>nativeResults(invalid));
  const names = sourceTestNames(webFiles,62);
  const tap = 'TAP version 13\n'+names.map((name,index)=>`# Subtest: ${name}\nok ${index+1} - ${name}\n  ---\n  duration_ms: 0.1\n  type: 'test'\n  ...\n`).join('')+
    `1..62\n# tests 62\n# suites 0\n# pass 62\n# fail 0\n# cancelled 0\n# skipped 0\n# todo 0\n# duration_ms 1\n`;
  assert.equal(tapResults(tap,names).count,62);
  for (const invalid of [tap.replace('# skipped 0','# skipped 1'),tap.replace('# todo 0','# todo 1'),
    tap.replace('# fail 0','# fail 1'),tap.replace('# cancelled 0','# cancelled 1'),tap.replace('ok 1 -','not ok 1 -'),
    tap.replace(`ok 1 - ${names[0]}`,`ok 1 - ${names[0]} # SKIP`),tap.replace('1..62','1..61'),
    tap.replace('# tests 62','# tests 61'),tap.replaceAll(names[1],names[0]),tap.replace('ok 2 -','ok 1 -'),
    tap.replace(/^# Subtest:[\s\S]*?  \.\.\.\n/m,''),tap+'# tests 62\n',tap+'# unknown result\n',tap.slice(0,-25)]) reject(()=>tapResults(invalid,names));
  reject(()=>exactNames(['a','a'],['a','b'],'duplicate'));reject(()=>exactNames([],['a'],'missing'));
  const goodPackage = 'SDK package smoke: 14/14 passed (ownership, integrity, upgrade, rollback, IO recovery and paths).\n';
  assert.equal(packageResults(goodPackage),14);
  reject(()=>packageResults(goodPackage.replace('14/14','13/14')));reject(()=>packageResults(goodPackage+goodPackage));
  const result = {exitCode:0,program:'node',arguments:['--test'],startedAt:'2026-09-26T00:00:00Z',finishedAt:'2026-09-26T00:00:01Z'};
  commandResult(result,'node',['--test'],'self-test');
  for (const change of [{exitCode:1},{exitCode:'0'},{arguments:[]},{program:'other'},{timedOut:true},{finishedAt:'invalid'}]) reject(()=>commandResult({...result,...change},'node',['--test'],'self-test'));
  if (fs.existsSync(path.join(suite,'web-tests.result.json'))) tapResults(log('web-tests.log'),names);
  if (fs.existsSync(path.join(suite,'packaging-tests.result.json'))) tapResults(log('delivery-identity.log'),sourceTestNames(['tests/sdk/delivery-identity.test.cjs'],7));
  unchanged();
  console.log(JSON.stringify({collectorSelfTest:'passed',negativeCases,readOnlyInputsUnchanged:true,summaryWritten:false}));
}
function collect() {
  const frozen = read('source-inputs.json');
  assert.equal(frozen.sha256,'a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550');
  assert.equal(frozen.files.length,711);assert.deepEqual(frozen,id.source(root),'frozen source identities');
  const version = build.version(root);assert.equal(version,'0.3.0-preview.11');
  const architectures = ['x86','x64'].map(arch=>verifyArchitecture(arch,frozen));
  exactNames(fs.readdirSync(path.join(root,'tests/web')).filter(name=>name.endsWith('.test.mjs')),webFiles.map(name=>path.basename(name)),'all Web test files');
  const webResult = read('web-tests.result.json');
  commandResult(webResult,'node',['--test','--test-reporter=tap',...webFiles],'Web TAP tests');
  const webTests = tapResults(log('web-tests.log'),sourceTestNames(webFiles,62));
  const packageRuns = read('packaging-tests.result.json');
  exactNames(packageRuns.map(result=>result.name),['sdk-package-smoke','delivery-identity'],'packaging runs');
  for (const result of packageRuns) {
    commandResult(result,'node',result.name==='sdk-package-smoke'?['tests/sdk/sdk-package-smoke.cjs']:['--test','--test-reporter=tap','tests/sdk/delivery-identity.test.cjs'],result.name);
    assert.equal(result.rawFile,`${result.name}.log`);
  }
  const packageTestsPassed = packageResults(log('sdk-package-smoke.log'));
  const deliveryIdentityTests = tapResults(log('delivery-identity.log'),sourceTestNames(['tests/sdk/delivery-identity.test.cjs'],7));
  const audits = read('audits.result.json');
  exactNames(audits.map(result=>result.package),['sdk/web','templates/desktop-app/web','demos/workbench/web','tests/acceptance','benchmarks/hosts/electron'],'audits');
  for (const result of audits) {
    assert.equal(result.exitCode,0,result.package);assert.equal(result.rawFile,`audit-${result.package.replaceAll('/','-')}.json`);
    assert.equal(read(result.rawFile).metadata.vulnerabilities.total,0,result.rawFile);
  }
  const sdkPackage = verifySdkPackage();
  const desktopGate = read('desktop-gate.json');assert.equal(desktopGate.processes.length,2);
  exactNames(desktopGate.processes.map(p=>p.ProcessId),[81476,37500],'external QEMU process snapshot');
  for (const process of desktopGate.processes) assert.equal(process.Name,'qemu-system-x86_64.exe');
  assert.match(desktopGate.scope,/no desktop workload launched/i);
  const desktopGateAfterBuild = read('desktop-gate-after-build.json');
  assert.deepEqual(desktopGateAfterBuild.processes,[],'post-build desktop process snapshot');
  assert.ok(Date.parse(desktopGateAfterBuild.checkedAt)>=Date.parse(desktopGate.checkedAt),'desktop gate order');
  assert.match(desktopGateAfterBuild.scope,/no desktop test launched/i);
  const historicalFailures = [];
  for (const name of fs.readdirSync(suite).filter(name=>name.endsWith('.result.json'))) {
    const value = read(name);
    if (!Array.isArray(value) && Number.isInteger(value.exitCode) && value.exitCode!==0) {
      const logName = name.replace(/\.result\.json$/,'.log');
      assert.ok(fs.existsSync(path.join(suite,logName)),`preserved failure log: ${name}`);log(logName);
      historicalFailures.push({record:name,exitCode:value.exitCode,log:logName,sha256:consumed.get(id.safePath(suite,logName))});
    }
  }
  assert.ok(historicalFailures.some(value=>value.record==='web-renewal-review-red-attempt1.result.json'),'original review failure preserved');
  const summary = {schema:'soui.session-renewal-preflight/v1',createdAt:new Date().toISOString(),version,
    sourceSha256:frozen.sha256,sourceFiles:frozen.files.length,architectures,webTests,packageTestsPassed,
    deliveryIdentityTests,fullAudits:audits.length,sdkPackage,host:read('host-os.json'),desktopGate,desktopGateAfterBuild,historicalFailures,
    evidenceFiles:[...consumed].filter(([file])=>normalizedPath(file).startsWith(normalizedPath(suite)+'/')).map(([file,sha256])=>({path:path.relative(suite,file).replaceAll('\\','/'),sha256})),
    collectorValidation:'Exact executed test names/status/counts, archived/live build identities, installed bytes, external consumer linkage, package bytes and source identities.',
    testScope:'Eight SDK tests and two Demo tests without business windows, WebView or desktop interaction. Clock and platform simulations do not prove real-duration or GUI behavior.',
    pending:['real 30-minute and eight-hour renewal with task/subscription continuity','Win7 SP1 x86 guest','V02 IME, resize and scroll with tasks','V04 real tray/menu/shortcut/residency','Debug matrix','full source relocation','isolated long stability','Electron comparison','production approval'],
    desktopAndGuestExecuted:false,sourceDeliveryVerified:false,benchmarkValid:false,releaseReady:false};
  assert.deepEqual(frozen,id.source(root),'source remained frozen through collection');unchanged();
  fs.writeFileSync(path.join(suite,'summary.json'),id.encoded(summary));
  console.log(JSON.stringify({version,sourceSha256:frozen.sha256,testsPerArchitecture:architectures.map(value=>value.testsPassed),installedFiles:architectures.map(value=>value.installedSdk.files.length),webTestsPassed:webTests.count,packageTestsPassed,deliveryIdentityTestsPassed:deliveryIdentityTests.count,desktop:false,releaseReady:false}));
}
if (require.main===module) {
  assert.ok(process.argv.length===2||(process.argv.length===3&&process.argv[2]==='--self-test'),'only --self-test is accepted');
  if (process.argv[2]==='--self-test') selfTest();else collect();
}
module.exports = {nativeResults,tapResults,packageResults,commandResult,verifyArchitecture};
