'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const name='verification/production-validation/0.5-uia-and-build-log-2026-10-16',archive=__dirname+'/archive/'+name;
assert(!fs.existsSync(archive));assert(JSON.parse(fs.readFileSync(__dirname+'/INTEGRATION-VERIFIED.json')).passed);
const groups={
 'long-quota':'C:/work/sm05-long-quota-1',
 'uia-events':'C:/work/sm05-uia-events-1',
 'uia-delivery':'C:/work/sm05-uia-delivery-2',
 'uia-install':'C:/work/sm05-uia-install-2',
 'consumer-reader':'C:/work/sm05-configure-log-consumers-1',
 'notice-materials':'C:/work/sm05-kernel-notices-2',
 'integration':__dirname,
};
const excluded=new Set(['out','prefix','before','proposal','tools','cmake','distribution','application','web','archive']);
const rows=[];
function copy(source,relative){
 const before=e.pin(source),target=archive+'/'+relative;fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);
 const after=e.pin(target);assert.equal(before.bytes,after.bytes);assert.equal(before.sha256,after.sha256);e.checkPin(before);
 rows.push({original:before,retained:relative,bytes:after.bytes,sha256:after.sha256});
}
function visit(group,folder,relative=''){
 for(const entry of fs.readdirSync(folder,{withFileTypes:true})){
  if(!relative&&excluded.has(entry.name))continue;
  const name=relative?relative+'/'+entry.name:entry.name,file=folder+'/'+entry.name;
  assert(!entry.isSymbolicLink(),'No reparse archive inputs');
  if(entry.isDirectory()){visit(group,file,name);continue;}
  if(!entry.isFile())continue;
  if(/(?:\.(?:cjs|ps1|md|txt|json|ndjson|yaml|cpp|h|cmake)|(?:stdout|stderr)\.bin)$/i.test(name))copy(file,group+'/'+name);
 }
}
for(const [group,folder] of Object.entries(groups))visit(group,folder);
for(const file of ['RESULT.json','INPUTS.json','desktop.ndjson','desktop.context.json','desktop.ready.json','desktop.finished.json','desktop-stdout.bin','desktop-stderr.bin']){
 const input='C:/work/sm05-source24-pipeline-2/desktop-x64-Debug-source24-2/'+file;
 if(fs.existsSync(input))copy(input,'source24-debug-failure/'+file);
}
const index={schema:'soui.original-evidence-archive/1',scope:'Exact raw records and selected helper source for bounded quota/UIA/log/notices work; no binaries or final release qualification',
 excluded:'Build/install/prefix/toolchain trees and binary payloads are not copied. Original receipts retain their private-path pins. Proposal implementation is adopted separately.',files:rows,releaseReady:false};
fs.writeFileSync(archive+'/index.json',JSON.stringify(index,null,2)+'\n',{flag:'wx'});
fs.writeFileSync(archive+'/README.md',
 '# UIA、长期额度读取与构建原件修复记录\n\n'+
 '本目录逐字节保留选定原始输出、失败、控制脚本及散列。绝对路径是原执行位置；二进制、工具链和构建目录仍由原记录中的散列引用，不随归档重复提交。\n\n'+
 '- 长期 TaskStore 读取器：独立回归 260 项通过；没有实际八小时运行结论，整体缓存范围仍未闭合。\n'+
 '- UIA 状态变化：双架构各 11 项原生用例；实际安装、搬移后的消费者又分别执行 11 项，ON/OFF 与组件缺失拒绝均保留。真实读屏和业务投影仍待验收。\n'+
 '- 公共 UIA 构建与独立配置日志：306 项纯回归通过、1 项文件符号链接夹具因 EPERM 跳过。两个真实 fresh 配置验证副本不被后续配置改变，且不能覆盖。\n'+
 '- 安装验收及发布消费者日志绑定：16 项通过；原适配器忽略新绑定的两项失败保留。\n'+
 '- 补充许可：49 个来源文本与部署副本逐字节一致，新增 27 份文本；没有完成整颗二进制的授权审核。\n'+
 '- 五组实现组合后 211 项纯回归通过。首次组合脚本给 devkit 测试用了错误目录，原 ENOENT 保留；修正目录后仅补跑该组，复用前六组未改输入的结果。\n\n'+
 'Source24 的 Debug 桌面失败单独保留：Release 配置改变共用 CMake 日志，原校验正确拒绝。该历史失败不会因新快照实现改写为通过。最终整合候选必须执行实际公开构建、消费者、运行与发布验证。`releaseReady=false`。\n',
 {flag:'wx'});
fs.writeFileSync(__dirname+'/ARCHIVE-READY.json',JSON.stringify({relative:name,folder:archive,files:rows.length,bytes:rows.reduce((sum,row)=>sum+row.bytes,0),index:e.pin(archive+'/index.json')},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({files:rows.length,bytes:rows.reduce((sum,row)=>sum+row.bytes,0),candidateChanged:false}));
