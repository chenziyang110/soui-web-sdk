# 0.5 本地类型化任务族：2026-10-09 回归证据归档

这份归档保存已经发生的 Release 回归及其失败轨迹。最终原始结果来自 `out/f5p-e1d8d4dd/actual-89274fa8-933b-4728-99ba-6f82a411c3cb/results.json`，SHA-256 为 `3e9470cd858ce29622cbf8f60f1816cc3f2a6ec6533c6455f8400f573f47df2e`。归档人员只读取文件、复核证据并创建独立副本，没有重新编译、运行原生测试、操作 GUI、控制进程、运行审计或修改生产源码。

## 已观察到的有限结果

- x64 复用 f3 原始尝试已编译的 SDK 静态库和 11 个测试 EXE；x86 在 f5 实际尝试中重新编译相同的 12 个项目。归档人员核对了输出哈希、PE/COFF 架构、实际 CL/Link/Lib 命令及原始 owned process 报告。
- 两架构各执行 11 项选定 CTest，并各直接执行 normal 13 项、private 14 项任务族案例。测试与列表、24 项只读 MSBuild 属性查询都自然结束：root/supervisor exit 0、generic passed true、无需终止 owned job。37 条原始记录中包含复用的 x64 配置/编译两条；其余 35 条由 f5 实际尝试产生。
- 24 项实际属性查询绑定到两架构的 12 个实际项目，记录实际默认 MSVC `14.44.35207`、Hostx64、v143 与 Windows SDK `10.0.19041.0`；实际编译器报告版本 `19.44.35226.0`。实际默认属性及 `Microsoft.Cpp.Default.props`、`Microsoft.Cpp.props` 的记录与哈希已复核。这不是所有外部 MSBuild import 或加载内存的完整认证。
- SDK 及普通测试的实际命令不含 `SOUI_TASK_FAMILY_TEST_ONLY`。只有专用 private target 启用该宏，三个受控准备源码 `task_service.cpp`、`task_store.cpp`、`bridge_transport.cpp` 的实际命令分别核对。private 准备失败是可控注入，不能证明真实 OS OOM。
- CTest 定义副本与对应已编译目录中的原件字节一致；列表的 11 个命令分别指向对应 EXE。直接输出的案例名称、顺序与 13/14 数量均已复核。

## 源码绑定与保留的失败

冻结审查输入索引 SHA-256 为 `493d43655e241556b02804a7d345491e1632fb26184f49f7830c49369f09ec6f`，16 文件指纹为 `cb93c9e390aa53bc5c46196828b66f067e30824c5b5ca1387e294255c0f0fb3f`，基线为 `f6720b4936e488ae3edd9deb02ebf2613b5eba6c`。本次复核确认当前 16 文件、892 个全量原始输入、77 个实际原生编译输入及其原始快照未变化；归档后再次核对原件。完整 source identity 保留它原有的 LF 规范化规则，原始输入和归档副本始终字节精确。此前独立静态审查由 Root 和 merge_graph 完成，本归档人员没有重复该生产源码审查，也不扩大其批准范围。

原始未执行准备以及 r2 未执行准备都保留。以下已执行失败保持原状，不能被最终局部回归覆盖：

1. r2-final 的长路径编译发生 FTK1011，实际 compile root exit 1，没有执行测试。
2. f3 的 x64 配置与编译 root exit 0，但读取证据助手因未预先绑定 `SORTDEFAULT.NLS` 失败，没有执行测试。原始编译结果及完整读路径分类保留；后续磁盘观测不追认历史读取字节。
3. f4 完成 12 项 x64 属性查询与 CTest 列表后发生 `EEXIST` 目录碰撞，没有执行测试。
4. f5 仅将一个目录名 literal 从 `ctest-` 改为 `ctest-definitions-`。58 个准备 payload 和 Root 的前置检查助手均保留，修改前后驱动及精确差异都可核对。

归档助手自身第一次纯文件复核因旧记录没有 `resolvedPath` 字段而失败；该助手快照和失败说明也保留。修正只按记录实际提供的字段验证，没有重写原始证据，公开目录在全部纯复核通过后才创建。

## 未关闭门槛

四条 x64/x86 configure/compile 的 root exit 均为 0，generic passed 均为 false、supervisor exit 均为 124；原报告显示仍有 owned descendant，并完成 owned cleanup。`compilerTailAcceptance` 始终为 false。测试自然结束不改变这些原始监督结果。

`allPositiveGenericSupervisionPassed`、`validationPassed`、`qualificationPassed`、`sourceReadFullyPrelaunchBound`、`releaseReady` 均保持 false。先前未预先绑定的实际读取路径与 configure probe/selected project 分类没有被追认成输入闭合。没有全分发验收、GUI、DLL/pipe、OS 身份、Win7/PE 矩阵、性能、长期运行、完整 Collect 或新 vendor kernel 验证；当前交付仍是 preview。

## 文件形式与校验

`index.json` 对每个公开 payload 记录相对路径、字节数和 SHA-256，索引自身的哈希由归档完成报告提供。`byte-exact-map.json` 将每个原始文件映射到独立的 `raw/*.inert.snapshot`，或明确标注仅保留私有哈希。原件不改写、不移动，公开副本没有文本或换行规范化，没有链接或共享原件。

编译 EXE/DLL、COFF/静态库、PDB 等编译二进制、构建 `.bin`、MSBuild `.binlog` 与生成的 CompilerId 源文件不公开正文，见 `private-artifacts.json`。CL/MSBuild/Node/Windows PE 工具只公开路径、大小和哈希元数据。原始 stdout/stderr 和报告按字节归档，惯性快照不是可直接运行的验证入口。

`archive-summary.json` 保存复核的原始 PID 与十进制 64 位 creation FILETIME、argv/cwd/报告上下文、源码绑定、工具链属性及案例证据。路径和前后磁盘哈希描述观察时磁盘对象，不能认证历史载入内存，也不能推断未执行的系统行为。
