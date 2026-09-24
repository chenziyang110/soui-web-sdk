# preview.5 任务容量与独立交付短验收

日期：2026-09-24。候选 `0.3.0-preview.5`，SDK 构建源码指纹 `16d1dccc47ebacff83bf0a94abeed21c75cd7bcd049559802598a5e44656f237`。本次针对公共 `task_store` 在 1,024 个未终结任务处越过上限的问题，增加明确容量拒绝并向文件扫描及 Workbench 返回 `E_BUSY`。所有通过和身份字段见 [result.json](result.json)。

本机 x86/x64 Release 完整构建、SDK 包/锁验证、原生契约、Web 测试及模板/Workbench 默认和 React 三个实际桌面 smoke 入口均通过。新增原生测试在两架构分别覆盖硬上限、重复请求幂等、冲突、取消请求仍占位、确认取消后回收、拒绝不消耗任务 ID、八线程并发准入及目录扫描的 `E_BUSY` 传播。原始 [x86 构建](x86-sdk-build.json)、[x64 构建](x64-sdk-build.json)、[x86 smoke](x86-sdk-smoke.json)、[x64 smoke](x64-sdk-smoke.json) 和两份 CTest 日志均归档。三个 Web 锁文件的 npm 审计在本轮均报告 0 项。

从当前源码打出 402 文件的 [SDK 包](sdk-manifest.json) 并验证包与源码匹配，随后在仓库外新建 `CapacityTrial`，校验 [项目锁](sdk-lock.json)。该项目的 x86/x64 Release 分别编译成功；两个可执行文件均在本机以 `--smoke-test` 实际启动、退出码 0，日志记录 `application-ready` 和 `host-close-complete`。归档了两份[构建身份](x86-independent-build.json)和[启动日志](x86-independent-smoke.log)；x64 对应文件同目录。`result.json` 记录可执行文件散列，测试项目及包实体保留在本机 D: 验证目录，不随仓库分发。

完整源码清单生成后还执行了无 Git 的跨目录搬移：先校验 29,823 个清单文件，再从 ZIP 解压至独立 D: 目录并逐文件验证；该目录中 x86/x64 Release 完整应用构建、原生/JavaScript 测试及三个桌面 smoke 入口再次通过，且构建源码指纹与仓库相同。[x86 搬移构建](x86-relocated-build.json)、[x64 搬移构建](x64-relocated-build.json)及同目录两架构 smoke、CTest 日志均已保存。最终归档在纳入这些证据后重新生成并复核；代码字节保持相同。底层 Miniblink/SOUI 依赖的源码重编译尚未在搬移目录执行。

SDK 打包的 Windows 发布目录重命名偶发 `EPERM`：改动前默认临时目录 12 次中失败 6 次，另一磁盘临时目录 12 次均通过，不能据此判定具体锁持有者。现在仅在暂存目录仍存在、目标尚不存在的 `EPERM/EACCES` 上做最多五次有界重试；注入测试确认瞬时拒绝后成功、持续拒绝仍失败并清理暂存目录，本机默认临时目录 12 次均通过。该修复只降低已观察到的偶发打包失败，不掩盖永久权限错误。

这是容量与独立模板的短验收，不覆盖完整公共任务服务、六组 Demo 业务、并发关窗长稳、8 小时混合负载、资源收敛、目标机性能或 Electron 同负载对照。新版的 Windows 7 SP1 x86 QEMU [三应用入口](../win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)及[四模式公共消费者](../win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)三轮短测另行归档；这两项也不构成生产放行。`releaseReady=false`。
