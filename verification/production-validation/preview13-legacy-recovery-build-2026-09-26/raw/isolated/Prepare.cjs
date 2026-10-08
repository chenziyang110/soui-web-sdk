'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview9-tray',source='C:/SOUI-SDK-production-validation/preview13-legacy-recovery-build-20260926/project',project=path.join(root,'project');
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs')),sdk=require(path.join(repo,'tools/sdk/soui-sdk.cjs'));
const business=id.readJson(path.join(root,'business-input.json')),lock=sdk.readLock(source),names=[...business.files.map(f=>f.path),...lock.manifest.files.map(f=>f.path),'sdk-lock.json'];
assert(!fs.existsSync(project));assert.equal(names.length,new Set(names).size);const before=id.snapshot(source,names);
for(const f of before.files){const b=fs.readFileSync(id.safePath(source,f.path));assert.equal(id.hash(b),f.sha256);const target=id.safePath(project,f.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,b,{flag:'wx'});}
assert.equal(id.snapshot(source,names).sha256,before.sha256);assert.equal(id.snapshot(project,names).sha256,before.sha256);assert.equal(sdk.readLock(project).manifest.version,'0.3.0-preview.13');
const toolchain=[];
for(const name of ['vcpkg.props','vcpkg.targets']){const file=path.join('F:/GITHUB/vcpkg/scripts/buildsystems/msbuild',name),b=fs.readFileSync(file);fs.mkdirSync(path.join(root,'toolchain'),{recursive:true});fs.writeFileSync(path.join(root,'toolchain',name+'.snapshot'),b,{flag:'wx'});toolchain.push({source:file,sha256:id.hash(b),bytes:b.length});}
fs.writeFileSync(path.join(root,'preparation.json'),id.encoded({ready:true,source,sourceUnchanged:true,input:before,projectSource:id.source(project),toolchain,
 childEnvironment:{VcpkgEnabled:'false',VcpkgAutoLink:'false',VcpkgEnableManifest:'false'},globalSettingsModified:false,compiled:false,guiExecuted:false,releaseReady:false}),{flag:'wx'});
console.log(id.encoded({status:'prepared',files:before.files.length,projectSourceSha256:id.source(project).sha256,releaseReady:false}));
