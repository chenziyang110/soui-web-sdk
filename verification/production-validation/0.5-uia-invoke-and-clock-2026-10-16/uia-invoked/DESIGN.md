# UIA Invoke 成功通知修复

2026-10-16，独立实现前决策。基线是已封存但未采用的 uia-events-1 提案，候选 Source24 保持不变。

已实现的按钮 IInvokeProvider 将命令排队并由真实 owner 调用 typed action；现有属性/结构/焦点通知不覆盖无状态按钮的 Invoked 事件。按 Microsoft 的 Invoke pattern 约定，完成动作后应发送 UIA_Invoke_InvokedEventId。该事件不能由排队成功、鼠标悬停或授权拒绝触发。

只在输入、原生授权、派发结果与输出 schema 均成功，且同一非秘密节点 incarnation 仍有效时调用现有动态绑定的 UiaRaiseAutomationEvent。不增加公共方法，也不声称覆盖应用绕过此入口自行触发的业务动作。真实 dispatch 返回后，在其 callback borrow 结束前发送事件；系统调用在共享锁外，由现有 API borrow 保留 HWND/模块，阻止重入 pump/关闭释放。

一轮 pump 仍最多处理 8 个命令，总事件 API 调用最多 16 次。成功 Invoke 消耗同一事件预算，后续属性/结构/焦点派发使用余量。通知失败不重放已提交业务，pump 如实返回失败；关闭和撤销保持原有优先级。

验证使用真实 HWND、真实 UIAutomation MTA 客户端与事件订阅，先用原基线证明缺通知，然后验证成功收到事件且已执行原生业务；授权拒绝、派发失败、回调关闭均不产生完成通知。保留全部原有 11 项回归，两架构实际执行。它不代替真实产品读屏、键盘或目标系统验收。

来源：https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-implementinginvoke
