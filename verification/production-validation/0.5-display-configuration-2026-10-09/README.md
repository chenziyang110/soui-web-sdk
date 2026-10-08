# 0.5 显示配置与标准测量接入：历史原件归档

本目录逐字保存两套冻结证据：原生显示切片566项（原索引 SHA256 e93ad06ff204961290a1f3d1d05765fa728faf78bc6a58b3405e3007305f9033）及标准接入1892项（a02b4d01c786b8cb9a1a3c2b5dfe493851ca70f8e1f6b3b0a50eb00f4f0f76a9）。版本历史为0.5.0-preview.1；原生切片基于4fc8c199，标准接入基于41a8435e。它们是不同构建时间的来源与工具身份，不合并成新的当前源码或正式验收。原始索引也完整保留；其中路径属于原私有来源目录，不是本归档文件路径。未复制项须查 byte-exact-map.json 和 private-artifacts.json。

原生来源保留48次 Job（33 generic true、15 false，48 closure confirmed）、45/45纯 reader、两架构各9个TEST_ONLY系统目录/API fixture，以及所有早期错误、失败收集和修订记录。初始 Buffer 快照曾编码为JSON、初始 summary误读 member 字段、早期纯测试缺少预先固定的client字节副本，以及两次历史x86 collector拒绝均如实保留；修正原件单独存在，不追认旧失败。TEST_ONLY返回值并非真实目标系统/驱动测试。

标准接入绑定42输入、17项实际编译fingerprint（bdf842a10f52dcb3854bcd68d8ee744d368affae8fa9fac3fc4c49272c0bc562）、9个工具与18个真实PE身份，实际工程/Cache/Compiler CMake/tlog对应SDK19041、v143和C++17。标准21外层、10内层Job、额外只读验证与4个PowerShell入口投影原件均保留，含原始RED、早期测试夹具失败、修后105/105。两架构各protocol 147项/146通过/1既有serializer跳过、native16/16、tail-protocol157/157、tail-native21/21；实际负向Tail夹具Job的失败不被改成通用通过。x64 preparation自然完成；x86编译本体root0，但vctip尾进程清理后generic=false、supervisor124、closure=true。没有应用或追认compiler-tail例外。默认标准入口仅做纯reader回归，真实配置读取需显式选项；仅验证实际AST/Collect块投影，没有执行FullTest/FullCollect。

双架构当前显示记录按本轮native PID/精确FILETIME、context、原始报告/输出及EXE散列绑定。OS报告当前HDMI配置，刷新分数14850000/247500=60Hz。60Hz配置读取不等于实际present/scanout、物理面板认证、高刷新率、输入延迟、帧时间或性能通过。所有physicalPresentation、opticalCalibration、performanceGate、releaseReady及新OS矩阵结论保持false；没有GUI、ETW、SendInput、VM、Win7/PE或长期运行验证。将来也不能把组件缺失或非WDDM的unavailable诊断标为性能通过。

真实编译EXE/OBJ/LIB/PDB、外部Node/CL/MSBuild/User32等PE以及binlog正文没有发布，仅保留原路径、大小、SHA256和已有架构元数据。按字节magic检测已改名的PE，包含标准切片两个pinned-helper.exe.renamed，仍为hash-only；不能以inert名字将真实工具当源码发布。所有公开导入文件（含脚本、CMake、vcxproj、tlog、原始日志）均以.inert.snapshot结尾，字节和换行未归一化。两个TEST_ONLY junction仅有元数据，未跟随。

额外执行真实Node/npm CLI的npm --prefix sdk/web audit --json：现有依赖、无安装/更新、隔离空user/global配置和私有cache、child环境移除凭据相关名字而不输出值，vulnerabilities.total=0、root/supervisor0、generictrue、自然闭合。raw/audit保存实际命令/工具/包锁身份与原始Job输出。

index.json逐项索引归档文件（不含自身）；byte-exact-map.json关联冻结原件与新文件散列。SHA用于发现本地意外不一致，不认证能够重写整套证据的同权限攻击者。Root后续负责Git中的-text属性、忽略文件显式纳入、逐项HEAD blob核验、文档与清单；本归档本身不宣称完成这些Git步骤。
