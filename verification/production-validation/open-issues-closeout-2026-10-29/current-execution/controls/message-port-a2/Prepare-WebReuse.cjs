'use strict';
// Read/copy-only preparation; does not execute a compiler, package manager, typechecker, product or VM.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const repo='C:/sc07',sealed='C:/sr06',original='F:/work/soui-issue96/out/acceptance/bd7afa33-ea0d-4e55-b482-d2b01f64ee19';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=f=>{const before=fs.lstatSync(f,{bigint:true});assert(before.isFile()&&!before.isSymbolicLink());const b=fs.readFileSync(f),after=fs.lstatSync(f,{bigint:true});for(const k of ['dev','ino','size','mtimeNs','ctimeNs'])assert.equal(before[k],after[k]);return {path:f,bytes:b.length,sha256:hash(b)};};
const write=(f,o)=>fs.writeFileSync(f,JSON.stringify(o,null,2)+'\n',{flag:'wx'});
function walk(d,p=''){return fs.readdirSync(path.join(d,p),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=p?p+'/'+e.name:e.name;return e.isDirectory()?walk(d,n):(assert(e.isFile()),[n]);}).sort();}
const source=repo+'/tests/message-port-app',sourceCopy=path.join(__dirname,'source'),webCopy=path.join(__dirname,'web-output'),evidence=path.join(__dirname,'web-reuse');
for(const d of [sourceCopy,webCopy,evidence]){assert(!fs.existsSync(d));fs.mkdirSync(d);}
const sourcePins=[pin(__filename)],copies=[];
function remember(f){const p=pin(f);sourcePins.push(p);return p;}
function copy(a,b){const p=remember(a);fs.mkdirSync(path.dirname(b),{recursive:true});fs.copyFileSync(a,b,fs.constants.COPYFILE_EXCL);const q=pin(b);assert.equal(q.bytes,p.bytes);assert.equal(q.sha256,p.sha256);copies.push({source:p,copied:q});return q;}
for(const name of walk(source))copy(path.join(source,name),path.join(sourceCopy,name));
const ordinary=remember(repo+'/tests/acceptance-app/shutdown-poll.h');assert.equal(ordinary.sha256,pin(sourceCopy+'/shutdown-poll.h').sha256);
const web=JSON.parse(fs.readFileSync(original+'/web-build.json')),build=JSON.parse(fs.readFileSync(original+'/build.json'));
assert.equal(web.webSdkSha256,build.webSdk.sha256);assert.equal(web.consumerSourceSha256,build.consumerSource.sha256);assert.equal(web.typecheck.passed,true);
for(const name of ['web-build.json','build.json','web-typecheck.tsconfig.json','web-typecheck.log'])copy(original+'/'+name,path.join(evidence,'original-'+name));
const oldDist=walk(original+'/web-sdk/dist'),currentDist=walk(sealed+'/sdk/web/dist');assert.deepEqual(oldDist,currentDist);
const sdkEquivalence=[];
for(const name of oldDist){const before=remember(original+'/web-sdk/dist/'+name),current=remember(sealed+'/sdk/web/dist/'+name);assert.equal(before.bytes,current.bytes);assert.equal(before.sha256,current.sha256);sdkEquivalence.push({name,original:before,current});}
const a=JSON.parse(fs.readFileSync(original+'/web-sdk/package.json')),b=JSON.parse(fs.readFileSync(sealed+'/sdk/web/package.json'));
remember(original+'/web-sdk/package.json');remember(sealed+'/sdk/web/package.json');assert.equal(a.version,'0.5.1-preview.1');assert.equal(b.version,'0.6.0');
const changedKeys=Object.keys({...a,...b}).filter(k=>JSON.stringify(a[k])!==JSON.stringify(b[k]));assert.deepEqual(changedKeys,['version']);
const fixtureEquivalence=[];
for(const name of walk(source+'/web')){const old=remember(original+'/consumer-source/web/'+name),current=remember(source+'/web/'+name);assert.equal(old.bytes,current.bytes);assert.equal(old.sha256,current.sha256);fixtureEquivalence.push({name,original:old,current});}
for(const expected of web.output.files){const p=pin(original+'/web-output/'+expected.path);assert.equal(p.bytes,expected.bytes);assert.equal(p.sha256,expected.sha256);copy(original+'/web-output/'+expected.path,path.join(webCopy,expected.path));}
assert.deepEqual(walk(webCopy),web.output.files.map(f=>f.path).sort());
for(const p of sourcePins)assert.deepEqual(pin(p.path),p);
write(path.join(evidence,'Reuse.actual.json'),{schema:'soui.current-message-port-web-byte-reuse/v1',createdUtc:new Date().toISOString(),sourcePins,copies,
  currentFixtureSource:source,currentNativeSourceIdentityRequired:true,originalWebProducer:original,originalVersion:'0.5.1-preview.1',currentInstalledSdkVersion:'0.6.0',
  originalWebBuild:web,fixtureWebByteEquivalence:fixtureEquivalence,publicSdkExecutableDistByteEquivalence:sdkEquivalence,packageMetadataChangedKeys:changedKeys,
  versionMetadataNotUsedAsBundleCode:'Original build-web aliases directly to dist/index.js and TypeScript paths directly to dist/index.d.ts; exact executable/declaration files match. Original package/version and typecheck records remain unchanged.',
  ordinaryShutdownHelperByteIdentical:true,webOutput:web.output,webOutputCopiedTo:webCopy,webSdkRebuilt:false,webRebuilt:false,typecheckReexecuted:false,
  originalNativeConsumerIdentityReused:false,oldFailureReclassified:false,productExecuted:false,productionQualificationComplete:false,releaseReady:false});
console.log(JSON.stringify({reuse:path.join(evidence,'Reuse.actual.json'),sourceFiles:walk(sourceCopy).length,webFiles:walk(webCopy).length,sdkDistEquivalentFiles:sdkEquivalence.length,fixtureWebEquivalentFiles:fixtureEquivalence.length,buildExecuted:false}));
