'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
for(const variant of ['red','green']){
 const target=__dirname+'/'+variant+'2';assert(!fs.existsSync(target));fs.cpSync(__dirname+'/'+variant,target,{recursive:true,errorOnExist:true,force:false});
 const cmake=fs.readFileSync(target+'/CMakeLists.txt','utf8');assert(cmake.includes('PRIVATE Soui::Sdk '));fs.writeFileSync(target+'/CMakeLists.txt',cmake.replace('PRIVATE Soui::Sdk ','PRIVATE Soui::Sdk Soui::ActionJson '));
}
const original=fs.readFileSync(__dirname+'/run.cjs','utf8');fs.writeFileSync(__dirname+'/run2.cjs',original.replace("['red','green'].includes(variant)","['red2','green2'].includes(variant)").replace("variant==='red'?11:0","variant==='red2'?11:0"),{flag:'wx'});
