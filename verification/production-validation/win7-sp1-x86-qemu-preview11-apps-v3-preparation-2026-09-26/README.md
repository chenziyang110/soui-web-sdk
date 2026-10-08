# preview.11 Win7 x86：v3 子目录输入已冻结，第三轮未启动

状态为 **prepared-launch-gate-blocked-before-attempt**。本目录归档 v3 的文件准备及门禁证据；第三轮客体、三个应用均未启动，没有验证布局修正后的实际结果，也没有新增 Win7 应用 smoke 通过。

## 原因与最小布局变更

[第二轮原始串口](raw/preparation/parent-attempt2/serial-export.txt)及其八份原文件来自真实 v2 失败：清单 809 字节、9 行记录匹配，实际根目录枚举 11 项，额外 `D:\$RECYCLE.BIN` 为隐藏系统目录。原“无子目录、恰好 10 文件”的检查正确拒绝，退出 1，尚未进入 ZIP、普通文件客体散列或应用执行。完整第二轮记录见[原失败归档](../win7-sp1-x86-qemu-preview11-apps-attempt2-2026-09-26/README.md)。证据没有说明是谁创建该目录，也不倒填首次失败的未知枚举。

v3 把十个冻结文件放到卷内 `soui-input` 子目录；卷根系统元数据在输入目录之外。所有客体脚本、TSV、两个 ZIP 与 v2 原字节一致；546 个完整应用文件（含各 5 个 legal）也未修改。没有忽略 `$RECYCLE.BIN` 或放宽集合/散列规则，任何额外项若出现在输入目录内仍拒绝。

[input.json](raw/preparation/input.json)明确 `layoutVersion=3`、`diagnosticsVersion=2`、宿主 `mountRoot` 和 `inputSubdir=soui-input`；[校验器](raw/preparation/Verify-AppSerial.cjs)要求实际客体 Transfer 为对应子目录，保留全部退出码、载荷身份、ready 与关闭顺序门槛。[父 freeze](raw/preparation/parent-freeze.json)及[父输入](raw/preparation/input.parent.json)绑定 v2 来源。

## 身份与已完成检查

- 候选：`0.3.0-preview.11`、x86 Release；build ID `7e000158-98f0-4349-854e-15c581663737`。
- compiled source SHA-256：`a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`；不声称本轮重新编译。
- v3 freeze SHA-256：`10c9a4e90cf5e25a2c98d0f6107eb7881a5ab39caf768973d761a1045566826d`。
- v3 input SHA-256：`599889c30862e6b6730ba0d23a1196e0ad1b73b3ce3e1d1418928faa235b89dc`。
- [synthetic 自检](raw/preparation/self-test-only-v4-results.json)：1 个完整样本接受、27 个负例拒绝，包含输入目录内额外普通文件、额外回收站目录、错误卷根/子目录，以及 hash 正确但非零退出或缺失/乱序关闭；它不是客体执行证据。
- [宿主诊断自检](raw/preparation/host-diagnostic-self-test.log)：仅调用诊断函数，并以原 guard 验证多一个 FileInfo/DirectoryInfo 均拒绝；未执行客体脚本主体、ZIP 工具或应用。[语法记录](raw/preparation/syntax-check.json)也不代表 Win7 运行。

## 第三轮在创建 attempt 前被门禁拒绝

[门禁原始 JSON](raw/launch/preview11-win7-v3-launch-gate-20260926.json)记录检查时间 `2026-09-26T08:08:54.8133600Z`、`attemptExists=false`，当时存在其他任务 QEMU PID 72440（`vds-format-v1-20260926`）。[启动脚本](raw/launch/preview11-qemu-launch-v3.ps1)在创建 attempt 和启动 QEMU 前检查这些负载，故本次没有第三轮 QEMU PID、桌面截图、客体命令或串口结果；未停止其他任务。

冻结输入原目录仍为 `C:\SOUI-SDK-production-validation\preview11-win7-preparation-v3-20260926`。待宿主全部桌面/QEMU 负载空闲，重新做门禁和冻结复核后，新 attempt 应使用下面精确挂载路径；不能误挂其内的 `soui-input`：

```text
-drive file=fat:ro:C:\SOUI-SDK-production-validation\preview11-win7-preparation-v3-20260926\volume,format=raw,if=ide,index=1
```

确认 fresh snapshot 的 Win7 桌面和盘符后，仅发送一次（D: 为实际传输盘时）：

```cmd
cmd.exe /c D:\soui-input\run-preview11.cmd
```

[monitor 脚本](raw/launch/preview11-qemu-monitor-v3.cjs)只允许该冻结命令。端口、PID 和串口路径必须在实际启动时重新确定；这里保存的脚本是尚未越过门禁的准备，不能当作启动成功。完整准备说明见[原字节 README](raw/preparation/README.md)。

## 归档范围

[raw-manifest.json](raw-manifest.json)列出 40 份原字节文件，共 900,124 字节，记录各来源、长度与 SHA-256；复制后复核源文件和副本一致。保存只读验证脚本、输入/freeze、构建记录、必要 v2 来源、合成结果摘要及 launcher/monitor/门禁；不保存应用 ZIP/二进制、546 个 payload 文件、完整 synthetic 串口大样本或系统磁盘，因此此小归档不是可独立运行的传输卷。

`guestExecuted=false`、`attemptCreated=false`、`functionalSmokePassed=false`、`realRenewalVerified=false`、`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`、`productionReady=false`。后续第三轮必须独立记录，不改写前两次失败，也不把本次准备当作修复通过。
