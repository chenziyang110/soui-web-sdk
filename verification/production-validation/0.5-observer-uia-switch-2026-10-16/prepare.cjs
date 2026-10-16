'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const from='C:/work/sm05-reference-uia-client-1',root='C:/work/sm05-release-050';
const e=require(root+'/tools/internal/dependency-evidence.cjs');
for(const rel of ['build.ps1','cmake/toolchains/SouiLLVM22.cmake','tools/internal/Invoke-ToolFileIdentity.ps1','tools/internal/Tool-FileIdentity.ps1']){
 const dest=__dirname+'/'+rel;fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(from+'/'+rel,dest,fs.constants.COPYFILE_EXCL);
}
let run=fs.readFileSync(from+'/run-build.cjs','utf8').replace('/Release/ReferenceUiaClient.exe','/Release/ParserAfter.exe');
run=run.replace("root+'/source-manifest.json',", "root+'/benchmarks/protocol/host64-build-debug-observer.cpp',__dirname+'/before/host64-build-debug-observer.cpp',root+'/source-manifest.json',");
fs.writeFileSync(__dirname+'/run-build.cjs',run,{flag:'wx'});
const original=JSON.parse(fs.readFileSync(__dirname+'/original-input.json'));
assert(original.rootArguments.includes('-WithUia'));
const cases=[['with-uia',x=>{},'accept'],['without-uia',x=>{x.rootArguments=x.rootArguments.filter(a=>a!=='-WithUia');},'accept'],
 ['duplicate-uia',x=>{x.rootArguments.splice(10,0,'-WithUia');},'input-duplicate-switch'],
 ['unknown-uia',x=>{x.rootArguments[x.rootArguments.indexOf('-WithUia')]='-WithUiaUnknown';},'input-explicit-profile'],
 ['wrong-profile',x=>{x.rootArguments[x.rootArguments.indexOf('-NativeToolchain')+1]='MSVC';},'input-explicit-profile']];
fs.mkdirSync(__dirname+'/inputs');
const inputs=[];for(const [name,change,expected] of cases){const x=structuredClone(original);change(x);const p=__dirname+'/inputs/'+name+'.json';fs.writeFileSync(p,JSON.stringify(x,null,2)+'\n',{flag:'wx'});inputs.push({name,expected,pin:e.pin(p)});}
fs.writeFileSync(__dirname+'/PREPARED.json',JSON.stringify({before:e.pin(__dirname+'/before/host64-build-debug-observer.cpp'),after:e.pin(root+'/benchmarks/protocol/host64-build-debug-observer.cpp'),original:e.pin(__dirname+'/original-input.json'),inputs,scope:'Real production load_input parser; no source-pin or build acceptance claim'},null,2)+'\n',{flag:'wx'});
