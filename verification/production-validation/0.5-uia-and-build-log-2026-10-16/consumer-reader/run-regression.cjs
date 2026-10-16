'use strict';
const fs=require('node:fs'),Module=require('node:module'),path=require('node:path');
const root='C:/work/sm05-release-050',proposal=__dirname+'/proposal',cache=new Map();
function overlayPath(file){
 if(typeof file==='string'){
  const relative=path.relative(root,path.resolve(file)),alternate=path.join(proposal,relative);
  if(!relative.startsWith('..')&&!relative.split(path.sep).includes('node_modules')&&fs.existsSync(alternate))return alternate;
 }
 return file;
}
const sourceFs={...fs,
 statSync:(file,...args)=>fs.statSync(overlayPath(file),...args),
 lstatSync:(file,...args)=>fs.lstatSync(overlayPath(file),...args),
 existsSync:file=>fs.existsSync(overlayPath(file)),
 copyFileSync(source,target,...args){
 if(typeof source==='string'){
  const relative=path.relative(root,path.resolve(source)),alternate=path.join(proposal,relative);
  if(!relative.startsWith('..')&&!relative.split(path.sep).includes('node_modules')&&fs.existsSync(alternate))source=alternate;
 }
 return fs.copyFileSync(source,target,...args);
},readFileSync(file,...args){
 if(typeof file==='string'){
  const relative=path.relative(root,path.resolve(file)),alternate=path.join(proposal,relative);
  if(!relative.startsWith('..')&&!relative.split(path.sep).includes('node_modules')&&fs.existsSync(alternate))file=alternate;
 }
 return fs.readFileSync(file,...args);
}};
function relativeLoad(name,m){
  const requested=path.resolve(path.dirname(m.filename),name);
  if(fs.existsSync(path.join(proposal,path.relative(root,requested))))return load(requested);
  return load(Module._resolveFilename(name,m));
}
function load(file){
  const relative=path.relative(root,file),candidate=path.join(proposal,relative);
  if(relative.startsWith('..')||relative.split(path.sep).includes('node_modules')||!file.endsWith('.cjs'))return require(file);
  if(cache.has(candidate))return cache.get(candidate).exports;
  const m=new Module(file,module);cache.set(candidate,m);m.filename=file;m.paths=Module._nodeModulePaths(path.dirname(file));
  m.require=name=>name==='node:fs'?sourceFs:name.startsWith('.')?relativeLoad(name,m):require(name);
  m._compile(fs.readFileSync(fs.existsSync(candidate)?candidate:file,'utf8'),file);return m.exports;
}
const script=process.argv[2];if(!script)throw Error('Select one existing regression script');
const file=path.join(root,script),m=new Module(file,module);m.filename=file;m.paths=Module._nodeModulePaths(path.dirname(file));
m.require=name=>name==='node:fs'?sourceFs:name.startsWith('.')?relativeLoad(name,m):require(name);
process.argv=[process.execPath,file,__dirname+'/'+(process.argv[3]||'legacy-quota-regression')+'.json'];
const proposed=path.join(proposal,script);m._compile(fs.readFileSync(fs.existsSync(proposed)?proposed:file,'utf8'),file);
