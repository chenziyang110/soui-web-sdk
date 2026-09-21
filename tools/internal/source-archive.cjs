'use strict';
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const root = path.resolve(__dirname,'../..');
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const upstream = ['third_party/sources/miniblink','third_party/sources/soui'];
const skipDirs = new Set(['out','dist','node_modules','.git','.vs']);
const manifestFile = path.join(root,'source-manifest.json');
function collect(relative='') {
 const result = [];
 for(const entry of fs.readdirSync(path.join(root,relative),{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))) {
  const name = relative ? relative+'/'+entry.name : entry.name;
  if(entry.isSymbolicLink()) throw new Error('Linked source path: '+name);
  if(name==='source-manifest.json' || skipDirs.has(entry.name)) continue;
  if(upstream.includes(name)) {
   const manifest = JSON.parse(fs.readFileSync(path.join(root,name,'SOURCE-MANIFEST.json'),'utf8'));
   for(const file of manifest.files) {
    if(/\.(obj|pdb|pch|res|idb|ilk|exp|dll|lib|exe|tlog)$/i.test(file.path)) continue;
    result.push(name+'/'+file.path);
   }
   result.push(name+'/SOURCE-MANIFEST.json');
   if(fs.existsSync(path.join(root,name,'LOCAL-CHANGES.patch'))) result.push(name+'/LOCAL-CHANGES.patch');
  } else if(entry.isDirectory()) result.push(...collect(name));
  else if(entry.isFile()) result.push(name);
 }
 return result;
}
if(process.argv[2]==='verify') {
 const manifest=JSON.parse(fs.readFileSync(manifestFile,'utf8'));
 for(const file of manifest.files) {
  if(!file.path || file.path.includes('..') || path.isAbsolute(file.path)) throw new Error('Invalid manifest path');
  const full=path.join(root,file.path);
  if(!fs.existsSync(full) || fs.statSync(full).size!==file.bytes || sha(full)!==file.sha256) throw new Error('Missing or modified: '+file.path);
 }
 console.log(JSON.stringify({status:'verified',files:manifest.files.length,releaseReady:false}));
} else {
 const names=[...new Set(collect())].sort();
 const files=names.map(name=>({path:name,bytes:fs.statSync(path.join(root,name)).size,sha256:sha(path.join(root,name))}));
 fs.writeFileSync(manifestFile,JSON.stringify({schema:'soui.full-source/v1',version:fs.readFileSync(path.join(root,'VERSION'),'utf8').trim(),releaseReady:false,files},null,2)+'\n');
 const output=path.join(root,'out/package');
 fs.mkdirSync(output,{recursive:true});
 fs.writeFileSync(path.join(output,'files.txt'),[...names,'source-manifest.json'].join('\n')+'\n');
 console.log(JSON.stringify({files:files.length,bytes:files.reduce((n,f)=>n+f.bytes,0)}));
}
