'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',proposal=__dirname+'/proposal';
assert(!fs.existsSync(proposal));
for(const name of ['tools/internal/llvm-installed-acceptance.cjs','tests/acceptance/llvm-installed-entry.test.cjs']){
 const output=proposal+'/'+name;fs.mkdirSync(path.dirname(output),{recursive:true});fs.copyFileSync(root+'/'+name,output,fs.constants.COPYFILE_EXCL);
}
// The same loader and proposed dependencies as the already sealed SDK repair.
fs.copyFileSync('C:/work/sm05-uia-delivery-2/run-regression.cjs',__dirname+'/run-regression.cjs',fs.constants.COPYFILE_EXCL);
for(const name of ['tools/internal/configure-log-evidence.cjs']){
 const output=proposal+'/'+name;fs.mkdirSync(path.dirname(output),{recursive:true});fs.copyFileSync('C:/work/sm05-uia-delivery-2/proposal/'+name,output,fs.constants.COPYFILE_EXCL);
}
console.log('Prepared private consumer-reader repair; candidate unchanged');
