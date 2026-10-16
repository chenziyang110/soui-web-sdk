'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const old='C:/work/sm05-source27-pipeline-1',folder='C:/work/sm05-source28-pipeline-1';
const observer='tools/internal/Observe-LlvmBuild.ps1',indexTest='tests/sdk/llvm-binary-command-index.Tests.ps1',aliasTest='tests/sdk/llvm-binary-alias.Tests.ps1';
assert(fs.readFileSync(root+'/'+observer).equals(fs.readFileSync(__dirname+'/before-Observe-LlvmBuild.ps1')));
assert(fs.readFileSync(__dirname+'/combined.log','utf8').includes('all modeled behavior cases passed'));
const actual=JSON.parse(fs.readFileSync(__dirname+'/actual-alias.log','utf8'));
assert(actual.physicalToolIdentityChecked&&actual.actualSdkArchiveFound&&!actual.qualificationPassed);
assert(!fs.existsSync(folder));fs.mkdirSync(folder);
fs.copyFileSync(root+'/source-manifest.json',folder+'/source27-manifest.json',fs.constants.COPYFILE_EXCL);
fs.copyFileSync(root+'/'+indexTest,__dirname+'/before-index-test.ps1',fs.constants.COPYFILE_EXCL);
const changes=[observer,indexTest,aliasTest];
for(const [src,dest] of [['Observe-LlvmBuild-v2.ps1',observer],['llvm-binary-command-index.Tests.ps1',indexTest],['llvm-binary-alias.Tests.ps1',aliasTest]]){
 const bytes=fs.readFileSync(__dirname+'/'+src,'utf8').replaceAll('\r\n','\n').replaceAll('\n','\r\n');fs.writeFileSync(root+'/'+dest,bytes);
}
const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const run=cp.spawnSync(ps,['-NoLogo','-NoProfile','-File',root+'/'+indexTest],{cwd:root,windowsHide:true,encoding:'utf8'});
fs.writeFileSync(__dirname+'/public-regressions.stdout.log',run.stdout,{flag:'wx'});fs.writeFileSync(__dirname+'/public-regressions.stderr.log',run.stderr,{flag:'wx'});
assert(!run.error&&run.status===0,run.stderr);
const rawRel='verification/production-validation/0.5-incremental-tool-alias-2026-10-16',archive=root+'/'+rawRel;
fs.mkdirSync(archive);const records=[];
function copy(source,destination){fs.mkdirSync(path.dirname(destination),{recursive:true});fs.copyFileSync(source,destination,fs.constants.COPYFILE_EXCL);
 const original=e.pin(source),retained=e.pin(destination);assert.equal(original.sha256,retained.sha256);records.push({original,retained});}
for(const item of fs.readdirSync(__dirname,{withFileTypes:true}))if(item.isFile())copy(path.join(__dirname,item.name),archive+'/repair/'+item.name);
for(const suffix of ['1','2','3']){
 const dir=old+'/build-x64-Debug-source27-'+suffix;
 for(const name of fs.readdirSync(dir))copy(dir+'/'+name,archive+'/failed-outer-'+suffix+'/'+name);
}
const attempts=['attempt-c75d96f1-8195-46e5-a392-c249aa30042d','attempt-cea2b9b5-dd59-44c8-86fa-710efe4cf287','attempt-6a171c53-4fed-4668-beb3-8b78d511e73e'];
const originalAttempts=[];
for(const [i,name] of attempts.entries()){
 const dir=root+'/out/standard-build/'+name;const originals=[];
 for(const item of fs.readdirSync(dir,{withFileTypes:true}))if(item.isFile())originals.push(e.pin(dir+'/'+item.name));
 originalAttempts.push({directory:dir,files:originals});
 for(const leaf of ['result.json','index.json','llvm-observer-stderr.bin','build-finished.json','host64-closeout.json'])if(fs.existsSync(dir+'/'+leaf))copy(dir+'/'+leaf,archive+'/failed-standard-'+(i+1)+'/'+leaf);
}
copy(old+'/RESOURCE-REUSE-DIAGNOSIS.json',archive+'/resource-reuse-diagnosis.json');
copy(old+'/RUNTIME-PATH-DIAGNOSIS.json',archive+'/runtime-path-diagnosis.json');
fs.writeFileSync(archive+'/INDEX.json',JSON.stringify({records,originalAttempts,changes:changes.map(p=>e.pin(root+'/'+p)),
 originalFailuresPreserved:true,pureRegressionsPassed:true,actualPhysicalAliasClassified:true,
 freshBuildAndIncrementalAcceptancePending:true,releaseReady:false},null,2)+'\n');
fs.writeFileSync(archive+'/README.md','# 增量构建工具短路径认证修复\n\n真实增量构建正常退出后，观察器无法识别 SDK 归档命令中的 8.3 工具路径：当前构建没有执行归档器，原逻辑只从实际进程与 CMake 长路径缓存建立别名。独立读取实际 Ninja 命令复现了空候选；修复后以原有持有文件身份核对工具路径，准确找到唯一 SDK 归档。\n\n修复只识别既有直接命令及固定 CMake 外壳形态，逐一核对真实工具文件；不新增“已执行”进程，不改变历史产物、对象、依赖、自然关闭或配置绑定检查。定向回归先失败后通过，既有 12 项索引检查和新增路径、未知外壳、伪工具拒绝检查通过。原有资源清单不一致和本轮误用 NVM 链接路径的失败另行保留。\n\n本归档保存诊断及失败摘要；INDEX 中 originalAttempts 指向完整本地原件，仅记录身份引用，不能把摘要当成完整可搬移构建证明。新源码完整构建及真实增量构建仍须执行。\n');
const log=root+'/CHANGELOG.md';let text=fs.readFileSync(log,'utf8');const title='## 0.5.0（候选验证中，尚未发布）';assert(text.includes(title));
text=text.replace(title,()=>title+'\n\n- 修正 LLVM 增量构建对未重新执行的归档器/链接器短路径的识别；配置路径仍经真实文件身份认证，不视为本轮执行证明。');fs.writeFileSync(log,text);
const doc=root+'/docs/testing.md';fs.appendFileSync(doc,'\nLLVM 增量构建会单独认证生成命令中的工具路径（含 Windows 8.3 路径），即使本轮没有再次运行该工具。此认证只证明工具文件身份；复用产物仍须匹配历史自然构建、当前对象及依赖，不能据此添加实际进程记录。\n');
fs.appendFileSync(root+'/.gitattributes','\n'+rawRel+'/** -text -eol whitespace=cr-at-eol,-blank-at-eof,-blank-at-eol\n');
const git=args=>{const r=cp.spawnSync('C:/Program Files/Git/cmd/git.exe',args,{cwd:root,windowsHide:true,encoding:'utf8',maxBuffer:32*1024*1024});assert.equal(r.status,0,r.stderr);};
git(['add','--','.gitattributes']);git(['add','--',rawRel,...changes,'docs/testing.md','CHANGELOG.md']);
for(const row of records){const relative=path.relative(root,row.retained.path).replaceAll('\\','/');const r=cp.spawnSync('C:/Program Files/Git/cmd/git.exe',['show',':'+relative],{cwd:root,windowsHide:true,maxBuffer:32*1024*1024});assert.equal(r.status,0);assert(r.stdout.equals(fs.readFileSync(row.retained.path)),relative);}
git(['diff','--cached','--check','--',rawRel,...changes,'docs/testing.md','CHANGELOG.md','.gitattributes']);
const prior=JSON.parse(fs.readFileSync(old+'/PREPARED.json')),helpers=[];
for(const row of prior.helpers){e.checkPin(row.current);const relative=path.relative(old,row.current.path);assert(!relative.startsWith('..'));
 const value=fs.readFileSync(row.current.path,'utf8').replaceAll('sm05-source27-complete-web-1','sm05-source28-complete-web-1').replaceAll('source27-1','source28-1').replaceAll('out/release-observer-x64-26','out/release-observer-x64-28');
 const target=path.join(folder,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,value,{flag:'wx'});helpers.push({original:row.current,current:e.pin(target)});
}
let controller=fs.readFileSync('C:/work/sm05-source26-pipeline-1/run.cjs','utf8').replaceAll('sm05-source26-pipeline-1','sm05-source28-pipeline-1').replaceAll('source26-1','source28-1').replaceAll("'source25-'","'source27-'").replaceAll("[arch,'26']","[arch,'28']").replaceAll("['outer',arch,'26','26','26']","['outer',arch,'28','28','28']");
controller=controller.replace("const e=require(root+'/tools/internal/dependency-evidence.cjs'),log=folder+'/pipeline.ndjson';",s=>s+"\nassert.equal(process.execPath.replaceAll('\\\\','/').toLowerCase(),'c:/users/11034/appdata/local/nvm/v24.14.0/node.exe');");
const point="      node('native-'+arch+'-'+config,'run-native2.cjs',['native',arch,config,'source28-1']);";
assert(controller.includes(point));controller=controller.replace(point,()=>"      if(arch==='x64'&&config==='Debug')node('incremental-x64-Debug','run-native2.cjs',['build',arch,config,'source28-incremental']);\n"+point);
fs.writeFileSync(folder+'/run.cjs',controller,{flag:'wx'});
fs.writeFileSync(folder+'/PREPARED.json',JSON.stringify({fix:e.pin(root+'/'+observer),changes:changes.map(p=>e.pin(root+'/'+p)),helpers,
 rawArchive:e.pin(archive+'/INDEX.json'),controller:e.pin(folder+'/run.cjs'),previousSource:e.pin(folder+'/source27-manifest.json'),
 fullFreshAndActualIncrementalRequired:true,releaseReady:false},null,2)+'\n');
console.log(JSON.stringify({adopted:true,folder,rawFiles:records.length,freshAndIncrementalPending:true}));
