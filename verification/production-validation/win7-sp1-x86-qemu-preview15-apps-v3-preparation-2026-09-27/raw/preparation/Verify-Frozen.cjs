'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=__dirname,hash=b=>crypto.createHash('sha256').update(b).digest('hex'),encode=o=>JSON.stringify(o,null,2)+'\n',read=p=>JSON.parse(fs.readFileSync(path.join(root,p),'utf8').replace(/^\uFEFF/,''));
function walk(rel=''){return fs.readdirSync(path.join(root,rel),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=rel?rel+'/'+e.name:e.name;return e.isDirectory()?walk(n):[n]}).sort()}
try {
 const f=read('freeze.json'),input=read('input.json'),build=read('build-Release.json'),self=read('self-test-results.json');
 assert.equal(f.schema,'soui.win7-app-preparation-freeze/v2');assert.equal(f.version,'0.3.0-preview.15');assert.equal(f.guestExecuted,false);assert.equal(f.releaseReady,false);
 assert.equal(f.buildId,'b8c26d8d-7144-4eb0-b889-0034ec8029fb');assert.equal(f.sourceSha256,'b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07');
 assert.equal(hash(encode(f.files)),f.filesSha256);assert.deepEqual(walk().filter(n=>n!=='freeze.json'),f.files.map(e=>e.path));
 for(const e of f.files){assert(!e.path.includes('\\')&&!e.path.split('/').some(n=>!n||n==='..'||n==='.'||n.includes(':')));const b=fs.readFileSync(path.join(root,e.path));assert.equal(b.length,e.bytes);assert.equal(hash(b),e.sha256,e.path);}
 assert.equal(hash(fs.readFileSync(path.join(root,'input.json'))),f.inputSha256);assert.equal(input.version,f.version);assert.equal(input.buildId,f.buildId);assert.equal(input.sourceSha256,f.sourceSha256);
 assert.equal(input.buildRecordSha256,hash(fs.readFileSync(path.join(root,'build-Release.json'))));assert.equal(build.buildId,f.buildId);assert.equal(build.source.sha256,f.sourceSha256);assert.equal(build.version,f.version);
 assert.equal(input.harnessParentFreezeSha256,hash(fs.readFileSync(path.join(root,'harness-parent-freeze.json'))));
 assert.equal(input.layoutVersion,3);assert.equal(input.mountRoot,path.join(root,'volume'));assert.equal(input.inputSubdir,'soui-input');assert.deepEqual(fs.readdirSync(input.mountRoot),['soui-input']);
 assert.equal(input.closeDiagnostics.deadlineMs,60000);assert.equal(input.closeDiagnostics.atMs,55000);
 const transfer=path.join(input.mountRoot,input.inputSubdir);assert.deepEqual(fs.readdirSync(transfer).sort(),input.transferFiles.map(e=>e.path));
 for(const e of input.transferFiles){const b=fs.readFileSync(path.join(transfer,e.path));assert.equal(b.length,e.bytes);assert.equal(hash(b),e.sha256);}
 for(const name of ['SouiStarter','SouiWorkbench']) {const p=input.packages[name];assert.deepEqual(p.files,build.payloads[name].files);assert.equal(p.payloadSha256,build.payloads[name].sha256);assert.equal(hash(encode(p.files)),p.payloadSha256);for(const e of p.files){const b=fs.readFileSync(path.join(root,'payloads',name,e.path));assert.equal(b.length,e.bytes);assert.equal(hash(b),e.sha256);}const zip=fs.readFileSync(path.join(transfer,p.zipName));assert.equal(zip.length,p.zipBytes);assert.equal(hash(zip),p.zipSha256);}
 assert.equal(self.inputSha256,f.inputSha256);assert.equal(self.acceptedFixtures,1);assert.equal(self.rejectedFixtures,27);assert.equal(self.syntheticOnly,true);assert.equal(self.guestExecuted,false);
 console.log(JSON.stringify({status:'frozen-preparation-intact',version:f.version,files:f.files.length,freezeSha256:hash(fs.readFileSync(path.join(root,'freeze.json'))),inputSha256:f.inputSha256,buildId:f.buildId,sourceSha256:f.sourceSha256,guestExecuted:false,releaseReady:false}));
} catch(e){console.error(e.stack);process.exitCode=1;}
