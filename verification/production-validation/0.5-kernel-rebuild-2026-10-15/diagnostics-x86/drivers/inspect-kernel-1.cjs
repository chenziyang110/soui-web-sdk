'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',arch=process.argv[2];assert(['x64','x86'].includes(arch));
const prefix=path.join(__dirname,'kernel-'+arch+'-1'),report='C:/work/sm05-kernel-release-050-'+arch+'-1/build-Miniblink.json';
const proof=require(root+'/tools/internal/dependency-evidence.cjs').verifyComponent(root,arch,'kernel',
 {report,capture:prefix+'/kernel'},'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe');
fs.writeFileSync(prefix+'/component-proof.json',JSON.stringify(proof,null,2));
const readobj='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/llvm-readobj.exe';
const old=root+'/third_party/prebuilt/'+arch+'/miniblink/node.dll',fresh=proof.artifacts[0].path;
const names=(file,label)=>{const text=cp.execFileSync(readobj,['--coff-exports',file],{encoding:'utf8',maxBuffer:16777216});fs.writeFileSync(prefix+'/'+label+'-exports.txt',text);return [...text.matchAll(/^  Name: (.*)$/gm)].map(m=>m[1].trim()).sort();};
const before=names(old,'old'),after=names(fresh,'rebuilt'),missing=before.filter(x=>!after.includes(x)),added=after.filter(x=>!before.includes(x));
const required=['wkePrepareWorkerShutdown','wkeFinalizePrepared','wkeFinalize'];
const result={arch,sourceIdentity:proof.sourceIdentity,artifact:proof.artifacts[0],before:before.length,after:after.length,missing,added,required:required.map(name=>({name,present:after.includes(name)}))};
fs.writeFileSync(prefix+'/export-comparison.json',JSON.stringify(result,null,2));
console.log(JSON.stringify(result));assert.equal(missing.length,0);assert(required.every(n=>after.includes(n)));
