# preview.11 Win7 SP1 x86：首次客体应用验收在启动前失败

结论为 **`failed-before-apps`**。Win7 客体与桌面已启动，但客体脚本在传输目录文件集合检查处失败，三个应用入口均未执行，没有任何 smoke 通过。本记录不能作为 Win7 应用兼容性、真实续期或生产验收通过证据。

- 候选：`0.3.0-preview.11`、x86、Release；构建 ID `7e000158-98f0-4349-854e-15c581663737`。
- SDK 输入 SHA-256：`a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`。
- 原准备冻结文件 SHA-256：`96d05251d6dd78848306fa3868c671f511c2b9ebc658951bf2f29d8e6a81b5ba`。本轮未修改原冻结输入。
- 客体报告：Windows `6.1.7601`、SP1、32-bit、指针 4 字节、PowerShell 2。
- 客体脚本退出码为 `1`；Starter、Workbench 默认页、Workbench React 页通过数均为 `0`。

## 原始执行与失败边界

[启动前检查](raw/attempt/gate.json) 当时没有其它被检查的桌面应用或 QEMU 进程，宿主长测已有终态。该检查仅是当时的启动准入快照，不证明性能隔离。QEMU 采用临时快照、无网络和只读 VVFAT 传输盘；完整参数保存在 [launch.json](raw/attempt/launch.json)。

最初的 HMP helper 使用相对输出路径，请求截图失败。调度工具当时观察到 `Error: Could not create...`，原 helper 的工具会话随后退出 `1` 并输出 `Monitor timeout`；没有独立保存这次 helper 的原始 stdout/stderr/退出结果，因此这里不补造该类记录。[desktop-ready.json](raw/attempt/desktop-ready.json) 保留了当时的失败说明，原 helper 源码也已归档。

后续 v2 helper 改用绝对路径，取得 [桌面截图](raw/attempt/desktop-01.png)，可见 Win7 桌面与 `QEMU VVFAT (D:)` 自动播放窗口。确认桌面就绪后，仅发送一次 `cmd.exe /c D:\run-preview11.cmd`，见 [命令记录](raw/attempt/command-send.json) 和对应 monitor 原始日志。两次成功截图的 PPM 与 PNG 均按原字节保存；[命令后的截图](raw/attempt/run-01.png) 不作为应用成功证据。

[串口原文](raw/attempt/serial-export.txt) 只有四帧，且含完整 `SOUI_SERIAL_V1` / `SOUI_SERIAL_END`。独立解码逐帧检查了严格 Base64、声明长度和 SHA-256，输出保留原字节：

| 解码文件 | 字节数 | 实际内容 |
| --- | ---: | --- |
| [guest-identity.txt](raw/attempt/decoded/guest-identity.txt) | 143 | Win7 SP1 x86、PowerShell 2 身份 |
| [harness.log](raw/attempt/decoded/harness.log) | 35 | `failed: Transfer file set differs` |
| [harness-exception.txt](raw/attempt/decoded/harness-exception.txt) | 27 | `Transfer file set differs` |
| [run-exitcode.txt](raw/attempt/decoded/run-exitcode.txt) | 3 | `1` |

原冻结 [run-apps.ps1](raw/preparation/harness/run-apps.ps1) 在第 76 行检查文件数量与子目录，失败发生在写出 `transfer-observed.tsv`、解压、载荷校验及第 96–98 行三个 `Run-App` 调用之前。四帧完整只证明失败报告导出完整；缺少传输观察、应用运行与 smoke 日志，不能提升为应用通过。

## 校验重采样与关机

首次串口校验留下的 [verify.stdout.log](raw/attempt/verify.stdout.log) 与 [verify.stderr.log](raw/attempt/verify.stderr.log) 保留不动。当时工具报告校验退出 `1`，但串口文件仍打开，后续散列读取遇到文件共享错误，未写成独立 result JSON。该原始退出结果缺口没有追补为旧时刻证据。

QEMU 停止后，使用原冻结 [Verify-AppSerial.cjs](raw/preparation/Verify-AppSerial.cjs) 对同一串口文件重新执行一次只读校验。新 [verify-recapture.result.json](raw/attempt/verify-recapture.result.json) 记录本次实际数值退出码 `1`、无超时、参数、时刻以及校验器、输入和串口的前后身份；原字节 stdout 为 0 字节，stderr 为 `Required raw file missing: transfer-observed.tsv`。独立四帧审查见 [serial-independent-review.json](raw/attempt/serial-independent-review.json)。

发送 `system_powerdown` 后，[QEMU 原始结果](raw/attempt/qemu-result.json) 为退出 `0`、`timedOut=false`，基础磁盘前后 SHA-256 相同，冻结输入复核退出 `0`。这些仅证明客体进程结束及原输入保留，不能抵消客体脚本退出 `1`。

宿主原传输目录的 10 个文件在关机后再次检查，文件集合、长度和散列均与冻结输入一致。但客体没有导出它实际看到的文件名、数量和属性，**当前证据不足以确定根因**。后续诊断必须另建 attempt 与输入记录，不修改或覆盖本次失败样本。

## 归档范围

[raw-manifest.json](raw-manifest.json) 收录 42 份文件，共 4,162,037 字节：整个原 attempt 的 24 份文件、新校验与独立审查记录及四个解码文件，共 33 份；另附 9 份原冻结校验器、清单与脚本引用。每份文件记录原始来源、长度和 SHA-256，复制后复核源文件与归档文件逐字节相同。原始文件在重采样前后也已确认未变。

本归档不包含应用 ZIP、EXE/DLL 或系统磁盘；其身份引用原冻结输入。本次 `functionalSmokePassed`、`realRenewalVerified`、`releaseReady`、`productionReady` 均为 `false`。
