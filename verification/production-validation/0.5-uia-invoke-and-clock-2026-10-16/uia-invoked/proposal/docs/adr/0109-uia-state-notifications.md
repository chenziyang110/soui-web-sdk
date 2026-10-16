# ADR-0109：有限 UIA 投影变化通知

日期：2026-10-16。状态：实现已整合；双架构定向验证通过，最终候选与业务辅助功能待验。

## 背景

ADR-0047 的 provider 已有真实属性查询、typed owner 派发和关闭借用保护，但 publish 只替换投影，没有发送 UIA 变化事件。原生订阅客户端在真实 HWND 上能读到新公开值，却收不到 Value 变化。独立 red3 已实际观察此失败；早期 red1 的 CMake 路径错误和 red2 的客户端 SetValue 失败另行保留，不当作目标缺陷复现。

## 决定

publish 继续原子接受经验证的可信原生投影，并投递已有的 owner wake。变化通知由现有 pump 排出，每次最多 16 个，不在 publish 或持有共享锁期间调用 UIAutomationCore。对未开始通知的相邻发布允许合并到最新公开状态；已开始的批次按序完成，然后比较最新状态。不会创建后台线程或计时器。最多保留已通知、当前通知和最新发布的三份有界投影，加上不含字段副本的有限通知表。

现有 API 通过系统目录动态加载，增加 PropertyChanged、StructureChanged、AutomationEvent；不新增静态 UIAutomationCore 依赖，不提高 Windows 7 SP1 基线。公开属性变化按对应属性发送；节点添加、删除、换 incarnation 或排序改变发送 root ChildrenInvalidated；真实 owner 发布焦点改变后发送 FocusChanged。secret 值与状态仍不能进入投影，也不产生 Value 事件。

每次系统 API 调用仍持有 HWND/API 借用；通知期间禁止递归 pump，close 立即撤销但等待真实借用释放。通知失败从 pump 返回原 HRESULT，未完成项保留以供后续重试；已经接受的公开投影不回滚，不能把事件提交等同于客户端已消费。普通 owner pump 与现有 wake 均可接续有界批次。

## 兼容与验证

不改变公开结构布局、文档/节点身份、授权或业务派发规则。应用原本必须转交 wake 或调用 pump，现在该调用同时推进通知。publish 接受状态与异步通知交付明确分离。保持最多八条业务命令规则。

验证计划：原生订阅客户端的公开值、状态、焦点和结构事件；无变化不重复发送；secret 不泄漏；通知中止/关闭后旧 provider 惰性；既有八项原生回归；双架构编译及运行。有限事件客户端不是完整屏幕阅读器或目标系统认证，参考业务投影仍需后续接线。

依据：[Microsoft 属性事件](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiaraiseautomationpropertychangedevent)、[结构事件](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiaraisestructurechangedevent)、[UIA 客户端线程模型](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-threading)。

## 已执行的有限验证

x64 green3 与 x86 green1 均独立编译当前 proposal provider、原生测试与原 action_contract 源码；每架构 11 个独立进程均根/监督自然 0/0，无强制清理。保持公开 CTest 的逐用例进程隔离，不能将包含真实 UIA 客户端后的 COM 初始化检查合并到同一个进程；green2 的 all 失败保留。

真实订阅覆盖公开值、重复发布无新 Value 事件、root 子树失效、实际 owner 焦点及撤销前未排出的通知。secret 非空值仍先拒绝。diagnostic2 只调用一次系统属性事件 API，实际收到两次内容正确的客户端回调；最终按至少一次完整内容与无变化后的静默检查，不把系统回调数量当成 provider 的恰好一次契约。所有诊断与失败原件保留。

动作完成事件（例如 Invoked）、参考业务控件投影、真实读屏及目标系统不是这批状态通知测试的结论。定向验证的 Source24 原输入与结果保留，不能自动认证后续整合候选或发布。

## Invoked 通知补充

成功的 UIA Invoke 在原生授权、typed dispatch 与输出契约检查通过后，通过原有动态系统 API 发送 Invoked；排队、拒绝、派发失败以及回调中撤销均不能产生成功事件。发送前再次核对同一公开节点身份，期间保留现有 API/回调借用，锁外调用系统，保持关闭与递归 pump 规则。

最多八条命令产生的 Invoked 调用也计入每轮十六次事件预算，状态通知使用余量。事件失败由 pump 返回，但已经执行的业务不重放；它与可重试的状态通知不同。调用成功表示原生动作已经执行，不代表后台长任务已经结束；应用绕过 UIA 入口触发的动作不在本改动范围内。

补充回归使用真实 HWND 与 UIAutomation 客户端订阅，验证已执行的可观察结果、拒绝、失败与关闭路径。保留原有状态通知测试；实际执行结果与最终集成结果分别记录。

依据：[Microsoft Invoke pattern](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-implementinginvoke)。
