'use strict';
// Guest data copy only; never runs a PE, compiler or application.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
assert.equal(process.argv.length,3);assert.equal(process.arch,'x64');
const root='C:/sr06',planFile=process.argv[2],hash=b=>crypto.createHash('sha256').update(b).digest('hex');
assert.equal(hash(fs.readFileSync(planFile)),'d3cb416c6dbc4630b378907c4a6bf8ae9201db652f00a91de9e7e708993d6ea5');const plan=JSON.parse(fs.readFileSync(planFile,'utf8'));
assert.equal(plan.rows.length,79);const markerModule=require(path.join(root,'tools/internal/measurement-validation.cjs'));
const evidence=require(path.join(root,'tools/internal/build-evidence.cjs'));
const id=require(path.join(root,'tools/internal/delivery-identity.cjs'));
function plain(p){for(let at=path.resolve(p);;at=path.dirname(at)){try{assert.equal(fs.lstatSync(at).isSymbolicLink(),false,'linked path');}catch(error){if(error.code!=='ENOENT')throw error;}if(path.dirname(at)===at)break;}}
function pin(p){plain(p);const stat=fs.lstatSync(p,{bigint:true});assert.ok(stat.isFile()&&stat.nlink===1n);const b=fs.readFileSync(p);return {path:p,bytes:b.length,sha256:hash(b),fileId:String(stat.ino),device:String(stat.dev)};}
const before=plan.groups.map(g=>{const accepted=evidence.verifyAcceptedBuild(root,g.architecture,'Release',false),setup=markerModule.preparedAt(root,g.architecture);
 assert.equal(accepted.build.source.sha256,'67a8c649a1a08e96e76dd4d0f7782b53f229e1f26ad65e6a2738bc7173555264');
 const marker=pin(path.join(markerModule.buildDirectory(g.architecture,root),'measurement-tools.json'));
 assert.equal(marker.sha256,g.marker.sha256);const tools=setup.report.executables.map(row=>pin(path.join(root,row.path)));
 assert.equal(tools.length,9);for(const t of tools){const expected=g.tools.find(row=>path.resolve(row.path).toLowerCase()===path.resolve(t.path).toLowerCase());assert.ok(expected);assert.equal(t.bytes,expected.bytes);assert.equal(t.sha256,expected.sha256);}
 return {architecture:g.architecture,marker,tools,toolDirectory:path.dirname(setup.tools.OwnedProcessRunner),sourceSha256:accepted.build.source.sha256};});
for(const group of before){const allowed=new Set([...group.tools.map(t=>path.basename(t.path).toLowerCase()),...plan.rows.filter(r=>r.architecture===group.architecture).map(r=>r.name.toLowerCase())]);
 for(const name of fs.readdirSync(group.toolDirectory)){assert.ok(allowed.has(name.toLowerCase()),'unlisted tool-directory sibling refused');pin(path.join(group.toolDirectory,name));}}
const targets=new Set();
const checked=plan.rows.map(row=>{const group=before.find(g=>g.architecture===row.architecture),sourceExpected=path.join(root,'out',row.architecture,'bin/Release/SouiStarter',row.name),targetExpected=path.join(group.toolDirectory,row.name);
 assert.ok(/^[a-z0-9_.-]+\.dll$/i.test(row.name));assert.equal(path.resolve(row.source.path).toLowerCase(),path.resolve(sourceExpected).toLowerCase());assert.equal(path.resolve(row.target).toLowerCase(),path.resolve(targetExpected).toLowerCase());
 assert.equal(targets.has(path.resolve(row.target).toLowerCase()),false);targets.add(path.resolve(row.target).toLowerCase());plain(row.target);
 const source=pin(row.source.path);assert.equal(source.bytes,row.source.bytes);assert.equal(source.sha256,row.source.sha256);
 let existing=null;try{existing=pin(row.target);}catch(error){if(error.code!=='ENOENT')throw error;}if(existing){assert.equal(existing.bytes,row.source.bytes);assert.equal(existing.sha256,row.source.sha256,'refuse differing existing target');}
 return {...row,beforeSource:source,existing};});
const output='C:/release06-control/tooldeps-'+crypto.randomUUID();assert.equal(fs.existsSync(output),false);fs.mkdirSync(output);
const write=(n,v)=>fs.writeFileSync(path.join(output,n),JSON.stringify(v,null,2)+'\n',{flag:'wx'});write('Preflight.actual.json',{planSha256:hash(fs.readFileSync(planFile)),groups:before,rows:checked,allSourcesAndTargetsCheckedBeforeCopy:true});
const copied=[];
try{
 for(const row of checked){if(!row.existing)fs.copyFileSync(row.source.path,row.target,fs.constants.COPYFILE_EXCL);const actual=pin(row.target);assert.equal(actual.bytes,row.source.bytes);assert.equal(actual.sha256,row.source.sha256);copied.push({target:actual,source:row.beforeSource,originalManifestSource:row.originalManifestSource,copied:!row.existing});}
 for(const g of before){const setup=markerModule.preparedAt(root,g.architecture),marker=pin(g.marker.path);assert.equal(marker.sha256,g.marker.sha256);for(const t of g.tools){const actual=pin(t.path);assert.equal(actual.sha256,t.sha256);assert.equal(actual.fileId,t.fileId);assert.equal(actual.device,t.device);}assert.equal(setup.report.executables.length,9);}
 assert.equal(id.source(root).sha256,'67a8c649a1a08e96e76dd4d0f7782b53f229e1f26ad65e6a2738bc7173555264');
 for(const row of checked)assert.equal(pin(row.source.path).sha256,row.source.sha256);
 write('Delta.actual.json',{schema:'soui.release06-tool-runtime-copy/v1',copied,groups:before,all79TargetRawExact:true,sourceAppsUnchanged:true,markerAnd18PeUnchanged:true,
  preparedAtExtraDependenciesAreSeparateEvidence:true,toolsOrProductsExecuted:false,systemRuntimeInstalled:false,sourceOrBudgetModified:false,releaseReady:false});
 console.log('TOOL_DEPS_OK rows=79 evidence='+output);
}catch(error){write('Failure.actual.json',{message:error.message,stack:error.stack,copied,originalsPreserved:true,noAutomaticOverwriteOrDeletion:true});throw error;}
