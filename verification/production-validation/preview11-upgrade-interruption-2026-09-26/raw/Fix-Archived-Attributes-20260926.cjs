'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root='C:/SOUI-SDK-development/preview9-tray/verification/production-validation/preview11-upgrade-interruption-2026-09-26';
const manifestPath=path.join(root,'raw-manifest.json'),manifest=JSON.parse(fs.readFileSync(manifestPath,'utf8'));
const name='raw/business-source/.gitattributes',next=name+'.snapshot';
const entry=manifest.files.find(f=>f.path===name);if(!entry)throw Error('Original attributes record missing');
const before=fs.readFileSync(path.join(root,name));const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
if(before.length!==entry.bytes||hash(before)!==entry.sha256||fs.existsSync(path.join(root,next)))throw Error('Unsafe archive rename');
// Preserve the original bytes while preventing an archived configuration from
// overriding the repository-wide -text rule for raw evidence siblings.
fs.renameSync(path.join(root,name),path.join(root,next));entry.path=next;
const script=fs.readFileSync(__filename),scriptName='raw/Fix-Archived-Attributes-20260926.cjs';
fs.writeFileSync(path.join(root,scriptName),script,{flag:'wx'});
manifest.files.push({path:scriptName,source:__filename,bytes:script.length,sha256:hash(script)});
manifest.files.sort((a,b)=>a.path.localeCompare(b.path));
fs.writeFileSync(manifestPath,JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({originalBytesPreserved:true,renamedFrom:name,renamedTo:next,manifestSha256:hash(fs.readFileSync(manifestPath))}));
