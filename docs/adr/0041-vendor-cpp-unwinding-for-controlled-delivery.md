# ADR0041：受控内核交付的 C++ 异常展开

状态：架构决定；两架构受影响文件的首轮编译已记录，完整内核及失败回收验证尚未完成。源码配置修正不代表发布验收通过。

## 原因

受控 broker、拖放复制和任务交接以局部 C++ 异常围栏及 RAII 释放尚未交接的资源。vendor 工程的 Release 与 Release_NoNode 在 Win32 下原先设置 ExceptionHandling=false，x64 为 Sync。默认 MSVC 模式仅部分支持 C++ 异常展开，不能据此证明 try 块外对象的析构。因此，使用 /EHsc 的私有回归结果不能外推到旧 Win32 内核。

## 决定

将上述两个 Win32 配置的全部重复 ItemDefinitionGroup 统一为 Sync，对应 /EHsc。PCH 与实际翻译单元采用同一配置。既有 x64 配置保持 Sync；旧固定二进制不因源工程变化获得新的身份或验证结论。

公开 C ABI、pack、结构字段、调用约定及 token 格式保持 ADR0040 的约定。异常必须在内部 C++ 操作或回调围栏中处理，不能穿过公开 C 入口或 Windows 回调。/EHc 假设 extern "C" 调用不抛出，因此 C 入口之外的 catch 不是补救契约。第三方关闭异常展开的栈帧也不能获得本工程的展开保证。

这项决定只补齐普通 C++ 异常展开的编译前提。访问违规、SEH、PartitionAlloc 的 fail-fast 及任意 Blink/V8 内存耗尽仍不构成本地可恢复异常。已有部分页面构造、worker 启动与关闭缺口须另行实现真实所有权和清理回执；启用 /EHsc 不会使这些缺口自动闭合。

## 验证与影响

实际构建必须记录 v143、选定 Windows SDK、配置、PCH 和每个翻译单元的编译参数及输入。先编译两架构实际受影响文件，再完成完整内核重建、SDK 接入和真实窗口回归。局部模型、配置标签及旧 DLL 都不能代替这一证据。

新内核的字节、体积、时序与资源行为需重新测量，源码、产物和验收记录须绑定同一候选。标准 C++ ABI 未变不等于新旧二进制可混用；发布包使用其准确清单与散列。

依据：[Microsoft 的 /EH 编译选项说明](https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model?view=msvc-170)。具体默认展开限制、标准 C++ 异常模型与 extern "C" 假设由该文档规定；上述所有权与发布要求是本项目的决定。
