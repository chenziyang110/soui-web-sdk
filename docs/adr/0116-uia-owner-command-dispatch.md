# ADR-0116：Windows UIA 在窗口线程发起的同步操作

日期：2026-10-16。状态：已实现；两架构各 19 项专项回归和实际 NVDA 文本/键盘/表单消费者通过，最终公开构建与包另验。

## 背景

真实跨进程 UIA 客户端调用 fragment SetFocus 时，Windows 在 HWND owner
线程进入 provider。原实现把所有 owner 同步请求拒绝，返回
UIA_E_INVALIDOPERATION。仅处理 focus 后，NVDA 已观察到真实焦点切换，
但同一实际流程随后在 SetValue 返回相同拒绝。Windows 可能将同步模式调用
派发到 HWND 线程；公共契约必须覆盖填写、选择和焦点，而不能要求客户端
直接跨线程调用 provider。ProviderOwnsSetFocus 的独立试验未解决问题。

## 决定

SetValue、Select 和 SetFocus 在 owner 线程发起时，使用同一个有界同步路径。
请求仍检查节点身份、可用性、秘密/只读字段、类型契约和容量，入同一 FIFO，
再同步发送已配置的 wake 消息。WndProc 通过 handle_message 调用原 pump，
应用授权及业务只在 pump 中执行，不增加另一派发器或共享业务闭包。

一次同步调用最多处理原定八个命令和十六项事件；尚未执行的请求取消并返回
超时，不在 owner 等待自己，也不重排队列。缺少 wake 转发也失败且不能迟到
执行。其他线程的固定 250 毫秒等待和异步 Invoke 保持不变。

SendMessage 期间持有既有 HWND/API borrow。回调中 close 仍撤销并返回
S_FALSE，borrow 释放后完成关闭。policy、业务或通知回调期间的同步 owner
请求在入队前拒绝，避免重入业务。业务本身须遵守既有不阻塞 owner 的契约。

## 备选与兼容

删除拒绝后直接等待会让 owner 等待自己；直接执行业务会绕过现有授权、
状态与关闭边界；共享闭包会延长业务生命周期。均不采用。没有新增公共
结构或函数；正确转发 wake 的宿主现在可处理 Windows 的同步 UIA 操作。
未初始化 COM、默认拒绝、秘密字段、节点撤销等原约束保持不变。

## 验证与迁移

保留原始焦点/填写失败以及只修 focus 后的失败。新增 owner 同步操作、
真实 Windows UIA 模式调用、重入拒绝、权限拒绝与取消后不执行回归；
保留全部原有用例、FIFO 上限和关闭借用测试。按正式 CTest 的独立进程
方式运行各用例，避免同进程前序 UIA 创建 MTA 改变 COM 未初始化场景。
两架构实际 NVDA 文本、键盘表单、秘密字段与动态删除还须完整执行。
本记录不宣称未完成项目通过。应用应继续转发 wake，遵守 close pending，
且不得持 UI 锁进入 pump。

依据：[UIA ProviderOptions](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/ne-uiautomationcore-provideroptions)、[SetFocus](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/nf-uiautomationcore-irawelementproviderfragment-setfocus)、[SendMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagew)。

实际 UIAutomationCore 会在模式写入前额外请求焦点。测试分别验证一次填写、一次选择及每次派发的授权，记录额外焦点次数，不把 Windows 的合法焦点请求误计为重复业务写入。
