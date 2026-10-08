'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview14-scheduler',parent='C:/SOUI-SDK-production-validation/sdk-core-native-backups-20260927';
for(const name of ['VERSION','sdk/include/soui/version.h','sdk/web/package.json','sdk/web/package-lock.json','README.md']){
 const f=path.join(repo,name),text=fs.readFileSync(f,'utf8');assert(text.includes('0.3.0-preview.14'),name);fs.writeFileSync(f,text.replaceAll('0.3.0-preview.14','0.3.0-preview.15'));
}
for(const name of ['Capture-Command.cjs','kernel-import.json'])fs.copyFileSync(path.join(parent,name),path.join(__dirname,name),fs.constants.COPYFILE_EXCL);
let verify=fs.readFileSync(path.join(parent,'Verify-Packages.cjs'),'utf8').replaceAll('0.3.0-preview.14','0.3.0-preview.15');
const needle='sdk.pack(repo,current,version);const next=sdk.readPackage(current);';assert(verify.includes(needle));
verify=verify.replace(needle,needle+"\nrequire('node:child_process').execFileSync(process.execPath,[path.join(repo,'tools/sdk/soui-sdk.cjs'),'verify-source','--package',current,'--source',repo],{windowsHide:true});");
fs.writeFileSync(path.join(__dirname,'Verify-Packages.cjs'),verify,{flag:'wx'});
console.log('Prepared preview.15; preview.14 package and failure unchanged.');
