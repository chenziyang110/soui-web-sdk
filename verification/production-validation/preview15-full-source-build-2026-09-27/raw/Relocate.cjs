'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=__dirname,original='C:/SOUI-SDK-development/preview14-scheduler';
const destination='C:/SOUI-SDK-source-validation/preview15-no-git';
const pkg='C:/SOUI-SDK-production-validation/sdk-core-native-backups-preview15-20260927/sdk-package';
const hash=data=>crypto.createHash('sha256').update(data).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(name,value)=>fs.writeFileSync(path.join(root,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
async function capture(name,program,args){
 const child=cp.spawn(process.execPath,[path.join(root,'Capture-Command.cjs'),name,'1200000',program,...args],{windowsHide:true,stdio:'inherit'});
 const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('close',resolve);});
 const result=read(path.join(root,'command-logs',name,'result.json'));
 assert.equal(code,0,name);assert.equal(result.exitCode,0,name);assert.equal(result.timedOut,false,name);assert.equal(result.error,null,name);return result;
}
async function main(){
 const packaged=read(path.join(root,'package-result.json'));
 assert.equal(packaged.exitCode,0);assert.equal(packaged.archiveSha256,'4c0255ca2a215e819bbda339d7432fc37398c3cef98b7d234f0411fcd4a4dd92');
 assert.equal(hash(fs.readFileSync(packaged.archive)),packaged.archiveSha256);
 assert.equal(hash(fs.readFileSync(path.join(pkg,'sdk-manifest.json'))),'44839f856df8d9542231e7bcd38af4ca9a37663aa8d4e6fb755868faacb076da');
 assert(!fs.existsSync(destination),'Never overwrite an existing extraction');
 const zip='C:/Program Files/7-Zip/7z.exe';assert(fs.existsSync(zip));
 write('relocate-input.json',{...packaged,destination,sdkPackage:pkg,sdkManifestSha256:hash(fs.readFileSync(path.join(pkg,'sdk-manifest.json'))),extractor:{path:zip,sha256:hash(fs.readFileSync(zip))},driverSha256:hash(fs.readFileSync(__filename)),captureSha256:hash(fs.readFileSync(path.join(root,'Capture-Command.cjs'))),startedAt:new Date().toISOString()});
 await capture('extract-source',zip,['x',packaged.archive,'-o'+destination,'-y']);
 assert(!fs.existsSync(path.join(destination,'.git')));
 assert.equal(hash(fs.readFileSync(path.join(destination,'source-manifest.json'))),packaged.sourceManifestSha256);
 await capture('verify-extracted-source',process.execPath,[path.join(destination,'tools/internal/source-archive.cjs'),'verify']);
 await capture('verify-extracted-sdk',process.execPath,[path.join(destination,'tools/sdk/soui-sdk.cjs'),'verify-source','--package',pkg,'--source',destination]);
 const id=require(path.join(destination,'tools/internal/delivery-identity.cjs')),source=id.source(destination);
 assert.equal(source.sha256,'b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07');
 assert.equal(source.files.length,717);
 const sdk=require(path.join(destination,'tools/sdk/soui-sdk.cjs')).readPackage(pkg);assert.equal(sdk.files.length,419);
 write('relocate-result.json',{status:'extracted-source-and-sdk-matched',version:packaged.version,archiveSha256:packaged.archiveSha256,destination,gitPresent:false,sourceManifestSha256:packaged.sourceManifestSha256,fullSourceFiles:packaged.sourceFiles,sdkManifestSha256:hash(fs.readFileSync(path.join(pkg,'sdk-manifest.json'))),sdkFiles:sdk.files.length,applicationSource:source,finishedAt:new Date().toISOString(),compilationPending:true,guiExecuted:false,sourceDeliveryVerified:false,releaseReady:false});
 console.log(JSON.stringify({status:'extracted-source-and-sdk-matched',files:packaged.sourceFiles,sdkFiles:sdk.files.length,destination,compilationPending:true}));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
