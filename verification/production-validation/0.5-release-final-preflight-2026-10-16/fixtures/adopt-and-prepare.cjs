'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const old='C:/work/sm05-source26-pipeline-1',folder='C:/work/sm05-source27-pipeline-1';
const rest=JSON.parse(fs.readFileSync(__dirname+'/remaining-tests2/RESULT.json'));
assert(rest.complete&&rest.results.length===5&&rest.results.every(row=>row.passed));rest.pins.forEach(e.checkPin);
const audits=JSON.parse(fs.readFileSync('C:/work/sm05-release-audit-1/AUDIT-RESULTS.json'));
assert(audits.completed&&audits.passed&&audits.records.length===11);audits.records.forEach(e.checkPin);
const docs=JSON.parse(fs.readFileSync('C:/work/sm05-api-docs-1/PROPOSAL.json'));
docs.inputs.forEach(e.checkPin);for(const row of docs.changed){e.checkPin(row.before);e.checkPin(row.after);}
const license=JSON.parse(fs.readFileSync('C:/work/sm05-current-license-identity-1/PROPOSAL.json'));
[license.before,license.after,...license.inputs].forEach(e.checkPin);
const pins=JSON.parse(fs.readFileSync(old+'/PREPARED.json'));pins.helpers.forEach(row=>e.checkPin(row.current));
assert(!fs.existsSync(folder));fs.mkdirSync(folder);
const rawRel='verification/production-validation/0.5-release-final-preflight-2026-10-16';
const archive=root+'/'+rawRel;assert(!fs.existsSync(archive));fs.mkdirSync(archive,{recursive:true});const records=[];
function copy(source,dest){
 assert(!fs.lstatSync(source).isSymbolicLink());
 if(fs.statSync(source).isDirectory()){
  fs.mkdirSync(dest,{recursive:true});for(const name of fs.readdirSync(source))copy(path.join(source,name),path.join(dest,name));
 }else{fs.copyFileSync(source,dest,fs.constants.COPYFILE_EXCL);const original=e.pin(source),retained=e.pin(dest);assert.equal(original.sha256,retained.sha256);records.push({original,retained});}
}
copy(__dirname,archive+'/fixtures');
copy(old+'/unit-x64-Release-source26-1',archive+'/original-full-unit-attempt');
copy('C:/work/sm05-release-audit-1',archive+'/dependency-audits');
copy('C:/work/sm05-api-docs-1',archive+'/api-docs');
copy('C:/work/sm05-current-license-identity-1',archive+'/binary-license-identity');
fs.mkdirSync(archive+'/prior-documents');
for(const row of docs.changed){copy(row.before.path,archive+'/prior-documents/'+path.basename(row.before.path));fs.copyFileSync(row.after.path,row.before.path);}
copy(license.before.path,archive+'/prior-documents/license-review.json');fs.copyFileSync(license.after.path,license.before.path);
const changed=['tests/sdk/verification-profile.test.cjs','tests/acceptance/installed-host-runner.test.cjs',
 ...docs.changed.map(row=>row.relative),'third_party/manifests/license-review.json'];
fs.writeFileSync(archive+'/INDEX.json',JSON.stringify({records,changed:changed.map(name=>e.pin(root+'/'+name)),
 originalFullUnitFailed:true,fixtureRegressionsPassed:true,dependencyAuditScopes:11,dependencyAuditKnownVulnerabilities:0,
 binaryLicenseReviewComplete:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
fs.writeFileSync(archive+'/README.md','# 发布前测试夹具与文档同步\n\n完整 x64 Release 单元测试在汇总器测量分组夹具失败：新增前置检查使按总调用次数停止的夹具提前中止。改为在下一个明确阶段停止，仍断言六组测量及各自 LLVM 身份。后续分组又发现安装消费者夹具未提供生产读取器已要求的原始 observer context；补齐模型记录，生产身份校验未变。两项各九个定向用例和剩余公开 Node 测试组通过，原失败及后续原件均保留。\n\n十一处 Web/测试依赖使用当前锁文件完成 npm 审计，均无已知漏洞。API/架构文档修正早期版本留下的可选 UIA、本地 Agent 和 WebView2 说明。内核许可检查单改为实际 0.5.0 双架构 DLL 的身份与构建来源，授权审核仍为未完成。\n\n这批改动没有修改运行代码、构建观察器或原生编译输入。后续使用既有正式增量构建校验：重新核对历史真实观察与每个复用编译输入，冻结新候选并运行完整矩阵。上述测试和审计不代表整体发布资格。\n',{flag:'wx'});
const changelog=root+'/CHANGELOG.md';let text=fs.readFileSync(changelog,'utf8');
const title='## 0.5.0（候选验证中，尚未发布）';assert.equal(text.split(title).length,2);
text=text.replace(title,title+'\n\n- 修正发布汇总与安装消费者测试夹具的过时假设；同步可选 UIA、本地 Agent、现代后端说明及当前内核许可检查单的二进制身份。');fs.writeFileSync(changelog,text);
fs.appendFileSync(root+'/.gitattributes','\n'+rawRel+'/** -text -eol whitespace=cr-at-eol,-blank-at-eof,-blank-at-eol\n');
const git=args=>{const result=cp.spawnSync('C:/Program Files/Git/cmd/git.exe',args,{cwd:root,windowsHide:true,encoding:'utf8',maxBuffer:16*1024*1024});assert.equal(result.status,0,result.stderr);return result.stdout;};
git(['add','--','.gitattributes']);git(['add','--',rawRel,...changed,'CHANGELOG.md']);
for(const row of records){const relative=path.relative(root,row.retained.path).replaceAll('\\','/');const result=cp.spawnSync('C:/Program Files/Git/cmd/git.exe',['show',':'+relative],{cwd:root,windowsHide:true,maxBuffer:32*1024*1024});assert.equal(result.status,0);assert(result.stdout.equals(fs.readFileSync(row.retained.path)),relative+' index bytes differ');}
git(['diff','--cached','--check','--',rawRel,...changed,'.gitattributes','CHANGELOG.md']);
copy(root+'/source-manifest.json',folder+'/source26-manifest.json');
const helpers=[];
for(const row of pins.helpers){const relative=path.relative(old,row.current.path);assert(!relative.startsWith('..'));
 let value=fs.readFileSync(row.current.path,'utf8').replaceAll('sm05-source26-complete-web-1','sm05-source27-complete-web-1').replaceAll('source26-1','source27-1');
 // Preserve the unchanged, actual compiler/measurement preparations; the
 // standard incremental reader validates their complete original evidence.
 const target=path.join(folder,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,value,{flag:'wx'});helpers.push({original:row.current,current:e.pin(target)});
}
let controller=fs.readFileSync(old+'/run.cjs','utf8').replaceAll('sm05-source26-pipeline-1','sm05-source27-pipeline-1');
controller=controller.replace("e.checkPin(JSON.parse(fs.readFileSync(folder+'/PREPARED.json')).fix);","JSON.parse(fs.readFileSync(folder+'/PREPARED.json')).changes.forEach(e.checkPin);");
const begin=controller.indexOf('  // Preserve prior actual output'),end=controller.indexOf("  node('complete-web'",begin);assert(begin>=0&&end>begin);
controller=controller.slice(0,begin)+`  // Public incremental acceptance revalidates original inputs and outputs.
  // Do not rewrite unchanged measurement preparation or remove accepted objects.
  for(const arch of ['x64','x86'])step('verify-tools-'+arch,process.execPath,[root+'/tools/internal/measurement-validation.cjs','prepared',arch,
    '--toolchain-request',root+'/out/'+(arch==='x64'?'benchmark-protocol':'benchmark-protocol-x86')+'/llvm-profile-request-Release.json']);
  const prepared=JSON.parse(fs.readFileSync(folder+'/PREPARED.json'));
  e.checkPin(prepared.observer);
  require(root+'/tools/internal/host64-build-debug-observer.cjs').loadPrepared(prepared.observer.path,prepared.observer.sha256,root);
`+controller.slice(end);
controller=controller.replaceAll('source26-1','source27-1').replaceAll("[arch,'26']","[arch,'27']").replaceAll("['outer',arch,'26','26','26']","['outer',arch,'27','27','27']");
fs.writeFileSync(folder+'/run.cjs',controller,{flag:'wx'});
const check=cp.spawnSync(process.execPath,['--check',folder+'/run.cjs'],{windowsHide:true,encoding:'utf8'});assert.equal(check.status,0,check.stderr);
fs.writeFileSync(folder+'/PREPARED.json',JSON.stringify({changes:changed.map(name=>e.pin(root+'/'+name)),
 archive:e.pin(archive+'/INDEX.json'),helpers,controller:e.pin(folder+'/run.cjs'),observer:e.pin(root+'/out/release-observer-x64-26/prepared.json'),
 previousSource:e.pin(folder+'/source26-manifest.json'),preservedFailure:e.pin(old+'/unit-x64-Release-source26-1/RESULT.json'),
 reuse:'Public incremental path only; all original compile/source/tool checks remain required',releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({adopted:true,rawFiles:records.length,folder,incrementalVerificationRequired:true}));
