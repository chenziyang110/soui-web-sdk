# preview.11 同文档续期夹具：独立构建证据

本目录只归档外部夹具的编译、严格 TypeScript 检查、无 GUI 自检与分析器合成测试。未执行本夹具 GUI、35 分钟或 8 小时真实续期；不代表 Workbench V02、资源、性能或生产验收通过。

- 冻结 SDK 输入：a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550（711 项规范化源码输入；本轮未修改 SDK）。
- 最终 freeze.json SHA-256：7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d。
- 成功：x86-attempt4、x64-attempt1；每个 attempt 的 13 项原始步骤记录均 exitCode=0、timedOut=false，包含严格 TS 检查和原生 --self-check。
- 未保存两次最外层 Build.ps1 退出的独立原始记录；外层工具当时报告 0。这里仅据已保存的 13 项步骤记录作结论，不补造外层 raw 退出证据。
- 分析器合成自检：88/88；syntheticOnly=true、actualGuiExecuted=false、actualClockRenewalVerified=false。模拟时钟与模拟 NDJSON 不能证明真实时长。

## 保留的失败及实际修正

| 原始 attempt | 原始失败 | 后续实际修正 |
| --- | --- | --- |
| x86-attempt1 | 严格 TypeScript 检查中 task kind 仍为 unknown | 明确收窄 kind 后再赋给字符串观察字段 |
| x86-attempt2 | legacy 产物清单的路径键不匹配 | 使用实际清单的精确相对键，保留原失败输出 |
| x86-attempt3 | C++ JSON 字符串辅助函数参与 ADL 冲突，COM 声明缺失 | 改用 json_quoted 避免冲突，并显式包含 objbase.h |

失败 attempts 的源码、驱动、参数、步骤退出码及 stdout/stderr 原字节均保留；成功重试没有覆盖失败。

## 未冻结的宿主链接输入

两架构真实 link.command.1.tlog 显示宿主 vcpkg 集成引入 F:\GitHub\vcpkg\installed\<arch>-windows\lib 的 LIBPATH 和 *.lib 引用。这些输入没有进入本次冻结集合。因此本证据不能称为封闭、可复现的构建，也不能称为干净机器验证通过。已归档链接/编译命令 tlog、CMakeCache、相关项目文件与可找到的响应文件，原始编码不变。

## 审计与复核

npm audit --json 使用两次成功 attempt 各自已锁定的 toolchain 目录以及记录中的 Node/npm 入口；保存原字节 stdout/stderr、实际退出码、参数、时刻及运行前后工具链散列。未执行 audit fix。当前结果：
- x86-attempt4：exitCode=0，vulnerabilities={"info":0,"low":0,"moderate":0,"high":0,"critical":0,"total":0}，inputsUnchanged=true。
- x64-attempt1：exitCode=0，vulnerabilities={"info":0,"low":0,"moderate":0,"high":0,"critical":0,"total":0}，inputsUnchanged=true。

独立复核原文见 raw/independent-build-review.json；raw-manifest.json 给出每份原始文件的路径、长度和 SHA-256。归档器拒绝覆盖，复制后同时复核源文件与目标文件字节。

本归档不收录 EXE/DLL/LIB、node_modules、完整 SDK 输入或运行载荷副本。其身份保留在冻结清单和构建记录中；这些省略的载荷仍在外部原始目录，不能仅靠本归档重现运行。

releaseReady、realRenewalVerified、guiExecuted、resourceGate、benchmarkValid、closedReproducibleBuildVerified 和 cleanMachineVerified 均为 false。
