'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const sdk=require('../../tools/sdk/soui-sdk.cjs'),observer=require('../../tools/internal/llvm-build-observer.cjs');
const root=path.resolve(__dirname,'../..');
test('installed source SDK owns every input required before an LLVM application build',()=>{
 for(const relative of observer.codeNames)
   assert(sdk.managedFiles.includes(relative)||sdk.managedRoots.some(base=>relative.startsWith(base+'/')),
     'Missing SDK build input: '+relative);
});
test('relocated LLVM request inputs have the same raw bytes as their canonical source delivery',()=>{
 for(const relative of ['cmake/toolchains/SouiLLVM22.cmake','tools/internal/Invoke-ToolFileIdentity.ps1','tools/internal/Tool-FileIdentity.ps1']){
   const raw=fs.readFileSync(path.join(root,relative));
   assert(raw.equals(Buffer.from(raw.toString('latin1').replace(/\r\n/g,'\n'),'latin1')),
     'Source checkout differs from canonical SDK input: '+relative);
 }
});
