# 可选压缩档位

本能力属于未发布 SDK 候选，只改变应用或 Bundle 的归档封装。默认仍使用
原 ZIP 参数和原文件名；项目配置、载荷、Web 后端、运行目录及已发布固定包
均沿用原契约。`-Profile` 选择 compatibility/modern/auto 后端，与压缩档位分开。

## 选择档位

从完整 SDK 源码根目录执行，已有构建、启动证明和体积预算检查继续生效。
此公开归档入口面向内置 Starter、Workbench、ModernStarter 和 AutoStarter；生成项目的
统一 `package` 命令仍返回既有能力限制，不会因选择新档位而获得应用分发入口。
生成项目的构建、部署及内部受证据约束的暂存能力保留。

示例：

```powershell
# 兼容原调用，等价于显式 -CompressionProfile zip。
powershell.exe -File tools/Package-Applications.ps1 -Architecture x64
# 选择 7z 档位。
powershell.exe -File tools/Package-Applications.ps1 -Architecture x64 -CompressionProfile 7z-balanced
powershell.exe -File tools/Package-Applications.ps1 -Architecture x64 -Bundle -CompressionProfile 7z-small
# 统一入口同样支持，--profile 仍选择 Web 后端。
powershell.exe -File tools/Soui.ps1 package --kind applications --architecture x64 --compression-profile 7z-balanced --bundle
```

| 档位 | 格式和算法 | 参数 | 请求字典 | 固实 |
|---|---|---|---:|---|
| zip（默认） | ZIP | `-tzip -mx=5 -mmt=2` | 未设置，沿用工具默认 | 未设置 |
| 7z-balanced | 7z / LZMA2 | `-t7z -m0=LZMA2 -mx=7 -md=32m -ms=on -mmt=2` | 32 MiB | 是 |
| 7z-small | 7z / LZMA2 | `-t7z -m0=LZMA2 -mx=9 -md=64m -ms=on -mmt=2` | 64 MiB | 是 |

档位定义集中在 `tools/internal/compression-profiles.cjs`，打包入口只接受列出的档位。
表中参数用于创建归档，统一请求两线程；解压实测使用报告所记命令及工具的
默认解码线程策略，不把它当成业务安装器的线程限制。
更大字典不保证更小；工具也可能依据实际输入缩小归档字典。请求字典、编码器
内存及解码器内存是不同指标，应以实际归档和测量结果判断。

默认输出保持 `<应用>-<版本>-windows-<架构>.zip` 和原 Bundle ZIP 名称。
非默认应用输出增加 `-7z-balanced.7z` 或 `-7z-small.7z` 后缀；Bundle 名称
同样标明档位。已有输出不会被覆盖。每个归档继续附带 `.sha256` 和 `.json`。
报告保留 `archive.compression`，追加明确档位、格式、请求字典、固实设置、
实际参数及归档工具版本/散列；同时绑定工具报告使用的编解码库，包含本机
7z.dll。ZIP 省略参数不等于零字典或没有工具默认值。

7z 接收方需要支持该格式的解压工具；新的档位尚未自动接入下游 NSIS 壳或
自包含卸载器。Bundle 解压后仍按[运行时去重包说明](runtime-packaging.md)恢复
独立应用目录。`--compression-profile` 和 `--bundle` 只适用于 applications，
不改变源码包与 SDK 安装包格式。

## 复现实测

`tools/Measure-Compression.ps1` 接受已经暂存好的目录。它不建立构建或启动
证明，也不绕过标准打包入口的证据要求。在 PowerShell 会话从仓库根目录执行：

```powershell
& ./tools/Measure-Compression.ps1 `
  -InputPath @('path/to/staged-x64', 'path/to/bundle-x64') `
  -OutputRoot 'path/to/new-measurement' -ExtractionRuns 3
```

测量在 Windows 构建机运行，使用 Node.js、7-Zip 和系统 64 位 .NET Framework 4
C# 编译器。监督器只编译一次，编译不计时；这些测量依赖不进入应用载荷。

工具复制并固定输入，串行测量三个档位；输出目录必须不存在。所有轮次使用
全新解压目录，执行完整性检测与逐文件散列复核。复制、散列、完整性检测及
Bundle 恢复不计入压缩/解压耗时。报告保留每轮的实际参数、退出状态、耗时、
内存峰值、归档身份和工具身份，失败不能计为成功。默认不清空系统缓存，
本机重复解压结果不能冒充冷缓存或整个安装流程性能。

耗时采用单调时钟，从恢复被测进程前开始，到其所属 Job 排空为止；不包含
监督器启动、挂起进程的创建与 Job 设置。结束检查存在毫秒级轮询误差，
应结合全部轮次和负载规模阅读，不能把微小差值视为确定的性能提升。

监督器在执行前将被测进程加入独立 Windows Job，退出后读取内核持续记账的
进程/Job 私有提交量峰值；先执行 32 MiB 实际分配并触碰的校准及超时退出校验。
报告的 PID/映像列表来自轮询观察，短命进程可能未被采到；完整进程数量以
Job 的 `totalProcesses` 为准。该列表用于解释样本，不是完整进程审计。
这里的内存代表压缩或解压工具及同属 Job 的辅助进程需要的私有提交量，不是
字典配置、磁盘分页量、物理驻留工作集或应用运行内存。统计语义见微软的
[Job 内存峰值说明](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information)
和[进程内存计数说明](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)。

2026-10-23 的 `preview.1` 真实 Starter、Workbench 压缩测量摘录见
[历史压缩测量](../verification/production-validation/auto-host-pr-2026-10-23/historical-compression.md)。
该记录绑定原冻结快照，不代表拆分后 `preview.3` 已重测，也不推算下游安装器收益。
设计与兼容性决定见 [ADR 0126](adr/0126-optional-compression-profiles.md)。
