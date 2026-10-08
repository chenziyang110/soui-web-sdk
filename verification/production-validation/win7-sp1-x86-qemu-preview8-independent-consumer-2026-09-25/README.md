# preview.8 独立生成项目在 Windows 7 SP1 x86 QEMU 的运行验证

状态：**独立项目的指定虚拟机短启动通过；生产准入未通过**。从 SHA-256 `00C405582079762CF01778841FFEA1A9B91392897B6B1E7178778330657241B5` 的 preview.8 最终完整源码 ZIP 全新解压、核验，并用 `New-Project.ps1` 在 SDK 仓库外生成 `Preview8Win7`。生成过程校验并安装 408 文件 SDK 包；[项目包锁](sdk-lock.json)、[SDK 包清单](sdk-manifest.json)和[x86 Release 构建报告](build-Release.json)分别固定包与构建身份，构建 ID 为 `bd875e60-72b6-436b-8fd4-e9719d92a814`。本机先完成编译与真实 `--smoke-test` 启动/关闭。该应用只使用生成模板与已安装 SDK，不使用 Workbench 原生代码。

将该项目的 71 文件运行目录打包，宿主测试 ZIP、重新解压并逐文件比较[载荷散列清单](payload-sha256.tsv)。[输入记录](input.json)绑定源码 ZIP、SDK 包、包锁、构建、应用 ZIP、EXE、引擎、SOUI DLL 与客体脚本散列。应用 ZIP SHA-256 为 `28E08487FC58CB3D4097235F696F84FC5E27CF3C4C0230B696FE013214C21035`。客体 QEMU 使用 Windows 7 Enterprise SP1 x86 基础镜像、`-snapshot`、只读 FAT 传输盘和禁用网络；在 NTFS 解包后再次校验全部 71 个文件和 5 份许可材料，随后运行生成项目。QEMU 已退出，基础磁盘未写回。

[完成画面](evidence/complete-console.png)显示应用退出码 0 与 15 份结果已导出。[原始串口帧](serial-export.txt)由[独立核验器](Verify-ConsumerSerial.ps1)检查长度、SHA-256、来宾 `6.1.7601`/SP1/32-bit 身份、全载荷校验、应用就绪与正常关闭日志，结果见[核验输出](verifier-output.txt)。[反例自检](self-test-output.txt)在重新计算串口帧散列后伪造引擎散列，核验器仍拒绝。原始客体文件在 [results](results/)；从本目录运行核验器与反例自检均通过。

[首次尝试](failed-attempt/)保留了真实失败：该 Win7 客体的 PowerShell 不支持测试脚本所用的 `Dispose()` 方法，校验日志报错，而旧批处理把该步骤误记为退出 0；尽管应用随后退出 0，首次尝试**不计通过**。第二次使用新的只读传输盘，将校验脚本改为 `Clear()`，并要求 PowerShell 退出码、71 文件和 5 份许可材料的成功日志同时成立；[第二次客体日志](results/payload-verify.log)与独立核验均通过。失败样本由新版核验器拒绝，未以成功样本覆盖。

这只证明最终源码包生成的一个最小独立项目能在 Win7 SP1 x86 虚拟机的软件路径短启动并正常关闭。没有验证物理设备、独立业务长任务/窗口交互、安装升级、DPI/IME、多屏、资源长稳或安全维护；与其他虚拟机任务并行的时间数据不作性能结论。`sourceDeliveryVerified=false`、`releaseReady=false`。
