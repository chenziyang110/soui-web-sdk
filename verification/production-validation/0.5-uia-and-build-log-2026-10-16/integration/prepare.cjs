'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs'),proposal=__dirname+'/proposal';
assert(!fs.existsSync(proposal));
const packetNames=['C:/work/sm05-long-quota-1/PROPOSAL-v2.json','C:/work/sm05-uia-events-1/PROPOSAL.json',
 'C:/work/sm05-uia-delivery-2/PROPOSAL.json','C:/work/sm05-configure-log-consumers-1/PROPOSAL.json','C:/work/sm05-kernel-notices-2/PROPOSAL.json'];
const selected=new Map(),parents=[];
for(const file of packetNames){
 const packet=JSON.parse(fs.readFileSync(file));e.checkPin(packet.currentSource||packet.source24Seal);parents.push(e.pin(file));
 for(const row of packet.files||packet.changes){
  assert(!selected.has(row.relative),'Overlapping proposal file requires explicit review: '+row.relative);
  if(row.before)e.checkPin(row.before);else assert(!fs.existsSync(root+'/'+row.relative));e.checkPin(row.after);
  const target=proposal+'/'+row.relative;fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(row.after.path,target,fs.constants.COPYFILE_EXCL);
  selected.set(row.relative,{before:row.before,selectedFrom:e.pin(file),originalProposal:row.after});
 }
}
assert.equal(selected.size,65);
function edit(relative,transform){
 const target=proposal+'/'+relative;
 if(!selected.has(relative)){
  const source=root+'/'+relative;assert(fs.existsSync(source));fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);
  selected.set(relative,{before:e.pin(source),selectedFrom:null,originalProposal:null});
 }
 const before=fs.readFileSync(target,'utf8'),after=transform(before);assert.notEqual(after,before,relative+' edit must be effective');fs.writeFileSync(target,after);
}
function replaceOnce(text,before,after){assert.equal(text.split(before).length,2,'Unique edit anchor required');return text.replace(before,after);}
edit('CHANGELOG.md',text=>replaceOnce(text,'## 0.5.0（候选验证中，尚未发布）','## 0.5.0（候选验证中，尚未发布）\n\n'+
 '- 可选 UIA 模块增加有界属性、结构与焦点通知，保留秘密字段隔离、节点撤销与关闭借用约束。公共构建支持 `-WithUia` / `--with-uia`，并绑定实际交付库。\n'+
 '- 每次 LLVM 构建保存独立 CMake 配置日志，修复 Release 配置覆盖 Debug 验证原件；主构建、安装验收与发布消费者均校验原始观察器和副本归属。\n'+
 '- 随包补齐固定内核源码中的 27 份原始许可/版权文本；索引区分源码材料与实际链接成员，完整授权审核仍单列。'));
edit('docs/adr/README.md',text=>text+'\n'+
 '- [ADR-0109：有限 UIA 投影变化通知](0109-uia-state-notifications.md)（双架构定向验证通过；实际业务与读屏单列）\n'+
 '- [ADR-0110：显式构建与绑定可选 UIA 交付](0110-uia-build-delivery.md)（ON/OFF 搬移消费者通过；最终公开入口待验）\n'+
 '- [ADR-0111：保存每次构建的独立 CMake 配置日志](0111-immutable-cmake-configure-log.md)（真实重配置探针通过；四配置完整复验待执行）\n');
edit('docs/testing.md',text=>text+'\n'+
 'UIA 通知验证见 [状态通知](acceptance/uia-state-notifications.md)；可选构建、实际安装和关闭组件拒绝见 [UIA 交付](acceptance/uia-build-delivery.md)。最终四配置构建须显式选择 `-WithUia`，CTest 才注册该模块的 11 个独立进程用例。\n\n'+
 '共用 CMake 配置日志按 [ADR-0111](adr/0111-immutable-cmake-configure-log.md) 留存每次原始副本。`node --test tests/sdk/llvm-evidence-readers.test.cjs tests/acceptance/llvm-installed-entry.test.cjs` 覆盖 SDK、安装验收和发布消费者；读取器测试不替代真实编译。\n');
edit('docs/build.md',text=>text+'\n## 可选 Windows 辅助功能模块\n\n'+
 '`tools/Build.ps1 -Architecture x64 -Configuration Release -WithUia` 或 `tools/Soui.ps1 build --architecture x64 --configuration Release --with-uia` 显式构建 `Soui::Uia`；可组合既有 `-SdkOnly`、`-Install` 和现代后端选项。未选择时明确配置 OFF。应用仍需按公共 API 注册可信原生投影和派发策略，构建选项不会自动投影业务页面。\n\n'+
 'CMake 消费者使用 `find_package(SouiSdk 0.5.0 EXACT CONFIG REQUIRED COMPONENTS Uia)` 与 `Soui::Uia`。未安装该组件会拒绝 REQUIRED 请求。历史 MSVC 编译器尾进程例外未覆盖 UIA，不能与该例外组合；正常 MSVC/LLVM 构建保持各自既有验证规则。\n');
edit('docs/adr/0108-long-task-request-quota.md',text=>replaceOnce(text,'状态：proposed；独立目录中的解析与集成回归已通过，尚未采用到冻结候选','状态：实现已整合；独立解析与集成回归通过，最终候选的真实八小时验证待执行'));
edit('docs/adr/0109-uia-state-notifications.md',text=>replaceOnce(replaceOnce(text,
 '状态：独立候选，尚未整合；现有 Source24 构建输入保持不变。','状态：实现已整合；双架构定向验证通过，最终候选与业务辅助功能待验。'),
 '此提案未整合，也未改变 Source24 或发布资格。','定向验证的 Source24 原输入与结果保留，不能自动认证后续整合候选或发布。'));
edit('docs/adr/0110-uia-build-delivery.md',text=>replaceOnce(replaceOnce(text,
 '状态：独立提案，待统一整合与实际交付验证。','状态：实现已整合；独立安装已验证，最终公开入口验证待执行。'),
 '当前 Source24 不变。','上述探针使用原 Source24 核心及独立 UIA 实现，不能认证后续整合候选。'));
edit('docs/adr/0111-immutable-cmake-configure-log.md',text=>replaceOnce(text,'状态：提案，待整合后的实际完整构建。','状态：实现已整合，待新候选的实际完整构建。')+'\n'+
 '安装验收和发布消费者共用的读取器同样核对副本归属及原始观察器参数。两项回归先复现旧适配器忽略该字段，再验证篡改、错误路径、缺失/重复副本和错误 argv 均拒绝；该组 16 项通过。辅助源码已由共同的控制源码清单继承绑定，未另设放宽路径。\n');
fs.writeFileSync(__dirname+'/PREPARED.json',JSON.stringify({schema:'soui.prepared-integration/1',adopted:false,currentSource:e.pin(root+'/source-manifest.json'),parents,
 files:[...selected].map(([relative,values])=>({relative,...values,after:e.pin(proposal+'/'+relative)})),releaseReady:false},null,2)+'\n',{flag:'wx'});
fs.copyFileSync('C:/work/sm05-uia-delivery-2/run-regression.cjs',__dirname+'/run-regression.cjs',fs.constants.COPYFILE_EXCL);
console.log(JSON.stringify({prepared:true,files:selected.size,candidateChanged:false}));
