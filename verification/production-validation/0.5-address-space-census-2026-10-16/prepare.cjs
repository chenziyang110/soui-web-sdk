'use strict';
const fs=require('node:fs'),path=require('node:path');
const from='C:/work/sm05-uia-events-1',root='C:/work/sm05-release-050';
for(const name of ['tools/internal/Invoke-ToolFileIdentity.ps1','tools/internal/Tool-FileIdentity.ps1','cmake/toolchains/SouiLLVM22.cmake']){
 const target=__dirname+'/'+name;fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(from+'/'+name,target,fs.constants.COPYFILE_EXCL);
}
let build=fs.readFileSync(from+'/build.ps1','utf8');
build=build.replace("[ValidateSet('before','proposal')][string]$Provider='before',",'');
build=build.replace("& $cmake @configure ('-DPROVIDER_SOURCE='+(Join-Path $PSScriptRoot ($Provider+'/sdk/accessibility/uia/windows_uia_provider.cpp')))","& $cmake @configure");
fs.writeFileSync(__dirname+'/build.ps1',build,{flag:'wx'});
const target=__dirname+'/proposal/tests/acceptance-app/main.cpp';
fs.copyFileSync(root+'/tests/acceptance-app/main.cpp',target,fs.constants.COPYFILE_EXCL);
