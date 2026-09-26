# preview.11 Win7 SP1 x86：第二次客体在传输目录检查处失败

结论为 **`failed-before-apps`**。新增诊断在实际 Win7 SP1 x86 / PowerShell 2 客体中执行，记录传输盘根目录出现 `$RECYCLE.BIN` 子目录。原严格集合检查因此拒绝继续；三个应用入口均未启动，没有 smoke 通过。

- 候选：`0.3.0-preview.11`、x86、Release；构建 ID `7e000158-98f0-4349-854e-15c581663737`。
- SDK 输入 SHA-256：`a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`。
- v2 准备 freeze SHA-256：`20f5b68956e9483dfeff26349bd81d49bcd81e4f6aeb8bf0ee7d462588dc9d13`。
- v2 input SHA-256：`d8d509e2932074a5a677b9dab4e4c1d77c0f9d34108edf95c4a132e4a6d1f4e6`。
- 原串口为 5,978 字节，SHA-256：`3984889246a74c24d67dbd8775a7198af16852e3ba36da046dab2c9f736dfd94`。

## 实际诊断

[启动前准入记录](raw/attempt/gate.json) 当时未发现所检查的其它桌面/QEMU 进程；这是启动快照，不是性能隔离结论。确认 [Win7 桌面与 VVFAT 自动播放窗口](raw/attempt/boot-01.png) 后，发送一次冻结客体命令，见 [command-send.json](raw/attempt/command-send.json)。完整启动参数、monitor 原始日志、PPM/PNG 和 QEMU 输出均已保留。

[串口原文](raw/attempt/serial-export.txt) 有八个完整帧及 `SOUI_SERIAL_END`。独立解码检查严格 Base64、每帧声明长度与 SHA-256，八个文件按原字节保存，见 [独立审查](raw/attempt/serial-independent-review.json)。

| 证据 | 实际结果 |
| --- | --- |
| [guest-identity.txt](raw/attempt/decoded/guest-identity.txt) | Windows `6.1.7601`、SP1、32-bit、指针 4 字节、PowerShell 2 |
| [transfer-diagnostics.txt](raw/attempt/decoded/transfer-diagnostics.txt) | 枚举 11 项，其中目录 1 项，预期普通文件 10 项 |
| [transfer-entries.tsv](raw/attempt/decoded/transfer-entries.tsv) | `$RECYCLE.BIN` 为 `Hidden, System, Directory` / `System.IO.DirectoryInfo`，另有 10 个普通文件 |
| [transfer-manifest-raw.tsv](raw/attempt/decoded/transfer-manifest-raw.tsv) | 原始 809 字节与冻结清单逐字节相同，SHA-256 匹配 |
| [transfer-lines.tsv](raw/attempt/decoded/transfer-lines.tsv) | 实际解析 9 行；每行原文、长度及 3 列形状均匹配冻结清单 |
| [harness.log](raw/attempt/decoded/harness.log)、[harness-exception.txt](raw/attempt/decoded/harness-exception.txt) | `Transfer file set differs` |
| [run-exitcode.txt](raw/attempt/decoded/run-exitcode.txt) | `1` |

客体普通文件的名称和长度与输入清单一致，但**没有完成这些普通文件的客体散列验证**。诊断中的清单哈希只证明清单本身，清单内列出的其他文件哈希仍是预期值。

[v2 客体脚本](raw/preparation/harness/run-apps.ps1) 在第 104 行导出诊断，第 105 行执行与 [首次脚本](raw/reference/attempt1/run-apps.ps1) 相同的“不得有子目录、数量必须精确匹配”检查。此次在该处抛出异常，尚未进入第 107 行的文件散列循环、ZIP 检查、解压、载荷校验或第 125–127 行的三个应用调用。八帧导出完整不能替代这些未执行步骤。

直接触发条件已明确为传输盘根目录多出 `$RECYCLE.BIN` 子目录。证据没有说明该目录由谁创建、何时创建。首次 attempt 未采集目录枚举，本次新证据也不倒填为首次当时已知的根因。

## 终态与校验

在 [qemu-result.json](raw/attempt/qemu-result.json) 已写成后才进行此次重采样：QEMU 经 `system_powerdown` 于 `2026-09-26T07:59:51.8562765Z` 完成，实际退出 `0`、`timedOut=false`，基础磁盘前后散列相同，冻结输入复核退出 `0`。这是客体生命周期与输入保留证据，不能抵消客体脚本退出 `1`。

使用原 v2 冻结 [Verify-AppSerial.cjs](raw/preparation/Verify-AppSerial.cjs) 对停止写入的原串口执行一次只读校验；[独立退出记录](raw/attempt/verify-recapture.result.json) 为数值 `1`、无超时，原字节 stdout 为空，stderr 为 `Required raw file missing: transfer-observed.tsv`。校验器、input、串口的前后身份均记录在内，旧样本未覆盖。

v3 的传输子目录布局属于另一份准备与后续 attempt，本记录不宣称它已运行或已解决问题。原严格检查未放宽；本次三个应用均未启动或通过。

## 归档范围

[raw-manifest.json](raw-manifest.json) 收录 46 份文件，共 2,489,637 字节：原 attempt 的 18 份文件、新校验与独立审查、八个解码文件合计 31 份；另有 15 份 v2 冻结输入、诊断脚本及必要的首次样本对照。每份文件记录来源、长度和 SHA-256，复制后同时复核原件与归档字节。首次 attempt 的文件与归档均未修改。

本目录不包含应用 ZIP、EXE/DLL 或系统磁盘；它是失败证据归档，不是可独立运行的交付包。`functionalSmokePassed`、`realRenewalVerified`、`releaseReady`、`productionReady` 均为 `false`。
