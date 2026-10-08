'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview9-tray',output=path.join(repo,'dist/soui-sdk-0.3.0-preview.13-legal-identity-full-source.zip');
const env={...process.env,PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
assert(!fs.existsSync(output));
fs.writeFileSync(path.join(root,'package-input.json'),JSON.stringify({output,childPSModulePath:env.PSModulePath,globalSettingsModified:false},null,2)+'\n',{flag:'wx'});
const result=cp.spawnSync(process.execPath,[path.join(root,'Capture-Command.cjs'),'source-package','300000','powershell.exe','-NoProfile','-File',path.join(repo,'tools/Package.ps1'),'-OutputFile',output],{env,windowsHide:true,stdio:'inherit'});
assert.equal(result.status,0);
for(const name of ['archive.log','archive-test.log'])fs.copyFileSync(path.join(repo,'out/package',name),path.join(root,name),fs.constants.COPYFILE_EXCL);
