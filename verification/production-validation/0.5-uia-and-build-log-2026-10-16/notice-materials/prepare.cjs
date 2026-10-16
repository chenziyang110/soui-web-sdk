'use strict';
// Prepare exact source material outside the frozen release candidate.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',folder=__dirname,proposal=folder+'/proposal';
const e=require(root+'/tools/internal/dependency-evidence.cjs');
const audit=JSON.parse(fs.readFileSync(folder+'/AUDIT.json'));
e.checkPin(audit.sourceManifest);assert(!fs.existsSync(proposal));
const excluded=new Map([
 ['third_party/sources/miniblink/third_party/WebKit/Source/bindings/templates/copyright_block.txt','Generator template containing placeholders; retained at its original source path, not copied as an upstream notice.'],
 ['third_party/sources/miniblink/third_party/WebKit/Source/build/scripts/license.py','Build script, not a standalone notice document.'],
 ['third_party/sources/miniblink/third_party/WebKit/Source/build/scripts/license.pyc','Compiled build script, not a standalone notice document.'],
]);
const changes=[],materials=[];
function write(relative,bytes){
 const target=path.join(proposal,relative);assert(!fs.existsSync(target));
 fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});
 changes.push({relative,before:fs.existsSync(root+'/'+relative)?e.pin(root+'/'+relative):null,after:e.pin(target)});
}
for(const row of audit.files){
 e.checkPin(row.source);
 if(excluded.has(row.relative))continue;
 const bytes=fs.readFileSync(row.source.path);assert(bytes.length>0&&!bytes.includes(0),'Text-only material required');
 let targets=[...row.retainedAt];
 if(!targets.length){
  const relative='third_party/licenses/kernel-source/'+row.relative.slice('third_party/sources/miniblink/'.length)+'.txt';
  assert(!fs.existsSync(root+'/'+relative));write(relative,bytes);targets=[relative];
 }
 for(const target of targets){
  const file=fs.existsSync(proposal+'/'+target)?proposal+'/'+target:root+'/'+target;
  assert.deepEqual(fs.readFileSync(file),bytes,'Source notice copy differs');
 }
 materials.push({source:row.relative,bytes:bytes.length,sha256:row.source.sha256,retainedAt:targets});
}
assert.equal(excluded.size,3);assert.equal(materials.length,49);assert.equal(changes.length,27);
const index={schema:'soui.kernel-source-notice-materials/1',
 selection:'Named LICENSE/COPYING/NOTICE/COPYRIGHT text documents across the retained Miniblink source, including source-only build and test dependencies.',
 scope:'Source material inventory; inclusion does not assert the component is linked into either architecture. This does not replace file-header review, linked-member inventory or license approval.',
 sourceManifestSha256:audit.sourceManifest.sha256,materials,
 excluded:[...excluded].map(([source,reason])=>({source,reason})),binaryLicenseReviewComplete:false,releaseReady:false};
write('third_party/licenses/kernel-source/index.json.txt',Buffer.from(JSON.stringify(index,null,2)+'\n'));
write('third_party/licenses/kernel-source/README.md',Buffer.from(
 '# 保留源码中的补充许可材料\n\n'+
 '本目录补齐已固定 Miniblink 源码中尚未随应用交付的 27 份原始许可或版权文本。所有副本逐字节保留，文件名追加 `.txt`。'+
 '[材料索引](index.json.txt)登记 49 个来源文件及其实际副本；其中 22 个来源已有原样材料，继续引用原位置。\n\n'+
 '收录范围包含源码树内的构建工具、测试和辅助组件。收录不表示该组件实际链接进当前 DLL，也不表示这些文本涵盖文件头中的全部声明。'+
 '索引记录的是准备材料时的 Source24 清单散列，并非后续候选的完整身份。实际二进制成员、适用条款与再分发审核仍需单独完成。\n\n'+
 '文件名匹配出的 Python 脚本、字节码和含占位符的版权模板没有当作许可复制，排除理由与原路径均保留。历史许可审核状态不改变。\n'));
for(const [relative,addition] of [
 ['third_party/licenses/KERNEL-NOTICES.md','\n[Supplemental source materials](kernel-source/README.md) retain 27 additional exact notice texts and index 49 named source declarations, including build/test dependencies. The index distinguishes the three excluded scripts/templates. Inclusion is not evidence of linked-member selection or completed license review.\n'],
 ['THIRD_PARTY_NOTICES.md','\n[Supplemental kernel source materials](third_party/licenses/kernel-source/README.md) preserve previously omitted named license/copyright texts from the fixed source tree, with exact source-to-copy hashes. Build/test materials are explicitly included as a source inventory; final binary applicability and redistribution review remain separate.\n'],
])write(relative,Buffer.concat([fs.readFileSync(root+'/'+relative),Buffer.from(addition)]));
e.checkPin(audit.sourceManifest);
fs.writeFileSync(folder+'/PROPOSAL.json',JSON.stringify({schema:'soui.private-change-proposal/1',adopted:false,currentSource:audit.sourceManifest,
 scope:'Exact additional source notice texts and inventory only; no license grant or review approval',files:changes,
 originals:materials.map(row=>({path:root+'/'+row.source,bytes:row.bytes,sha256:row.sha256})),
 evidence:[e.pin(folder+'/AUDIT.json'),e.pin(__filename)],releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({copied:27,indexed:49,excluded:3,changes:changes.length,adopted:false}));
