# ADR 0118：MiniBlink 关闭的分阶段继续状态

状态：作为自动宿主的必要前置纳入本候选；当前验证见
[PR 验证范围](../../verification/production-validation/auto-host-pr-2026-10-23/README.md)。

## 背景与决定

公共 Host 的 close/destroy 调用 MiniBlink adapter 的 unmount。若 UI 线程
同步等待引擎 FIFO 屏障或 Bridge worker 退出，尚未退休的引擎/业务工作可能
阻塞窗口消息循环。自动宿主需要继续派发消息、保留所有权并观察真实清理完成。

将关闭链保存为适配器持有的步骤：引擎屏障、Bridge 退休、引擎清理、endpoint
请求与排空、UI 资源退休。未完成的引擎步骤只入队一次，由原 cleanup FIFO
执行；UI 只读取已同步发布的结果，未完成时返回既有 `E_CURSOR_CLOSE_PENDING`。
Bridge 使用原 `try_stop` 保留线程并作零等待观察，不同步 join 或停止共享引擎。

关闭开始即撤回文档和普通操作准入。重复调用继续原步骤，不重新排队已接纳的
任务；pending 时 reload/show/resize/focus 被拒绝，pump 只推进关闭。任务状态
跨调用持有，不能捕获已经返回的调用栈。原生错误仍为错误，定时重试不等于完成。

## 所有权与兼容

Host、adapter、借用的 Bridge、两个 HWND 和 Runtime Host 计数保留到真实清理完成。
调用方在原 UI owner 持续处理消息，并在原关闭总期限内重试 close/destroy；
超时不能作为释放对象的依据。未挂载且没有存续资源的适配器允许跨构造线程
执行幂等 unmount，不能用诊断构造线程替代实际窗口所有者约束。

普通关闭也可能需要后续 UI 轮次，沿用已有 pending/保活协议。私有实现布局变化
需要重新编译源码消费者，不替换已发布 0.5.0。此决定不增加公共调试接口、
不更改 reload 为异步操作，也不改变 MiniBlink 进程级最终退出责任。

## 备选与验证边界

不全局改变具有栈捕获的同步生命周期调用，不给 future.get 添加超时后强制
释放对象，不从其他线程销毁 WebView。共享 engine FIFO 保留既有调度与所有权。

无 DLL 回归检查占用的 FIFO 下返回 pending、重复轮询不重复入队、普通操作
被拒绝、原 engine/worker 保留及完成后的收尾。真实应用另检查页面业务往返、
关闭和进程自然退出。这些检查不能推定所有 V8 暂停、COM/OLE、用户回调交叉
时序或长期稳定性均已验证；并行调试原型的历史结果不移植为本 PR 的通过证据。
