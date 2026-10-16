'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',work=__dirname,e=require(root+'/tools/internal/dependency-evidence.cjs');
const header=work+'/proposal/sdk/include/soui/runtime/accessibility/windows_uia_provider.h';let h=fs.readFileSync(header,'utf8');
h=h.replace('// atomic and bounded. A new document requires close then a new controller.',`// atomic and bounded. A new document requires close then a new controller.
  // Accepted changes wake the owner; public property/structure/focus events are
  // delivered later by pump(). Intermediate unnotified state may be coalesced.
  // S_OK accepts the projection, not delivery to a client. No secret Value event.`);
h=h.replace('// At most eight commands per call. WndProc must forward the wake message;',`// At most eight commands and sixteen event API calls per call. WndProc forwards
  // the wake message; event failures are returned without rolling back accepted
  // state and an ordinary later pump can retry the remaining notifications.
  // WndProc must forward the wake message;`);
fs.writeFileSync(header,h);
const cmake=work+'/proposal/tests/native/uia/CMakeLists.txt';
fs.writeFileSync(cmake,fs.readFileSync(cmake,'utf8').replace('com-initialization api-close)','com-initialization api-close property-events tree-events revoked-events)'));
const adr=work+'/proposal/docs/adr/0109-uia-state-notifications.md';fs.mkdirSync(path.dirname(adr),{recursive:true});
fs.writeFileSync(adr,fs.readFileSync(work+'/ADR-draft.md','utf8')+`
## 已执行的有限验证

x64 green3 与 x86 green1 均独立编译当前 proposal provider、原生测试与原 action_contract 源码；每架构 11 个独立进程均根/监督自然 0/0，无强制清理。保持公开 CTest 的逐用例进程隔离，不能将包含真实 UIA 客户端后的 COM 初始化检查合并到同一个进程；green2 的 all 失败保留。

真实订阅覆盖公开值、重复发布无新 Value 事件、root 子树失效、实际 owner 焦点及撤销前未排出的通知。secret 非空值仍先拒绝。diagnostic2 只调用一次系统属性事件 API，实际收到两次内容正确的客户端回调；最终按至少一次完整内容与无变化后的静默检查，不把系统回调数量当成 provider 的恰好一次契约。所有诊断与失败原件保留。

动作完成事件（例如 Invoked）、参考业务控件投影、真实读屏及目标系统不是这批状态通知测试的结论。此提案未整合，也未改变 Source24 或发布资格。
`);
const note=work+'/proposal/docs/acceptance/uia-state-notifications.md';fs.mkdirSync(path.dirname(note),{recursive:true});
fs.writeFileSync(note,`# 有限原生 UIA 状态通知

windows_uia_provider::publish 接受可信 owner 的公开投影后唤醒原 owner。现有 pump 每次最多派发八条业务命令、调用十六次 UIA 事件 API；WndProc 继续转交已有 wake，普通消息泵也可调用 pump。publish 的 S_OK 表示投影已接受，不证明任何客户端已消费事件。

当前通知覆盖公开名称、可用/屏外状态、边界、任务状态、非敏感输入值、单选标志、焦点，以及节点结构/身份/次序变更。未开始的相邻状态可合并；已经开始的批次先结束，再处理最新状态。结构变化发送 ChildrenInvalidated，消费者应重读子树。secret 不允许投影值或状态，不提供 Value pattern，也不发送 Value 事件。

通知在共享锁外调用 Windows API，保留实际 HWND/API 借用并禁止递归 pump。close 立即撤销，待真实借用释放后才允许销毁窗口与 controller。尚未排出的通知被撤销。系统 API 失败由 pump 返回，已接受状态不回滚；可由后续 owner pump 重试，不可把失败伪装为已送达。

新增原生 CTest：sdk.native.uia.property-events、tree-events、revoked-events。与八个原用例一样，每例一个进程；COM 初始化负例不可合并到已经激活 UIA 的进程。独立提案两架构各 11 项自然通过，详见 ADR-0109，尚未采用为当前候选证据。测试使用真实 UIAutomationCore 客户端；不等于完整屏幕阅读器或目标系统认证。
`);
const current=JSON.parse(fs.readFileSync(work+'/INPUTS.json'));e.checkPin(current.source);for(const item of current.files)e.checkPin(item.pin);
const evidence=[];for(const name of ['x64-green3','x86-green1']){
 const p=work+'/'+name+'-VERIFIED.json',v=JSON.parse(fs.readFileSync(p));assert(v.expectedObserved&&v.records.length===12);
 assert(v.records.every(r=>r.natural&&r.matched&&r.result.exitCode===0&&r.result.supervisorExitCode===0));
 evidence.push(e.pin(p));
}
const relative=[...current.files.map(f=>f.relative),'docs/adr/0109-uia-state-notifications.md','docs/acceptance/uia-state-notifications.md'];
fs.writeFileSync(work+'/PROPOSAL.json',JSON.stringify({schema:'soui.private-change-proposal/1',adopted:false,currentSource:current.source,
 scope:'Bounded state-change UIA notifications; no reference-business projection or full screen-reader qualification',
 files:relative.map(p=>({relative:p,before:fs.existsSync(root+'/'+p)?e.pin(root+'/'+p):null,after:e.pin(work+'/proposal/'+p)})),
 evidence,testsPerArchitecture:11,additionalAdoptionEdits:['Link ADR-0109 from docs/adr/README.md','Add CHANGELOG and docs/testing links while preserving long-quota proposal v2 additions'],
 currentVersionQualified:false,releaseReady:false},null,2),{flag:'wx'});
console.log(JSON.stringify({sealed:true,files:relative.length,nativeTests:22,adopted:false}));
