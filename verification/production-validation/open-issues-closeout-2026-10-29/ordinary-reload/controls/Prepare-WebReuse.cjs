'use strict';
// Metadata/read/copy preparation only; never builds or runs a typechecker/test.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const repo='C:/sc07',sealed='C:/sr06',oldRoot='F:/work/soui-issue96';
const original=oldRoot+'/out/acceptance/2566629a-7a06-4302-8fbf-04809a50da86';
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
function pin(p){const a=fs.lstatSync(p,{bigint:true});assert(a.isFile()&&!a.isSymbolicLink());const b=fs.readFileSync(p),z=fs.lstatSync(p,{bigint:true});for(const k of ['dev','ino','size','mtimeNs','ctimeNs','nlink'])assert.equal(a[k],z[k]);return {path:p,bytes:b.length,sha256:digest(b)};}
function walk(d,p=''){return fs.readdirSync(path.join(d,p),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=p?p+'/'+e.name:e.name;return e.isDirectory()?walk(d,n):(assert(e.isFile()),[n]);}).sort();}
const sourcePins=[pin(__filename)],copies=[];
const remember=p=>{const r=pin(p);sourcePins.push(r);return r;};
function copy(a,b){const before=remember(a);fs.mkdirSync(path.dirname(b),{recursive:true});if(fs.existsSync(b)){assert.equal(pin(b).sha256,before.sha256);assert.equal(pin(b).bytes,before.bytes);}else fs.copyFileSync(a,b,fs.constants.COPYFILE_EXCL);const after=pin(a),copied=pin(b);assert.deepEqual(after,before);assert.equal(copied.bytes,before.bytes);assert.equal(copied.sha256,before.sha256);copies.push({source:before,copied,sourceAfter:after});}
const source=repo+'/tests/acceptance-app',frozen=path.join(__dirname,'source'),output=path.join(__dirname,'web-output'),evidence=path.join(__dirname,'web-reuse');
assert(fs.existsSync(frozen));assert(!fs.existsSync(output)&&!fs.existsSync(evidence));fs.mkdirSync(output);fs.mkdirSync(evidence);
for(const name of walk(source))copy(path.join(source,name),path.join(frozen,name));
assert.equal(pin(frozen+'/main.cpp').sha256,'4d58281549806425f097ee0ca9bc04160f43ca112c027b46b4dfc199a8173be4');
const identityFile=repo+'/tools/internal/delivery-identity.cjs',identity=require(identityFile);remember(identityFile);
const buildFile=original+'/build.json',webFile=original+'/web-build.json',build=JSON.parse(fs.readFileSync(buildFile)),web=JSON.parse(fs.readFileSync(webFile));
remember(buildFile);remember(webFile);
assert.equal(build.schema,'soui.acceptance-build/v3');assert.equal(web.schema,'soui.acceptance-web/v1');assert.equal(build.version,'0.5.1-preview.1');
assert.equal(web.consumerSourceSha256,build.consumerSource.sha256);assert.equal(web.webSdkSha256,build.webSdk.sha256);assert.deepEqual(web,build.web);
assert.deepEqual(identity.snapshot(original+'/consumer-source',identity.walk(original+'/consumer-source')),build.consumerSource);
assert.deepEqual(identity.snapshot(original+'/web-sdk',identity.walk(original+'/web-sdk')),build.webSdk);
assert.deepEqual(identity.snapshot(original+'/web-output',identity.walk(original+'/web-output')),web.output);
assert.equal(web.typecheck.passed,true);assert.equal(web.typecheck.typescript,'5.9.3');
const configFile=original+'/web-typecheck.tsconfig.json',logFile=original+'/web-typecheck.log',config=JSON.parse(fs.readFileSync(configFile));
assert.equal(pin(configFile).sha256,web.typecheck.configSha256);assert.equal(pin(logFile).sha256,web.typecheck.logSha256);
assert.equal(path.win32.normalize(config.compilerOptions.baseUrl).toLowerCase(),path.win32.normalize(original).toLowerCase());
assert.deepEqual(config.files.map(n=>path.win32.normalize(n).toLowerCase()),[path.win32.join(original,'consumer-source/web/main.ts').toLowerCase()]);
assert.deepEqual(config.compilerOptions.paths,{'@soui-sdk':['web-sdk/dist/index.d.ts']});
const webNames=['index.html','main.ts'];assert.deepEqual(walk(source+'/web'),webNames);assert.deepEqual(walk(original+'/consumer-source/web'),webNames);
const fixtureWebByteEquivalence=webNames.map(name=>{const a=remember(original+'/consumer-source/web/'+name),b=remember(source+'/web/'+name);assert.equal(a.bytes,b.bytes);assert.equal(a.sha256,b.sha256);return {name,original:a,current:b};});
const distNames=walk(original+'/web-sdk/dist');assert.equal(distNames.length,26);assert.deepEqual(distNames,walk(sealed+'/sdk/web/dist'));
const publicSdkExecutableDistByteEquivalence=distNames.map(name=>{const a=remember(original+'/web-sdk/dist/'+name),b=remember(sealed+'/sdk/web/dist/'+name);assert.equal(a.bytes,b.bytes);assert.equal(a.sha256,b.sha256);return {name,original:a,current:b};});
const oldPackage=original+'/web-sdk/package.json',newPackage=sealed+'/sdk/web/package.json';remember(oldPackage);remember(newPackage);
const a=JSON.parse(fs.readFileSync(oldPackage)),b=JSON.parse(fs.readFileSync(newPackage));assert.equal(a.version,'0.5.1-preview.1');assert.equal(b.version,'0.6.0');
const packageMetadataChangedKeys=Object.keys({...a,...b}).filter(k=>JSON.stringify(a[k])!==JSON.stringify(b[k]));assert.deepEqual(packageMetadataChangedKeys,['version']);
const sourceDistScope={repositorySource:'C:/sc07/sdk/web/src',sealedSource:'C:/sr06/sdk/web/src',files:[]};
assert.deepEqual(walk(sourceDistScope.repositorySource),walk(sourceDistScope.sealedSource));
for(const name of walk(sourceDistScope.repositorySource)){const x=remember(path.join(sourceDistScope.repositorySource,name)),y=remember(path.join(sourceDistScope.sealedSource,name));assert.equal(x.bytes,y.bytes);assert.equal(x.sha256,y.sha256);sourceDistScope.files.push({name,repository:x,sealed:y});}
const producerControls=[];
for(const name of ['tests/acceptance/build-web.mjs','tests/acceptance/package.json','tests/acceptance/package-lock.json','tools/internal/create-miniblink-entry.mjs']){
 const originalPin=remember(oldRoot+'/'+name),currentPin=remember(repo+'/'+name);producerControls.push({name,original:originalPin,current:currentPin,byteEqual:originalPin.bytes===currentPin.bytes&&originalPin.sha256===currentPin.sha256});
}
assert.equal(producerControls.find(r=>r.name==='tests/acceptance/build-web.mjs').byteEqual,true);
const oldAcceptancePackage=JSON.parse(fs.readFileSync(oldRoot+'/tests/acceptance/package.json')),currentAcceptancePackage=JSON.parse(fs.readFileSync(repo+'/tests/acceptance/package.json'));
assert.equal(oldAcceptancePackage.devDependencies.typescript,web.typecheck.typescript);assert.equal(currentAcceptancePackage.devDependencies.typescript,web.typecheck.typescript);
for(const name of ['build.json','web-build.json','web-typecheck.tsconfig.json','web-typecheck.log','web-build.log'])copy(original+'/'+name,path.join(evidence,'original-'+name));
for(const row of web.output.files){const f=remember(original+'/web-output/'+row.path);assert.equal(f.bytes,row.bytes);assert.equal(f.sha256,row.sha256);copy(f.path,path.join(output,row.path));}
assert.deepEqual(walk(output),web.output.files.map(r=>r.path).sort());
for(const row of sourcePins)assert.deepEqual(pin(row.path),row);
const result={schema:'soui.current-ordinary-web-byte-reuse/v1',createdUtc:new Date().toISOString(),sourcePins,copies,currentFixtureSource:source,currentNativeSourceIdentityRequired:true,
 originalWebProducer:original,originalVersion:'0.5.1-preview.1',currentInstalledSdkVersion:'0.6.0',originalWebBuild:web,
 fixtureWebByteEquivalence,publicSdkExecutableDistByteEquivalence,currentVsSealedSdkWebSource:sourceDistScope,packageMetadataChangedKeys,
 originalTypecheckConfig:pin(configFile),originalTypecheckCwd:config.compilerOptions.baseUrl,originalTypecheckRecordedFiles:config.files,originalTypecheckDeclaredAlias:config.compilerOptions.paths,
 producerControls,originalAcceptanceTools:oldAcceptancePackage,currentAcceptanceTools:currentAcceptancePackage,
 provenanceLimits:['Original Vite6.4.4 output is reused as exact immutable test assets; current tools declare Vite6.4.5. No current-bundler/typechecker execution or equivalent current-toolchain output claim.',
 'Current classic-entry generator includes Web target support and differs from the original producer; this preparation never invokes either generator or rewrites old generated HTML.',
 'Original absolute typecheck config/CWD, completion time and whole-consumer fingerprint remain original. They are not retagged as the patched native consumer.',
 'Public SDK dist has exact matching runtime/declaration bytes; package version changes only metadata because the original build aliases dist files directly.',
 'Original compiler loaded-binary identity is not reconstructed from version metadata.'],
 webOutput:web.output,webOutputCopiedTo:output,webSdkRebuilt:false,webRebuilt:false,typecheckReexecuted:false,currentToolchainOutputEquivalenceClaimed:false,
 originalNativeConsumerIdentityReused:false,oldFailureReclassified:false,productExecuted:false,productionQualificationComplete:false,releaseReady:false};
fs.writeFileSync(path.join(evidence,'Reuse.actual.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({reuse:pin(path.join(evidence,'Reuse.actual.json')),sourceFiles:walk(frozen).length,webFiles:walk(output).length,webBytes:web.output.bytes,sdkDistEquivalentFiles:distNames.length,fixtureWebEquivalentFiles:fixtureWebByteEquivalence.length,buildExecuted:false,currentToolchainOutputEquivalenceClaimed:false}));
