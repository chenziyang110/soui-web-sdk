'use strict';
// Read-only verifier for one generated project. Only summary-generated.json is written.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve('C:/SOUI-SDK-development/preview9-tray');
const suite = __dirname;
const project = path.join(suite,'generated-project-attempt1');
const packageDirectory = path.join(suite,'sdk-package-attempt1');
const name = 'RenewalStarter', version = '0.3.0-preview.11';
const id = require(path.join(root,'tools/internal/delivery-identity.cjs'));
const build = require(path.join(root,'tools/internal/build-evidence.cjs'));
const packaging = require(path.join(root,'tools/sdk/soui-sdk.cjs'));
const {commandResult} = require('./collect-summary.cjs');
const consumed = new Map();
const normalizedPath = value=>value.replaceAll('\\','/').toLowerCase();
const excluded = new Set(['node_modules','dist','out','.vs']);
function bytes(file) {
  const data = fs.readFileSync(file), hash = id.hash(data);
  if (consumed.has(file)) assert.equal(consumed.get(file),hash,`changed during validation: ${file}`);
  consumed.set(file,hash);return data;
}
function text(file) {
  const data = bytes(file), utf16 = data.length>=2&&data[0]===0xff&&data[1]===0xfe;
  if(utf16)assert.equal(data.length%2,0,'complete UTF-16LE input');
  return new TextDecoder(utf16?'utf-16le':'utf-8',{fatal:true}).decode(data).replace(/^\uFEFF/,'');
}
const read = name=>JSON.parse(text(id.safePath(suite,name)));
const log = name=>text(id.safePath(suite,name)).replace(/\r\n/g,'\n');
function equalBytes(actual,expected,label) {
  assert.equal(actual.length,expected.length,`${label}: length`);
  assert.equal(id.hash(actual),id.hash(expected),`${label}: bytes`);
}
function exactNames(actual,expected,label) {
  assert.equal(new Set(actual).size,actual.length,`${label}: duplicate`);
  assert.deepEqual([...actual].sort(),[...expected].sort(),`${label}: names`);
}
function insideGenerated(file) {
  assert.ok(normalizedPath(path.resolve(file)).startsWith(normalizedPath(project)+'/'),`outside generated project: ${file}`);
}
const textExtensions = new Set(['.cpp','.h','.txt','.json','.html','.ts','.tsx','.css','.md','.xml','.manifest','.rc','.mjs','.cjs']);
function templateBytes(relative,data) {
  if (!textExtensions.has(path.extname(relative).toLowerCase())) return data;
  return Buffer.from(new TextDecoder('utf-8',{fatal:true}).decode(data).replace(/^\uFEFF/,'')
    .replaceAll('SouiStarter',name).replaceAll('soui-starter',name.toLowerCase())
    .replaceAll('../../../sdk/','../sdk/').replaceAll('../../../tools/','../tools/'),'utf8');
}
function machine(binary,expected) {
  assert.ok(binary.length>=64);assert.equal(binary.toString('ascii',0,2),'MZ');
  const offset=binary.readUInt32LE(0x3c);assert.ok(offset>=64&&offset+24<=binary.length);
  assert.equal(binary.toString('binary',offset,offset+4),'PE\0\0');
  assert.equal(binary.readUInt16LE(offset+4),expected);
  return expected.toString(16).padStart(4,'0');
}
function verifyTemplate(manifest) {
  const template=path.join(root,'templates/desktop-app');
  const names=id.walk(template,'',(_,leaf)=>excluded.has(leaf));
  assert.equal(names.length,15,'current template files');
  for(const relative of names) {
    assert.ok(!manifest.files.some(file=>file.path===relative),'template/SDK ownership overlap');
    equalBytes(bytes(id.safePath(project,relative)),templateBytes(relative,bytes(id.safePath(template,relative))),relative);
  }
  assert.deepEqual(read('generated-project-attempt1/project.json'),{schema:'soui.application/v1',name,sdkLock:'sdk-lock.json',templateVersion:version,nativeTarget:name,entry:'web/index.html',build:'tools/Build.ps1'});
  equalBytes(bytes(path.join(project,'VERSION')),Buffer.from(version+'\r\n'),'generated VERSION');
  equalBytes(bytes(path.join(project,'.gitignore')),bytes(path.join(root,'.gitignore')),'generated gitignore');
  equalBytes(bytes(path.join(project,'.gitattributes')),Buffer.from('* text=auto eol=lf\n'+manifest.files.map(file=>file.path+' -text\n').join('')),'pinned SDK attributes');
  const extras=['project.json','VERSION','.gitignore','.gitattributes','sdk-lock.json'];
  exactNames(id.walk(project,'',(_,leaf)=>excluded.has(leaf)),[...names,...manifest.files.map(file=>file.path),...extras],'complete generated source tree');
  const app=text(path.join(project,'app/main.cpp'));
  assert.match(app,/if\s*\(\s*!bridge\.enable_session_renewal\(\)\s*\)\s*return 11;/,'template explicitly opts into renewal');
  assert.match(text(path.join(project,'web/src/native.ts')),/return connectSouiNative<AppMethods>\(\);/,'public lease-aware connector');
  assert.match(text(path.join(project,'web/vite.config.ts')),/path\.resolve\(__dirname,'\.\.\/sdk\/web\/src\/index\.ts'\)/,'local generated Web SDK alias');
  return {files:names.length,allowedReplacements:[['SouiStarter',name],['soui-starter',name.toLowerCase()],['../../../sdk/','../sdk/'],['../../../tools/','../tools/']],
    exactBytesAfterDeclaredReplacements:true,explicitNativeRenewalOptIn:true,publicWebConnector:true};
}
function verifyBuildInputs(arch) {
  const dir=path.join(project,'out',arch),platform=arch==='x86'?'Win32':'x64';
  const cache=text(path.join(dir,'CMakeCache.txt')).replace(/\r\n/g,'\n');
  for(const [key,value] of [['CMAKE_HOME_DIRECTORY',project],['RenewalStarter_SOURCE_DIR',project],
    ['SouiSdk_SOURCE_DIR',path.join(project,'sdk')],['SOUI_DISTRIBUTION_ROOT',project],['CMAKE_GENERATOR_PLATFORM',platform]]) {
    const matches=[...cache.matchAll(new RegExp(`^${key}:[^=]+=([^\\n]*)`,'gm'))];assert.equal(matches.length,1,key);
    assert.equal(normalizedPath(matches[0][1]),normalizedPath(value),key);
  }
  assert.ok(!normalizedPath(cache).includes(normalizedPath(root)),'CMake cache excludes original checkout');
  const projects=id.walk(dir).filter(file=>file.endsWith('.vcxproj'));
  assert.ok(projects.includes('RenewalStarter.vcxproj')&&projects.includes('sdk/soui_sdk.vcxproj'));
  for(const relative of projects) {
    const file=path.join(dir,relative),xml=text(file),normalized=normalizedPath(xml);
    for(const forbidden of [root,'F:/work/SOUI_WEB'])assert.ok(!normalized.includes(normalizedPath(forbidden)),`${relative}: original checkout dependency`);
    for(const match of xml.matchAll(/<(?:ClCompile|ResourceCompile|ProjectReference) Include="([^"<>]+)"/g)) {
      const dependency=path.resolve(path.dirname(file),match[1]);insideGenerated(dependency);
      assert.ok(fs.statSync(dependency).isFile(),`${relative}: actual referenced input exists`);
    }
  }
  const appProject=text(path.join(dir,'RenewalStarter.vcxproj'));
  const release=[...appProject.matchAll(/<ItemDefinitionGroup Condition="([^"<>]*)">([\s\S]*?)<\/ItemDefinitionGroup>/g)].filter(match=>match[1].includes(`'Release|${platform}'`));
  assert.equal(release.length,1,'Release link configuration');
  const links=[...release[0][2].matchAll(/<AdditionalDependencies>([^<>]*)<\/AdditionalDependencies>/g)];assert.equal(links.length,1);
  const libs=links[0][1].split(';').map(normalizedPath);
  assert.deepEqual(libs.filter(value=>value.includes('/')||value.includes(':')),['sdk/release/soui_sdk.lib','sdk/release/soui_zlib.lib'],'only generated SDK relative library inputs');
  assert.ok(libs.includes('bcrypt.lib'),'native renewal entropy link dependency');
  for(const relative of ['sdk/Release/soui_sdk.lib','sdk/Release/soui_zlib.lib'])assert.ok(bytes(path.join(dir,relative)).length>0,'actual generated SDK library');
  const libraryPaths=[...release[0][2].matchAll(/<AdditionalLibraryDirectories>([^<>]*)<\/AdditionalLibraryDirectories>/g)];assert.equal(libraryPaths.length,1);
  assert.deepEqual(libraryPaths[0][1].split(';').map(normalizedPath),[normalizedPath(path.join(project,`third_party/prebuilt/${arch}/soui/lib`)),normalizedPath(path.join(project,`third_party/prebuilt/${arch}/soui/lib/$(Configuration)`)),'%(additionallibrarydirectories)']);
  return {projectFilesChecked:projects.length,sourceAndSdkRootsInsideGeneratedProject:true,originalCheckoutReferences:false};
}
function verify() {
  const frozen=read('source-inputs.json');assert.deepEqual(frozen,id.source(root),'current SDK source remains frozen');
  assert.equal(frozen.sha256,'a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550');
  const createArgs=['-NoProfile','-File',path.join(root,'tools/New-Project.ps1'),'-Name',name,'-OutputRoot',project,'-SdkPackage',packageDirectory];
  commandResult(read('generated-create.result.json'),'powershell.exe',createArgs,'generated-create');
  commandResult(read('generated-verify-project.result.json'),'node.exe',[path.join(root,'tools/sdk/soui-sdk.cjs'),'verify-project','--project',project],'generated-verify-project');
  const creation=log('generated-create.log');
  const creationParts=/^(\{[\s\S]*?\})\n(\{[\s\S]*?\})\nCreated ([^\n]+)\n?$/.exec(creation);assert.ok(creationParts,'complete generation log');
  const verified={status:'verified',version,files:418,releaseReady:false};
  assert.deepEqual(JSON.parse(creationParts[1]),verified);
  assert.deepEqual(JSON.parse(creationParts[2]),{status:'installed',version,files:418,projectRoot:project,releaseReady:false});
  assert.equal(creationParts[3],`${name} with SDK ${version}: ${project}`);
  assert.deepEqual(JSON.parse(log('generated-verify-project.log')),verified);
  const manifest=packaging.readPackage(packageDirectory),lock=packaging.readLock(project);
  assert.equal(manifest.version,version);assert.equal(manifest.files.length,418);assert.deepEqual(lock.manifest,manifest,'generated SDK lock exactly matches package');
  assert.equal(lock.manifestSha256,id.hash(id.encoded(manifest)));
  bytes(path.join(project,'sdk-lock.json'));bytes(path.join(packageDirectory,'sdk-manifest.json'));
  exactNames(id.walk(path.join(packageDirectory,'payload')),manifest.files.map(file=>file.path),'package exact paths');
  for(const entry of manifest.files)equalBytes(bytes(id.safePath(project,entry.path)),bytes(id.safePath(path.join(packageDirectory,'payload'),entry.path)),entry.path);
  const template=verifyTemplate(manifest),source=id.source(project),architectures=[];
  for(const arch of ['x86','x64']) {
    commandResult(read(`generated-build-${arch}.result.json`),'powershell.exe',['-NoProfile','-File',path.join(project,'tools/Build.ps1'),'-Architecture',arch,'-Configuration','Release'],`generated-build-${arch}`);
    const current=build.verifyBuild(project,arch,'Release');
    assert.deepEqual(read(`generated-${arch}-build-Release.json`),current,`${arch}: archived/live build receipt`);
    assert.deepEqual(current.source,source,`${arch}: exact current generated sources`);
    exactNames(Object.keys(current.payloads),[name],'generated build payload targets');
    equalBytes(bytes(path.join(suite,`generated-${arch}-native-Release.log`)),bytes(path.join(project,`out/${arch}/native-Release.log`)),`${arch}: archived/live native build log`);
    const inputs=verifyBuildInputs(arch),appRoot=path.join(project,`out/${arch}/bin/Release/${name}`);
    const pe=machine(bytes(path.join(appRoot,`${name}.exe`)),arch==='x86'?0x14c:0x8664);
    architectures.push({architecture:arch,configuration:'Release',buildId:current.buildId,sourceSha256:current.source.sha256,
      peMachine:pe,payload:current.payloads[name],buildInputs:inputs});
  }
  assert.deepEqual(source,id.source(project),'generated source unchanged during verification');
  assert.deepEqual(frozen,id.source(root),'SDK source unchanged during verification');
  for(const [file,sha256]of consumed)assert.equal(id.hash(fs.readFileSync(file)),sha256,`evidence bytes unchanged: ${file}`);
  const summary={schema:'soui.generated-session-renewal-preflight/v1',createdAt:new Date().toISOString(),version,projectName:name,
    sdkSourceSha256:frozen.sha256,generatedSourceSha256:source.sha256,generatedSourceFiles:source.files.length,
    sdkPackage:{files:418,manifestSha256:lock.manifestSha256,lockMatches:true,installedBytesMatch:true},template,architectures,
    evidenceFiles:[...consumed].filter(([file])=>normalizedPath(file).startsWith(normalizedPath(suite)+'/')).map(([file,sha256])=>({path:path.relative(suite,file).replaceAll('\\','/'),sha256})),
    scope:'Generated, independently compiled x86/x64 template using its pinned SDK copy. Build inputs and current bytes verified; no GUI execution or real-duration renewal claim.',
    desktopAndGuestExecuted:false,sourceDeliveryVerified:false,benchmarkValid:false,releaseReady:false};
  fs.writeFileSync(path.join(suite,'summary-generated.json'),id.encoded(summary));
  console.log(JSON.stringify({version,project:name,sdkPackageFiles:418,templateFiles:template.files,generatedSourceSha256:source.sha256,
    architectures:architectures.map(value=>({architecture:value.architecture,peMachine:value.peMachine,buildId:value.buildId})),desktop:false,releaseReady:false}));
}
if(require.main===module){assert.equal(process.argv.length,2,'no arguments are accepted');verify();}
