'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const manifest=JSON.parse(fs.readFileSync(root+'/source-manifest.json'));
const existing=manifest.files.filter(row=>row.path.startsWith('third_party/licenses/')).map(row=>({relative:row.path,...e.pin(root+'/'+row.path)}));
const selected=manifest.files.filter(row=>row.path.startsWith('third_party/sources/miniblink/')&&/^(?:license|copying|notice|copyright)(?:[._-].*)?$/i.test(path.basename(row.path)));
const files=selected.map(row=>{
 const source=e.pin(root+'/'+row.path),matches=existing.filter(file=>file.bytes===source.bytes&&file.sha256===source.sha256);
 return {relative:row.path,manifest:row,source,retainedAt:matches.map(file=>file.relative)};
});
assert(files.length>0);fs.writeFileSync(__dirname+'/AUDIT.json',JSON.stringify({sourceManifest:e.pin(root+'/source-manifest.json'),selection:'Case-insensitive LICENSE/COPYING/NOTICE/COPYRIGHT basenames with dot, underscore or dash suffix in the entire frozen Miniblink source tree; includes build/test materials, not linked-member or legal applicability proof',files,missing:files.filter(row=>!row.retainedAt.length).map(row=>row.relative),releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({selected:files.length,alreadyRetained:files.filter(row=>row.retainedAt.length).length,missing:files.filter(row=>!row.retainedAt.length).map(row=>row.relative)}));
