# requestClose 关闭入口叶回归元数据

本目录记录 Windows/MSVC Release 下 x64 和 x86 的 V4 叶回归实际结果。每架构保留 18 个阶段的原始退出状态、6 个旧行为反例，以及 11 个修复后直接运行和 11 个 CTest 用例。旧行为批次根进程与监督器均退出 8；六个失败都是各自预期断言，未分别声称每个子进程的数值退出码。修复后直接运行和 CTest 根进程与监督器均退出 0。

configure 与 build 的根进程退出 0，但监督器退出 124，原通用通过判定（genericPassed）仍为 false；其编译尾进程关闭只能作为有限继续条件。旧实验中的强制尾进程清理、失败包装器和静态阻塞记录均保留，不成为本次通过证据。

结果采用真实 message-only HWND、HMENU、Win32 定时器及 broker 资源账本；菜单成功销毁后才释放账本。callout 重入用例没有排队定时器。COM 覆盖只验证实际账本 API，没有 COM 对象；native admission fault 用例仍为未 drain。引擎与 UI 运行在同一原生线程，view 身份为不透明夹具。

这里只保存结果信息、字节数、SHA-256 和相对于仓库根目录的本地证据引用。私有源码快照、PE、DLL、编译器和工具实体均未复制；被引用的 out 文件仍属于维护者本地原件，不随这些元数据交付。两份架构记录可独立核对当前源码身份，公开测试目录的整合构建与运行应记录在另行执行的全套单元证据中。

这些叶结果不完成真实完整 DLL 启动与关闭、分离引擎/UI 并发、完整编译器输入闭包、目标系统矩阵、GUI、性能或正式发布资格。validationPassed、qualificationPassed、releaseReady 均保持 false。
