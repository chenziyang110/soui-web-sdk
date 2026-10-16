'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const edits=[['docs/api.md',[
 ['示例见模板；UIA与外部Agent服务不在本版实现范围内。',
  '示例见模板。0.5 的原生 UIA 与本地 Agent 分别显式启用，边界如下；页面 GUI 注册本身不会自动启用它们。'],
 ['## C++ 接入',`## 可选原生 UIA 与本地 Agent（0.5）

\`Soui::Uia\` 导出 \`<soui/runtime/accessibility/windows_uia_provider.h>\`。使用 \`Build.ps1 -WithUia\` 或 \`Soui.ps1 build --with-uia\` 构建；CMake 消费者显式请求 \`Uia\` 组件并链接 \`Soui::Uia\`。应用在实际 HWND 所有者线程初始化 COM、创建 provider、发布审核后的公开投影，并转发 \`WM_GETOBJECT\` 与选定的唤醒消息。普通 owner 消息泵继续调用 \`pump()\`，导航或退出时先撤销并等待 \`close()\` 返回 \`S_OK\`，再释放控制器与 HWND。模块不扫描 DOM，也不自动接入 Host 业务。

首批控件为按钮、非敏感单行输入、单选列表与只读任务状态。\`publish()\` 接纳状态后，由有界消息泵发送属性、结构及焦点通知；成功 Invoke 发送 Invoked 通知。Invoke 的返回表示排队受理，业务结果须另行观察。策略默认拒绝；应用将动作绑定到既有 typed native 业务入口，UIA 不提供 Agent 身份或原生用户手势。敏感输入不提供 Value pattern。构建、线程及关闭细节见 [UIA 设计](adr/0047-finite-windows-uia-provider.md)、[状态通知](adr/0109-uia-state-notifications.md)和[构建与交付](adr/0110-uia-build-delivery.md)。

\`json_bridge::create_local_endpoint()\` 在业务注册完成、首个页面 bootstrap 之前显式创建停止状态的 Windows 本地端点。应用配置允许的调用主体与能力后才启动；\`allow_local_action()\` 只公开已经注册的 typed action，长任务通过 \`register_local_task_family()\` 复用同一 Bridge 的任务服务。页面授权、UIA 桌面访问和本地 Agent 授权分别处理。端点由 Bridge 持有，应用必须在销毁 Bridge 前完成真实停止与 owner 收尾；详见[端点契约](adr/0046-windows-local-agent-endpoint.md)和[本地任务 family](local-task-families.md)。

\`enable_gui_commands()\` 为明确注册的 GUI 操作接入任务协议；\`bind_gui_document()\` 由原生 owner 绑定实际 Host 文档身份和有效性检查。关闭时先 \`close_gui_commands()\`，由 owner 持续 \`poll_gui_commands()\` 至 closed，再销毁 Host。它不会把任意页面 handler 转成可远程调用的方法；原生效果与页面确认各有独立校验，见 [GUI 授权边界](adr/0054-gui-command-authority.md)。这些接口可用不代表读屏器、所有参考应用或目标系统已通过验收。

## C++ 接入`]
 ]],['docs/architecture.md',[
 ['公共 SDK 尚未安装 WebView2 后端；`experiments/webview2-consumer/` 单独验证 WebView2 适配器，其异步初始化先于同步挂载，设计见 [ADR-0013](adr/0013-experimental-webview2-consumer.md)。',
  '公共 SDK 通过可选 `Soui::WebView2` 安装现代适配器，其异步初始化先于同步挂载。早期 `experiments/webview2-consumer/` 保留实验来源；当前交付与初始化契约见 [ADR-0016](adr/0016-runtime-profiles-and-modern-backend.md)和[现代后端](modern-backend.md)。'],
 ['完整 Agent 主体授权和后续工作见 [建设路线](roadmap/agent-gui-framework.md)。',
  '0.5 的本地 Agent 端点由应用在原生侧显式创建和授权，调用复用同一 Bridge 的动作与任务服务，见 [ADR-0046](adr/0046-windows-local-agent-endpoint.md)。可选 `Soui::Uia` 由独立的 HWND owner 控制器发布公开状态和调度允许的业务动作，不把桌面 UIA 访问转换为 Agent 授权，见 [ADR-0047](adr/0047-finite-windows-uia-provider.md)。后续范围与验收见 [建设路线](roadmap/agent-gui-framework.md)。']
 ]]];
const changed=[];
for(const [relative,replacements] of edits){
 const source=root+'/'+relative;let value=fs.readFileSync(source,'utf8');
 for(const [before,after] of replacements){assert.equal(value.split(before).length,2,relative+' anchor must be unique');value=value.replace(before,after);}
 const target=path.join(__dirname,'proposal',relative);fs.mkdirSync(path.dirname(target),{recursive:true});
 fs.writeFileSync(target,value,{flag:'wx'});changed.push({relative,before:e.pin(source),after:e.pin(target)});
 for(const match of value.matchAll(/\]\(([^)#]+\.md)(?:#[^)]*)?\)/g)){
  if(/^[a-z]+:/i.test(match[1]))continue;
  assert(fs.statSync(path.resolve(path.dirname(source),match[1])).isFile(),'Missing documentation link: '+match[1]);
 }
}
const inputs=['sdk/include/soui/json_bridge.h','sdk/include/soui/runtime/accessibility/windows_uia_provider.h',
 'docs/adr/0046-windows-local-agent-endpoint.md','docs/adr/0047-finite-windows-uia-provider.md',
 'docs/adr/0054-gui-command-authority.md','docs/adr/0109-uia-state-notifications.md','docs/adr/0110-uia-build-delivery.md'].map(p=>e.pin(root+'/'+p));
fs.writeFileSync(__dirname+'/PROPOSAL.json',JSON.stringify({changed,inputs,documentationLinksPresent:true,
 adopted:false,runtimeClaimsAdded:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log('Prepared two current API documents; source candidate remains frozen.');
