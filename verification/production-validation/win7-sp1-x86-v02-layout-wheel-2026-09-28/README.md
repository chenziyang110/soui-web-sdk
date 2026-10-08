# 当前 V02 Win7 SP1 x86 滚轮输入探索

本目录保留 148 份原始记录，绑定当前 718 文件 Demo 源码 SHA-256 15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d、正式 x86 Release 构建 f0c38056-058a-456e-be99-17446127f7ec、13 文件只读输入 SHA-256 c0c5b6c54198c25397756987f39bd396d934436e33e159054c93abcb60477a20。未改的探针和载荷清单可从先前的 verification/production-validation/win7-sp1-x86-v02-display-v2-interrupted-2026-09-28/raw/frozen-input 按字节复核；两份应用 ZIP 原件仍在外部冻结输入，仓库仅保留其大小、散列及客体传输/载荷核对结果，不能仅用仓库重建完整冻结输入。本轮单独保存改动过的客体脚本、串口导出和传输清单。
Win7 6.1.7601 SP1 x86 快照桌面临时为 1024×768，普通可见 Workbench 以 `--react --page=data --width=950 --height=550` 启动，实际窗口完整处于桌面内。原生探针绑定 PID 2116、窗口 262532 和进程创建身份，在自然前台连续发送八次真实系统滚轮，八份探针退出与结果均由严格校验器核对。
客体应用原生退出 0，正常关闭；50 帧完整串口、完整载荷与法律声明前后、冻结输入和基础盘均通过。宿主 56 次采样未见外部桌面负载，驱动与我方 QEMU 原始退出 0；29 张时间线截图的 PPM/PNG 原件与 SHA-256 保留。
截图中可见中文输入、动效及公共任务区域；但第一张已渲染截图晚于滚轮序列，缺少滚动前基线，不能严谨证明视觉滚动效果。本轮未输入中文、未启动公共任务或缩放窗口；不能当作 V02 真实并发通过。`win7CurrentV02Verified=false`、`releaseReady=false`。

首次归档校验因为先前仓库归档未保存 `SouiStarter.zip`/`SouiWorkbench.zip` 字节而原始退出 1，失败 stderr 保留在 `raw/command-logs/archive-layout-explore-v1-attempt1/`。修正后从外部冻结原件实际复核两个 ZIP 的大小与散列；仓库可移植复核只验证这两个 ZIP 的已提交元数据，不声称能从仓库重建它们。

归档成功与原件/工作区/暂存区复核的原始退出码保存在 `operations/`。第一次刷新完整源码清单期间出现外部 QEMU，门禁只中断我方清单进程；该次退出 1 及负载采样也保存在 `operations/`。随后空闲时第二次清单生成原始退出 0，完整源码清单独立校验原始退出 0、覆盖 36594 文件；第二次命令日志保存在外部 `preview15-v02-interaction-20260927/command-logs/layout-wheel-source-manifest-attempt2`，未计入本目录 148 份原始记录。

仓库副本可运行 `node verification/production-validation/win7-sp1-x86-v02-layout-wheel-2026-09-28/Verify-Operations-Portable.cjs` 核对上述三次操作日志的原始字节、退出码和外部负载采样；这项核对不改变失败的清单结论。
