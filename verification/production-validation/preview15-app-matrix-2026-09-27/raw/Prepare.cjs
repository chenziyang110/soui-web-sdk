'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const from='C:/SOUI-SDK-production-validation/preview14-app-matrix-20260927';
let script=fs.readFileSync(path.join(from,'Build-Matrix.cjs'),'utf8');
assert(script.includes('0.3.0-preview.14'));script=script.replaceAll('0.3.0-preview.14','0.3.0-preview.15');
assert(script.includes('sdk-core-native-backups-20260927'));script=script.replaceAll('sdk-core-native-backups-20260927','sdk-core-native-backups-preview15-20260927');
script=script.replace("VcpkgEnabled:'false',VcpkgAutoLink", "MSBUILDDISABLENODEREUSE:'1',VcpkgEnabled:'false',VcpkgAutoLink");
fs.writeFileSync(path.join(__dirname,'Build-Matrix.cjs'),script,{flag:'wx'});
fs.copyFileSync(path.join(from,'Capture-Command.cjs'),path.join(__dirname,'Capture-Command.cjs'),fs.constants.COPYFILE_EXCL);
