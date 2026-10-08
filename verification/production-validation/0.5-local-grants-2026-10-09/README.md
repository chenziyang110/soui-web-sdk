# 0.5 本地连接授权第一层：本机双架构验证归档

本目录保留 2026-10-09 当前 Windows 11 主机上执行的 Release x86/x64 SDK 静态库及六组原生测试证据。它验证权限策略第一层，不表示发布就绪，也不是 pipe、UIA、GUI 或操作系统身份验证。

原始入口为 `out/local-grant-validation-adc8da1d-ab99-4ee1-b6a3-32c9c092783f`。原始 683 项索引、冻结输入、结果、67 份实际编译输入快照和六次 configure/build/CTest Job 的 context、inputs、原始 stdout/stderr、native NDJSON 与 finished 记录均按原字节保留或映射。原索引 SHA-256：`caa6b5d907dadfacc5ee80d47ce5f379a9bcf0e050a523f7ff38ddb628e618f0`；规范化源码 SHA-256：`e581df02ecca925f9444def0ecc51c86b94ba89848aa4fd67f075daa1a237564`；67 项实际输入 SHA-256：`335f7465d3034e30eb074aecf89cb09e744f42ae4b54005365f71b3d39a824f4`。

两架构的 configure、编译和 CTest 共六个 Job 均 root exit 0、supervisor exit 0、generic passed=true、自然 completed、activeProcesses=0、closureConfirmed=true。每架构实际 CTest 6/6：contract、action-contract、session-renewal、task-scope、task-operation-query、local-authorization。本地授权组实际 10/10 策略 case，包括 page/local 互拒、实际 binding/store 归属、方法与 capability 交集、续租别名、精确撤销、entropy 钩子内 close 的发行与续租竞态、失效后的真实 cleanup owner、state/binding 寿命，以及全部 worker 经后置 FIFO sentinel 排空后的 queued admission 拒绝。时间和 entropy 测试钩子仅用于确定性测试，不声称完成长期并发压力验证。

SDK Web 现有依赖额外执行一次实际 `npm --prefix sdk/web audit --json`（通过 Node 的真实 npm CLI，未安装或更新依赖）。审计 vulnerabilities.total=0，root/supervisor exit 0，受监督 Job 自然归零。`raw/audit` 保存该次工具/包锁输入、原始输出和独立 Job 记录；未对任何编译或测量采用尾进程政策例外。

`byte-exact-map.json` 逐项关联原路径、大小、SHA-256 和归档路径；`index.json` 索引本目录全部文件（不含自身）。`raw/original/index.json` 是完整原索引，不能将其未复制的路径误认为本目录内的文件。所有归档代码及生成构建文件都以 `.inert.snapshot` 结尾，未归一化换行或文本内容。CMake cache、actual vcxproj、compiler CMake、tlogs 和 LastTest 日志保留了实际 SDK 10.0.19041.0、v143/14.44.35207 与编译输入路径。

编译 obj/lib/pdb/EXE 等及 ABI 二进制仅保留原路径、大小和散列（见 `private-artifacts.json` 和原索引），没有将产物复制为源码证据。两份 MSBuild binlog 原件继续留在私有 out，正文不公开；本目录保留其 SHA-256/大小/原路径及实际 stdout、tlog、cache/project。Root 已报告私有 binlog gzip 解压扫描 0 匹配；归档者未重复扫描，此信息不是公开可独立复算的 binlog 正文证据。

本轮未验证 DLL 交付、真实窗口、pipe/gateway、UIA、OS 身份、Win7/PE 或其他系统矩阵、8 小时运行、完整 Collect、正式发布门槛。`releaseReady` 保持 false。关闭 binding 是后续 admission 边界而不是在途 handler/IO 屏障；本轮尚无实际本地端点。散列及路径绑定用于发现意外不一致，不声称抵御同一权限用户恶意重写整套来源文件。
