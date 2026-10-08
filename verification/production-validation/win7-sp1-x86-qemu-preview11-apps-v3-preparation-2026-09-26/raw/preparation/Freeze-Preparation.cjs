'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=__dirname;
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const encoded=v=>JSON.stringify(v,null,2)+'\n';
if(fs.existsSync(path.join(root,'freeze.json'))) throw new Error('Refuse to replace the existing freeze');
function walk(rel='') {
  return fs.readdirSync(path.join(root,rel),{withFileTypes:true}).flatMap(e=>{
    const name=rel?`${rel}/${e.name}`:e.name;
    if(e.isSymbolicLink()) throw new Error('Linked path: '+name);
    return e.isDirectory()?walk(name):[name];
  }).sort();
}
const files=walk().map(name=>{const b=fs.readFileSync(path.join(root,name));return {path:name,bytes:b.length,sha256:hash(b)}});
const input=JSON.parse(fs.readFileSync(path.join(root,'input.json'),'utf8'));
const manifest={schema:'soui.win7-app-preparation-freeze/v1',frozenAt:new Date().toISOString(),version:input.version,
  candidateCommit:'bec71cef4135087b76ccb368db8544950c1806b8',
  buildId:input.buildId,sourceSha256:input.sourceSha256,guestExecuted:false,releaseReady:false,
  diagnosticsVersion:input.diagnosticsVersion,parentFreezeSha256:input.parentFreezeSha256,
  layoutVersion:input.layoutVersion,mountRoot:input.mountRoot,inputSubdir:input.inputSubdir,
  filesSha256:hash(encoded(files)),files};
fs.writeFileSync(path.join(root,'freeze.json'),encoded(manifest),{flag:'wx'});
process.stdout.write(JSON.stringify({status:'preparation-frozen',files:files.length,freezeSha256:hash(encoded(manifest)),guestExecuted:false,releaseReady:false})+'\n');
