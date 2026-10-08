# ADR-0038：SDK 引擎线程创建失败的回滚

- 日期：2026-10-09
- 状态：accepted；实现与实际双架构结果分别绑定后续冻结输入和原始报告
- 范围：`engine_thread::start()` 尚未创建 worker 时的失败；不修改内核启动、公共头文件或 ABI

## 背景

当前实现先创建 Windows 唤醒事件，再在互斥锁内发布 `starting` 和 `startup_complete_ = false`，最后构造 `std::thread`。线程构造抛异常时没有 worker 可以完成启动通知，状态和事件仍留在对象上。随后的 `stop()` 或析构等待启动完成，因而可能永久等待。这是源码确认的 SDK 生命周期缺陷；它不证明历史 Miniblink 挂载超时或资源增长的原因。

Microsoft 的 [std::thread 文档](https://learn.microsoft.com/en-us/cpp/standard-library/thread-class?view=msvc-170) 说明线程创建失败会抛出 `std::system_error`；构造表达式的分配也可能失败。Windows [CreateEventW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw) 失败返回 NULL。已有事件失败返回 false 的语义保留。

## 决定

只捕获现有 worker 构造表达式的异常。在现有互斥锁内把未启动的对象还原为 `stopped`，把 `startup_complete_` 设为 true，清空 owner 线程身份，移除并关闭本次尝试的唤醒事件。释放锁后通知状态等待者并返回 false。此时没有 worker、没有接纳工作，initializer 和 shutdown 均不执行；同一个对象可以再次尝试启动。

不把 `start()` 声明为 noexcept，也不扩大到参数构造、互斥锁错误或已经进入 initializer 的异常。真实 worker 的 initializer 返回 false 或抛异常仍走既有 `failed` 状态、同一 owner 上的 shutdown 和 join 语义；本决定不把该状态改成可直接重试的 `stopped`。不增加 initializer 超时、线程 detach、强制取消或借用对象提前释放。

## 测试边界

新增只在独立回归目标编译时定义的 `SOUI_ENGINE_STARTUP_TEST_ONLY` 私有 seam。它仅替换本文件的事件创建/关闭调用点，并在 worker 构造前注入受控异常；函数由回归目标提供。正式 `soui_sdk` 不定义该宏，不含 hook 状态、故障环境变量或新公开接口。回归目标直接编译实际 `engine_thread.cpp`，不同时链接 SDK 中的第二份实现。

测试检查受控 `system_error`、`bad_alloc`、事件创建失败、重复失败、同对象真实重试、正常 FIFO 与同 owner shutdown，以及 initializer 失败的原语义。事件账本仅计数 seam 实际创建和关闭的 HANDLE，不能外推整个进程或内核没有泄漏。并发交错使用 promise/future，不用 Sleep 猜调度；析构是否有界由外层持有 Job 的超时和完整关闭证据确认。

RED 使用修改前正文的私有 inert 副本，加上与 GREEN 相同的调用点 seam。注入构造异常后真正析构该对象，旧实现应在外层期限内不能自然结束；保留失败原件和本任务 Job 清理结果，不还原生产代码、不管理无关进程。GREEN 使用当前实际生产源码。受控故障证明代码分支和生命周期，不声称复现操作系统实际耗尽资源。旧失败原件不能被后续成功覆盖。

## 兼容与未关闭门槛

公共 C++ 类型布局、方法签名、FIFO、消息泵、Windows 7 最低目标和内核输入不变。正常启动和初始化失败保持原行为。验证仅为无窗口 SDK 回归，不代表真实 Miniblink 启动、C ABI 异常边界、vendor 线程/事件失败、GUI、目标 OS 矩阵或长稳完成。vendor 的部分初始化安全退出与 fail-fast 需要独立 ADR 和候选内核身份，本切片不修改它们。
