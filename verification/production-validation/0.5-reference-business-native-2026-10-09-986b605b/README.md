# 0.5.0 参考工作台有限原生业务观察

本目录保存 Root 已执行的有限业务测试及其历史失败原始记录。所有 raw 文件都是原件逐字节复制，统一追加 .raw 后缀；控制入口仅供阅读，不能在归档内执行。二进制、对象文件、链接库、符号文件及工具仍在私有目录，private-artifact-index.json 只记录原路径、长度与 SHA-256。

Native helper V1 未执行。V2 x64 在配置阶段因错误比较 CMake 的 MSBuild 属性文件版本标识而失败，没有业务测试执行。V3 x64 已成功生成、编译实际消费者，但 registry 以路径分隔符字符串比较拒绝了等价 fixture 路径，同样没有业务测试执行。两次失败及其进程关闭事实完整保留。V4 只修正精确三项命令中 EXE 和 fixture 路径的规范化，不放宽 case 字面量、超时或监督门槛。

V4 两架构各完成四项直接业务测试和四项串行 CTest 测试：fixed-summary、cancel、invalid-form、revoked-document。八项直接测试均以匹配的外层 PID 与字符串 FILETIME 返回实际 Bridge 析构 join 后的类型化收据；两个外层 CTest Job 均自然 0/0 退出，四个 JUnit case 无失败、错误、跳过。Win32 的短 revoked-document CTest 子进程没有出现在采样的 owned-member 行中；其 JUnit 内部 PID/FILETIME 不作为独立外层 Owned 证明。

每架构的配置和编译均为 root 0 / supervisor 124：成功根进程退出后仍有一个自有成员，2 秒 drain 后被 Job 清理。genericPassed 仍为 false，整体 controller 退出 1；completed=true 表示有限流程完成，不表示整架构构建监督成功。没有延长门槛、删除尾进程记录或把原失败改成通过。

实际 Release CL/LINK、PE、SDK 版本和缓存记录绑定固定 14.44.35207 Hostx64、SDK 19041 与 NativeV10 已安装并搬移的正常 /MD SDK。没有重新编译 SDK，也没有通过源码或私有 target 回退。每架构 41 份 V3 输入和五份实际 actiongen 输出保持绑定；V4 source/controller 的旧失败和原件保持不变。

这是正常安装 SDK 上的有限 C++ 业务、任务、取消、无效参数和撤销场景观察。浏览器、三前端交互、渲染器、GUI 生命周期、长会话、长期资源稳定性、目标 OS 矩阵及正式资格均未由这些原生测试验证。qualificationPassed=false、releaseReady=false。38 项纯模型测试与 NativeV10 两架构的生产者证据已在 c59d2104 独立归档，范围没有被本目录扩大。

Root archival adoption, 2026-10-10: all indexed inert raw copies were rechecked against their saved byte lengths and SHA-256 with zero mismatches. Root preserves this finite historical evidence in Git; no archived control entry was executed and no earlier run verdict was changed. The archive attributes keep every stored file byte-exact.
