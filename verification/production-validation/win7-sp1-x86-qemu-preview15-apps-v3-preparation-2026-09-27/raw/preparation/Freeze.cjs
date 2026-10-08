'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=__dirname,hash=b=>crypto.createHash('sha256').update(b).digest('hex'),encode=o=>JSON.stringify(o,null,2)+'\n';
function walk(rel=''){return fs.readdirSync(path.join(root,rel),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=rel?rel+'/'+e.name:e.name;return e.isDirectory()?walk(n):[n]}).sort()}
const input=JSON.parse(fs.readFileSync(path.join(root,'input.json'),'utf8'));
assert.equal(input.buildId,'b8c26d8d-7144-4eb0-b889-0034ec8029fb');assert(!fs.existsSync(path.join(root,'freeze.json')));
const files=walk().map(p=>{const b=fs.readFileSync(path.join(root,p));return{path:p,bytes:b.length,sha256:hash(b)}});
const manifest={schema:'soui.win7-app-preparation-freeze/v2',version:input.version,buildId:input.buildId,sourceSha256:input.sourceSha256,inputSha256:hash(fs.readFileSync(path.join(root,'input.json'))),layoutVersion:3,filesSha256:hash(encode(files)),files,frozenAt:new Date().toISOString(),guestExecuted:false,releaseReady:false};
fs.writeFileSync(path.join(root,'freeze.json'),encode(manifest),{flag:'wx'});
console.log(JSON.stringify({status:'current-guest-preparation-frozen',files:files.length,sha256:hash(encode(manifest)),guestExecuted:false}));
