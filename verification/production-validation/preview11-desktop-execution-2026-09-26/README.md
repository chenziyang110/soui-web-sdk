# preview.11 本机桌面短测与失败记录

2026-09-26，Windows 11 10.0.26200 x64 宿主；版本 `0.3.0-preview.11`。沿用[预检](../preview11-session-renewal-preflight-2026-09-26/README.md)的 711 份编译输入，SHA-256 `a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`；本轮没有修改或重新编译 SDK。x86/x64 应用 buildId 分别为 `7e000158-98f0-4349-854e-15c581663737`、`6c61f9e3-065a-40c5-b281-ace7ca80df02`。

## 实际结果

| 场景 | 结果与边界 |
|---|---|
| x86/x64 窗口线程停滞、目录取消 | 四次原始退出码均为 0，无监督超时；窗口超时后的恢复、公共目录对话框程序化取消成功，不代表真实用户输入 |
| Starter、Workbench 默认与 React 入口 | 两架构共六入口启动/就绪/关闭成功；`Test.ps1` 记录数值退出码并核对完整载荷，仍是短启动测试 |
| 仓库外生成项目 RenewalStarter | x86/x64 EXE 的 `--smoke-test` 均退出 0，含 `application-ready` → `host-close-complete`；使用原独立构建、锁定 418 文件 SDK |
| x86 公共托盘/菜单/快捷键消费者 | **失败，退出 1**：托盘安装、快捷键注册/冲突检查成功，自动请求菜单时返回 `E_MENU_FOREGROUND`，尚未显示菜单；清理完整。不能据此宣称菜单、实际快捷键或真实托盘交互通过 |
| x86 Workbench 三窗口驻留 | **失败，退出 3**：slot 1 就绪，slot 2 触发内部 45 秒页面就绪期限（`document=waiting,bridge=waiting`）；尚未进入 smoke 阶段。slot 3 状态未判定。Host/服务/Runtime 有序清理，无剩余测试进程 |

[独立只读复核](raw/desktop-independent-review.json)列出结果、构建/架构/散列、95 份相关输入及证据限制；[归档清单](raw-manifest.json)绑定本目录原始字节。`raw/` 的绝对路径只记录本次执行来源，不是可移植构建配置。

## 保留的失败及下一步诊断

- [前次退出码缺失样本](../preview11-session-renewal-preflight-2026-09-26/raw/desktop-attempt1/x86-window-thread-timeout.result.json)仍不计通过。本轮 `.NET Process` 持有句柄并取得数值退出码，没有覆盖该样本。
- `desktop-attempt2` 在 x86 菜单失败后停止；`desktop-applications-attempt1` 在 x86 驻留失败后停止。未执行的 x64 菜单/驻留不能标为通过。剩余独立启动场景另开目录继续，未把失败改成成功。
- `desktop-smoke-attempt1` 的生成项目入口错误地指向不存在的 `tools/Test.ps1`，外层 PowerShell 退出 `-196608`，应用未启动。随后 `desktop-direct-smoke-attempt1` 调用实际 EXE 并保留两个新样本。
- 归档前对未改动的 Web SDK 锁文件重新执行审计：首次因 npm 服务 TLS 连接中断失败；保留原日志后重试退出 0、漏洞总数 0。首次网络失败没有被记成审计通过。
- 菜单失败只确认前台激活未被接受。自动后台定时器、隐藏 owner 与前台权限的关系尚未确证；`nativeError=2` 不是这个 API 的可靠根因说明。下一步记录 owner/foreground/输入桌面身份，并验证真实托盘或快捷键触发；不得用强抢前台或跳过错误代替验收。
- 驻留下一步保持同一 EXE/参数/门限，使用新日志路径，启用 `SOUI_DIAGNOSTIC_SCRIPT=1`、`SOUI_BRIDGE_TRACE=1` 保存每个 adapter 的脚本与通信诊断；不延长门限掩盖问题。此路径前版未获真实通过，不能声称已证明 preview.11 回归。

## 身份、占用与未覆盖范围

两个正式应用的完整载荷各为 Starter 71 文件、Workbench 475 文件，与原构建和 smoke 身份一致。生成项目每架构 71 文件在执行前和独立复核时与构建/锁匹配；直接监督器只对 EXE 做运行前后采样，复核时全载荷校验**不等于每轮退出时采样或文件锁定**。四项原生桌面 EXE 由完整编译日志、启动散列、当前散列及 PE 架构关联，不能冒称已在原九个无桌面测试的二进制快照中。

宿主空闲时串行执行上述负载。准备启动 Win7 时，另一台 `ifs-media-integration-20260926` QEMU 已启动，门禁在创建本次客体前拒绝启动；[占用记录](raw/desktop-gate-after-smokes-20260926.json)保留 PID 和启动参数。没有启动平行虚拟机，也没有中断其他任务。Win7 冻结输入继续见[原准备](../win7-sp1-x86-qemu-preview11-apps-2026-09-26/README.md)，仍未执行。

真实超过 30 分钟/八小时续期、V02 中文 IME/滚动/缩放与任务并行、Win7、物理设备、隔离资源与 Electron 同机性能均未通过。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
