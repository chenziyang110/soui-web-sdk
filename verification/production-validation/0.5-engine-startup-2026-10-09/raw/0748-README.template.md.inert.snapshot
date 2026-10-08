# 0.5 SDK 引擎线程启动回滚：本机双架构局部证据

2026-10-09，当前 Windows 11 主机，Release x64/x86。此归档记录 SDK `engine_thread` 在事件创建或工作线程构造失败时的窄回滚，以及正常初始化、既有诊断结果寿命和实际 SDK 宏关闭编译。它不是 Miniblink 内核初始化修复、真实窗口或发布验收。

`validationPassed=false`、`allPositiveGenericSupervisionPassed=false`、`compilerTailAcceptance=false`、`releaseReady=false` 均保留。两架构配置和编译共四个实际 Job 的根进程退出 0，但残留 owned 编译辅助进程需要终止整个持有 Job：原 `generic passed=false`、supervisor exit 124、`owned-descendants-remain`、`closureConfirmed=true`、最终 activeProcesses=0 没有改成通过，也没有应用编译尾进程政策。

首个实际入口位于 `out/engine-startup-sdk-validation-b5312436-e181-415a-8b79-f22ca568d329/actual-3e8b84ec-c0cd-427c-8a54-e1a79fdfb6ed`，286 项原始索引 SHA-256 为 `092c59c35d2dcf962a3a3347a1e283980b8ded995f7c6b402a6fa72390c6ac9b`。x64 配置和编译根进程均成功退出；脚本随后错误地要求生成 vcxproj 必须字面包含 `<VCToolsVersion>14.44.35207</VCToolsVersion>`，记录 AssertionError，尚未执行 CTest、直接回归或 RED。该项目实际从默认 props 求值工具版本；原编译日志、错误结果、脚本、冻结输入和全部索引字节保留。这是脚本假设失败，不能记成 SDK 回归失败，也不能把根进程编译成功等同于监督通过。

续跑位于 `out/engine-startup-continuation-7e151d3f-4976-49af-a4cc-e68831db35c6/actual-70059d03-7f13-4310-a50d-565bbb7d0382`，386 项原始索引 SHA-256 为 `26f5f3554f92ba64ec201fa8dde8fdd04896724efda656db6e7b76820b073112`。其冻结输入 SHA-256 为 `d3058e44941616b9d50d1ae10bedfa7d64d38aed93a471c151933e4b28a8aa02`；结果 SHA-256 为 `a33097915c7b87006b3146b5f89bb2dbb161c3a4fd2bc895a4b83ca344e903b8`。

续跑核对实际 CMake 编译器路径与磁盘 SHA、cache/toolchain、每架构五份真实 vcxproj、CL/Link/Lib tlogs、owned 成员 PID 与十进制字符串 FILETIME，以及五份 MSBuild `-getProperty` 输出。工具为 Hostx64、MSVC 14.44.35207、v143 和 Windows SDK 10.0.19041.0。只求值属性，不设置工具版本覆盖或执行额外 MSBuild target。实际日志中的既有全局 VCPKG include/lib 集成原样记录，未改配置；本轮不声称封闭了所有外部 props、库或加载模块来源。文件路径和磁盘散列不认证执行内存或防注入。

x64 复用首轮按相同冻结源编译出的四个测试/控制 EXE 和完整 `soui_sdk.lib`；x86 在新目录实际编译。实际 SDK 静态库与生产控制入口的 `SOUI_ENGINE_STARTUP_TEST_ONLY` 均关闭，私有 seam 只在受控回归和旧 body RED 目标开启。宏关闭的既有诊断测试实际链接 SDK 库。原 CTest 定义按字节复制到新的测试日志目录，其命令仍指向真实编译 EXE；旧构建树和索引不改写。

两架构各实际 CTest 3/3（启动回滚、既有诊断超时、宏关闭生产控制）和直接受控回归 9/9，相关 Job 均 root/supervisor exit 0、generic passed=true、自然 completed、activeProcesses=0、closureConfirmed=true。受控 case 涵盖无初始化器、CreateEvent 返回失败、线程构造 system_error/bad_alloc、连续失败与同对象重试、重复 start 的临时事件归还、原初始化失败语义，以及用同步信号明确阻塞启动/stop。它们证明受控分支与资源所有权，不声称重现系统资源耗尽、历史 x86 挂载超时或操作系统并发压力。

两架构的旧 body RED 在真实输出 `Controlled launch exception observed; old-body destructor follows` 后卡于旧析构边界，外层 3 秒受监督超时，root/supervisor exit 124、generic passed=false、reason=timeout，持有 Job 强制清理后 closureConfirmed=true、activeProcesses=0。RED 是预期负面回归，不是自然通过或 OS 故障重现。原 body 和仅加入同一 TEST_ONLY seam 的惯性源码、差异及未执行旧测试冻结均保留。

计数按唯一原生 Job 计算：22 个实际 native phases，其中包含复用的两个首轮 x64 配置/编译 Job和 20 个新增续跑 Job；另有一次 Root 执行的 SDK Web 依赖审计。每项的精确 PID/FILETIME、argv、context、完整 native NDJSON、stdout/stderr 与原状态均可由 `archive-summary.json` 和 `byte-exact-map.json` 定位。审计运行 Node 的真实 npm CLI、读取已固定 package/lock、使用不同空 user/global npmrc 和受限子进程环境，未安装更新依赖。其 Job 自然退出归零，原审计报告 vulnerabilities.total=0；这是依赖审计，不是前端编译或全应用验证。

早期准备脚本错误、旧未执行输入、首轮工具版本字面断言错误和 Root 只读复核的大小写 marker 检查修订都保留。某次早期准备错误只有说明与源码快照，未单独捕获的 stderr 不冒称原始 stderr。Root 独立字节复核报告 SHA-256 为 `924fc1b4d37346ebc491dcaa391dee2c69cc876b3003e613b6aae191fe088127`；本归档作者也编写了启动实现和测试，因此此处的复制/哈希核验不是对自己生产或测试设计的独立批准。Root/另一审阅者的独立源码审查与 Root 执行原件分别保留。

`index.json` 只索引本目录实际文件（不含自身）。`byte-exact-map.json` 保存原组名、原绝对/相对路径、大小、SHA-256、归档路径及处理方式；所有原字节复制到扁平 `raw/` 并以 `.inert.snapshot` 结尾，不改换行、不执行归档脚本或生成构建文件。原索引仍记录私有文件，不能将其中没有 archivePath 的路径误认为公开文件。`private-artifacts.json` 保留 EXE/DLL/COFF/OBJ/LIB/PDB 等二进制与 MSBuild binlog 的原路径、SHA 和大小，仅私有保留正文；内容魔数检查也阻止改名为 inert 的二进制混入公开归档。外部 MSVC/Windows/Node 工具二进制不复制。

本轮没有运行真实窗口、键鼠输入、VM/Win7/PE 系统矩阵、GUI/UIA、Miniblink 内核、长期泄漏/8 小时、完整 SDK 分发构建、完整 Collect 或发布验收。正式 SDK 没有故障环境开关或新公开 ABI；内核初始化/线程事件/C ABI 的其他风险仍需另行设计验证。完整源码输入与散列绑定用于发现意外漂移，不保证抵御同权限用户重写整套证据。
