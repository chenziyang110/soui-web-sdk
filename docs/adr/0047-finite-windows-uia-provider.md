# ADR-0047：有限原生 UI Automation 投影与 owner 执行边界

日期：2026-10-09；集成状态更新：2026-10-10。状态：可选 SDK 模块已接入，双架构有限原生与安装/搬移消费者已运行；Host 业务投影仍待接入。范围：0.5 P0 05-08b；按钮、单行输入、单选列表和只读任务状态。以 Windows 7 SP1 API 基线、C++17、SDK19041 构建的有限 provider，不代表 Win7 实机、读屏、长期或目标系统资格已通过。

## 背景与实际入口

`sdk/web/src/gui.ts` 的 `soui.gui/1` 是页面内显式语义注册，包含 id、role、name、enabled、busy、state 和命名 actions。它有 256 节点和 64KiB 快照上限，不赋予用户手势。当前没有将该注册表转换为可信 native GUI 的接口。`action-contracts.ts` 与 native `action_contract` 使用同一个 `soui.action/1`，ADR-0014/0024/0027/0037 保留原生最终校验。

当前 `webview_host::pump()` 和 Workbench 的 `PumpHost()` 在 UI owner 运行；窗口已有消息路由，未发现 SDK 原生 `WM_GETOBJECT` provider。Miniblink adapter 有实际 execution-context receipt，但页面发来的 generation 不因此成为 native 权威值。Workbench 在 owner 初始化 STA；新模块不替应用初始化或结束 COM。

Project cognition 当前返回 missing_graph_store（数据库缺失），不能提供已验证路径；本记录据上述有限 live source 读取和 SDK19041 声明作出决定，未启动 sp 工作流或将图摘要当作事实。

## 决定：显式原生投影

可信 native owner 发布不可变投影。它采用已有 `soui.gui/1` 的 id/role/name/enabled/busy 含义，不增加 wire 协议，也不抓 DOM。只有 native 审核的公开字段可进入投影；任意页面 JS handler 不可因 UIA 可见而调用。投影字段包括 native 文档代次、稳定注册 id、native incarnation、公开名称/状态/值、真实屏幕坐标，以及绑定到现有 typed native action 的方法与 contract。

总节点不超过 256，公共字段及绑定文本合计不超过 64KiB，单值不超过 4096 UTF-16 单元，队列不超过 64 个命令。树只有 root → 控件、list → item 两层；无虚拟化、多选、任意 DOM、TextPattern 或历史无界缓存。列表固定要求一项选中，通过 Selection/SelectionItem，按钮通过 Invoke，非敏感单行输入通过 Value，任务状态为只读 Text 控件。焦点与坐标由 native owner 发布；可选 Focus 同样绑定 typed native business 方法，不能从外部 wire 中相信坐标或焦点。

secret 输入不接受可镜像值字段，不提供 Value pattern 或 Value 属性，只提供 native 允许的公开名称及 IsPassword。本切片不支持通过 UIA 填写 secret。任务结果、异常原文、token、operation 输入和任意 state 不投影。TS sensitive 字段不能扩大 native 白名单。

native incarnation 单调分配且不复用，耗尽即拒绝；同一文档同一注册持续存在时保留 incarnation，移除再加入获得新身份。child RuntimeId 使用 UiaAppendRuntimeId + 文档代次/ incarnation 的整数分段，非数组下标；HWND root 返回 NULL RuntimeId。更换文档、删除节点、关闭/revoke 后旧 COM 对象的语义方法返回 UIA_E_ELEMENTNOTAVAILABLE。QueryInterface/AddRef/Release 仍遵守 COM 身份和寿命。

## COM、队列和实际 owner 泵

每个 provider 实现 Simple 与 Fragment；root 另实现 FragmentRoot。provider 聚合 FTM，返回 ServerSideProvider | UseComThreading。线程安全共享 state 只拥有不可变投影、身份、命令、原生同步资源和 API 计数；不拥有 Host、UI callback、STA 接口或跨 apartment 代理。owner controller 单独拥有 policy 和业务回调，只在创建 HWND 的实际线程使用。

COM 查询只复制已发布事实。所有业务变更先进入有界队列，使用实际 PostMessage 唤醒 owner，WndProc 转交模块的 private wake message，常规 Host pump 也可显式排空以避免丢失唤醒。PostMessage 只调度，不构造手势。所有 callback 和 UIA API 调用均在释放 state 锁后运行；callback 期间递归 pump 返回 S_FALSE，不嵌套执行业务。每次执行前重验 native 文档/注册身份、enabled/busy、native policy 和 `action_contract::validates_input`。

Invoke 是异步排队请求，S_OK 仅表示已受理。SetValue/Selection 的 S_OK 必须表示 owner 已实际完成该同步业务变更；非 owner COM 线程最多等固定 250ms，owner 上同步 mutator 返回 UIA_E_INVALIDOPERATION，不直接执行或自等。超时取消尚未开始的命令；已提交的业务不保证回滚，不能把 timeout 当作未执行证明。owner 回调必须短且非阻塞，长任务使用现有 task_service，禁止借 provider 创建另一套任务调度器。UIA 只看到之后由 owner 发布的实际结果。

默认 policy 拒绝。应用显式将某项 GUI action 绑定到已注册 typed native 业务入口；UIA 调用本身不创建 authorization_scope、bearer、Agent 身份或 native_user_gesture。policy 不能只凭方法名或 UIA 客户端声称的 actor 扩权。输入和输出继续采用同一个 soui.action/1 contract，敏感/手势要求依现有原生策略拒绝。

close/revoke 先使共享 state 失效、取消未提交命令并拒绝新借用。实际执行回调和使用 HWND/UIA API 的借用计数归零之前，close 返回 pending，owner 在泵中重试，不能释放 controller 或销毁 HWND。回调内要求关闭只撤销并返回 pending；退出回调后再释放 owner callback。残留外部 COM 引用仅持惰性 state，不能延长 Host/业务捕获对象寿命；其最后 Release 可以在任意 COM 线程执行。

## 可选模块与 PE 缺组件

实现位于可选 `sdk/accessibility/uia/`（避开默认 sdk/src GLOB），公开头位于 sdk/include；通过默认关闭的 `SOUI_BUILD_UIA` 接入独立 `Soui::Uia` target，默认 `Soui::Sdk` 不调用或链接该模块。SDK 配置导出实际 `Uia` 组件状态；安装的 `Soui::Uia` 与 SDK 使用同一导出集合。Workbench 尚未接入投影。

即使显式启用，不能静态链接 Uiautomationcore.lib 或调用静态 UIA API，避免 PE 主 EXE 在能力探测前导入 UIAutomationCore.dll。从 GetSystemDirectoryW 的受控完整路径 LoadLibraryW，逐项 GetProcAddress Win7 API；缺 DLL/导出返回固定 capability_unavailable，普通 SDK/页面语义仍可工作。使用后的系统模块明确 pin 到进程寿命，以免 UIA/COM 调用栈中卸载；不声称 DLL unload 已验证，也不在任意 COM Release 中 FreeLibrary。

owner 收到 WM_GETOBJECT 时比较 DWORD(lParam) 与 UiaRootObjectId，原样向动态 UiaReturnRawElementProvider 传 wParam/lParam；其他 object id 继续原 WndProc。关闭先 revoke，随后 owner 用 UiaReturnRawElementProvider(hwnd, 0, 0, nullptr) 释放窗口映射，再销毁 HWND。Win8 起的 UiaDisconnectProvider 不属于本切片依赖。首批 client 读取/pattern 可用，完整事件、读屏行为和宿主集成单独验证，不据可轮询状态声称通知完整。

## 备选、迁移与验证

仅 JSON 快照没有原生 COM provider；DOM 自动抓取不能建立 native 字段/动作权限；COM 线程直接调用 UI/renderer 会绕过 owner 并破坏退出寿命；把 UIA 接到 local Agent handle_local 会错误混用主体和授权。上述方案均不采用。

未来应用 opt-in Soui::Uia，建立 controller，发布 native 审核投影，转发 WM_GETOBJECT/private wake，在普通 owner pump 排空；导航和关闭先 revoke。现有 SDK 和 Workbench 行为保留，默认不开 UIA。Host 适配时仍以真实 execution-context receipt/owner generation 为准，不接受页面自报代次。

真实 native 测试使用实际 HWND/WndProc 与 owner 消息泵，另一个实际 MTA UIA client 调用 ElementFromHandle 引发 WM_GETOBJECT；不得把手工 SendMessage 或 JSON 模型当成 UIA client 识别。八项测试覆盖 COM QI/refcount、属性/真实 patterns、稳定 RuntimeId、secret/stale、错误线程排队、原生策略拒绝、队列容量、超时取消、回调中关闭与撤销后引用。`api-close` 观察真实跨线程系统 API 调用与随后关闭，不保证调度实际重叠。Root 已在开发机统一执行两架构直接用例及 CTest；Win7、PE 缺组件、读屏和长期稳定性仍 pending。

## 有限实测与源码交付范围

独立 provider 的早期 x64/Win32 构建分别运行八项直接用例和 CTest8，真实业务回调仅发生在 owner；真实 UIA client 找到投影按钮并触发一次实际 Invoke。原配置/编译出现 root0/supervisor124 的编译器尾进程，记录继续保持 generic-false。后续正常 SDK 的 NativeV10 全套集成运行包含同一 provider 源码、公开头和八项 UIA 测试；两个架构的 126 项 CTest 均正常结束。这个有限测试集不证明尚未接入的参考应用 UIA 业务流程。

独立安装和搬移结果位于 `out/uia-install-v1/x64/summary.json`（SHA256 `b916541a0d01c1899ff88b48a11b963778f52c980428f8071a1863d192d5cd96`）和 `out/uia-install-v1/Win32/summary.json`（`f112a49ce7eeba162027d42fb1523721e48601348bb1bc31ffbcf27f4a422b27`）。已安装与搬移后的真实消费者均以 imported `Soui::Uia` 编译、链接，直接调用及 CTest 验证真实 nullptr HWND 参数拒绝；默认 OFF 包无 Uia 导出，请求 REQUIRED Uia 的实际配置返回1/1。这些编译阶段的0/124原始结果没有改为通过；该测试只证明组件/符号/头文件交付及参数拒绝，不证明 provider 可用性。

公开 `tests/uia-consumer/` 的两个构建/原生输入保留上述消费者的准确原始字节。旧安装试验绑定23个核心 SDK CPP；后续 SDK 增加端点和启动修复，最终候选须重新构建、安装、搬移和验证，不能把旧载荷资格继承给新的提交。正式发布、完整工具链、目标系统、Host 业务投影、读屏与长期运行资格继续为 false。

官方依据：[server-side provider](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-serversideprovider)、[WM_GETOBJECT](https://learn.microsoft.com/en-us/windows/win32/winauto/wm-getobject)、[UiaReturnRawElementProvider](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiareturnrawelementprovider)、[ProviderOptions](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/ne-uiautomationcore-provideroptions)、[FTM 限制](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cocreatefreethreadedmarshaler)、[RuntimeId](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/nf-uiautomationcore-irawelementproviderfragment-getruntimeid)、[Invoke 异步契约](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-implementinginvoke)、[Value SetValue](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/nf-uiautomationcore-ivalueprovider-setvalue)、[Select](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/nf-uiautomationcore-iselectionitemprovider-select)、[Disconnect Win8 要求](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiadisconnectprovider)。
